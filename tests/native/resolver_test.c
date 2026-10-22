/* Execute the production resolver, with virtual time and a recording HID sink.
 * This checks state/order invariants, not USB, radio, or real scheduler latency.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#ifndef ADV2_RESOLVER_SOURCE
#define ADV2_RESOLVER_SOURCE "../../src/behaviors/behavior_adv2_resolver.c"
#endif
#include ADV2_RESOLVER_SOURCE

static int64_t now_ms;
static uint64_t work_order;
static struct k_work_delayable *workers[100];
static size_t worker_count;
static unsigned errors, failures;
static bool windows_mode, number_mode;
static uint8_t hid_mods;
static unsigned hid_mod_counts[8];
static bool hid_keys[256], physical_keys[128];
struct output { int64_t time; uint32_t code; bool down; uint8_t mods; };
static struct output outputs[10000];
static size_t output_count;

void mock_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    errors++;
}
int64_t k_uptime_get(void) { return now_ms; }
void k_msleep(int delay) { now_ms += delay; }
void k_work_init_delayable(struct k_work_delayable *work, void (*fn)(struct k_work *)) {
    *work = (struct k_work_delayable){.work.handler = fn};
    workers[worker_count++] = work;
}
int k_work_cancel_delayable(struct k_work_delayable *work) { work->scheduled = false; return 0; }
int k_work_reschedule(struct k_work_delayable *work, int delay) {
    work->scheduled = true;
    work->due = now_ms + delay;
    work->order = ++work_order;
    return 1;
}
int k_work_schedule(struct k_work_delayable *work, int delay) {
    return work->scheduled ? 0 : k_work_reschedule(work, delay);
}
uint8_t zmk_hid_get_explicit_mods(void) { return hid_mods; }
bool zmk_keymap_layer_active(uint8_t layer) {
    return layer == WINDOWS_LAYER ? windows_mode : layer == NUMPAD_LAYER && number_mode;
}
int raise_zmk_keycode_state_changed_from_encoded(uint32_t encoded, bool down, int64_t time) {
    uint32_t code = encoded & 0x00ffffffu;
    unsigned usage = code & 0xffff;
    if (code >= LCTRL && code <= RGUI) {
        unsigned mod = code - LCTRL;
        if (down) hid_mod_counts[mod]++;
        else if (hid_mod_counts[mod]) hid_mod_counts[mod]--;
        else mock_error("Unbalanced HID modifier release 0x%x", code);
        if (hid_mod_counts[mod]) hid_mods |= 1u << mod;
        else hid_mods &= ~(1u << mod);
    } else if (usage < ARRAY_SIZE(hid_keys)) {
        /* ZMK pre-releases a usage that is already down before another press. */
        hid_keys[usage] = down;
    }
    if (output_count >= ARRAY_SIZE(outputs)) abort();
    outputs[output_count++] = (struct output){time, code, down, hid_mods | SELECT_MODS(encoded)};
    return 0;
}

