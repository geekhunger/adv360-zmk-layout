/* SPDX-License-Identifier: MIT
 * Opt-in, finite RAM trace. No general logging, raw reports, or letter contents.
 * Collection never writes to USB. Only STOP/DUMP exports the frozen buffer.
 */
#include <adv360_trace.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <stdio.h>
#include <string.h>

#define TRACE_CAPACITY 2048
#define TRACE_LIMIT_MS 60000
struct trace_record {
    uint32_t us;
    uint16_t code;
    uint8_t stage;
    uint8_t down;
    int32_t a;
    int32_t b;
};
static struct trace_record records[TRACE_CAPACITY];
static struct k_spinlock trace_lock;
static uint32_t used, dropped, started_cycle;
static int64_t started_ms;
static bool active;
static uint16_t usb_previous[3];
static uint8_t split_previous[16];
static const struct device *const trace_uart = DEVICE_DT_GET(DT_NODELABEL(adv360_trace_uart));

static uint32_t elapsed_us(void) {
    return k_cyc_to_us_floor32(k_cycle_get_32() - started_cycle);
}

/* Caller owns trace_lock. A full buffer freezes rather than overwriting history. */
static void append(uint8_t stage, uint16_t code, bool down, int32_t a, int32_t b) {
    if (!active) return;
    if (k_uptime_get() - started_ms >= TRACE_LIMIT_MS) {
        active = false;
        return;
    }
    if (used == TRACE_CAPACITY) {
        dropped++;
        active = false;
        return;
    }
    records[used++] = (struct trace_record){elapsed_us(), code, stage, down, a, b};
}

void adv360_trace_position(uint8_t stage, int32_t position, bool down, int32_t a, int32_t b) {
    if (!adv360_trace_allowed_position(position)) return;
    k_spinlock_key_t lock = k_spin_lock(&trace_lock);
    append(stage, position, down, a, b);
    k_spin_unlock(&trace_lock, lock);
}

void adv360_trace_emit(uint32_t encoded, bool down, uint8_t mods) {
    uint16_t usage = encoded & 0xffff;
    uint16_t page = (encoded >> 16) & 0xff;
    if ((page != 0 && page != 7) || !adv360_trace_allowed_usage(usage)) return;
    k_spinlock_key_t lock = k_spin_lock(&trace_lock);
    append(ADV_TRACE_EMIT, usage, down, mods, 0);
    k_spin_unlock(&trace_lock, lock);
}

void adv360_trace_snapshot(const uint8_t *positions, size_t len, int result) {
    k_spinlock_key_t lock = k_spin_lock(&trace_lock);
    if (active) {
        for (int p = 0; p < 128 && (size_t)(p / 8) < len; p++) {
            if (!adv360_trace_allowed_position(p)) continue;
            uint8_t bit = 1u << (p % 8);
            bool down = (positions[p / 8] & bit) != 0;
            if (down != ((split_previous[p / 8] & bit) != 0)) {
                append(ADV_TRACE_SPLIT_TX, p, down, result, 0);
            }
            if (!result) {
                if (down) split_previous[p / 8] |= bit;
                else split_previous[p / 8] &= ~bit;
            }
        }
    }
    k_spin_unlock(&trace_lock, lock);
}

void adv360_trace_usb(uint8_t stage, const uint8_t *report, size_t len,
                      int result, uint32_t wait_us) {
    /* This diagnostic build is NKRO/report protocol. Copy ONLY the 8 modifier
     * bits and the six whitelisted navigation bits, never the raw payload.
     */
    if (len < 14 || report[0] != 1 || stage < ADV_TRACE_USB_WAIT || stage > ADV_TRACE_USB_DONE) return;
    uint16_t mask = report[1];
    static const uint8_t usages[] = {0x2a, 0x4c, 0x4f, 0x50, 0x51, 0x52};
    for (size_t i = 0; i < ARRAY_SIZE(usages); i++) {
        if (report[3 + usages[i] / 8] & (1u << (usages[i] % 8))) mask |= 1u << (8 + i);
    }
    k_spinlock_key_t lock = k_spin_lock(&trace_lock);
    size_t slot = stage - ADV_TRACE_USB_WAIT;
    if (active && (mask != usb_previous[slot] || result || wait_us >= 5000)) {
        append(stage, mask, false, result, wait_us);
        usb_previous[slot] = mask;
    }
    k_spin_unlock(&trace_lock, lock);
}

static void serial_line(const char *text) {
    uint32_t dtr = 0;
    if (uart_line_ctrl_get(trace_uart, UART_LINE_CTRL_DTR, &dtr) || !dtr) return;
    while (*text) uart_poll_out(trace_uart, *text++);
}

static const char *role(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    return "left";
#else
    return "right";
#endif
}

static void stop_capture(void) {
    k_spinlock_key_t lock = k_spin_lock(&trace_lock);
    active = false;
    k_spin_unlock(&trace_lock, lock);
}

static void command(const char *text) {
    char line[160];
    if (!strcmp(text, "ADVTRACE START")) {
        k_spinlock_key_t lock = k_spin_lock(&trace_lock);
        if (active) {
            k_spin_unlock(&trace_lock, lock);
            serial_line("ERROR already_active\n");
            return;
        }
        used = dropped = 0;
        memset(usb_previous, 0, sizeof(usb_previous));
        memset(split_previous, 0, sizeof(split_previous));
        started_ms = k_uptime_get();
        started_cycle = k_cycle_get_32();
        active = true;
        k_spin_unlock(&trace_lock, lock);
        snprintf(line, sizeof(line), "START 1 %s 60000\n", role());
        serial_line(line);
    } else if (!strcmp(text, "ADVTRACE STOP")) {
        stop_capture();
        serial_line("STOP\n");
    } else if (!strcmp(text, "ADVTRACE DUMP")) {
        stop_capture();
        snprintf(line, sizeof(line), "BEGIN 1 %s %u %u\n", role(), used, dropped);
        serial_line(line);
        for (uint32_t i = 0; i < used; i++) {
            const struct trace_record *r = &records[i];
            snprintf(line, sizeof(line), "T %u %u %u %u %d %d\n",
                     r->us, r->stage, r->code, r->down, r->a, r->b);
            serial_line(line);
        }
        serial_line("END\n");
    } else if (!strcmp(text, "ADVTRACE INFO")) {
        snprintf(line, sizeof(line), "INFO 1 %s %u %u\n", role(), TRACE_CAPACITY, TRACE_LIMIT_MS);
        serial_line(line);
    }
}

static void trace_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    if (!device_is_ready(trace_uart)) return;
    char line[40];
    size_t length = 0;
    bool overflow = false;
    for (;;) {
        unsigned char ch;
        while (uart_poll_in(trace_uart, &ch) == 0) {
            if (ch == '\n') {
                if (!overflow) { line[length] = 0; command(line); }
                length = 0; overflow = false;
            } else if (ch != '\r') {
                if (length + 1 < sizeof(line)) line[length++] = ch;
                else overflow = true;
            }
        }
        k_spinlock_key_t lock = k_spin_lock(&trace_lock);
        if (active && k_uptime_get() - started_ms >= TRACE_LIMIT_MS) active = false;
        k_spin_unlock(&trace_lock, lock);
        k_msleep(10);
    }
}
K_THREAD_DEFINE(adv360_trace_thread, 2048, trace_thread, NULL, NULL, NULL, 7, 0, 500);
