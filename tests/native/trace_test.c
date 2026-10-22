/* Test the production trace body with deterministic clock/UART substitutes. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ADV360_SCOPED_TRACE 1
#include <adv360_trace.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ARG_UNUSED(x) (void)(x)
#define IS_ENABLED(x) 1
#define DT_NODELABEL(x) 0
#define DEVICE_DT_GET(x) (&dummy_device)
#define UART_LINE_CTRL_DTR 1
#define K_THREAD_DEFINE(...)
struct device { int unused; };
static struct device dummy_device;
struct k_spinlock { int unused; };
typedef int k_spinlock_key_t;
static int64_t mock_ms;
static char serial_output[200000];
static size_t serial_count;
static int64_t k_uptime_get(void) { return mock_ms; }
static uint32_t k_cycle_get_32(void) { return mock_ms * 1000; }
static uint32_t k_cyc_to_us_floor32(uint32_t x) { return x; }
static k_spinlock_key_t k_spin_lock(struct k_spinlock *x) { return 0; }
static void k_spin_unlock(struct k_spinlock *x, k_spinlock_key_t key) {}
static void k_msleep(int x) { mock_ms += x; }
static bool device_is_ready(const struct device *d) { return true; }
static int uart_poll_in(const struct device *d, unsigned char *c) { return -1; }
static int uart_fifo_fill(const struct device *d, const uint8_t *bytes, int length) {
    static unsigned calls;
    if (++calls % 4 == 0) return 0; /* Backpressure must not lose bytes. */
    int written = length > 7 ? 7 : length;
    assert(serial_count + written < sizeof(serial_output));
    memcpy(serial_output + serial_count, bytes, written);
    serial_count += written; serial_output[serial_count] = 0;
    return written;
}
static int uart_line_ctrl_get(const struct device *d, int cmd, uint32_t *value) {
    *value = 1; return 0;
}
#include TRACE_SOURCE

static void start(void) { command("ADVTRACE START"); assert(active); }
static void stop(void) { command("ADVTRACE STOP"); assert(!active); }
int main(void) {
    assert(!active);
    adv360_trace_position(ADV_TRACE_SCAN, 38, true, 0, 0);
    assert(used == 0);
    command("START"); assert(!active);
    command("ADVTRACE INFO"); assert(!active && used == 0);
    puts("PASS recorder inactive until exact START command");

    start();
    for (int p = 0; p < 128; p++) adv360_trace_position(ADV_TRACE_SCAN, p, true, 0, 0);
    assert(used == 13);
    for (uint32_t i = 0; i < used; i++) assert(adv360_trace_allowed_position(records[i].code));
    stop(); start();
    for (int u = 0; u < 256; u++) adv360_trace_emit(0x70000 | u, true, 0);
    assert(used == 14);
    for (uint32_t i = 0; i < used; i++) assert(adv360_trace_allowed_usage(records[i].code));
    puts("PASS position/usage allowlists exclude every letter and Space");

    stop(); start();
    uint8_t snapshot[16]; memset(snapshot, 255, sizeof(snapshot));
    adv360_trace_snapshot(snapshot, sizeof(snapshot), 0); assert(used == 13);
    puts("PASS split snapshots retain only permitted positions");

    stop(); start();
    uint8_t report[22] = {1}; report[3] = 0xf0; /* A-D, no allowed key. */
    adv360_trace_usb(ADV_TRACE_USB_SUBMIT, report, sizeof(report), 0, 0); assert(used == 0);
    report[1] = 8; report[3 + 0x2a / 8] |= 1 << (0x2a % 8);
    adv360_trace_usb(ADV_TRACE_USB_SUBMIT, report, sizeof(report), 0, 0);
    assert(used == 1 && records[0].code == (8 | 256));
    report[3] = 0;
    adv360_trace_usb(ADV_TRACE_USB_SUBMIT, report, sizeof(report), 0, 0); assert(used == 1);
    puts("PASS USB records contain only modifier/navigation mask, never raw reports");

    int64_t initial_start = started_ms;
    mock_ms += 100;
    command("ADVTRACE START"); assert(started_ms == initial_start);
    mock_ms = initial_start + 60000;
    adv360_trace_position(ADV_TRACE_SCAN, 38, true, 0, 0);
    assert(!active && used == 1);
    puts("PASS 60-second limit and no deadline extension by repeated START");

    start();
    for (int i = 0; i <= TRACE_CAPACITY; i++) adv360_trace_position(ADV_TRACE_SCAN, 38, i % 2, 0, 0);
    assert(!active && used == TRACE_CAPACITY && dropped == 1);
    puts("PASS full buffer freezes without overwriting or blocking input");

    start();
    size_t before = serial_count;
    adv360_trace_position(ADV_TRACE_SCAN, 38, true, 0, 0);
    adv360_trace_emit(0x7002a, true, 8);
    assert(serial_count == before);
    command("ADVTRACE DUMP"); assert(!active);
    assert(strstr(serial_output + before, "BEGIN 1 left 2 0\n"));
    assert(strstr(serial_output + before, "END\n"));
    assert(strstr(serial_output + before, "T "));
    adv360_trace_position(ADV_TRACE_SCAN, 38, false, 0, 0); assert(used == 2);
    puts("PASS collection has no serial output; DUMP stops before exporting");
    puts("PASS partial/temporarily full serial FIFO preserves exported bytes");
    puts("0 failed assertions");
    return 0;
}
