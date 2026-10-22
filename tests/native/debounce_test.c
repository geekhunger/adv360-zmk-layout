/* SPDX-License-Identifier: MIT
 * Exercise the pinned ZMK debouncer itself, not a copy of its algorithm.
 * Input is a clean, synthetic electrical level sampled every 1 ms, matching
 * the board's debounce-scan-period-ms. This cannot prove real switch bounce
 * characteristics, scan scheduling latency, or the cause of every user hitch.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <zmk/debounce.h>

struct sampled_key {
    struct zmk_debounce_state state;
    struct zmk_debounce_config config;
    unsigned downs;
    unsigned ups;
};

static unsigned failures;

static struct sampled_key key_with_threshold(unsigned ms) {
    return (struct sampled_key){.config = {.debounce_press_ms = ms, .debounce_release_ms = ms}};
}

static void check(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void sample(struct sampled_key *key, bool level) {
    zmk_debounce_update(&key->state, level, 1, &key->config);
    if (zmk_debounce_get_changed(&key->state)) {
        if (zmk_debounce_is_pressed(&key->state)) key->downs++;
        else key->ups++;
    }
}

static void feed(struct sampled_key *key, bool level, unsigned ms) {
    for (unsigned i = 0; i < ms; i++) sample(key, level);
}

static struct sampled_key five_taps(unsigned debounce_ms, unsigned width_ms, unsigned gap_ms) {
    struct sampled_key key = key_with_threshold(debounce_ms);
    feed(&key, false, 20);
    for (unsigned i = 0; i < 5; i++) {
        feed(&key, true, width_ms);
        feed(&key, false, gap_ms);
    }
    feed(&key, false, 100);
    check(!zmk_debounce_is_active(&key.state), "debouncer settles after release");
    return key;
}

static void short_press_regression(void) {
    for (unsigned width = 7; width <= 15; width++) {
        struct sampled_key old = five_taps(15, width, 40);
        struct sampled_key corrected = five_taps(5, width, 40);
        check(old.downs == 0 && old.ups == 0, "15 ms loses five isolated 7-15 ms presses");
        check(corrected.downs == 5 && corrected.ups == 5,
              "5 ms preserves five isolated 7-15 ms presses");
    }
    puts("PASS: five short presses disappear at 15 ms, survive at 5 ms");
}

static void short_release_regression(void) {
    for (unsigned gap = 7; gap <= 15; gap++) {
        struct sampled_key old = five_taps(15, 30, gap);
        struct sampled_key corrected = five_taps(5, 30, gap);
        check(old.downs == 1 && old.ups == 1,
              "15 ms merges five 30 ms presses across short release gaps");
        check(corrected.downs == 5 && corrected.ups == 5,
              "5 ms preserves all five presses across short release gaps");
    }
    puts("PASS: five taps with short release gaps merge at 15 ms, stay distinct at 5 ms");
}

static void boundary_regression(void) {
    /* The production integrator checks the threshold BEFORE incrementing.
     * At a 1 ms cadence a threshold of 15 needs 16 contrary samples.
     */
    for (unsigned threshold = 5; threshold <= 15; threshold += 10) {
        struct sampled_key key = key_with_threshold(threshold);
        feed(&key, true, threshold);
        check(key.downs == 0, "threshold samples alone do not yet flip the production integrator");
        sample(&key, true);
        check(key.downs == 1, "next contrary sample flips the production integrator");
        feed(&key, false, threshold);
        check(key.ups == 0, "release follows the same sample-count threshold");
        sample(&key, false);
        check(key.ups == 1 && !zmk_debounce_is_active(&key.state), "release settles on next sample");
    }
    for (unsigned width = 16; width <= 20; width++) {
        struct sampled_key old = five_taps(15, width, 20);
        struct sampled_key corrected = five_taps(5, width, 20);
        check(old.downs == 5 && old.ups == 5 && corrected.downs == 5 && corrected.ups == 5,
              "long enough presses are preserved with both thresholds");
    }
    puts("PASS: real integrator boundary and 16-20 ms press sweep");
}

static void noise_regression(void) {
    for (unsigned width = 1; width < 5; width++) {
        struct sampled_key key = five_taps(5, width, 20);
        check(key.downs == 0 && key.ups == 0, "5 ms still rejects isolated sub-5 ms press noise");
        key = key_with_threshold(5);
        feed(&key, true, 30);
        for (unsigned i = 0; i < 5; i++) {
            feed(&key, false, width);
            feed(&key, true, 20);
        }
        check(key.downs == 1 && key.ups == 0,
              "5 ms rejects short release chatter while key stays held");
        feed(&key, false, 20);
        check(key.ups == 1, "real release is still delivered after ignored release chatter");
    }
    puts("PASS: sub-5 ms isolated noise remains filtered in both directions");
}

static void independent_keys_regression(void) {
    struct sampled_key modifier = key_with_threshold(5);
    struct sampled_key backspace = key_with_threshold(5);
    for (unsigned t = 0; t < 250; t++) {
        sample(&modifier, t < 220);
        sample(&backspace, t < 200 && t % 40 < 30);
    }
    check(modifier.downs == 1 && modifier.ups == 1, "held modifier remains balanced");
    check(backspace.downs == 5 && backspace.ups == 5,
          "five Backspaces remain distinct while another key is held");
    puts("PASS: held modifier and repeated Backspace debounce independently");
}

int main(void) {
    short_press_regression();
    short_release_regression();
    boundary_regression();
    noise_regression();
    independent_keys_regression();
    if (failures) {
        fprintf(stderr, "%u debounce assertions failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("All five production-debouncer test groups passed; no keyboard accessed.");
    return EXIT_SUCCESS;
}
