/* SPDX-License-Identifier: MIT
 * Per-Bluetooth-slot OS memory. No key, HID, timer or resolver interception.
 * USB deliberately uses the currently selected BLE slot as requested.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zmk/activity.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define WINDOWS_LAYER 1
#define SAVE_RETRIES 3

static uint8_t windows[ZMK_BLE_PROFILE_COUNT];
static bool dirty[ZMK_BLE_PROFILE_COUNT];
static uint8_t failures[ZMK_BLE_PROFILE_COUNT];
static struct k_spinlock state_lock;
static bool ready;
static bool restoring;
K_THREAD_STACK_DEFINE(profile_os_stack, 2048);
static struct k_work_q profile_os_queue;

static int profile_os_init(void) {
    static const struct k_work_queue_config config = {.name = "OS profile storage"};
    k_work_queue_start(&profile_os_queue, profile_os_stack,
                       K_THREAD_STACK_SIZEOF(profile_os_stack), 10, &config);
    return 0;
}
SYS_INIT(profile_os_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static void save_profiles(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(save_work, save_profiles);

static void schedule_save(void) {
    k_work_reschedule_for_queue(&profile_os_queue, &save_work,
                               K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
}

static void save_profiles(struct k_work *work) {
    ARG_UNUSED(work);
    // Do not initiate flash writes while the user is actively typing.
    if (zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE) {
        schedule_save();
        return;
    }
    bool retry = false;
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        k_spinlock_key_t key = k_spin_lock(&state_lock);
        bool pending = dirty[i] && failures[i] < SAVE_RETRIES;
        uint8_t value = windows[i];
        k_spin_unlock(&state_lock, key);
        if (!pending) {
            continue;
        }
        char path[32];
        snprintf(path, sizeof(path), "adv360_os/p%d", i);
        int err = settings_save_one(path, &value, sizeof(value));
        key = k_spin_lock(&state_lock);
        if (err == 0) {
            // A toggle during the write must not be lost.
            dirty[i] = windows[i] != value;
            failures[i] = 0;
        } else {
            failures[i]++;
        }
        retry |= dirty[i] && failures[i] < SAVE_RETRIES;
        k_spin_unlock(&state_lock, key);
        if (err) {
            LOG_ERR("OS profile %d save failed: %d", i, err);
        }
    }
    if (retry) {
        schedule_save();
    }
}

static int restore_profile(void) {
    int index = zmk_ble_active_profile_index();
    if (index < 0 || index >= ZMK_BLE_PROFILE_COUNT) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    bool win = windows[index] != 0;
    k_spin_unlock(&state_lock, key);
    if (zmk_keymap_layer_active(WINDOWS_LAYER) == win) {
        return 0;
    }
    restoring = true;
    int err = win ? zmk_keymap_layer_activate(WINDOWS_LAYER)
                  : zmk_keymap_layer_deactivate(WINDOWS_LAYER);
    restoring = false;
    return err;
}

static int profile_os_event(const zmk_event_t *eh) {
    if (!ready) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (as_zmk_ble_active_profile_changed(eh)) {
        // The event can be queued; use the actual active slot, not a stale index.
        restore_profile();
    } else {
        const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
        if (ev && ev->layer == WINDOWS_LAYER && !restoring) {
            int index = zmk_ble_active_profile_index();
            if (index >= 0 && index < ZMK_BLE_PROFILE_COUNT) {
                k_spinlock_key_t key = k_spin_lock(&state_lock);
                bool changed = windows[index] != ev->state;
                if (changed) {
                    windows[index] = ev->state;
                    dirty[index] = true;
                    failures[index] = 0;
                }
                k_spin_unlock(&state_lock, key);
                if (changed) {
                    schedule_save();
                }
            }
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int profile_os_load(const char *name, size_t len, settings_read_cb read_cb,
                           void *cb_arg) {
    // Stable per-slot keys: profile count changes do not invalidate other slots.
    if (name[0] != 'p' || name[1] < '0' || name[1] > '9') {
        return -ENOENT;
    }
    unsigned index = 0;
    for (const char *p = name + 1; *p; p++) {
        if (*p < '0' || *p > '9') {
            return -ENOENT;
        }
        index = index * 10 + (*p - '0');
        if (index >= ZMK_BLE_PROFILE_COUNT) {
            return -EINVAL;
        }
    }
    if (len != sizeof(uint8_t)) {
        return -EINVAL;
    }
    uint8_t value;
    int count = read_cb(cb_arg, &value, sizeof(value));
    if (count < 0) {
        return count;
    }
    if (count != (int)sizeof(value) || value > 1) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    windows[index] = value;
    k_spin_unlock(&state_lock, key);
    return 0;
}

static int profile_os_commit(void) {
    // All h_set callbacks (including ble/active_profile) precede h_commit.
    // Events during settings loading must never overwrite restored values.
    ready = true;
    return restore_profile();
}

SETTINGS_STATIC_HANDLER_DEFINE(adv360_os, "adv360_os", NULL, profile_os_load,
                               profile_os_commit, NULL);
ZMK_LISTENER(adv360_profile_os, profile_os_event);
ZMK_SUBSCRIPTION(adv360_profile_os, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(adv360_profile_os, zmk_layer_state_changed);
