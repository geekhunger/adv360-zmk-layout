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
#define ADV2_MAX_POSITIONS 128
#define NAV_DUAL_TAPPING_TERM_MS 90
#define NAV_DUAL_QUICK_TAP_MS 90

enum nav_state {
    NAV_IDLE,
    NAV_FORWARDED,
    NAV_HANDLED,
};

static uint8_t nav_states[ADV2_MAX_POSITIONS];

enum nav_dual_phase {
    NAV_DUAL_IDLE,
    NAV_DUAL_PENDING,
    NAV_DUAL_HOLDING,
    NAV_DUAL_TAP_DOWN,
};

struct nav_dual_state {
    uint16_t action;
    int32_t position;
    int64_t last_tap_released_at;
    enum nav_dual_phase phase;
    bool physically_pressed;
    bool hold_active;
    bool release_pending;
    uint8_t dependents;
    struct nav_dual_state *hold_owner;
    struct zmk_behavior_binding_event press_event;
    struct k_work_delayable hold_work;
};

static struct nav_dual_state nav_duals[2];
static uint8_t nav_alt_holds;
static uint32_t nav_alt_keycode;

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

static void tap_code(uint32_t keycode) {
    emit_code(keycode, true);
    emit_code(keycode, false);
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

static void pass_key(uint16_t id) {
    uint32_t keycode = base_keycode(id);
    if (keycode != 0) {
        tap_code(keycode);
    }
}

static void resolve_semantic(uint16_t id, bool held) {
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
            pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
        return;

    case ADV2_Z:
    case ADV2_X:
        if (alt && !cg) {
            tap_without(MOD_ALT, base_keycode(id));
        } else {
            pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
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
        pass_key(id);
        return;
    }
}

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
        nav_states[event.position] = NAV_FORWARDED;
        emit_code(keycode, true);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (nav_states[event.position] == NAV_FORWARDED) {
        emit_code(nav_keycode(action), false);
    }
    nav_states[event.position] = NAV_IDLE;
    return ZMK_BEHAVIOR_OPAQUE;
}

static uint16_t nav_dual_to_navigation(uint16_t action) {
    switch (action & ADV2_ID_MASK) {
    case 5: return ADV2_NAV_DELETE;
    case 6: return ADV2_NAV_BACKSPACE;
    default: return 0;
    }
}

static struct nav_dual_state *nav_dual_for_action(uint16_t action) {
    switch (action & ADV2_ID_MASK) {
    case 5: return &nav_duals[0];
    case 6: return &nav_duals[1];
    default: return NULL;
    }
}

static struct nav_dual_state *other_nav_dual(struct nav_dual_state *state) {
    return state == &nav_duals[0] ? &nav_duals[1] : &nav_duals[0];
}

static void reset_nav_dual(struct nav_dual_state *state) {
    state->position = -1;
    state->phase = NAV_DUAL_IDLE;
    state->physically_pressed = false;
    state->hold_active = false;
    state->release_pending = false;
    state->dependents = 0;
    state->hold_owner = NULL;
}

static void activate_nav_dual_hold(struct nav_dual_state *state) {
    if (state->hold_active) {
        return;
    }

    if (nav_alt_holds == 0) {
        nav_alt_keycode = is_windows() ? RALT : LALT;
        emit_code(nav_alt_keycode, true);
    }
    nav_alt_holds++;
    state->hold_active = true;
    state->phase = NAV_DUAL_HOLDING;
}

static void release_nav_dual_hold(struct nav_dual_state *state) {
    if (!state->hold_active) {
        return;
    }

    state->hold_active = false;
    if (nav_alt_holds > 0 && --nav_alt_holds == 0) {
        emit_code(nav_alt_keycode, false);
    }
}

static void nav_dual_hold_timer(struct k_work *item) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(item);
    struct nav_dual_state *state =
        CONTAINER_OF(delayable, struct nav_dual_state, hold_work);

    if (state->physically_pressed && state->phase == NAV_DUAL_PENDING) {
        activate_nav_dual_hold(state);
    }
}

static void press_nav_dual_tap(struct nav_dual_state *state,
                               struct nav_dual_state *hold_owner,
                               struct zmk_behavior_binding_event event) {
    state->phase = NAV_DUAL_TAP_DOWN;
    state->hold_owner = hold_owner;
    if (hold_owner != NULL) {
        hold_owner->dependents++;
    }
    handle_navigation(nav_dual_to_navigation(state->action), true, event);
}

