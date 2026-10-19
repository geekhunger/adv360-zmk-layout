/*
 * SPDX-License-Identifier: MIT
 *
 * Sequential Bluetooth control for geekhunger's Advantage 360 layout.
 */

#define DT_DRV_COMPAT geekhunger_behavior_adv360_bt_control

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/ble.h>
#include <zmk/keymap.h>

#include <dt-bindings/zmk/adv360_bt_control.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define NUMPAD_LAYER 2
#define BLUETOOTH_LAYER 3
#define BT_CONTROL_TIMEOUT_MS 1500
#define ADV360_MAX_POSITIONS 128

static struct k_work_delayable bt_control_timeout_work;
static bool profile_passthrough[ADV360_MAX_POSITIONS];

static void deactivate_bt_control(void) {
    k_work_cancel_delayable(&bt_control_timeout_work);
    zmk_keymap_layer_deactivate(BLUETOOTH_LAYER);
}

static void bt_control_timeout(struct k_work *item) {
    ARG_UNUSED(item);
    zmk_keymap_layer_deactivate(BLUETOOTH_LAYER);
}

static int bt_control_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    const uint32_t action = binding->param1;

    switch (action) {
    case ADV360_BT_ARM:
        if (zmk_keymap_layer_active(BLUETOOTH_LAYER)) {
            deactivate_bt_control();
        } else {
            zmk_keymap_layer_activate(BLUETOOTH_LAYER);
            k_work_reschedule(&bt_control_timeout_work,
                              K_MSEC(BT_CONTROL_TIMEOUT_MS));
        }
        return ZMK_BEHAVIOR_OPAQUE;

    case ADV360_BT_CANCEL:
        deactivate_bt_control();
        return ZMK_BEHAVIOR_TRANSPARENT;

    case ADV360_BT_SELECT_1:
    case ADV360_BT_SELECT_2:
    case ADV360_BT_SELECT_3:
    case ADV360_BT_SELECT_4:
    case ADV360_BT_SELECT_5: {
        const bool num_active = zmk_keymap_layer_active(NUMPAD_LAYER);
        deactivate_bt_control();

        if (!num_active) {
            if (event.position < ADV360_MAX_POSITIONS) {
                profile_passthrough[event.position] = true;
            }
            return ZMK_BEHAVIOR_TRANSPARENT;
        }

        return zmk_ble_prof_select(action - ADV360_BT_SELECT_1);
    }

    case ADV360_BT_CLEAR:
        deactivate_bt_control();
        zmk_ble_clear_bonds();
        return ZMK_BEHAVIOR_OPAQUE;

    default:
        return -ENOTSUP;
    }
}

static int bt_control_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    const uint32_t action = binding->param1;

    if (action == ADV360_BT_CANCEL) {
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    if (action >= ADV360_BT_SELECT_1 && action <= ADV360_BT_SELECT_5 &&
        event.position < ADV360_MAX_POSITIONS &&
        profile_passthrough[event.position]) {
        profile_passthrough[event.position] = false;
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api bt_control_driver_api = {
    .binding_pressed = bt_control_pressed,
    .binding_released = bt_control_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int bt_control_init(const struct device *dev) {
    ARG_UNUSED(dev);
    memset(profile_passthrough, 0, sizeof(profile_passthrough));
    k_work_init_delayable(&bt_control_timeout_work, bt_control_timeout);
    return 0;
}

#define ADV360_BT_CONTROL_INST(n)                                                                 \
    BEHAVIOR_DT_INST_DEFINE(n, bt_control_init, NULL, NULL, NULL, POST_KERNEL,                    \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &bt_control_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ADV360_BT_CONTROL_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
