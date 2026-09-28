/*
 * SPDX-License-Identifier: MIT
 *
 * Compatibility resolver for geekhunger's Advantage 2 QMK layout.
 * The host must use a German keyboard layout, as with the QMK original.
 */

#define DT_DRV_COMPAT geekhunger_behavior_adv2_resolver

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

#include <dt-bindings/zmk/adv2_layout.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/modifiers.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define WINDOWS_LAYER 1
#define ADV2_ID_MASK 0xFF

#define MOD_SHIFT (MOD_LSFT | MOD_RSFT)
#define MOD_CTRL (MOD_LCTL | MOD_RCTL)
#define MOD_ALT (MOD_LALT | MOD_RALT)
#define MOD_GUI (MOD_LGUI | MOD_RGUI)
#define MOD_CG (MOD_CTRL | MOD_GUI)
#define MOD_SA (MOD_SHIFT | MOD_ALT)
#define MOD_SAG (MOD_SHIFT | MOD_ALT | MOD_GUI)
#define MOD_CSG (MOD_CTRL | MOD_SHIFT | MOD_GUI)
#define MOD_AG (MOD_ALT | MOD_GUI)
#define MOD_CS (MOD_CTRL | MOD_SHIFT)
#define MOD_ALL (MOD_CTRL | MOD_SHIFT | MOD_ALT | MOD_GUI)

#define ALT_CODE_DELAY_MS 12
#define NUMPAD_LAYER 2
#define ADV2_MAX_POSITIONS 128
#define ADV2_SEMANTIC_COUNT ADV2_SLASH
#define SEMANTIC_TAPPING_TERM_MS 170
#define MOD_TAP_TAPPING_TERM_MS 90
#define MOD_TAP_CHORD_GRACE_MS 12
#define NAV_REPEAT_WINDOW_MS 120
#define TAP_PULSE_MS 12
#define TAP_PULSE_GAP_MS 4
#define ADV2_CAPTURED_POSITION_EVENTS 40
#define ADV2_MOD_TAP_COUNT 6
#define ADV2_TAP_PULSE_COUNT 32

enum nav_state {
    NAV_IDLE,
    NAV_FORWARDED,
    NAV_HANDLED,
    NAV_SUPPRESSED,
};

static uint8_t nav_states[ADV2_MAX_POSITIONS];

struct nav_axis_state {
    int32_t position;
    uint16_t action;
};

static struct nav_axis_state horizontal_axis = {.position = -1};
static struct nav_axis_state vertical_axis = {.position = -1};

enum mod_tap_phase {
    MOD_TAP_IDLE,
    MOD_TAP_PENDING,
    MOD_TAP_RELEASED_GRACE,
    MOD_TAP_HOLDING,
    MOD_TAP_LATCHED,
    MOD_TAP_REPEAT_DOWN,
};

struct mod_tap_state {
    uint16_t action;
    int32_t position;
    int64_t pressed_at;
    int64_t grace_deadline;
    int64_t last_tap_released_at;
    uint64_t latch_targets[2];
    enum mod_tap_phase phase;
    bool physically_pressed;
    bool chord_participant;
    bool hold_active;
    uint32_t hold_keycode;
    struct k_work_delayable hold_work;
};

static struct mod_tap_state mod_taps[ADV2_MOD_TAP_COUNT];
static uint8_t mod_tap_hold_counts[8];

struct tap_pulse_state {
    uint32_t keycode;
    uint8_t queued;
    bool active;
    struct k_work_delayable release_work;
};

static struct tap_pulse_state tap_pulses[ADV2_TAP_PULSE_COUNT];

enum deferred_input_type {
    DEFERRED_POSITION,
    DEFERRED_MOD_TAP_PENDING,
    DEFERRED_MOD_TAP,
};

struct deferred_input_event {
    enum deferred_input_type type;
    struct zmk_position_state_changed_event position_event;
    uint16_t mod_tap_action;
    struct zmk_behavior_binding_event binding_event;
    struct mod_tap_state *mod_tap_state;
};

static struct deferred_input_event deferred_input_events[ADV2_CAPTURED_POSITION_EVENTS];
static uint8_t deferred_input_event_count;

struct semantic_tap_dance {
    uint16_t id;
    int32_t position;
    uint8_t count;
    uint8_t mods;
    bool active;
    bool pressed;
    bool decided;
    struct k_work_delayable timer;
};

static struct semantic_tap_dance semantic_dances[ADV2_SEMANTIC_COUNT];

static void resolve_semantic(uint16_t id, bool held, uint8_t count);
static int mod_tap_capture_position_listener(const zmk_event_t *eh);

/* Must run before the Tap-Dance interrupter so replayed events follow normal QMK order. */
ZMK_LISTENER(adv2_mod_tap_capture, mod_tap_capture_position_listener);
ZMK_SUBSCRIPTION(adv2_mod_tap_capture, zmk_position_state_changed);

struct modifier_entry {
    uint8_t flag;
    uint32_t keycode;
};

static const struct modifier_entry modifier_entries[] = {
    {MOD_LCTL, LCTRL}, {MOD_LSFT, LSHIFT}, {MOD_LALT, LALT}, {MOD_LGUI, LGUI},
    {MOD_RCTL, RCTRL}, {MOD_RSFT, RSHIFT}, {MOD_RALT, RALT}, {MOD_RGUI, RGUI},
};

static void emit_code(uint32_t keycode, bool pressed) {
    raise_zmk_keycode_state_changed_from_encoded(keycode, pressed, k_uptime_get());
}

static uint8_t activate_missing_modifiers(uint8_t desired) {
    const uint8_t missing = desired & ~zmk_hid_get_explicit_mods();

    for (size_t i = 0; i < ARRAY_SIZE(modifier_entries); i++) {
        if (missing & modifier_entries[i].flag) {
            emit_code(modifier_entries[i].keycode, true);
        }
    }
    return missing;
}

static void release_temporary_modifiers(uint8_t temporary) {
    for (size_t i = 0; i < ARRAY_SIZE(modifier_entries); i++) {
        if (temporary & modifier_entries[i].flag) {
            emit_code(modifier_entries[i].keycode, false);
        }
    }
}

static bool cancel_tap_pulse(uint32_t keycode) {
    for (size_t i = 0; i < ARRAY_SIZE(tap_pulses); i++) {
        struct tap_pulse_state *pulse = &tap_pulses[i];
        if (pulse->keycode != keycode || (!pulse->active && pulse->queued == 0)) {
            continue;
        }

        k_work_cancel_delayable(&pulse->release_work);
        pulse->queued = 0;
        if (pulse->active) {
            pulse->active = false;
            emit_code(keycode, false);
        }
        return true;
    }
    return false;
}