static void finish_nav_dual_tap(struct nav_dual_state *state,
                                struct zmk_behavior_binding_event event) {
    handle_navigation(nav_dual_to_navigation(state->action), false, event);
    state->last_tap_released_at = k_uptime_get();

    struct nav_dual_state *owner = state->hold_owner;
    state->hold_owner = NULL;
    if (owner != NULL && owner->dependents > 0) {
        owner->dependents--;
        if (owner->release_pending && owner->dependents == 0) {
            release_nav_dual_hold(owner);
            reset_nav_dual(owner);
        }
    }

    reset_nav_dual(state);
}

static int handle_nav_dual(uint16_t action, bool pressed,
                           struct zmk_behavior_binding_event event) {
    struct nav_dual_state *state = nav_dual_for_action(action);
    if (state == NULL || event.position >= ADV2_MAX_POSITIONS) {
        return -EINVAL;
    }

    if (pressed) {
        if (state->phase != NAV_DUAL_IDLE) {
            return ZMK_BEHAVIOR_OPAQUE;
        }

        state->action = action;
        state->position = event.position;
        state->press_event = event;
        state->physically_pressed = true;

        struct nav_dual_state *other = other_nav_dual(state);
        if (other->physically_pressed &&
            (other->phase == NAV_DUAL_PENDING || other->phase == NAV_DUAL_HOLDING)) {
            k_work_cancel_delayable(&other->hold_work);
            activate_nav_dual_hold(other);
            press_nav_dual_tap(state, other, event);
            return ZMK_BEHAVIOR_OPAQUE;
        }

        const uint8_t mods = zmk_hid_get_explicit_mods();
        if (has_any(mods, MOD_ALT | MOD_CG)) {
            press_nav_dual_tap(state, NULL, event);
            return ZMK_BEHAVIOR_OPAQUE;
        }

        if ((state->last_tap_released_at + NAV_DUAL_QUICK_TAP_MS) > k_uptime_get()) {
            press_nav_dual_tap(state, NULL, event);
            return ZMK_BEHAVIOR_OPAQUE;
        }

        state->phase = NAV_DUAL_PENDING;
        k_work_schedule(&state->hold_work, K_MSEC(NAV_DUAL_TAPPING_TERM_MS));
        return ZMK_BEHAVIOR_OPAQUE;
    }

    state->physically_pressed = false;
    switch (state->phase) {
    case NAV_DUAL_PENDING:
        k_work_cancel_delayable(&state->hold_work);
        handle_navigation(nav_dual_to_navigation(state->action), true, event);
        handle_navigation(nav_dual_to_navigation(state->action), false, event);
        state->last_tap_released_at = k_uptime_get();
        reset_nav_dual(state);
        break;
    case NAV_DUAL_TAP_DOWN:
        finish_nav_dual_tap(state, event);
        break;
    case NAV_DUAL_HOLDING:
        if (state->dependents > 0) {
            state->release_pending = true;
        } else {
            release_nav_dual_hold(state);
            reset_nav_dual(state);
        }
        break;
    case NAV_DUAL_IDLE:
        break;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int adv2_resolver_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    const uint16_t action = binding->param1;

    if (action & ADV2_DUAL_FLAG) {
        return handle_nav_dual(action, true, event);
    }

    if (action & ADV2_NAV_FLAG) {
        return handle_navigation(action, true, event);
    }

    resolve_semantic(action & ADV2_ID_MASK, (action & ADV2_HOLD_FLAG) != 0);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int adv2_resolver_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    const uint16_t action = binding->param1;

    if (action & ADV2_DUAL_FLAG) {
        return handle_nav_dual(action, false, event);
    }

    if (action & ADV2_NAV_FLAG) {
        return handle_navigation(action, false, event);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api adv2_resolver_driver_api = {
    .binding_pressed = adv2_resolver_pressed,
    .binding_released = adv2_resolver_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int adv2_resolver_init(const struct device *dev) {
    nav_duals[0].action = ADV2_DUAL_DELETE;
    nav_duals[1].action = ADV2_DUAL_BACKSPACE;
    nav_duals[0].last_tap_released_at = INT64_MIN / 2;
    nav_duals[1].last_tap_released_at = INT64_MIN / 2;
    reset_nav_dual(&nav_duals[0]);
    reset_nav_dual(&nav_duals[1]);
    k_work_init_delayable(&nav_duals[0].hold_work, nav_dual_hold_timer);
    k_work_init_delayable(&nav_duals[1].hold_work, nav_dual_hold_timer);
    return 0;
}

#define ADV2_RESOLVER_INST(n)                                                                     \
    BEHAVIOR_DT_INST_DEFINE(n, adv2_resolver_init, NULL, NULL, NULL, POST_KERNEL,                  \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &adv2_resolver_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ADV2_RESOLVER_INST)

#endif
