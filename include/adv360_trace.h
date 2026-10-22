/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum adv360_trace_stage {
    ADV_TRACE_SCAN = 1,
    ADV_TRACE_SPLIT_RX = 2,
    ADV_TRACE_RESOLVER_IN = 3,
    ADV_TRACE_RESOLVER_OUT = 4,
    ADV_TRACE_EMIT = 5,
    ADV_TRACE_USB_WAIT = 6,
    ADV_TRACE_USB_SUBMIT = 7,
    ADV_TRACE_USB_DONE = 8,
    ADV_TRACE_SPLIT_TX = 9,
    ADV_TRACE_SCAN_QUEUE = 10,
    ADV_TRACE_MOD_TAP_INPUT = 11,
};

/* Exactly the authorized Alt/Delete, Alt/Backspace and NUM positions. */
static inline bool adv360_trace_allowed_position(int32_t p) {
    return p == 35 || p == 38 || p == 70;
}
static inline bool adv360_trace_allowed_usage(uint16_t u) {
    return u == 0x2a || u == 0x4c || u == 0xe2 || u == 0xe6;
}

#ifdef CONFIG_ADV360_SCOPED_TRACE
void adv360_trace_position(uint8_t stage, int32_t position, bool down, int32_t a, int32_t b);
void adv360_trace_emit(uint32_t encoded, bool down, uint8_t mods);
void adv360_trace_snapshot(const uint8_t *positions, size_t len, int result);
void adv360_trace_usb(uint8_t stage, const uint8_t *report, size_t len,
                      int result, uint32_t elapsed_us);
#else
static inline void adv360_trace_position(uint8_t stage, int32_t position, bool down,
                                          int32_t a, int32_t b) {}
static inline void adv360_trace_emit(uint32_t encoded, bool down, uint8_t mods) {}
static inline void adv360_trace_snapshot(const uint8_t *positions, size_t len, int result) {}
static inline void adv360_trace_usb(uint8_t stage, const uint8_t *report, size_t len,
                                     int result, uint32_t elapsed_us) {}
#endif
