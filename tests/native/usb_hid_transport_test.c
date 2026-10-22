/* Compile the actual USB send function and state from the selected ZMK tree.
 * The simulated nrfx endpoint retains the pointer until transfer completion,
 * as usb_dc_nrfx.c does; it deliberately does NOT eagerly copy the report.
 * These tests establish buffer ownership, not physical USB delivery latency.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_ZMK_USB_BOOT 1
#define CONFIG_ZMK_POINTING 1
#define IS_ENABLED(x) (x)
#define K_MSEC(x) (x)
struct device { int unused; };
struct k_sem { unsigned count; };
#define K_SEM_DEFINE(name, initial, limit) struct k_sem name = { initial }
struct zmk_hid_keyboard_report { uint8_t data[22]; };
struct zmk_hid_consumer_report { uint8_t data[9]; };
struct zmk_hid_mouse_report { uint8_t data[9]; };
typedef struct { uint8_t data[8]; } zmk_hid_boot_report_t;
enum { USB_DC_SUSPEND, USB_DC_ERROR, USB_DC_RESET, USB_DC_DISCONNECTED,
       USB_DC_UNKNOWN, USB_DC_CONFIGURED };

static const uint8_t *inflight;
static size_t inflight_len;
static uint8_t received[16][64];
static size_t received_len[16];
static unsigned received_count, submitted, failures;
static bool complete_on_wait;
static int write_error, usb_status;
static void complete_transfer(void);
static int zmk_usb_get_status(void) { return usb_status; }
static int usb_wakeup_request(void) { return 0; }
static void k_sem_give(struct k_sem *s) { s->count = 1; }
static int k_sem_take(struct k_sem *s, int timeout) {
    (void)timeout;
    if (!s->count && complete_on_wait && inflight) complete_transfer();
    if (!s->count) return -EAGAIN;
    s->count = 0;
    return 0;
}
static int hid_int_ep_write(const struct device *dev, const uint8_t *data,
                           size_t len, void *written) {
    (void)dev; (void)written;
    submitted++;
    if (write_error) return write_error;
    if (inflight) return -EAGAIN;
    inflight = data;
    inflight_len = len;
    return 0;
}

#include USB_TRANSPORT_SOURCE

static void complete_transfer(void) {
    assert(inflight && inflight_len <= sizeof(received[0]) && received_count < 16);
    memcpy(received[received_count], inflight, inflight_len);
    received_len[received_count++] = inflight_len;
    inflight = NULL;
    in_ready_cb(NULL);
}
static void fresh(void) {
    inflight = NULL; inflight_len = 0; received_count = 0; submitted = 0;
    complete_on_wait = true; write_error = 0; usb_status = USB_DC_CONFIGURED;
    hid_sem.count = 1;
    memset(received, 0, sizeof(received));
}
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); failures++; } } while(0)

static void test_short_tap(void) {
    fresh();
    uint8_t live_report[22] = {1};
    live_report[8] = 0x04; /* NKRO Backspace bit, after report ID/mod/reserved. */
    CHECK(zmk_usb_hid_send_report(live_report, sizeof(live_report)) == 0);
    live_report[8] = 0; /* ZMK releases the key before the driver's completion. */
    CHECK(zmk_usb_hid_send_report(live_report, sizeof(live_report)) == 0);
    complete_transfer();
    CHECK(received_count == 2);
    CHECK(received[0][8] == 0x04);
    CHECK(received[1][8] == 0);
}
static void test_modifier_and_key_snapshots(void) {
    fresh();
    uint8_t live_report[22] = {1, 8}; /* Cmd down. */
    CHECK(zmk_usb_hid_send_report(live_report, sizeof(live_report)) == 0);
    live_report[8] = 0x04;
    CHECK(zmk_usb_hid_send_report(live_report, sizeof(live_report)) == 0);
    live_report[1] = 0; live_report[8] = 0;
    CHECK(zmk_usb_hid_send_report(live_report, sizeof(live_report)) == 0);
    complete_transfer();
    CHECK(received_count == 3);
    CHECK(received[0][1] == 8 && received[0][8] == 0);
    CHECK(received[1][1] == 8 && received[1][8] == 0x04);
    CHECK(received[2][1] == 0 && received[2][8] == 0);
}
static void test_timeout_does_not_reuse_inflight_buffer(void) {
    fresh(); complete_on_wait = false;
    uint8_t first[22] = {1, 8}; first[8] = 0x04;
    uint8_t next[22] = {1};
    CHECK(zmk_usb_hid_send_report(first, sizeof(first)) == 0);
    CHECK(zmk_usb_hid_send_report(next, sizeof(next)) == -EAGAIN);
    CHECK(submitted == 1); CHECK(hid_sem.count == 0);
    complete_transfer();
    CHECK(received[0][1] == 8 && received[0][8] == 0x04);
    CHECK(zmk_usb_hid_send_report(next, sizeof(next)) == 0);
    complete_transfer();
    CHECK(received[1][1] == 0 && received[1][8] == 0);
}
static void test_failed_submit_can_retry(void) {
    fresh(); write_error = -EIO;
    uint8_t report[22] = {1};
    CHECK(zmk_usb_hid_send_report(report, sizeof(report)) == -EIO);
    CHECK(hid_sem.count == 1 && inflight == NULL);
    write_error = 0;
    CHECK(zmk_usb_hid_send_report(report, sizeof(report)) == 0);
    complete_transfer();
}
static void test_multiple_report_sizes(void) {
    fresh();
    uint8_t keyboard[22] = {1, 2}; uint8_t consumer[9] = {2, 3};
    uint8_t mouse[9] = {3, 4}; uint8_t boot[8] = {5};
    uint8_t *reports[] = {keyboard,consumer,mouse,boot};
    const size_t lengths[] = {22,9,9,8};
    const uint8_t ids[] = {1,2,3,5};
    for (int i=0;i<4;i++) {
        CHECK(zmk_usb_hid_send_report(reports[i], lengths[i]) == 0);
        reports[i][0] = 99;
    }
    complete_transfer();
    CHECK(received_count == 4);
    for(int i=0;i<4;i++) { CHECK(received[i][0] == ids[i]); CHECK(received_len[i] == lengths[i]); }
}
static void run(const char *name, void (*test)(void)) {
    unsigned before = failures; test();
    printf("%s %s\n", before == failures ? "PASS" : "FAIL", name);
}
int main(void) {
    run("short USB tap survives deferred DMA",test_short_tap);
    run("USB modifier and key reports remain distinct",test_modifier_and_key_snapshots);
    run("USB timeout cannot overwrite an in-flight report",test_timeout_does_not_reuse_inflight_buffer);
    run("failed USB submission releases its semaphore",test_failed_submit_can_retry);
    run("keyboard/consumer/mouse/boot reports keep their contents",test_multiple_report_sizes);
    printf("%u failed assertions\n", failures);
    return failures ? 1 : 0;
}
