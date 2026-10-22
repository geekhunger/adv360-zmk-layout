/* Run the production module with synchronous layer events and mock NVS. */
#include <assert.h>
#include <stdlib.h>
#include "../../src/adv360_profile_os.c"

static int active_profile;
static bool layer_windows;
static enum zmk_activity_state activity;
static unsigned schedules, writes, layer_changes;
static uint8_t persisted[5];
static int storage_error;
static bool toggle_during_write;

k_spinlock_key_t k_spin_lock(struct k_spinlock *s) {
    assert(!s->held); s->held = 1; return 0;
}
void k_spin_unlock(struct k_spinlock *s, k_spinlock_key_t key) {
    (void)key; assert(s->held); s->held = 0;
}
int k_work_reschedule_for_queue(struct k_work_q *q, struct k_work_delayable *w, int ms) {
    assert(q == &profile_os_queue && ms == 29500 && !state_lock.held);
    schedules++; w->scheduled = true; return 1;
}
void k_work_queue_start(struct k_work_q *q, void *stack, size_t size, int priority,
                        const struct k_work_queue_config *config) {
    assert(q == &profile_os_queue && stack == profile_os_stack && size == 2048);
    assert(priority == 10 && config->name);
}
enum zmk_activity_state zmk_activity_get_state(void) { return activity; }
int zmk_ble_active_profile_index(void) { return active_profile; }
bool zmk_keymap_layer_active(uint8_t l) { assert(l == 1); return layer_windows; }
static int layer_event(uint8_t layer, bool state) {
    zmk_event_t ev = {.type = 1, .layer = {layer, state}};
    return profile_os_event(&ev);
}
static int set_layer(bool state) {
    if (layer_windows == state) return 0;
    layer_windows = state; layer_changes++;
    return layer_event(1, state);
}
int zmk_keymap_layer_activate(uint8_t l) { assert(l == 1); return set_layer(true); }
int zmk_keymap_layer_deactivate(uint8_t l) { assert(l == 1); return set_layer(false); }
static void toggle(void) { assert(set_layer(!layer_windows) == 0); }
static void profile_event(int index) {
    zmk_event_t ev = {.type = 2, .profile = {(uint8_t)index}};
    assert(profile_os_event(&ev) == 0);
}
static void select_profile(int index) { active_profile = index; profile_event(index); }
int settings_save_one(const char *path, const void *data, size_t len) {
    assert(!state_lock.held && activity != ZMK_ACTIVITY_ACTIVE && len == 1);
    int index = -1;
    assert(sscanf(path, "adv360_os/p%d", &index) == 1 && index >= 0 && index < 5);
    writes++;
    if (toggle_during_write) { toggle_during_write = false; toggle(); }
    if (storage_error) return storage_error;
    persisted[index] = *(const uint8_t *)data;
    return 0;
}
static void run_save(void) {
    assert(save_work.scheduled);
    save_work.scheduled = false;
    save_work.work.handler(&save_work.work);
}
static void reset(void) {
    memset(windows, 0, sizeof(windows)); memset(dirty, 0, sizeof(dirty));
    memset(failures, 0, sizeof(failures)); memset(persisted, 0, sizeof(persisted));
    ready = restoring = layer_windows = toggle_during_write = false;
    active_profile = storage_error = 0;
    schedules = writes = layer_changes = 0;
    save_work.scheduled = false;
    activity = ZMK_ACTIVITY_IDLE;
}
static int read_value(void *arg, void *data, size_t len) {
    assert(len == 1); *(uint8_t *)data = *(uint8_t *)arg; return 1;
}
static int read_short(void *arg, void *data, size_t len) {
    (void)arg; (void)data; (void)len; return 0;
}
static int read_error(void *arg, void *data, size_t len) {
    (void)arg; (void)data; (void)len; return -EIO;
}