static void tap_code(uint32_t keycode) {
    cancel_tap_pulse(keycode);
    emit_code(keycode, true);
    emit_code(keycode, false);
}

static void tap_pulse_release(struct k_work *item) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(item);
    struct tap_pulse_state *pulse =
        CONTAINER_OF(delayable, struct tap_pulse_state, release_work);

    if (!pulse->active && pulse->queued == 0) {
        return;
    }

    if (pulse->active) {
        pulse->active = false;
        emit_code(pulse->keycode, false);
        if (pulse->queued > 0) {
            k_work_reschedule(&pulse->release_work, K_MSEC(TAP_PULSE_GAP_MS));
        }
        return;
    }

    if (pulse->queued > 0) {
        pulse->queued--;
        pulse->active = true;
        emit_code(pulse->keycode, true);
        k_work_reschedule(&pulse->release_work, K_MSEC(TAP_PULSE_MS));
    }
}

/*
 * BLE can merge an immediate synthetic press/release into an unusably short
 * host pulse. Keep plain text and navigation taps down across a report window.
 * Each key owns its worker, so rolls between different letters remain parallel.
 */
static void pulse_code(uint32_t keycode) {
    struct tap_pulse_state *free_slot = NULL;
    struct tap_pulse_state *pulse = NULL;

    for (size_t i = 0; i < ARRAY_SIZE(tap_pulses); i++) {
        if (tap_pulses[i].keycode == keycode) {
            pulse = &tap_pulses[i];
            break;
        }
        if (tap_pulses[i].keycode == 0 && free_slot == NULL) {
            free_slot = &tap_pulses[i];
        }
    }

    if (pulse == NULL) {
        pulse = free_slot;
    }
    if (pulse == NULL) {
        LOG_ERR("No free synthetic tap pulse for keycode 0x%x", keycode);
        tap_code(keycode);
        return;
    }

    if (pulse->active || pulse->queued > 0) {
        if (pulse->queued < UINT8_MAX) {
            pulse->queued++;
        }
        return;
    }

    pulse->keycode = keycode;
    pulse->queued = 0;
    pulse->active = true;
    emit_code(keycode, true);
    k_work_reschedule(&pulse->release_work, K_MSEC(TAP_PULSE_MS));
}

/* QMK's without_mods(): release selected real modifiers, then restore them. */
static uint8_t suspend_modifiers(uint8_t mask) {
    uint8_t suspended = zmk_hid_get_explicit_mods() & mask;

    for (size_t i = 0; i < ARRAY_SIZE(modifier_entries); i++) {
        if (suspended & modifier_entries[i].flag) {
            emit_code(modifier_entries[i].keycode, false);
        }
    }

    return suspended;
}

static void resume_modifiers(uint8_t suspended) {
    for (size_t i = 0; i < ARRAY_SIZE(modifier_entries); i++) {
        if (suspended & modifier_entries[i].flag) {
            emit_code(modifier_entries[i].keycode, true);
        }
    }
}

static void tap_without(uint8_t mask, uint32_t keycode) {
    uint8_t suspended = suspend_modifiers(mask);
    tap_code(keycode);
    resume_modifiers(suspended);
}

static void tap_pair_without(uint8_t mask, uint32_t first, uint32_t second) {
    uint8_t suspended = suspend_modifiers(mask);
    tap_code(first);
    tap_code(second);
    resume_modifiers(suspended);
}

static void windows_alt_code(uint8_t mask, const uint32_t *digits, size_t count) {
    uint8_t suspended = suspend_modifiers(mask);

    emit_code(LALT, true);
    k_msleep(ALT_CODE_DELAY_MS);
    for (size_t i = 0; i < count; i++) {
        emit_code(digits[i], true);
        k_msleep(ALT_CODE_DELAY_MS);
        emit_code(digits[i], false);
        k_msleep(ALT_CODE_DELAY_MS);
    }
    emit_code(LALT, false);
    resume_modifiers(suspended);
}

#define WIN_ALT_CODE(mask, ...)                                                                   \
    do {                                                                                           \
        const uint32_t sequence[] = {__VA_ARGS__};                                                 \
        windows_alt_code((mask), sequence, ARRAY_SIZE(sequence));                                  \
    } while (0)

static bool is_windows(void) { return zmk_keymap_layer_active(WINDOWS_LAYER); }

static bool has_any(uint8_t mods, uint8_t mask) { return (mods & mask) != 0; }

static uint32_t base_keycode(uint16_t id) {
    switch (id) {
    case ADV2_W: return W;
    case ADV2_E: return E;
    case ADV2_R: return R;
    case ADV2_F: return F;
    case ADV2_A: return A;
    case ADV2_S: return S;
    case ADV2_D: return D;
    case ADV2_T: return T;
    case ADV2_G: return G;
    case ADV2_Z: return Z;
    case ADV2_X: return X;
    case ADV2_C: return C;
    case ADV2_V: return V;
    case ADV2_B: return B;
    case ADV2_Y: return Y;
    case ADV2_U: return U;
    case ADV2_I: return I;
    case ADV2_O: return O;
    case ADV2_H: return H;
    case ADV2_N: return N;
    case ADV2_K: return K;
    case ADV2_L: return L;
    case ADV2_P: return P;
    case ADV2_J: return J;
    case ADV2_M: return M;
    case ADV2_COMMA: return COMMA;
    case ADV2_DOT: return DOT;
    case ADV2_SLASH: return FSLH;
    default: return 0;
    }
}

static void pass_key(uint16_t id, uint8_t count) {
    uint32_t keycode = base_keycode(id);
    const uint8_t mods = zmk_hid_get_explicit_mods();

    /* QMK key_pass(): OS command chords are issued once, text taps keep the dance count. */
    if ((!is_windows() && has_any(mods, MOD_GUI)) ||
        (is_windows() && has_any(mods, MOD_CTRL))) {
        count = 1;
    }

    for (uint8_t i = 0; keycode != 0 && i < count; i++) {
        pulse_code(keycode);
    }
}