static const uint16_t semantic_at[128] = {
    [16]=ADV2_W, [17]=ADV2_E, [18]=ADV2_R, [19]=ADV2_F,
    [22]=ADV2_Y, [23]=ADV2_U, [24]=ADV2_I, [25]=ADV2_O,
    [29]=ADV2_A, [30]=ADV2_S, [31]=ADV2_D, [32]=ADV2_T, [33]=ADV2_G,
    [40]=ADV2_H, [41]=ADV2_N, [42]=ADV2_K, [43]=ADV2_L, [44]=ADV2_P,
    [47]=ADV2_Z, [48]=ADV2_X, [49]=ADV2_C, [50]=ADV2_V, [51]=ADV2_B,
    [54]=ADV2_J, [55]=ADV2_M, [56]=ADV2_COMMA, [57]=ADV2_DOT, [58]=ADV2_SLASH,
};
static int dispatch_keymap(struct zmk_position_state_changed_event *ev) {
    int p = ev->data.position;
    struct zmk_behavior_binding binding = {.param1=semantic_at[p]};
    struct zmk_behavior_binding_event event = {.position=p, .timestamp=ev->data.timestamp};
    for (size_t i=0; i<ARRAY_SIZE(mod_taps); i++) {
        if (mod_taps[i].position == p) binding.param1 = mod_taps[i].action;
    }
    switch (p) {
    case 71: binding.param1=ADV2_NAV_LEFT; break;
    case 72: binding.param1=ADV2_NAV_DOWN; break;
    case 73: binding.param1=ADV2_NAV_UP; break;
    case 74: binding.param1=ADV2_NAV_RIGHT; break;
    case 52: emit_code(LCTRL, ev->data.state); return 0;
    case 64: emit_code(LGUI, ev->data.state); return 0;
    case 65: emit_code(SPACE, ev->data.state); return 0;
    }
    if (!binding.param1) return 0;
    return ev->data.state ? adv2_resolver_pressed(&binding, event) : adv2_resolver_released(&binding, event);
}
int mock_raise_after(struct zmk_position_state_changed_event *ev, const struct zmk_listener *after) {
    int result;
    if (!after) {
        result = mod_tap_capture_position_listener(&ev->header);
        if (result != ZMK_EV_EVENT_BUBBLE) return result < 0 ? result : 0;
    }
    if (!after || after == &zmk_listener_adv2_mod_tap_capture) {
        result = semantic_tap_dance_position_listener(&ev->header);
        if (result != ZMK_EV_EVENT_BUBBLE) return result < 0 ? result : 0;
    }
    return dispatch_keymap(ev);
}
static void advance(int64_t target) {
    unsigned rounds=0;
    while (true) {
        struct k_work_delayable *next=NULL;
        for (size_t i=0; i<worker_count; i++) {
            struct k_work_delayable *w=workers[i];
            if (w->scheduled && w->due<=target &&
                (!next || w->due<next->due || (w->due==next->due && w->order<next->order))) next=w;
        }
        if (!next) break;
        if (++rounds>10000) { mock_error("Work queue did not settle"); break; }
        if (next->due>now_ms) now_ms=next->due;
        next->scheduled=false;
        next->work.handler(&next->work);
    }
    if (target>now_ms) now_ms=target;
}
static void key(int64_t time, int p, bool down) {
    advance(time);
    if (physical_keys[p]==down) { mock_error("Invalid physical sequence at position %d",p); return; }
    physical_keys[p]=down;
    struct zmk_position_state_changed_event ev={.data={p,down,time}};
    int result=mock_raise_after(&ev,NULL);
    if (result<0) mock_error("Dispatch failed: %d",result);
}
static void fresh(void) {
    now_ms=1000; work_order=0; worker_count=0; errors=0; output_count=0;
    windows_mode=false; number_mode=false; hid_mods=0;
    memset(hid_mod_counts,0,sizeof(hid_mod_counts));
    memset(hid_keys,0,sizeof(hid_keys));
    memset(physical_keys,0,sizeof(physical_keys));
    memset(nav_states,0,sizeof(nav_states));
    adv2_resolver_init(NULL);
}
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"  line %d: %s\n",__LINE__,#condition); failures++; \
} } while(0)
static size_t downs(uint32_t code, int mods) {
    size_t count=0;
    for(size_t i=0;i<output_count;i++) {
        if(outputs[i].down && outputs[i].code==code && (mods<0 || outputs[i].mods==mods)) count++;
    }
    return count;
}
static void settled(void) {
    advance(now_ms+1000);
    CHECK(errors==0);
    CHECK(hid_mods==0);
    CHECK(deferred_input_event_count==0);
    CHECK(pulse_deferred_event_count==0);
    CHECK(!pulse_order_barrier_active);
    for(size_t i=0;i<ARRAY_SIZE(mod_taps);i++) CHECK(mod_taps[i].phase==MOD_TAP_IDLE);
    for(size_t i=0;i<ARRAY_SIZE(hid_keys);i++) CHECK(!hid_keys[i]);
}
static void test_single_backspaces(void) {
    for(int duration=10;duration<=200;duration+=10) {
        fresh(); key(1000,38,true); key(1000+duration,38,false);
        CHECK(downs(BACKSPACE,0)==1); settled();
    }
}
static void test_modified_backspaces(void) {
    const int modifier_positions[]={35,67,68,66,69};
    const int expected[]={MOD_LALT,MOD_LGUI,MOD_LGUI,MOD_LSFT,MOD_RSFT};
    for(size_t m=0;m<ARRAY_SIZE(modifier_positions);m++) {
        fresh(); key(1000,modifier_positions[m],true);
        for(int i=0;i<5;i++) {key(1020+i*50,38,true);key(1040+i*50,38,false);}
        key(1280,modifier_positions[m],false);
        settled(); CHECK(downs(BACKSPACE,expected[m])==5);
    }
}
static void test_alternating_arrows(void) {
    fresh(); key(1000,35,true); key(1010,67,true);
    for(int i=0;i<10;i++) {int p=i%2?71:74;key(1020+i*30,p,true);key(1040+i*30,p,false);}
    key(1330,35,false);key(1340,67,false); settled();
    CHECK(downs(RIGHT,MOD_LALT|MOD_LGUI)==5);
    CHECK(downs(LEFT,MOD_LALT|MOD_LGUI)==5);
}
static void test_modifier_repress_during_arrow(void) {
    fresh();
    key(1000,35,true);key(1010,74,true);key(1020,35,false);
    key(1030,35,true);key(1040,74,false);key(1050,74,true);
    key(1060,74,false);key(1070,35,false);settled();
    CHECK(downs(RIGHT,MOD_LALT)==2);
    CHECK(downs(DELETE,-1)==0);
}
static void test_modifier_does_not_follow_new_word(void) {
    fresh();
    key(1000,35,true);key(1010,74,true);key(1020,35,false);
    key(1030,29,true);key(1040,74,false);key(1050,29,false);
    key(1060,51,true);key(1070,51,false);settled();
    CHECK(downs(B,0)==1);
    CHECK(downs(KP_PLUS,-1)==0);
}
static void test_word_then_backspaces(void) {
    fresh();
    key(1000,44,true);key(1010,44,false);key(1020,44,true);key(1030,44,false);
    key(1040,38,true);key(1050,38,false);key(1060,38,true);key(1070,38,false);
    key(1080,38,true);key(1090,38,false);settled();
    CHECK(downs(P,0)==2);CHECK(downs(BACKSPACE,0)==3);
}
static void test_backspace_repress_during_arrow(void) {
    fresh();
    key(1000,38,true);key(1010,74,true);key(1020,38,false);
    key(1030,38,true);key(1040,74,false);key(1050,38,false);settled();
    CHECK(downs(BACKSPACE,0)==1);
}
static void test_shift_tab_and_force_quit(void) {
    fresh();key(1000,69,true);key(1010,66,true);key(1020,66,false);key(1040,69,false);
    settled();CHECK(downs(TAB,MOD_RSFT)==1);CHECK(downs(ENTER,-1)==0);
    fresh();key(1000,35,true);key(1010,67,true);key(1020,68,true);
    key(1030,68,false);key(1050,67,false);key(1060,35,false);settled();
    CHECK(downs(ESCAPE,MOD_LALT|MOD_LGUI)==1);
}
static void test_plain_rolls_and_spaces(void) {
    fresh(); key(1000,42,true);key(1010,43,true);key(1020,42,false);
    key(1030,65,true);key(1040,43,false);key(1050,65,false);settled();
    CHECK(downs(K,0)==1);CHECK(downs(L,0)==1);CHECK(downs(SPACE,0)==1);
}
static void test_double_letter_space(void) {
    fresh();key(1000,44,true);key(1010,44,false);key(1020,44,true);key(1030,44,false);
    key(1040,65,true);key(1050,65,false);settled();
    size_t letters=0;
    for(size_t i=0;i<output_count;i++) {
        if(outputs[i].down&&outputs[i].code==P) letters++;
        if(outputs[i].down&&outputs[i].code==SPACE) CHECK(letters==2);
    }
    CHECK(downs(P,0)==2);CHECK(downs(SPACE,0)==1);
}
static void test_hold_and_repeat(void) {
    fresh();key(1000,23,true);key(1200,23,false);settled();CHECK(downs(LBKT,0)==1);
    fresh();key(1000,38,true);key(1020,38,false);key(1060,38,true);
    advance(1500);CHECK(hid_keys[BACKSPACE&0xffff]);key(1510,38,false);settled();
    CHECK(downs(BACKSPACE,0)==2);
}
static void test_early_shift_release(void) {
    fresh();key(1000,67,true);key(1010,66,true);key(1020,66,false);
    key(1030,44,true);key(1040,44,false);key(1050,67,false);settled();
    CHECK(downs(P,MOD_LGUI|MOD_LSFT)==1);
    CHECK(downs(TAB,-1)==0);CHECK(downs(ESCAPE,-1)==0);
}
static void test_shortcut_behind_repeated_letter(void) {
    fresh();key(1000,44,true);key(1010,44,false);key(1020,44,true);key(1030,44,false);
    key(1040,35,true);key(1041,74,true);key(1042,35,false);key(1043,74,false);
    settled();CHECK(downs(P,0)==2);CHECK(downs(RIGHT,MOD_LALT)==1);
    CHECK(downs(DELETE,-1)==0);
}
static void test_rolled_delete_chord(void) {
    const int modifier[]={35,67,68,66,69};
    const int expected[]={MOD_LALT,MOD_LGUI,MOD_LGUI,MOD_LSFT,MOD_RSFT};
    for(size_t m=0;m<ARRAY_SIZE(modifier);m++) {
        fresh();key(1000,modifier[m],true);key(1010,38,true);
        key(1020,modifier[m],false);key(1030,38,false);settled();
        CHECK(downs(BACKSPACE,expected[m])==1);
    }
}
static void test_arrow_chord_release_permutations(void) {
    const int held[]={35,67,66,74};
    for(int a=0;a<4;a++)for(int b=0;b<4;b++)for(int c=0;c<4;c++)for(int d=0;d<4;d++) {
        if(a==b||a==c||a==d||b==c||b==d||c==d)continue;
        fresh();for(int i=0;i<4;i++)key(1000+i*10,held[i],true);
        const int order[]={a,b,c,d};
        for(int i=0;i<4;i++)key(1050+i*10,held[order[i]],false);
        settled();CHECK(downs(RIGHT,MOD_LALT|MOD_LGUI|MOD_LSFT)==1);
        CHECK(downs(DELETE,-1)==0);CHECK(downs(ESCAPE,-1)==0);CHECK(downs(TAB,-1)==0);
    }
}
static void test_repress_after_space(void) {
    fresh();key(1000,35,true);key(1100,65,true);key(1110,35,false);
    key(1120,35,true);key(1130,65,false);key(1140,74,true);
    key(1150,74,false);key(1160,35,false);settled();
    CHECK(downs(RIGHT,MOD_LALT)==1);CHECK(downs(DELETE,-1)==0);
}
static void test_shifted_double_after_modifier_release(void) {
    fresh();key(1000,66,true);key(1100,42,true);key(1110,42,false);
    key(1120,42,true);key(1130,42,false);key(1140,66,false);settled();
    CHECK(downs(K,MOD_LSFT)==2);CHECK(downs(K,0)==0);
}
static uint32_t random_state=0x360;
static uint32_t next_random(void) {
    random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;
    return random_state;
}
static void test_mixed_sequence_cleanup(void) {
    const int positions[]={35,38,66,67,68,69,71,72,73,74,42,43,44,65};
    for(unsigned seed=1;seed<=2000;seed++) {
        fresh();random_state=seed;unsigned before=failures;
        for(unsigned i=0;i<100;i++) {
            int p=positions[next_random()%ARRAY_SIZE(positions)];
            key(now_ms+1+(next_random()%35),p,!physical_keys[p]);
        }
        for(size_t i=0;i<ARRAY_SIZE(positions);i++) if(physical_keys[positions[i]])key(now_ms+1,positions[i],false);
        settled();
        if(failures!=before) {fprintf(stderr,"  sequence seed %u\n",seed);break;}
    }
}
static void test_windows_navigation(void) {
    fresh();windows_mode=true;key(1000,35,true);key(1010,74,true);key(1020,74,false);
    key(1030,35,false);settled();CHECK(downs(RIGHT,MOD_LCTL)==1);
    fresh();windows_mode=true;key(1000,67,true);key(1010,74,true);key(1020,74,false);
    key(1030,67,false);settled();CHECK(downs(END,0)==1);
    fresh();windows_mode=true;key(1000,35,true);key(1010,38,true);key(1020,35,false);
    key(1030,38,false);settled();CHECK(downs(BACKSPACE,MOD_LCTL)==1);
    fresh();windows_mode=true;key(1000,67,true);key(1010,38,true);key(1020,67,false);
    key(1030,38,false);settled();CHECK(downs(HOME,MOD_LSFT)==1);CHECK(downs(BACKSPACE,0)==1);
}
static void test_symbol_snapshots(void) {
    fresh();key(1000,35,true);key(1100,18,true);key(1110,18,false);key(1120,35,false);
    settled();CHECK(downs(R,MOD_LALT)==1);
    fresh();key(1000,66,true);key(1100,23,true);key(1280,23,false);key(1290,66,false);
    settled();CHECK(downs(LBKT,MOD_LSFT)==1);
    fresh();key(1000,19,true);key(1180,19,false);settled();CHECK(downs(N8,MOD_LALT)==1);
    fresh();windows_mode=true;key(1000,19,true);key(1180,19,false);settled();CHECK(downs(N7,MOD_RALT)==1);
    fresh();key(1000,35,true);
    for(int i=0;i<4;i++){key(1100+i*25,57,true);key(1110+i*25,57,false);}
    key(1210,35,false);settled();CHECK(downs(N1,MOD_LSFT)==4);
}
static void test_delayed_pulse_queue_wrap(void) {
    fresh();
    /* Exercise reuse of every slot without allocating any state at runtime. */
    for(int round=0;round<3;round++) {
        emit_code(LSHIFT,true);
        for(int i=0;i<255;i++)pulse_code(K);
        emit_code(LSHIFT,false);
        advance(now_ms+3200);
        emit_code(LCTRL,true);
        for(int i=0;i<100;i++)pulse_code(K);
        emit_code(LCTRL,false);
        advance(now_ms+5000);
    }
    settled();CHECK(downs(K,MOD_LSFT)==765);CHECK(downs(K,0)==0);
    CHECK(downs(K,MOD_LCTL)==300);
}
static void test_word_correction_timing_sweep(void) {
    for(int step=1;step<=60;step++) {
        fresh();int64_t t=1000;
        const int word[]={42,43,44,44};
        for(size_t i=0;i<ARRAY_SIZE(word);i++){key(t,word[i],true);t+=step;key(t,word[i],false);t+=step;}
        for(int i=0;i<5;i++){key(t,38,true);t+=step;key(t,38,false);t+=step;}
        key(t,65,true);key(t+step,65,false);settled();
        CHECK(downs(K,0)==1);CHECK(downs(L,0)==1);CHECK(downs(P,0)==2);
        CHECK(downs(BACKSPACE,0)==5);CHECK(downs(SPACE,0)==1);
    }
}
static void test_long_session_without_reset(void) {
    const int positions[]={35,38,66,67,68,69,71,72,73,74,42,43,44,65};
    fresh();random_state=0x360037;
    for(unsigned i=0;i<50000;i++) {
        int p=positions[next_random()%ARRAY_SIZE(positions)];
        key(now_ms+1+(next_random()%35),p,!physical_keys[p]);
        if(output_count>8000)output_count=0; /* Only the trace storage is rotated. */
    }
    for(size_t i=0;i<ARRAY_SIZE(positions);i++)if(physical_keys[positions[i]])key(now_ms+1,positions[i],false);
    settled();
}
static void run(const char *name,void(*test)(void)) {
    const char *filter=getenv("TEST_FILTER");
    if(filter && !strstr(name,filter))return;
    unsigned before=failures;test();printf("%s %s\n",failures==before?"PASS":"FAIL",name);
    if(failures!=before || getenv("TEST_TRACE")) for(size_t i=0;i<output_count&&i<40;i++) {
        fprintf(stderr,"  %"PRId64" %s %04x mods=%02x\n",outputs[i].time,outputs[i].down?"down":"up",outputs[i].code,outputs[i].mods);
    }
}
int main(void) {
    run("single Backspace duration sweep",test_single_backspaces);
    run("repeated modified Backspaces",test_modified_backspaces);
    run("Alt+Cmd alternating arrows",test_alternating_arrows);
    run("repress Alt while old arrow is down",test_modifier_repress_during_arrow);
    run("released Alt must not follow a new word",test_modifier_does_not_follow_new_word);
    run("double letter then single Backspace taps",test_word_then_backspaces);
    run("reuse right Alt as Backspace during arrow release",test_backspace_repress_during_arrow);
    run("opposite Shift+Tab and Alt+Cmd+Esc",test_shift_tab_and_force_quit);
    run("plain overlapping letters and Space",test_plain_rolls_and_spaces);
    run("double letter before Space",test_double_letter_space);
    run("letter Hold and tap-tap-hold deletion",test_hold_and_repeat);
    run("early Shift release before shortcut key",test_early_shift_release);
    run("Alt+Right queued behind double letter",test_shortcut_behind_repeated_letter);
    run("modifier released before Backspace",test_rolled_delete_chord);
    run("all three-modifier arrow release permutations",test_arrow_chord_release_permutations);
    run("repress Alt while Space is still held",test_repress_after_space);
    run("both delayed double letters retain Shift",test_shifted_double_after_modifier_release);
    run("mixed sequences leave no held keys or queued events",test_mixed_sequence_cleanup);
    run("Windows word/line navigation and rolled Backspace",test_windows_navigation);
    run("symbol mappings preserve captured modifiers",test_symbol_snapshots);
    run("delayed modifier snapshots survive queue wrap",test_delayed_pulse_queue_wrap);
    run("word then five corrections over 60 timings",test_word_correction_timing_sweep);
    run("50000 mixed events without resolver reset",test_long_session_without_reset);
    printf("%u failed assertions\n",failures);
    return failures?1:0;
}