int main(void) {
    assert(profile_os_init() == 0);
    // First boot: all slots Mac, no writes on startup/reconnection/NUM changes.
    reset(); assert(profile_os_commit() == 0);
    for (int i = 0; i < 5; i++) { select_profile(i); assert(!layer_windows); }
    layer_event(2, true); layer_event(2, false); layer_event(3, true);
    assert(!schedules && !writes);

    // Exact existing toggle, all five independent slots, USB has no special path.
    for (int mask = 0; mask < 32; mask++) {
        reset(); assert(profile_os_commit() == 0);
        for (int i = 0; i < 5; i++) {
            select_profile(i);
            if (mask & (1 << i)) toggle();
        }
        unsigned before = schedules;
        for (int i = 4; i >= 0; i--) {
            select_profile(i); assert(layer_windows == !!(mask & (1 << i)));
            profile_event(i); // Connection notification, not a manual toggle.
        }
        assert(schedules == before);
        if (save_work.scheduled) run_save();
        for (int i = 0; i < 5; i++) assert(persisted[i] == !!(mask & (1 << i)));
        assert(!save_work.scheduled);
    }

    // Settings load order: ignore early events; apply AFTER all settings loaded.
    reset(); uint8_t one = 1, zero = 0;
    profile_event(0); layer_event(1, true);
    assert(!schedules);
    assert(profile_os_load("p3", 1, read_value, &one) == 0);
    active_profile = 3; // ble/active_profile was loaded after our setting.
    assert(profile_os_commit() == 0 && layer_windows && !schedules);
    select_profile(0); assert(!layer_windows);
    active_profile = 3; profile_event(0); // Stale queued index must not win.
    assert(layer_windows && !schedules);

    // Restart simulation: durable choices restored, not overwritten by restore.
    toggle(); toggle(); run_save(); assert(persisted[3] == 1);
    uint8_t saved[5]; memcpy(saved, persisted, sizeof(saved)); reset();
    for (int i = 0; i < 5; i++) {
        char name[8]; snprintf(name, sizeof(name), "p%d", i);
        assert(profile_os_load(name, 1, read_value, &saved[i]) == 0);
    }
    active_profile = 3; assert(profile_os_commit() == 0 && layer_windows && !schedules);
    toggle(); run_save(); assert(persisted[3] == 0); // Explicit Mac persists too.

    // No storage writes in input callbacks; postpone while actively typing.
    reset(); profile_os_commit(); activity = ZMK_ACTIVITY_ACTIVE; toggle();
    assert(!writes); run_save(); assert(!writes && save_work.scheduled);
    activity = ZMK_ACTIVITY_IDLE; run_save(); assert(writes == 1 && !save_work.scheduled);

    // Changes arriving during NVS save remain dirty and get a subsequent save.
    reset(); profile_os_commit(); toggle(); toggle_during_write = true;
    run_save(); assert(dirty[0] && save_work.scheduled && persisted[0] == 1);
    run_save(); assert(!dirty[0] && persisted[0] == 0 && !save_work.scheduled);

    // Storage failure: bounded retries, RAM choice intact; next toggle retries.
    reset(); profile_os_commit(); toggle(); storage_error = -EIO;
    for (int i = 0; i < SAVE_RETRIES; i++) run_save();
    assert(writes == SAVE_RETRIES && !save_work.scheduled && layer_windows);
    storage_error = 0; toggle(); run_save(); assert(!dirty[0] && persisted[0] == 0);

    // Malformed settings never change a valid entry.
    reset(); assert(profile_os_load("p0", 1, read_value, &one) == 0);
    const char *bad[] = {"", "p", "p-1", "p5", "p9999999999999", "p0/extra", "other"};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); i++)
        assert(profile_os_load(bad[i], 1, read_value, &zero) < 0);
    uint8_t bad_value = 2;
    assert(profile_os_load("p0", 1, read_value, &bad_value) == -EINVAL);
    assert(profile_os_load("p0", 0, read_value, &zero) == -EINVAL);
    assert(profile_os_load("p0", 2, read_value, &zero) == -EINVAL);
    assert(profile_os_load("p0", 1, read_short, NULL) == -EINVAL);
    assert(profile_os_load("p0", 1, read_error, NULL) == -EIO);
    assert(windows[0] == 1);
    profile_os_commit(); select_profile(255); toggle(); assert(!schedules);
    puts("PASS OS profiles: 32 combinations, startup/load, reconnect, USB-independent slot ownership, delayed persistence, races, bounded retries, malformed data");
}