static void resolve_semantic(uint16_t id, bool held, uint8_t count) {
    const uint8_t mods = zmk_hid_get_explicit_mods();
    const bool shift = has_any(mods, MOD_SHIFT);
    const bool alt = has_any(mods, MOD_ALT);
    const bool ctrl = has_any(mods, MOD_CTRL);
    const bool gui = has_any(mods, MOD_GUI);
    const bool cg = ctrl || gui;
    const bool sa = shift || alt;
    const bool no_mods = !has_any(mods, MOD_ALL);
    const bool win = is_windows();

    switch (id) {
    case ADV2_W:
        if (alt && !cg) {
            tap_without(MOD_ALT, W);
        } else {
            pass_key(id, count);
        }
        return;

    case ADV2_E:
        if (held && (no_mods || (sa && !cg))) {
            if (win) {
                WIN_ALT_CODE(MOD_SA, KP_N1, KP_N7, KP_N5);
            } else {
                tap_without(MOD_SA, LS(LA(Q)));
            }
            return;
        }
        if (sa && !cg) {
            if (shift) {
                tap_without(MOD_ALT, E);
            } else {
                tap_without(MOD_ALT, LC(LA(E)));
            }
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_R:
        if (sa && !cg) {
            if (shift) {
                tap_without(MOD_ALT, R);
            } else if (win) {
                WIN_ALT_CODE(MOD_SA, KP_N1, KP_N6, KP_N9);
            } else {
                tap_without(MOD_SA, LA(R));
            }
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_F:
        if (held && (no_mods || (sa && !cg))) {
            tap_without(MOD_SA, win ? RA(N7) : LA(N8));
            return;
        }
        if (alt && !cg) {
            tap_without(MOD_ALT, F);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_A:
        if (held && (no_mods || (sa && !cg))) {
            tap_without(MOD_ALT, SQT);
            return;
        }
        if (alt && !shift) {
            tap_without(MOD_ALT, KP_PLUS);
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, A);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_S:
        if (held && (no_mods || !cg)) {
            if (shift) {
                tap_without(MOD_ALT, LS(N4));
            } else if (alt) {
                tap_without(MOD_ALT, LS(N3));
            } else {
                tap_code(MINUS);
            }
            return;
        }
        if (alt && !shift) {
            tap_without(MOD_ALT, KP_MINUS);
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, S);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_D:
        if (held && (no_mods || !cg)) {
            if (shift) {
                if (win) {
                    WIN_ALT_CODE(MOD_ALT, KP_N9, KP_N2);
                } else {
                    tap_without(MOD_ALT, LA(N7));
                }
            } else if (alt) {
                if (win) {
                    WIN_ALT_CODE(MOD_SA, KP_N1, KP_N2, KP_N4);
                } else {
                    tap_without(MOD_SA, LA(N7));
                }
            } else {
                tap_code(LS(N7));
            }
            return;
        }
        if (alt && !shift) {
            tap_without(MOD_ALT, KP_DIVIDE);
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, D);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_T:
        if (held && (no_mods || !cg)) {
            if (shift) {
                tap_without(MOD_SA, LS(BSLH));
            } else {
                tap_without(MOD_ALT, LS(N2));
            }
            return;
        }
        if (alt && !shift) {
            if (win) {
                WIN_ALT_CODE(MOD_ALT, KP_N0, KP_N1, KP_N5, KP_N3);
            } else {
                tap_without(MOD_ALT, LS(LA(D)));
            }
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, T);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_G:
        if (held && (no_mods || !cg)) {
            if (shift) {
                tap_without(MOD_SA, win ? RA(N8) : LA(N5));
            } else {
                tap_without(MOD_ALT, LS(N8));
            }
            return;
        }
        if (alt && !shift) {
            if (win) {
                WIN_ALT_CODE(MOD_ALT, KP_N2, KP_N4, KP_N8);
            } else {
                tap_without(MOD_ALT, LS(NUBS));
            }
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, G);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_Z:
    case ADV2_X:
        if (alt && !cg) {
            tap_without(MOD_ALT, base_keycode(id));
        } else {
            pass_key(id, count);
        }
        return;

    case ADV2_C:
        if (held && (no_mods || !cg)) {
            tap_without(MOD_SA, shift ? EQUAL : LS(EQUAL));
            return;
        }
        if (alt && !shift) {
            if (win) {
                WIN_ALT_CODE(MOD_ALT, KP_N1, KP_N8, KP_N4);
            } else {
                tap_without(MOD_ALT, LA(G));
            }
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, C);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_V:
        if (alt && !shift) {
            if (win) {
                WIN_ALT_CODE(MOD_SA, KP_N0, KP_N1, KP_N3, KP_N7);
            } else {
                tap_without(MOD_SA, LS(LA(E)));
            }
            return;
        }
        if (alt && !cg) {
            tap_without(MOD_ALT, V);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_B:
        if (held && (no_mods || !cg)) {
            if (shift) {
                tap_without(MOD_SA, win ? GRAVE : NUBS);
            } else {
                tap_without(MOD_ALT, win ? NUBS : GRAVE);
            }
            return;
        }
        if (alt && !cg) {
            tap_without(MOD_ALT, B);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_Y:
        if (held && (no_mods || (sa && !cg))) {
            tap_without(MOD_SA, win ? RA(N0) : LA(N9));
            return;
        }
        if (alt && !cg) {
            tap_without(MOD_ALT, Y);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_U:
        if (held && (no_mods || (sa && !cg))) {
            tap_without(MOD_ALT, LBKT);
            return;
        }
        if (alt && !shift) {
            if (win) {
                WIN_ALT_CODE(MOD_ALT, KP_N0, KP_N1, KP_N6, KP_N8);
            } else {
                tap_without(MOD_ALT, LA(U));
            }
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, U);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_I:
        if (held && (no_mods || (sa && !cg))) {
            if (win) {
                WIN_ALT_CODE(MOD_SA, KP_N1, KP_N7, KP_N4);
            } else {
                tap_without(MOD_SA, LA(Q));
            }
            return;
        }
        if (alt && !cg) {
            tap_without(MOD_ALT, I);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_O:
        if (held && (no_mods || (sa && !cg))) {
            tap_without(MOD_ALT, SEMI);
            return;
        }
        if (alt && !shift) {
            if (win) {
                WIN_ALT_CODE(MOD_ALT, KP_N1, KP_N5, KP_N5);
            } else {
                tap_without(MOD_ALT, LS(LA(O)));
            }
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, O);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_H:
        if (held && (no_mods || !cg)) {
            if (shift) {
                tap_without(MOD_SA, win ? RA(N9) : LA(N6));
            } else {
                tap_without(MOD_ALT, LS(N9));
            }
            return;
        }
        if (win && ctrl && !has_any(mods, MOD_SAG)) {
            tap_without(MOD_ALL, LG(DOWN_ARROW));
            return;
        }
        if (alt && !shift) {
            tap_without(MOD_ALT, BSLH);
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, H);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_N:
        if (alt && !shift) {
            tap_without(MOD_ALT, win ? RA(RBKT) : LA(N));
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, N);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_K:
        if (held) {
            if (win && ctrl && !has_any(mods, MOD_SAG)) {
                tap_without(MOD_ALL, LA(F4));
            } else if (alt && !cg) {
                tap_without(MOD_ALT, Q);
            } else {
                tap_code(Q);
            }
            return;
        }
        if (alt && !shift) {
            tap_without(MOD_ALT, LS(N0));
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, K);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_L:
        if (alt && !shift) {
            tap_without(MOD_ALT, win ? RA(Q) : LA(L));
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, L);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_P:
        if (held && (no_mods || (sa && !cg))) {
            if (win) {
                WIN_ALT_CODE(MOD_SA, KP_N9, KP_N6, KP_N0);
            } else {
                tap_without(MOD_SA, LA(P));
            }
            return;
        }
        if (sa && !cg) {
            if (shift) {
                tap_without(MOD_ALT, P);
            } else {
                tap_without(MOD_ALT, LS(N5));
            }
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_J:
        if (held && (no_mods || (sa && !cg))) {
            tap_without(MOD_SA, win ? LS(NUBS) : LS(GRAVE));
            return;
        }
        if (sa && !cg) {
            if (shift) {
                tap_without(MOD_ALT, J);
            } else {
                tap_without(MOD_ALT, LS(N6));
            }
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_M:
        if (held && (no_mods || (sa && !cg))) {
            if (win) {
                WIN_ALT_CODE(MOD_SA, KP_N2, KP_N3, KP_N0);
            } else {
                tap_without(MOD_SA, LA(M));
            }
            return;
        }
        if (sa && !cg) {
            if (shift) {
                tap_without(MOD_ALT, M);
            } else {
                tap_without(MOD_ALT, KP_MULTIPLY);
            }
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_COMMA:
        if (alt && !shift) {
            tap_without(MOD_ALL, LS(MINUS));
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, COMMA);
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_DOT:
        if (held && (no_mods || (sa && !cg))) {
            if (win) {
                WIN_ALT_CODE(MOD_SA, KP_N0, KP_N1, KP_N3, KP_N3);
            } else {
                tap_without(MOD_SA, LA(DOT));
            }
            return;
        }
        if (sa && !cg) {
            if (shift) {
                tap_without(MOD_ALT, DOT);
            } else {
                tap_without(MOD_ALL, LS(N1));
            }
            return;
        }
        pass_key(id, count);
        return;

    case ADV2_SLASH:
        if (alt && !shift) {
            if (win) {
                WIN_ALT_CODE(MOD_ALT, KP_N4, KP_N5);
            } else {
                tap_without(MOD_ALT, LA(FSLH));
            }
            return;
        }
        if (alt && shift && !cg) {
            tap_without(MOD_ALT, FSLH);
            return;
        }
        pass_key(id, count);
        return;
    }
}

static void clear_semantic_dance(struct semantic_tap_dance *dance) {
    dance->position = -1;
    dance->count = 0;
    dance->mods = 0;
    dance->active = false;
    dance->pressed = false;
    dance->decided = false;
}

static void decide_semantic_dance(struct semantic_tap_dance *dance, bool held) {
    if (!dance->active || dance->decided) {
        return;
    }

    dance->decided = true;
    k_work_cancel_delayable(&dance->timer);
    const uint8_t temporary_mods = activate_missing_modifiers(dance->mods);
    resolve_semantic(dance->id, held, dance->count);
    release_temporary_modifiers(temporary_mods);

    if (!dance->pressed) {
        clear_semantic_dance(dance);
    }
}

static void semantic_dance_timer(struct k_work *item) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(item);
    struct semantic_tap_dance *dance =
        CONTAINER_OF(delayable, struct semantic_tap_dance, timer);

    /* QMK key_down(): only an uninterrupted key that is still down gets its hold action. */
    decide_semantic_dance(dance, dance->pressed);
}

static int press_semantic_dance(uint16_t id, struct zmk_behavior_binding_event event) {
    if (id == 0 || id > ADV2_SEMANTIC_COUNT) {
        return -EINVAL;
    }

    struct semantic_tap_dance *dance = &semantic_dances[id - 1];
    if (!dance->active) {
        dance->id = id;
        dance->position = event.position;
        dance->count = 0;
        dance->active = true;
        dance->decided = false;
    }

    if (dance->decided || dance->position != event.position) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    dance->pressed = true;
    dance->mods |= zmk_hid_get_explicit_mods();
    if (dance->count < UINT8_MAX) {
        dance->count++;
    }

    /* QMK resets KEY_TAP_TIMEOUT on every press of the same Tap-Dance key. */
    k_work_reschedule(&dance->timer, K_MSEC(SEMANTIC_TAPPING_TERM_MS));
    return ZMK_BEHAVIOR_OPAQUE;
}

static int release_semantic_dance(uint16_t id, struct zmk_behavior_binding_event event) {
    if (id == 0 || id > ADV2_SEMANTIC_COUNT) {
        return -EINVAL;
    }

    struct semantic_tap_dance *dance = &semantic_dances[id - 1];
    if (!dance->active || dance->position != event.position) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    dance->pressed = false;
    dance->mods |= zmk_hid_get_explicit_mods();
    if (dance->decided) {
        clear_semantic_dance(dance);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int semantic_tap_dance_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* QMK preprocess_tap_dance(): a different key resolves every active dance first. */
    for (size_t i = 0; i < ARRAY_SIZE(semantic_dances); i++) {
        struct semantic_tap_dance *dance = &semantic_dances[i];
        if (dance->active && !dance->decided && dance->position != ev->position) {
            decide_semantic_dance(dance, false);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(adv2_semantic_tap_dance, semantic_tap_dance_position_listener);
ZMK_SUBSCRIPTION(adv2_semantic_tap_dance, zmk_position_state_changed);

static uint32_t nav_keycode(uint16_t action) {
    switch (action) {
    case ADV2_NAV_LEFT: return LEFT;
    case ADV2_NAV_UP: return UP_ARROW;
    case ADV2_NAV_RIGHT: return RIGHT;
    case ADV2_NAV_DOWN: return DOWN_ARROW;
    case ADV2_NAV_DELETE: return DELETE;
    case ADV2_NAV_BACKSPACE: return BACKSPACE;
    default: return 0;
    }
}

static struct nav_axis_state *nav_axis(uint16_t action) {
    switch (action) {
    case ADV2_NAV_LEFT:
    case ADV2_NAV_RIGHT:
        return &horizontal_axis;
    case ADV2_NAV_UP:
    case ADV2_NAV_DOWN:
        return &vertical_axis;
    default:
        return NULL;
    }
}

static bool resolve_windows_navigation(uint16_t action) {
    const uint8_t mods = zmk_hid_get_explicit_mods();
    const bool ctrl_mode = has_any(mods, MOD_CTRL) && !has_any(mods, MOD_AG);
    const bool alt_mode = has_any(mods, MOD_ALT) && !has_any(mods, MOD_CSG);

    if (!is_windows()) {
        return false;
    }

    if (ctrl_mode) {
        switch (action) {
        case ADV2_NAV_LEFT: tap_without(MOD_CTRL, HOME); return true;
        case ADV2_NAV_UP: tap_without(MOD_CTRL, LC(HOME)); return true;
        case ADV2_NAV_RIGHT: tap_without(MOD_CTRL, END); return true;
        case ADV2_NAV_DOWN: tap_without(MOD_CTRL, LC(END)); return true;
        case ADV2_NAV_DELETE: tap_pair_without(MOD_CS, LS(END), DELETE); return true;
        case ADV2_NAV_BACKSPACE: tap_pair_without(MOD_CS, LS(HOME), BACKSPACE); return true;
        }
    }

    if (alt_mode) {
        switch (action) {
        case ADV2_NAV_LEFT: tap_without(MOD_ALT, LC(LEFT)); return true;
        case ADV2_NAV_RIGHT: tap_without(MOD_ALT, LC(RIGHT)); return true;
        case ADV2_NAV_DELETE: tap_without(MOD_ALT, LC(DELETE)); return true;
        case ADV2_NAV_BACKSPACE: tap_without(MOD_ALT, LC(BACKSPACE)); return true;
        }
    }

    return false;
}

static int handle_navigation(uint16_t action, bool pressed,
                             struct zmk_behavior_binding_event event) {
    if (event.position >= ADV2_MAX_POSITIONS) {
        return -EINVAL;
    }

    if (pressed) {
        if (resolve_windows_navigation(action)) {
            nav_states[event.position] = NAV_HANDLED;
            return ZMK_BEHAVIOR_OPAQUE;
        }

        uint32_t keycode = nav_keycode(action);
        if (keycode == 0) {
            return -EINVAL;
        }

        cancel_tap_pulse(keycode);

        struct nav_axis_state *axis = nav_axis(action);
        if (axis != NULL && axis->position >= 0 && axis->position != event.position) {
            if (axis->position < ADV2_MAX_POSITIONS &&
                nav_states[axis->position] == NAV_FORWARDED) {
                emit_code(nav_keycode(axis->action), false);
                nav_states[axis->position] = NAV_SUPPRESSED;
            }
        }

        nav_states[event.position] = NAV_FORWARDED;
        emit_code(keycode, true);
        if (axis != NULL) {
            axis->position = event.position;
            axis->action = action;
        }
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (nav_states[event.position] == NAV_FORWARDED) {
        emit_code(nav_keycode(action), false);
        struct nav_axis_state *axis = nav_axis(action);
        if (axis != NULL && axis->position == event.position) {
            axis->position = -1;
            axis->action = 0;
        }
    }
    nav_states[event.position] = NAV_IDLE;
    return ZMK_BEHAVIOR_OPAQUE;
}

static uint16_t mod_tap_to_navigation(uint16_t action) {
    switch (action & ADV2_ID_MASK) {
    case 5: return ADV2_NAV_DELETE;
    case 6: return ADV2_NAV_BACKSPACE;
    default: return 0;
    }
}

static bool mod_tap_is_navigation(uint16_t action) {
    return mod_tap_to_navigation(action) != 0;
}

static struct mod_tap_state *mod_tap_for_event(uint16_t action, int32_t position) {
    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        if (mod_taps[i].action == action && mod_taps[i].position == position) {
            return &mod_taps[i];
        }
    }
    return NULL;
}

static bool is_mod_tap_position(int32_t position) {
    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        if (mod_taps[i].position == position) {
            /* Releases must reach the behavior that handled their press. */
            if (mod_taps[i].phase != MOD_TAP_IDLE) {
                return true;
            }

            /* NUM overrides every dual-role position except transparent position 67. */
            return !zmk_keymap_layer_active(NUMPAD_LAYER) || position == 67;
        }
    }
    return false;
}

static void reset_mod_tap(struct mod_tap_state *state) {
    state->phase = MOD_TAP_IDLE;
    state->physically_pressed = false;
    state->hold_active = false;
    state->hold_keycode = 0;
    state->pressed_at = 0;
    state->grace_deadline = 0;
    state->latch_targets[0] = 0;
    state->latch_targets[1] = 0;
    state->chord_participant = false;
}

static uint8_t *mod_tap_hold_counter(uint32_t keycode) {
    for (size_t i = 0; i < ARRAY_SIZE(modifier_entries); i++) {
        if (modifier_entries[i].keycode == keycode) {
            return &mod_tap_hold_counts[i];
        }
    }
    return NULL;
}

static void release_deferred_input_events(void);
static void resolve_grace_mod_tap(struct mod_tap_state *state);

static void queue_mod_tap_marker(struct mod_tap_state *state,
                                 struct zmk_behavior_binding_event event) {
    if (deferred_input_event_count >= ADV2_CAPTURED_POSITION_EVENTS) {
        LOG_ERR("QMK mod-tap deferred input buffer full");
        return;
    }

    struct deferred_input_event *deferred =
        &deferred_input_events[deferred_input_event_count++];
    deferred->type = DEFERRED_MOD_TAP_PENDING;
    deferred->mod_tap_action = state->action;
    deferred->binding_event = event;
    deferred->mod_tap_state = state;
}

static int find_mod_tap_marker(struct mod_tap_state *state) {
    for (uint8_t i = 0; i < deferred_input_event_count; i++) {
        if (deferred_input_events[i].type == DEFERRED_MOD_TAP_PENDING &&
            deferred_input_events[i].mod_tap_state == state) {
            return i;
        }
    }
    return -1;
}

static void remove_deferred_input(uint8_t index) {
    for (uint8_t i = index + 1; i < deferred_input_event_count; i++) {
        deferred_input_events[i - 1] = deferred_input_events[i];
    }
    deferred_input_event_count--;
}

static uint32_t mod_tap_hold_keycode(uint16_t action) {
    switch (action & ADV2_ID_MASK) {
    case 5:
    case 6:
        return is_windows() ? RALT : LALT;
    case 7:
        return LSHIFT;
    case 8:
        return is_windows() ? LCTRL : LGUI;
    case 9:
        return RSHIFT;
    default:
        return 0;
    }
}

static uint32_t mod_tap_tap_keycode(uint16_t action) {
    switch (action & ADV2_ID_MASK) {
    case 7: return TAB;
    case 8: return ESCAPE;
    case 9: return ENTER;
    default: return 0;
    }
}

static bool mod_tap_is_unresolved(const struct mod_tap_state *state) {
    return state->phase == MOD_TAP_PENDING || state->phase == MOD_TAP_RELEASED_GRACE;
}

static uint8_t unresolved_mod_tap_count(void) {
    uint8_t count = 0;
    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        if (mod_tap_is_unresolved(&mod_taps[i])) {
            count++;
        }
    }
    return count;
}

static bool deferred_position_is_active(int32_t position) {
    bool active = false;

    for (uint8_t i = 0; i < deferred_input_event_count; i++) {
        if (deferred_input_events[i].type != DEFERRED_POSITION ||
            deferred_input_events[i].position_event.data.position != position) {
            continue;
        }
        active = deferred_input_events[i].position_event.data.state;
    }

    return active;
}

static int32_t latest_deferred_active_position(void) {
    bool active[ADV2_MAX_POSITIONS] = {false};
    uint8_t pressed_order[ADV2_MAX_POSITIONS] = {0};

    for (uint8_t i = 0; i < deferred_input_event_count; i++) {
        if (deferred_input_events[i].type != DEFERRED_POSITION) {
            continue;
        }

        const struct zmk_position_state_changed *position_event =
            &deferred_input_events[i].position_event.data;
        if (position_event->position >= ADV2_MAX_POSITIONS) {
            continue;
        }
        active[position_event->position] = position_event->state;
        if (position_event->state) {
            pressed_order[position_event->position] = i + 1;
        }
    }

    int32_t latest_position = -1;
    uint8_t latest_order = 0;
    for (int32_t position = 0; position < ADV2_MAX_POSITIONS; position++) {
        if (active[position] && pressed_order[position] >= latest_order) {
            latest_position = position;
            latest_order = pressed_order[position];
        }
    }
    return latest_position;
}

static void activate_mod_tap_hold(struct mod_tap_state *state) {
    if (state->hold_active) {
        return;
    }

    int marker = find_mod_tap_marker(state);
    if (marker >= 0) {
        remove_deferred_input(marker);
    }

    state->hold_keycode = mod_tap_hold_keycode(state->action);
    uint8_t *counter = mod_tap_hold_counter(state->hold_keycode);
    if (state->hold_keycode == 0 || counter == NULL) {
        LOG_ERR("Unsupported mod-tap hold action 0x%x", state->action);
        reset_mod_tap(state);
        return;
    }
    if ((*counter)++ == 0) {
        emit_code(state->hold_keycode, true);
    }
    state->hold_active = true;
    state->phase = MOD_TAP_HOLDING;
}

static void release_mod_tap_hold(struct mod_tap_state *state) {
    if (!state->hold_active) {
        return;
    }

    state->hold_active = false;
    uint8_t *counter = mod_tap_hold_counter(state->hold_keycode);
    if (counter != NULL && *counter > 0 && --(*counter) == 0) {
        emit_code(state->hold_keycode, false);
    }
}

static void mark_mod_tap_chord(struct mod_tap_state *state) {
    bool overlap = has_any(zmk_hid_get_explicit_mods(), MOD_ALL);

    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        struct mod_tap_state *other = &mod_taps[i];
        if (other == state || other->phase == MOD_TAP_IDLE ||
            other->phase == MOD_TAP_REPEAT_DOWN) {
            continue;
        }

        other->chord_participant = true;
        overlap = true;
    }

    state->chord_participant = overlap;
}

static bool released_grace_active(void) {
    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        if (mod_taps[i].phase == MOD_TAP_RELEASED_GRACE) {
            return true;
        }
    }
    return false;
}

static bool valid_latch_target(int32_t position) {
    return position >= 0 && position < ADV2_MAX_POSITIONS;
}

static bool mod_tap_has_latch_target(const struct mod_tap_state *state,
                                     int32_t position) {
    if (!valid_latch_target(position)) {
        return false;
    }
    return (state->latch_targets[position / 64] & (1ULL << (position % 64))) != 0;
}

static bool mod_tap_has_latch_targets(const struct mod_tap_state *state) {
    return state->latch_targets[0] != 0 || state->latch_targets[1] != 0;
}

static void add_mod_tap_latch_target(struct mod_tap_state *state,
                                     int32_t position) {
    if (valid_latch_target(position)) {
        state->latch_targets[position / 64] |= 1ULL << (position % 64);
    }
}

static void remove_mod_tap_latch_target(struct mod_tap_state *state,
                                        int32_t position) {
    if (valid_latch_target(position)) {
        state->latch_targets[position / 64] &= ~(1ULL << (position % 64));
    }
}

static bool latched_mod_tap_targets(int32_t position) {
    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        if (mod_tap_has_latch_target(&mod_taps[i], position)) {
            return true;
        }
    }
    return false;
}

static void release_latched_mod_taps(int32_t position) {
    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        struct mod_tap_state *state = &mod_taps[i];
        if (!mod_tap_has_latch_target(state, position)) {
            continue;
        }

        remove_mod_tap_latch_target(state, position);
        if (mod_tap_has_latch_targets(state)) {
            continue;
        }

        if (state->physically_pressed) {
            state->phase = MOD_TAP_HOLDING;
        } else {
            release_mod_tap_hold(state);
            reset_mod_tap(state);
        }
    }
}

static void latch_active_mod_tap_holds(int32_t target_position) {
    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        struct mod_tap_state *state = &mod_taps[i];
        if (state->phase != MOD_TAP_HOLDING) {
            continue;
        }

        add_mod_tap_latch_target(state, target_position);
        if (!state->physically_pressed) {
            state->phase = MOD_TAP_LATCHED;
        }
    }
}

static void promote_unresolved_for_live_action(int32_t target_position) {
    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        struct mod_tap_state *state = &mod_taps[i];
        if (!mod_tap_is_unresolved(state)) {
            continue;
        }

        k_work_cancel_delayable(&state->hold_work);
        activate_mod_tap_hold(state);
    }

    latch_active_mod_tap_holds(target_position);

    if (unresolved_mod_tap_count() == 0) {
        release_deferred_input_events();
    }
}

static void promote_unresolved_for_completed_action(int32_t target_position) {
    struct mod_tap_state *synthetic_holds[ADV2_MOD_TAP_COUNT];
    uint8_t synthetic_hold_count = 0;

    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        struct mod_tap_state *state = &mod_taps[i];
        if (!mod_tap_is_unresolved(state)) {
            continue;
        }

        const bool was_released = state->phase == MOD_TAP_RELEASED_GRACE;
        k_work_cancel_delayable(&state->hold_work);
        activate_mod_tap_hold(state);
        if (was_released && state->phase == MOD_TAP_HOLDING) {
            synthetic_holds[synthetic_hold_count++] = state;
        }
    }

    if (unresolved_mod_tap_count() == 0) {
        release_deferred_input_events();
    }

    release_latched_mod_taps(target_position);

    for (uint8_t i = 0; i < synthetic_hold_count; i++) {
        release_mod_tap_hold(synthetic_holds[i]);
        reset_mod_tap(synthetic_holds[i]);
    }
}

static void mod_tap_hold_timer(struct k_work *item) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(item);
    struct mod_tap_state *state =
        CONTAINER_OF(delayable, struct mod_tap_state, hold_work);

    if (state->physically_pressed && state->phase == MOD_TAP_PENDING) {
        activate_mod_tap_hold(state);
        if (unresolved_mod_tap_count() == 0) {
            release_deferred_input_events();
        }
    } else if (state->phase == MOD_TAP_RELEASED_GRACE) {
        resolve_grace_mod_tap(state);
    }
}

static void resolve_expired_mod_taps(int64_t now) {
    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        struct mod_tap_state *state = &mod_taps[i];
        if (state->phase == MOD_TAP_PENDING && state->physically_pressed &&
            now - state->pressed_at >= MOD_TAP_TAPPING_TERM_MS) {
            k_work_cancel_delayable(&state->hold_work);
            activate_mod_tap_hold(state);
        } else if (state->phase == MOD_TAP_RELEASED_GRACE &&
                   now >= state->grace_deadline) {
            k_work_cancel_delayable(&state->hold_work);
            resolve_grace_mod_tap(state);
        }
    }

    if (unresolved_mod_tap_count() == 0) {
        release_deferred_input_events();
    }
}

static void tap_mod_tap(uint16_t action, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);

    uint16_t navigation = mod_tap_to_navigation(action);
    if (navigation != 0) {
        if (!resolve_windows_navigation(navigation)) {
            pulse_code(nav_keycode(navigation));
        }
    } else {
        uint32_t keycode = mod_tap_tap_keycode(action);
        if (keycode != 0) {
            tap_code(keycode);
        }
    }
}

static bool resolve_mod_tap_marker_as_tap(struct mod_tap_state *state) {
    int marker = find_mod_tap_marker(state);
    if (marker < 0) {
        return false;
    }

    deferred_input_events[marker].type = DEFERRED_MOD_TAP;
    return true;
}

static void resolve_grace_mod_tap(struct mod_tap_state *state) {
    if (state->phase != MOD_TAP_RELEASED_GRACE) {
        return;
    }

    struct mod_tap_state *synthetic_holds[ADV2_MOD_TAP_COUNT];
    uint8_t synthetic_hold_count = 0;
    const uint16_t tap_action = state->action;
    const struct zmk_behavior_binding_event tap_event = {
        .position = state->position,
        .timestamp = state->grace_deadline - MOD_TAP_CHORD_GRACE_MS,
    };
    const bool tap_queued = resolve_mod_tap_marker_as_tap(state);

    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        struct mod_tap_state *other = &mod_taps[i];
        if (other == state || !mod_tap_is_unresolved(other)) {
            continue;
        }

        const bool was_released = other->phase == MOD_TAP_RELEASED_GRACE;
        k_work_cancel_delayable(&other->hold_work);
        activate_mod_tap_hold(other);
        if (was_released && other->phase == MOD_TAP_HOLDING) {
            synthetic_holds[synthetic_hold_count++] = other;
        }
    }

    state->last_tap_released_at = state->grace_deadline - MOD_TAP_CHORD_GRACE_MS;
    reset_mod_tap(state);

    if (tap_queued) {
        release_deferred_input_events();
    } else {
        tap_mod_tap(tap_action, tap_event);
    }

    for (uint8_t i = 0; i < synthetic_hold_count; i++) {
        release_mod_tap_hold(synthetic_holds[i]);
        reset_mod_tap(synthetic_holds[i]);
    }
}

