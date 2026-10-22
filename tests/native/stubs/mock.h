#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ARG_UNUSED(x) ((void)(x))
#define CONTAINER_OF(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define DT_HAS_COMPAT_STATUS_OKAY(x) 1
#define IS_ENABLED(x) 0
#define DT_INST_FOREACH_STATUS_OKAY(x)
#define LOG_MODULE_DECLARE(...)
#define LOG_ERR(...) mock_error(__VA_ARGS__)
#define K_MSEC(x) (x)
#define K_NO_WAIT 0

struct device { int unused; };
struct k_work { void (*handler)(struct k_work *); };
struct k_work_delayable { struct k_work work; int64_t due; uint64_t order; bool scheduled; };
void mock_error(const char *format, ...);
int64_t k_uptime_get(void);
void k_msleep(int delay);
void k_work_init_delayable(struct k_work_delayable *, void (*)(struct k_work *));
int k_work_cancel_delayable(struct k_work_delayable *);
int k_work_schedule(struct k_work_delayable *, int delay);
int k_work_reschedule(struct k_work_delayable *, int delay);
static inline struct k_work_delayable *k_work_delayable_from_work(struct k_work *w) {
    return CONTAINER_OF(w, struct k_work_delayable, work);
}

struct zmk_behavior_binding { uint32_t param1; };
struct zmk_behavior_binding_event { int32_t position; int64_t timestamp; };
struct behavior_driver_api {
    int (*binding_pressed)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*binding_released)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
};
#define ZMK_BEHAVIOR_OPAQUE 0
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_EV_EVENT_CAPTURED 2
typedef struct { int unused; } zmk_event_t;
struct zmk_position_state_changed { int32_t position; bool state; int64_t timestamp; };
struct zmk_position_state_changed_event {
    zmk_event_t header;
    struct zmk_position_state_changed data;
};
struct zmk_listener { int (*callback)(const zmk_event_t *); };
#define ZMK_LISTENER(name, cb) const struct zmk_listener zmk_listener_##name = {cb}
#define ZMK_SUBSCRIPTION(name, ...) \
    static void mock_subscribe_##name(void) __attribute__((constructor)); \
    static void mock_subscribe_##name(void) { mock_register_listener(&zmk_listener_##name); }
#define ZMK_EVENT_RAISE_AFTER(ev, listener) mock_raise_after(&(ev), &zmk_listener_##listener)
#define ZMK_EVENT_RAISE_AT(ev, listener) mock_raise_at(&(ev), &zmk_listener_##listener)
void mock_register_listener(const struct zmk_listener *);
int mock_raise_after(struct zmk_position_state_changed_event *, const struct zmk_listener *);
int mock_raise_at(struct zmk_position_state_changed_event *, const struct zmk_listener *);
static inline const struct zmk_position_state_changed *as_zmk_position_state_changed(const zmk_event_t *ev) {
    return &((const struct zmk_position_state_changed_event *)ev)->data;
}
static inline struct zmk_position_state_changed_event
copy_raised_zmk_position_state_changed(const struct zmk_position_state_changed *ev) {
    return (struct zmk_position_state_changed_event){.data = *ev};
}
int raise_zmk_keycode_state_changed_from_encoded(uint32_t, bool, int64_t);
uint8_t zmk_hid_get_explicit_mods(void);
bool zmk_keymap_layer_active(uint8_t layer);
