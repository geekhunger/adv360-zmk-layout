#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define ZMK_BLE_PROFILE_COUNT 5
#define CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE 29500
#define ARG_UNUSED(x) ((void)(x))
#define K_MSEC(x) (x)
#define ZMK_EV_EVENT_BUBBLE 0
#define LOG_MODULE_DECLARE(...)
#define LOG_ERR(...) ((void)0)
struct k_work { void (*handler)(struct k_work *); };
struct k_work_delayable { struct k_work work; bool scheduled; };
#define K_WORK_DELAYABLE_DEFINE(name, fn) struct k_work_delayable name = {{fn}, false}
struct k_work_q { int unused; };
struct k_work_queue_config { const char *name; };
#define K_THREAD_STACK_DEFINE(name, size) unsigned char name[size]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)
#define SYS_INIT(...)
void k_work_queue_start(struct k_work_q *, void *, size_t, int, const struct k_work_queue_config *);
struct k_spinlock { int held; };
typedef int k_spinlock_key_t;
k_spinlock_key_t k_spin_lock(struct k_spinlock *);
void k_spin_unlock(struct k_spinlock *, k_spinlock_key_t);
int k_work_reschedule_for_queue(struct k_work_q *, struct k_work_delayable *, int);
typedef int (*settings_read_cb)(void *, void *, size_t);
int settings_save_one(const char *, const void *, size_t);
#define SETTINGS_STATIC_HANDLER_DEFINE(...)
enum zmk_activity_state { ZMK_ACTIVITY_ACTIVE, ZMK_ACTIVITY_IDLE, ZMK_ACTIVITY_SLEEP };
enum zmk_activity_state zmk_activity_get_state(void);
int zmk_ble_active_profile_index(void);
bool zmk_keymap_layer_active(uint8_t);
int zmk_keymap_layer_activate(uint8_t);
int zmk_keymap_layer_deactivate(uint8_t);
struct zmk_layer_state_changed { uint8_t layer; bool state; };
struct zmk_ble_active_profile_changed { uint8_t index; };
typedef struct {
 int type;
 struct zmk_layer_state_changed layer;
 struct zmk_ble_active_profile_changed profile;
} zmk_event_t;
static inline const struct zmk_layer_state_changed *as_zmk_layer_state_changed(const zmk_event_t *e) {
 return e->type == 1 ? &e->layer : NULL;
}
static inline const struct zmk_ble_active_profile_changed *as_zmk_ble_active_profile_changed(const zmk_event_t *e) {
 return e->type == 2 ? &e->profile : NULL;
}
#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)