static void release_deferred_input_events(void) {
    for (int32_t position = 0; position < ADV2_MAX_POSITIONS; position++) {
        if (deferred_position_is_active(position)) {
            latch_active_mod_tap_holds(position);
        }
    }

    while (deferred_input_event_count > 0 && unresolved_mod_tap_count() == 0) {
        struct deferred_input_event event = deferred_input_events[0];
        for (uint8_t i = 1; i < deferred_input_event_count; i++) {
            deferred_input_events[i - 1] = deferred_input_events[i];
        }
        deferred_input_event_count--;

        if (event.type == DEFERRED_MOD_TAP) {
            tap_mod_tap(event.mod_tap_action, event.binding_event);
        } else if (event.type == DEFERRED_POSITION) {
            ZMK_EVENT_RAISE_AFTER(event.position_event, adv2_mod_tap_capture);
        } else {
            LOG_ERR("Unresolved mod-tap marker reached replay");
        }
    }
}

static bool eager_action_position(int32_t position) {
    return position >= 71 && position <= 74;
}

static int defer_position_event(const struct zmk_position_state_changed *event) {
    if (deferred_input_event_count >= ADV2_CAPTURED_POSITION_EVENTS) {
        LOG_ERR("QMK mod-tap capture buffer full");
        return -ENOMEM;
    }

    struct deferred_input_event *deferred =
        &deferred_input_events[deferred_input_event_count++];
    deferred->type = DEFERRED_POSITION;
    deferred->position_event = copy_raised_zmk_position_state_changed(event);
    return 0;
}

