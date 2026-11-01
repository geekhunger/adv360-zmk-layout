/* Execute the real MOD controller; it must not emit any host key events. */
#include <assert.h>
#include <stdio.h>
#include "../../src/behaviors/behavior_adv360_bt_control.c"

static bool layers[5];
static int active_profile;
static unsigned toggles, clears, selections;
static uint8_t remembered_modes[5];

bool zmk_keymap_layer_active(uint8_t l) { assert(l < 5); return layers[l]; }
int zmk_keymap_layer_activate(uint8_t l) { assert(l == ADV360_BLUETOOTH); layers[l] = true; return 0; }
int zmk_keymap_layer_deactivate(uint8_t l) { assert(l == ADV360_BLUETOOTH); layers[l] = false; return 0; }
int adv360_profile_os_cycle(void) {
    assert(!layers[ADV360_BLUETOOTH] && !bt_control_timeout_work.scheduled);
    remembered_modes[active_profile] = (remembered_modes[active_profile] + 1) % 3;
    layers[1] = remembered_modes[active_profile] == 1;
    layers[2] = remembered_modes[active_profile] == 2;
    toggles++;
    return 0;
}
int zmk_ble_prof_select(uint8_t i) {
    assert(i < 5 && !layers[ADV360_BLUETOOTH]); active_profile = i; selections++;
    layers[1] = remembered_modes[i] == 1; layers[2] = remembered_modes[i] == 2; return 0;
}
void zmk_ble_clear_bonds(void) { assert(!layers[ADV360_BLUETOOTH]); clears++; }
void k_work_init_delayable(struct k_work_delayable *w, void (*fn)(struct k_work *)) {
    *w = (struct k_work_delayable){.work.handler = fn};
}
int k_work_cancel_delayable(struct k_work_delayable *w) { w->scheduled = false; return 0; }
int k_work_reschedule(struct k_work_delayable *w, int delay) {
    assert(delay == 1500); w->scheduled = true; return 1;
}
static int press(uint32_t action) {
    struct zmk_behavior_binding b = {.param1 = action};
    return bt_control_pressed(&b, (struct zmk_behavior_binding_event){.position = 28});
}
static int release(uint32_t action) {
    struct zmk_behavior_binding b = {.param1 = action};
    return bt_control_released(&b, (struct zmk_behavior_binding_event){.position = 28});
}
static void arm(void) {
    assert(press(ADV360_BT_ARM) == 0 && layers[ADV360_BLUETOOTH]);
    assert(release(ADV360_BT_ARM) == 0 && layers[ADV360_BLUETOOTH]);
}
int main(void) {
    assert(bt_control_init(NULL) == 0);
    // Sequential MOD -> former OS switch position toggles exactly once.
    for (int profile = 0; profile < 5; profile++) {
        zmk_ble_prof_select(profile);
        for (int n = 0; n < 3; n++) {
            uint8_t before = remembered_modes[profile]; arm();
            assert(press(ADV360_BT_TOGGLE_OS) == 0);
            assert(remembered_modes[profile] == (before + 1) % 3 && !layers[ADV360_BLUETOOTH]);
            assert(layers[1] == (remembered_modes[profile] == 1));
            assert(layers[2] == (remembered_modes[profile] == 2));
            unsigned count = toggles;
            assert(release(ADV360_BT_TOGGLE_OS) == 0 && toggles == count);
        }
    }
    assert(toggles == 15);
    // Works with NUM additionally held, but does not release NUM itself.
    layers[ADV360_NUMPAD] = true; arm(); press(ADV360_BT_TOGGLE_OS);
    assert(layers[ADV360_NUMPAD] && !layers[ADV360_BLUETOOTH]); release(ADV360_BT_TOGGLE_OS);
    // Ordinary input still passes through and exits MOD; timeout cancels only MOD.
    arm(); assert(press(ADV360_BT_CANCEL) == ZMK_BEHAVIOR_TRANSPARENT);
    assert(release(ADV360_BT_CANCEL) == ZMK_BEHAVIOR_TRANSPARENT && !layers[ADV360_BLUETOOTH]);
    unsigned count = toggles; arm(); bt_control_timeout(NULL);
    assert(!layers[ADV360_BLUETOOTH] && toggles == count);
    // Existing MOD -> NUM -> 1..5 selection and clear commands stay intact.
    for (unsigned i = 0; i < 5; i++) {
        layers[ADV360_NUMPAD] = true; arm(); unsigned before = selections;
        assert(press(ADV360_BT_SELECT_1 + i) == 0);
        assert(selections == before + 1 && active_profile == (int)i);
        assert(release(ADV360_BT_SELECT_1 + i) == 0);
    }
    layers[ADV360_NUMPAD] = false; arm(); unsigned before = selections;
    assert(press(ADV360_BT_SELECT_1) == ZMK_BEHAVIOR_TRANSPARENT);
    assert(release(ADV360_BT_SELECT_1) == ZMK_BEHAVIOR_TRANSPARENT && selections == before);
    arm(); press(ADV360_BT_CLEAR); release(ADV360_BT_CLEAR); assert(clears == 1);
    arm(); press(ADV360_BT_ARM); assert(!layers[ADV360_BLUETOOTH]);
    puts("PASS MOD OS toggle: three-mode cycle, all slots, one-shot exit, opaque release, NUM retained, timeout and existing Bluetooth commands");
}
