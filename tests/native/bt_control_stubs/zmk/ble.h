#include <mock.h>
#define ZMK_BEHAVIOR_TRANSPARENT 1
int zmk_ble_prof_select(uint8_t index);
void zmk_ble_clear_bonds(void);
int zmk_keymap_layer_activate(uint8_t layer);
int zmk_keymap_layer_deactivate(uint8_t layer);
int zmk_keymap_layer_toggle(uint8_t layer);