static int mod_tap_capture_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    resolve_expired_mod_taps(k_uptime_get());
    const bool mod_tap_position = is_mod_tap_position(ev->position);

    if (!ev->state && !mod_tap_position && latched_mod_tap_targets(ev->position) &&
        !deferred_position_is_active(ev->position)) {
        struct zmk_position_state_changed_event released_event =
            copy_raised_zmk_position_state_changed(ev);
        int result = ZMK_EVENT_RAISE_AFTER(released_event, adv2_mod_tap_capture);
        release_latched_mod_taps(ev->position);
        return result < 0 ? result : ZMK_EV_EVENT_CAPTURED;
    }

    if (mod_tap_position) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state) {
        if (released_grace_active() ||
            (eager_action_position(ev->position) && unresolved_mod_tap_count() > 0)) {
            promote_unresolved_for_live_action(ev->position);
            return ZMK_EV_EVENT_BUBBLE;
        }

        if (unresolved_mod_tap_count() > 0) {
            latch_active_mod_tap_holds(ev->position);
            int result = defer_position_event(ev);
            return result < 0 ? result : ZMK_EV_EVENT_CAPTURED;
        }
        latch_active_mod_tap_holds(ev->position);
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (unresolved_mod_tap_count() > 0) {
        const bool completes_action = deferred_position_is_active(ev->position);
        int result = defer_position_event(ev);
        if (result < 0) {
            return result;
        }
        if (completes_action) {
            promote_unresolved_for_completed_action(ev->position);
        }
        return ZMK_EV_EVENT_CAPTURED;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

static int handle_mod_tap(uint16_t action, bool pressed,
                          struct zmk_behavior_binding_event event) {
    struct mod_tap_state *state = mod_tap_for_event(action, event.position);
    if (state == NULL || event.position >= ADV2_MAX_POSITIONS) {
        return -EINVAL;
    }

    if (pressed) {
        if (state->phase != MOD_TAP_IDLE) {
            return ZMK_BEHAVIOR_OPAQUE;
        }

        const int64_t now = k_uptime_get();
        mark_mod_tap_chord(state);
        state->physically_pressed = true;

        if (mod_tap_is_navigation(action) && unresolved_mod_tap_count() == 0 &&
            deferred_input_event_count == 0 &&
            now - state->last_tap_released_at <= NAV_REPEAT_WINDOW_MS) {
            state->phase = MOD_TAP_REPEAT_DOWN;
            handle_navigation(mod_tap_to_navigation(state->action), true, event);
            return ZMK_BEHAVIOR_OPAQUE;
        }

        state->pressed_at = now;
        state->phase = MOD_TAP_PENDING;
        queue_mod_tap_marker(state, event);
        k_work_schedule(&state->hold_work, K_MSEC(MOD_TAP_TAPPING_TERM_MS));
        return ZMK_BEHAVIOR_OPAQUE;
    }

    state->physically_pressed = false;
    switch (state->phase) {
    case MOD_TAP_PENDING: {
        k_work_cancel_delayable(&state->hold_work);

        if (state->chord_participant) {
            state->phase = MOD_TAP_RELEASED_GRACE;
            state->grace_deadline = k_uptime_get() + MOD_TAP_CHORD_GRACE_MS;

            const int32_t active_position = latest_deferred_active_position();
            if (active_position >= 0) {
                promote_unresolved_for_live_action(active_position);
            } else {
                k_work_reschedule(&state->hold_work,
                                  K_MSEC(MOD_TAP_CHORD_GRACE_MS));
            }
            break;
        }

        const uint16_t tap_action = state->action;
        const bool tap_queued = resolve_mod_tap_marker_as_tap(state);
        state->last_tap_released_at = k_uptime_get();
        reset_mod_tap(state);

        if (!tap_queued) {
            tap_mod_tap(tap_action, event);
        }
        if (unresolved_mod_tap_count() == 0) {
            release_deferred_input_events();
        }
        break;
    }
    case MOD_TAP_REPEAT_DOWN:
        handle_navigation(mod_tap_to_navigation(state->action), false, event);
        state->last_tap_released_at = k_uptime_get();
        reset_mod_tap(state);
        break;
    case MOD_TAP_HOLDING:
        if (mod_tap_has_latch_targets(state)) {
            state->phase = MOD_TAP_LATCHED;
        } else {
            release_mod_tap_hold(state);
            reset_mod_tap(state);
        }
        break;
    case MOD_TAP_RELEASED_GRACE:
    case MOD_TAP_LATCHED:
    case MOD_TAP_IDLE:
        break;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int adv2_resolver_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    const uint16_t action = binding->param1;

    if (action & ADV2_DUAL_FLAG) {
        return handle_mod_tap(action, true, event);
    }

    if (action & ADV2_NAV_FLAG) {
        return handle_navigation(action, true, event);
    }

    return press_semantic_dance(action & ADV2_ID_MASK, event);
}

static int adv2_resolver_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    const uint16_t action = binding->param1;

    if (action & ADV2_DUAL_FLAG) {
        return handle_mod_tap(action, false, event);
    }

    if (action & ADV2_NAV_FLAG) {
        return handle_navigation(action, false, event);
    }

    return release_semantic_dance(action & ADV2_ID_MASK, event);
}

static const struct behavior_driver_api adv2_resolver_driver_api = {
    .binding_pressed = adv2_resolver_pressed,
    .binding_released = adv2_resolver_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int adv2_resolver_init(const struct device *dev) {
    static const uint16_t actions[ADV2_MOD_TAP_COUNT] = {
        ADV2_DUAL_DELETE, ADV2_DUAL_BACKSPACE, ADV2_DUAL_SHIFT_TAB,
        ADV2_DUAL_CMD_ESCAPE, ADV2_DUAL_CMD_ESCAPE, ADV2_DUAL_SHIFT_ENTER,
    };
    static const int32_t positions[ADV2_MOD_TAP_COUNT] = {35, 38, 66, 67, 68, 69};

    for (size_t i = 0; i < ARRAY_SIZE(mod_taps); i++) {
        mod_taps[i].action = actions[i];
        mod_taps[i].position = positions[i];
        mod_taps[i].last_tap_released_at = INT64_MIN / 2;
        reset_mod_tap(&mod_taps[i]);
        k_work_init_delayable(&mod_taps[i].hold_work, mod_tap_hold_timer);
    }
    for (size_t i = 0; i < ARRAY_SIZE(mod_tap_hold_counts); i++) {
        mod_tap_hold_counts[i] = 0;
    }
    for (size_t i = 0; i < ARRAY_SIZE(tap_pulses); i++) {
        tap_pulses[i].keycode = 0;
        tap_pulses[i].queued = 0;
        tap_pulses[i].active = false;
        k_work_init_delayable(&tap_pulses[i].release_work, tap_pulse_release);
    }
    deferred_input_event_count = 0;
    horizontal_axis.position = -1;
    horizontal_axis.action = 0;
    vertical_axis.position = -1;
    vertical_axis.action = 0;

    for (size_t i = 0; i < ARRAY_SIZE(semantic_dances); i++) {
        semantic_dances[i].id = i + 1;
        clear_semantic_dance(&semantic_dances[i]);
        k_work_init_delayable(&semantic_dances[i].timer, semantic_dance_timer);
    }
    return 0;
}

#define ADV2_RESOLVER_INST(n)                                                                     \
    BEHAVIOR_DT_INST_DEFINE(n, adv2_resolver_init, NULL, NULL, NULL, POST_KERNEL,                  \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &adv2_resolver_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ADV2_RESOLVER_INST)

#endif
