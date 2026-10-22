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
#undef DT_DRV_COMPAT
#ifndef NUM_MOD_MORPH_SOURCE
#define NUM_MOD_MORPH_SOURCE "../../../zmk/app/src/behaviors/behavior_mod_morph.c"
#endif
#include NUM_MOD_MORPH_SOURCE
#include <dt-bindings/zmk/backlight.h>

static int64_t now_ms;
static uint64_t work_order;
static struct k_work_delayable *workers[100];
static size_t worker_count;
static unsigned errors, failures;
static bool windows_mode, number_mode;
static uint8_t hid_mods;
static uint8_t masked_mods;
static bool keymap_num_at_press[128];
static unsigned backlight_inc,backlight_dec;
static const struct behavior_mod_morph_config num_configs[4] = {
    {{.behavior_dev="kp",.param1=C_PREV},{.behavior_dev="bl",.param1=BL_DEC_CMD},MOD_SHIFT,MOD_SHIFT},
    {{.behavior_dev="kp",.param1=C_VOL_DN},{.behavior_dev="kp",.param1=C_BRI_DN},MOD_SHIFT,MOD_SHIFT},
    {{.behavior_dev="kp",.param1=C_VOL_UP},{.behavior_dev="kp",.param1=C_BRI_UP},MOD_SHIFT,MOD_SHIFT},
    {{.behavior_dev="kp",.param1=C_NEXT},{.behavior_dev="bl",.param1=BL_INC_CMD},MOD_SHIFT,MOD_SHIFT},
};
static struct behavior_mod_morph_data num_data[4];
static const struct device num_devices[4] = {
    {&num_configs[0],&num_data[0]}, {&num_configs[1],&num_data[1]},
    {&num_configs[2],&num_data[2]}, {&num_configs[3],&num_data[3]},
};
static const char *num_names[]={"num_left","num_down","num_up","num_right"};
static unsigned hid_report_delay_ms;
static unsigned hid_mod_counts[8];
static bool hid_keys[256], physical_keys[128];
struct output { int64_t time; uint32_t code; bool down; uint8_t mods; };
static struct output outputs[10000];
static size_t output_count;
static const struct zmk_listener *listeners[10];
static size_t listener_count;

void mock_register_listener(const struct zmk_listener *listener) {
    if(listener_count>=ARRAY_SIZE(listeners))abort();
    listeners[listener_count++]=listener;
}

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
int zmk_hid_masked_modifiers_set(zmk_mod_flags_t mods) { masked_mods=mods;return 0; }
int zmk_hid_masked_modifiers_clear(void) { masked_mods=0;return 0; }
const struct device *zmk_behavior_get_binding(const char *name) {
    for(size_t i=0;i<4;i++)if(strcmp(name,num_names[i])==0)return &num_devices[i];
    abort();
}
int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event,bool pressed) {
    if(strcmp(binding->behavior_dev,"kp")==0)
        return raise_zmk_keycode_state_changed_from_encoded(binding->param1,pressed,event.timestamp);
    if(strcmp(binding->behavior_dev,"bl")==0) {
        if(pressed) {
            if(binding->param1==BL_INC_CMD)backlight_inc++;
            else if(binding->param1==BL_DEC_CMD)backlight_dec++;
            else abort();
        }
        return 0;
    }
    abort();
}
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
    outputs[output_count++] = (struct output){time, code, down, (hid_mods & ~masked_mods) | SELECT_MODS(encoded)};
    now_ms += hid_report_delay_ms;
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
    if(ev->data.state)keymap_num_at_press[p]=number_mode;
    if(p==70){number_mode=ev->data.state;return 0;}
    if(keymap_num_at_press[p]) {
        if(p>=71 && p<=74) {
            binding.behavior_dev=num_names[p-71];
            return ev->data.state ? on_mod_morph_binding_pressed(&binding,event)
                                 : on_mod_morph_binding_released(&binding,event);
        }
        switch(p) {
        case 65: emit_code(C_PP,ev->data.state);return 0;
        case 66: emit_code(LSHIFT,ev->data.state);return 0;
        case 69: emit_code(RSHIFT,ev->data.state);return 0;
        case 38:
            return handle_navigation(ADV2_NAV_BACKSPACE,ev->data.state,event);
        case 67: break; /* NUM-transparent Cmd/Esc. */
        default: return 0;
        }
    }
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
static const struct zmk_listener *listener_at(size_t index) {
    return listeners[getenv("TEST_REVERSE_LISTENERS")?listener_count-1-index:index];
}
static int mock_raise_from(struct zmk_position_state_changed_event *ev, size_t start) {
    for(size_t i=start;i<listener_count;i++) {
        int result=listener_at(i)->callback(&ev->header);
        if(result!=ZMK_EV_EVENT_BUBBLE)return result<0?result:0;
    }
    return dispatch_keymap(ev);
}
int mock_raise_after(struct zmk_position_state_changed_event *ev, const struct zmk_listener *after) {
    if(!after)return mock_raise_from(ev,0);
    for(size_t i=0;i<listener_count;i++)if(listener_at(i)==after)return mock_raise_from(ev,i+1);
    mock_error("Replay AFTER an unregistered listener");return -EINVAL;
}
int mock_raise_at(struct zmk_position_state_changed_event *ev, const struct zmk_listener *at) {
    for(size_t i=0;i<listener_count;i++)if(listener_at(i)==at)return mock_raise_from(ev,i);
    mock_error("Replay AT an unregistered listener");return -EINVAL;
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
/* A real message-queue worker drains a batch without running delayed work
 * between every position. Keep this path separate from key(), which always
 * advances due timers first. Callers explicitly choose when workers can run.
 */
static void batch_key_at(int p, bool down, int64_t input_time) {
    if (physical_keys[p]==down) { mock_error("Invalid batched physical sequence at %d",p); return; }
    physical_keys[p]=down;
    struct zmk_position_state_changed_event ev={.data={p,down,input_time}};
    int result=mock_raise_after(&ev,NULL);
    if(result<0)mock_error("Batched dispatch failed: %d",result);
}
static void batch_key(int p, bool down) { batch_key_at(p,down,now_ms); }
static void fresh(void) {
    now_ms=1000; work_order=0; worker_count=0; errors=0; output_count=0;
    windows_mode=false; number_mode=false; hid_mods=0; hid_report_delay_ms=0;
    masked_mods=0;backlight_inc=0;backlight_dec=0;
    memset(num_data,0,sizeof(num_data));
    for(size_t i=0;i<4;i++)behavior_mod_morph_init(&num_devices[i]);
    memset(keymap_num_at_press,0,sizeof(keymap_num_at_press));
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
    CHECK(masked_mods==0);
    for(size_t i=0;i<4;i++)CHECK(num_data[i].pressed_binding==NULL);
    for(size_t i=0;i<ARRAY_SIZE(mod_taps);i++) CHECK(mod_taps[i].phase==MOD_TAP_IDLE);
    for(size_t i=0;i<ARRAY_SIZE(hid_keys);i++) CHECK(!hid_keys[i]);
}
static void test_single_backspaces(void) {
    for(int duration=10;duration<=200;duration+=10) {
        fresh(); key(1000,38,true); key(1000+duration,38,false);
        /* Long solitary presses are Alt holds, not retroactive corrections. */
        CHECK(downs(BACKSPACE,0)==(duration<90?1u:0u)); settled();
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
static void test_word_then_modified_backspace_sweep(void) {
    const int modifiers[]={35,67,68,66,69};
    const int expected[]={MOD_LALT,MOD_LGUI,MOD_LGUI,MOD_LSFT,MOD_RSFT};
    for(size_t m=0;m<ARRAY_SIZE(modifiers);m++)for(int step=1;step<=60;step++) {
        fresh();int64_t t=1000;
        const int word[]={42,43,44,44};
        for(size_t i=0;i<ARRAY_SIZE(word);i++){key(t,word[i],true);t+=step;key(t,word[i],false);t+=step;}
        key(t,modifiers[m],true);t+=step;
        for(int i=0;i<5;i++) {
            key(t,38,true);t+=step;key(t,38,false);t+=step;
        }
        advance(t+100);
        CHECK(downs(BACKSPACE,expected[m])==5);
        key(t+101,modifiers[m],false);settled();
        CHECK(downs(K,0)==1);CHECK(downs(L,0)==1);CHECK(downs(P,0)==2);
        CHECK(downs(BACKSPACE,-1)==5);
    }
}
static void test_word_tail_overlaps_modified_backspace(void) {
    const int modifiers[]={35,67,68,66,69};
    const int expected[]={MOD_LALT,MOD_LGUI,MOD_LGUI,MOD_LSFT,MOD_RSFT};
    for(size_t m=0;m<ARRAY_SIZE(modifiers);m++)for(int overlap=0;overlap<=150;overlap+=5) {
        fresh();key(1000,42,true);key(1015,43,true);key(1030,42,false);
        key(1045,44,true);key(1060,43,false);key(1075,modifiers[m],true);
        key(1080,38,true);
        if(overlap<=20) {key(1080+overlap,44,false);key(1100,38,false);}
        else {key(1100,38,false);key(1080+overlap,44,false);}
        for(int i=0;i<4;i++){key(1250+i*60,38,true);key(1270+i*60,38,false);}
        advance(1500);
        CHECK(downs(BACKSPACE,expected[m])==5);
        key(1510,modifiers[m],false);settled();
        CHECK(downs(K,0)==1);CHECK(downs(L,0)==1);CHECK(downs(P,0)==1);
    }
}
static void test_modified_corrections_with_report_delays(void) {
    const int modifiers[]={-1,35,67,68};
    const int expected[]={0,MOD_LALT,MOD_LGUI,MOD_LGUI};
    for(size_t m=0;m<ARRAY_SIZE(modifiers);m++)for(int delay=1;delay<=30;delay++) {
        fresh();hid_report_delay_ms=delay;int64_t t=1000;
        const int word[]={42,43,44,44};
        for(size_t i=0;i<ARRAY_SIZE(word);i++){key(t,word[i],true);t+=20;key(t,word[i],false);t+=20;}
        if(modifiers[m]>=0){key(t,modifiers[m],true);t+=20;}
        for(int i=0;i<8;i++){key(t,38,true);t+=20;key(t,38,false);t+=20;}
        advance(now_ms+500);
        unsigned before=failures;
        CHECK(downs(BACKSPACE,expected[m])==8);
        if(failures!=before)fprintf(stderr,"  modifier %d report delay %d ms\n",modifiers[m],delay);
        if(modifiers[m]>=0)key(now_ms+1,modifiers[m],false);
        settled();
    }
}
static void test_correction_behind_pulse_barrier(void) {
    const int modifiers[]={35,67,68};
    const int expected[]={MOD_LALT,MOD_LGUI,MOD_LGUI};
    for(size_t m=0;m<ARRAY_SIZE(modifiers);m++) {
        fresh();
        key(1000,42,true);key(1010,42,false);key(1020,42,true);key(1030,42,false);
        key(1040,modifiers[m],true);key(1041,38,true);key(1042,38,false);
        advance(1100);CHECK(downs(BACKSPACE,expected[m])==1);
        for(int i=0;i<5;i++) {
            key(1110+i*50,38,true);key(1130+i*50,38,false);
            advance(1145+i*50);CHECK(downs(BACKSPACE,expected[m])==(size_t)i+2);
        }
        key(1400,modifiers[m],false);settled();
        CHECK(downs(K,0)==2);CHECK(downs(DELETE,-1)==0);CHECK(downs(ESCAPE,-1)==0);
    }
}
static void test_pending_word_precedes_correction(void) {
    const int modifiers[]={35,67,68,66,69};
    const int expected[]={MOD_LALT,MOD_LGUI,MOD_LGUI,MOD_LSFT,MOD_RSFT};
    for(size_t m=0;m<ARRAY_SIZE(modifiers);m++) {
        fresh();
        /* W and X pass through as letters under all these modifier mappings. */
        key(1000,modifiers[m],true);key(1010,16,true);key(1020,48,true);
        key(1030,38,true);key(1040,16,false);key(1050,48,false);key(1060,38,false);
        key(1080,modifiers[m],false);settled();
        size_t letters=0;
        for(size_t i=0;i<output_count;i++) {
            if(outputs[i].down && (outputs[i].code==W || outputs[i].code==X))letters++;
            if(outputs[i].down && outputs[i].code==BACKSPACE)CHECK(letters==2);
        }
        CHECK(downs(W,-1)==1);CHECK(downs(X,-1)==1);
        CHECK(downs(BACKSPACE,expected[m])==1);
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
static void test_batched_word_corrections(void) {
    const int modifiers[]={-1,35,67,68,66,69};
    const int expected[]={0,MOD_LALT,MOD_LGUI,MOD_LGUI,MOD_LSFT,MOD_RSFT};
    for(size_t m=0;m<ARRAY_SIZE(modifiers);m++)for(int delay=0;delay<=30;delay++)for(int gap=0;gap<=100;gap+=5) {
        fresh();hid_report_delay_ms=delay;unsigned before=failures;
        const int word[]={42,43,44,44};
        for(size_t i=0;i<ARRAY_SIZE(word);i++) {
            batch_key(word[i],true);now_ms+=gap;batch_key(word[i],false);
        }
        if(modifiers[m]>=0)batch_key(modifiers[m],true);
        for(int i=0;i<5;i++) {
            int64_t input_pressed=now_ms;
            batch_key(38,true);now_ms+=gap;
            /* Sending an earlier HID report is processing time, not extra
             * physical hold duration of this correction. */
            batch_key_at(38,false,input_pressed+gap);
        }
        if(modifiers[m]>=0)batch_key(modifiers[m],false);
        settled();CHECK(downs(BACKSPACE,expected[m])==(gap<90?5u:0u));
        CHECK(downs(K,0)==1);CHECK(downs(L,0)==1);CHECK(downs(P,0)==2);
        if(failures!=before) {
            fprintf(stderr,"  batched modifier %d report delay %d gap %d ms\n",modifiers[m],delay,gap);
            return;
        }
    }
}
static void test_batched_mixed_cleanup(void) {
    const int positions[]={35,38,66,67,68,69,71,72,73,74,42,43,44,65};
    for(unsigned seed=1;seed<=2000;seed++) {
        fresh();random_state=seed;unsigned before=failures;
        hid_report_delay_ms=seed%4;
        for(unsigned i=0;i<100;i++) {
            int p=positions[next_random()%ARRAY_SIZE(positions)];
            if((next_random()%10)<2)advance(now_ms+next_random()%100);
            else now_ms+=next_random()%40;
            batch_key(p,!physical_keys[p]);
        }
        for(size_t i=0;i<ARRAY_SIZE(positions);i++)if(physical_keys[positions[i]])batch_key(positions[i],false);
        settled();
        if(failures!=before){fprintf(stderr,"  batched cleanup seed %u\n",seed);return;}
    }
}
static void nested_replay_sequence(int target, int pre, int step, int report_delay) {
    fresh();hid_report_delay_ms=report_delay;
    key(1000,43,true);key(1000+pre,43,false);key(1000+2*pre,43,true);
    const int t=1000+3*pre;
    key(t,target,true);key(t+step,42,true);key(t+2*step,69,true);
    key(t+3*step,42,false);key(t+4*step,42,true);key(t+5*step,44,true);
    key(t+6*step,target,false);key(t+7*step,69,false);
    key(t+8*step,42,false);key(t+9*step,43,false);key(t+10*step,44,false);
}
static void check_nested_replay_output(int target) {
    settled();
    CHECK(downs(L,-1)==2);CHECK(downs(P,-1)==1);
    size_t p_down=output_count,p_up=output_count;
    unsigned p_releases=0;
    for(size_t i=0;i<output_count;i++)if(outputs[i].code==P) {
        if(outputs[i].down)p_down=i;
        else {p_up=i;p_releases++;}
    }
    CHECK(p_releases==1);CHECK(p_down<p_up);
    uint32_t navigation=0;
    switch(target) {
    case 71: navigation=LEFT;break;
    case 72: navigation=DOWN_ARROW;break;
    case 73: navigation=UP_ARROW;break;
    case 74: navigation=RIGHT;break;
    case 65: navigation=SPACE;break;
    }
    if(navigation) {
        bool down=false;unsigned presses=0,releases=0;
        size_t target_down=output_count,target_up=output_count;
        for(size_t i=0;i<output_count;i++)if(outputs[i].code==navigation) {
            if(outputs[i].down){CHECK(!down);down=true;presses++;target_down=i;}
            else {CHECK(down);down=false;releases++;target_up=i;}
        }
        CHECK(presses==1);CHECK(releases==1);CHECK(!down);
        CHECK(target_down<target_up);
    }
}
static void test_nested_replay_keeps_distinct_events(void) {
    const int targets[]={71,72,73,74,65};
    for(size_t i=0;i<ARRAY_SIZE(targets);i++) {
        /* First double L opens a pulse barrier. While replaying its backlog,
         * Shift/Enter-up releases captured K/P/target events; the second K
         * opens another barrier. P-down and target-up are DISTINCT nested
         * captures, not another capture of the Shift/Enter-up replay head.
         * A one-bit "head captured again" flag silently dropped both events.
         * 30 ms first taps; second K is down 12 ms with a 6 ms release gap.
         */
        unsigned before=failures;
        nested_replay_sequence(targets[i],30,6,4);
        check_nested_replay_output(targets[i]);
        if(failures!=before)fprintf(stderr,"  nested replay target %d\n",targets[i]);
    }
}
static void test_nested_replay_timing_sweep(void) {
    const int targets[]={35,38,71,72,73,74,65};
    const int report_delays[]={0,1,2,4,8,22,30};
    const int first_tap_steps[]={16,30,50};
    const int roll_steps[]={6,8,16,20};
    for(size_t a=0;a<ARRAY_SIZE(targets);a++)
    for(size_t d=0;d<ARRAY_SIZE(report_delays);d++)
    for(size_t p=0;p<ARRAY_SIZE(first_tap_steps);p++)
    for(size_t s=0;s<ARRAY_SIZE(roll_steps);s++) {
        unsigned before=failures;
        nested_replay_sequence(targets[a],first_tap_steps[p],roll_steps[s],report_delays[d]);
        check_nested_replay_output(targets[a]);
        if(failures!=before) {
            fprintf(stderr,"  nested target %d first step %d roll step %d report delay %d\n",
                    targets[a],first_tap_steps[p],roll_steps[s],report_delays[d]);
            return;
        }
    }
}
static void test_nested_replay_fifo_contract(void) {
    fresh();
    /* Model the queue immediately AFTER its head was removed for replay.
     * K-up/L-up/P-up already wait in the tail. Replaying Shift/Enter-up
     * discovers older P-down and target-up, then recaptures the head itself.
     * All three must precede that tail, in discovery order, with their own
     * processing-stage flags. Appending would place P-up before P-down.
     */
    const struct zmk_position_state_changed tail[]={
        {.position=42,.state=false,.timestamp=1138},
        {.position=43,.state=false,.timestamp=1144},
        {.position=44,.state=false,.timestamp=1150},
    };
    const struct zmk_position_state_changed nested[]={
        {.position=44,.state=true,.timestamp=1120},
        {.position=72,.state=false,.timestamp=1126},
        {.position=69,.state=false,.timestamp=1132},
    };
    for(size_t i=0;i<ARRAY_SIZE(tail);i++)CHECK(defer_pulse_position_event(&tail[i],false)==0);
    pulse_order_barrier_replaying=true;
    for(size_t i=0;i<ARRAY_SIZE(nested);i++)CHECK(defer_pulse_position_event(&nested[i],i<2)==0);
    CHECK(pulse_deferred_event_count==ARRAY_SIZE(nested)+ARRAY_SIZE(tail));
    for(size_t i=0;i<ARRAY_SIZE(nested)+ARRAY_SIZE(tail);i++) {
        const struct zmk_position_state_changed *expected=
            i<ARRAY_SIZE(nested)?&nested[i]:&tail[i-ARRAY_SIZE(nested)];
        const struct pulse_deferred_event *actual=&pulse_deferred_events[i];
        CHECK(actual->event.data.position==expected->position);
        CHECK(actual->event.data.state==expected->state);
        CHECK(actual->event.data.timestamp==expected->timestamp);
        CHECK(actual->after_mod_tap_capture==(i<2));
    }

    fresh();
    for(unsigned i=0;i<ADV2_CAPTURED_PULSE_EVENTS-1;i++) {
        struct zmk_position_state_changed ev={.position=80+i,.state=true,.timestamp=i};
        CHECK(defer_pulse_position_event(&ev,false)==0);
    }
    pulse_order_barrier_replaying=true;
    CHECK(defer_pulse_position_event(&nested[0],true)==0);
    CHECK(pulse_deferred_event_count==ADV2_CAPTURED_PULSE_EVENTS);
    struct pulse_deferred_event snapshot[ADV2_CAPTURED_PULSE_EVENTS];
    memcpy(snapshot,pulse_deferred_events,sizeof(snapshot));
    CHECK(defer_pulse_position_event(&nested[1],true)==-ENOMEM);
    CHECK(pulse_deferred_event_count==ADV2_CAPTURED_PULSE_EVENTS);
    CHECK(memcmp(snapshot,pulse_deferred_events,sizeof(snapshot))==0);
    CHECK(errors==1); /* Expected overflow diagnostic; no overwritten event. */
    fresh();
}
static void test_num_media_both_os(void) {
    const uint32_t plain[]={C_PREV,C_VOL_DN,C_VOL_UP,C_NEXT};
    const uint32_t bright[]={0,C_BRI_DN,C_BRI_UP,0};
    for(int win=0;win<2;win++)for(int shift=0;shift<4;shift++)for(int arrow=0;arrow<4;arrow++) {
        fresh();windows_mode=win;
        key(1000,70,true);
        if(shift&1)key(1010,66,true);
        if(shift&2)key(1011,69,true);
        for(int tap=0;tap<5;tap++) {
            key(1020+tap*40,71+arrow,true);key(1040+tap*40,71+arrow,false);
        }
        if(shift&1)key(1230,66,false);
        if(shift&2)key(1231,69,false);
        key(1240,70,false);settled();
        if(!shift)CHECK(downs(plain[arrow],0)==5);
        else if(bright[arrow])CHECK(downs(bright[arrow],0)==5);
        else {CHECK(backlight_dec==(arrow==0?5:0));CHECK(backlight_inc==(arrow==3?5:0));}
        CHECK(downs(LEFT,-1)+downs(RIGHT,-1)+downs(UP_ARROW,-1)+downs(DOWN_ARROW,-1)==0);
        CHECK(downs(BACKSPACE,-1)+downs(DELETE,-1)==0);
    }
    for(int win=0;win<2;win++) {
        fresh();windows_mode=win;key(1000,70,true);
        key(1010,65,true);key(1030,65,false);key(1040,70,false);settled();
        CHECK(downs(C_PP,0)==1);CHECK(downs(SPACE,-1)==0);
    }
}
static void test_num_media_rolls(void) {
    const int shifts[]={66,69};
    const int order[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    for(int win=0;win<2;win++)for(int s=0;s<2;s++)for(int first=0;first<2;first++)
    for(int arrow=0;arrow<4;arrow++)for(int perm=0;perm<6;perm++) {
        fresh();windows_mode=win;
        key(1000,first?shifts[s]:70,true);
        key(1010,first?70:shifts[s],true);
        key(1020,71+arrow,true);
        const int positions[]={70,shifts[s],71+arrow};
        for(int i=0;i<3;i++)key(1040+i*20,positions[order[perm][i]],false);
        settled();
        if(arrow==0)CHECK(backlight_dec==1);
        if(arrow==1)CHECK(downs(C_BRI_DN,0)==1);
        if(arrow==2)CHECK(downs(C_BRI_UP,0)==1);
        if(arrow==3)CHECK(backlight_inc==1);
        CHECK(downs(TAB,-1)+downs(ENTER,-1)==0);
        CHECK(downs(C_PREV,-1)+downs(C_NEXT,-1)+downs(C_VOL_DN,-1)+downs(C_VOL_UP,-1)==0);
    }
    fresh();key(1000,70,true);key(1010,73,true);key(1020,69,true);
    advance(1600);CHECK(hid_keys[C_VOL_UP&0xffff]); /* Held action does not morph midway. */
    key(1610,70,false);key(1620,73,false);key(1630,69,false);settled();
    CHECK(downs(C_VOL_UP,0)==1);CHECK(downs(C_BRI_UP,-1)==0);
}

static size_t semantic_downs(struct output *result) {
    size_t n=0;
    for(size_t i=0;i<output_count;i++) {
        if(outputs[i].down && !(outputs[i].code>=LCTRL && outputs[i].code<=RGUI))
            result[n++]=outputs[i];
    }
    return n;
}

static void alt_symbol_roll(int alt, int letter, int extras, bool win, bool held, bool early) {
    fresh();windows_mode=win;
    if(extras&1)key(1000,66,true);
    if(extras&2)key(1010,69,true);
    if(extras&4)key(1020,67,true);
    if(extras&8)key(1030,52,true);
    /* A held-symbol reference starts after the 90 ms Alt decision. Without
     * this, the baseline buffers both letter edges until release and never
     * observes a semantic hold. The rolled case starts before 90 ms. */
    const int letter_at=(held && !early)?1160:1060;
    key(1040,alt,true);key(letter_at,letter,true);
    if(early)key(1080,alt,false);
    key(letter_at+(held?200:40),letter,false);
    if(!early)key(letter_at+(held?220:60),alt,false);
    if(extras&8)key(1400,52,false);
    if(extras&4)key(1410,67,false);
    if(extras&2)key(1420,69,false);
    if(extras&1)key(1430,66,false);
    settled();
}

static void test_alt_symbol_rolls(void) {
    const int alts[]={35,38};
    const int extras[]={0,1,2,3,4,5,8,9};
    struct output expected[512],actual[512];
    for(int win=0;win<2;win++)for(size_t a=0;a<2;a++)
    for(size_t m=0;m<ARRAY_SIZE(extras);m++)for(int held=0;held<2;held++)
    for(int p=0;p<128;p++)if(semantic_at[p]) {
        unsigned before=failures;
        alt_symbol_roll(alts[a],p,extras[m],win,held,false);
        size_t n=semantic_downs(expected);
        alt_symbol_roll(alts[a],p,extras[m],win,held,true);
        size_t actual_n=semantic_downs(actual);
        CHECK(actual_n==n);CHECK(n>0);
        for(size_t i=0;i<n && i<actual_n;i++) {
            CHECK(expected[i].code==actual[i].code);
            CHECK(expected[i].mods==actual[i].mods);
        }
        CHECK(downs(BACKSPACE,-1)==0);CHECK(downs(DELETE,-1)==0);
        if(before!=failures) {
            fprintf(stderr,"  Alt roll OS=%d alt=%d key=%d extras=%d held=%d\n",
                    win,alts[a],p,extras[m],held);
            return;
        }
    }
}

static void test_navigation_repeat_requires_consecutive_taps(void) {
    const int alts[]={35,38};
    const uint32_t codes[]={DELETE,BACKSPACE};
    const int interruptions[]={29,65,67};
    for(int win=0;win<2;win++)for(size_t a=0;a<2;a++)for(size_t p=0;p<3;p++) {
        fresh();windows_mode=win;
        key(1000,alts[a],true);key(1010,alts[a],false);
        key(1020,interruptions[p],true);key(1030,interruptions[p],false);
        advance(1050);size_t before=downs(codes[a],-1);
        key(1060,alts[a],true);advance(1200);
        CHECK(downs(codes[a],-1)==before);CHECK(!hid_keys[codes[a]&0xffff]);
        key(1210,29,true);key(1220,29,false);key(1230,alts[a],false);settled();
        CHECK(downs(KP_PLUS,-1)==1);CHECK(downs(codes[a],-1)==1);
    }
    /* User reproduction: a tap, a fast literal "num", then hold right Alt. */
    for(int win=0;win<2;win++) {
        fresh();windows_mode=win;
        key(1000,38,true);key(1010,38,false);
        key(1020,41,true);key(1030,41,false);
        key(1040,23,true);key(1050,23,false);
        key(1060,55,true);key(1070,55,false);
        key(1090,38,true);advance(1500);
        CHECK(downs(BACKSPACE,-1)==1);CHECK(!hid_keys[BACKSPACE&0xffff]);
        key(1510,29,true);key(1530,29,false);key(1550,38,false);settled();
        CHECK(downs(KP_PLUS,-1)==1);CHECK(downs(A,-1)==0);
    }
}

static void test_alt_shift_symbol_release_orders(void) {
    const int alts[]={35,38},shifts[]={66,69};
    const int release_order[][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    struct output expected[512],actual[512];
    for(int win=0;win<2;win++)for(size_t a=0;a<2;a++)for(size_t s=0;s<2;s++)
    for(int p=0;p<128;p++)if(semantic_at[p]) {
        alt_symbol_roll(alts[a],p,1<<s,win,false,false);
        size_t n=semantic_downs(expected);
        for(int alt_first=0;alt_first<2;alt_first++)for(size_t order=0;order<6;order++) {
            unsigned before=failures;
            fresh();windows_mode=win;
            key(1000,alt_first?alts[a]:shifts[s],true);
            key(1010,alt_first?shifts[s]:alts[a],true);
            key(1020,p,true);
            const int held[]={alts[a],shifts[s],p};
            for(int i=0;i<3;i++)key(1050+i*10,held[release_order[order][i]],false);
            settled();size_t actual_n=semantic_downs(actual);
            CHECK(actual_n==n);
            for(size_t i=0;i<n && i<actual_n;i++) {
                CHECK(expected[i].code==actual[i].code);
                CHECK(expected[i].mods==actual[i].mods);
            }
            if(before!=failures) {
                fprintf(stderr,"  Alt+Shift roll OS=%d alt=%d shift=%d key=%d first=%d order=%zu\n",
                        win,alts[a],shifts[s],p,alt_first,order);
                return;
            }
        }
    }
}

static void test_letter_then_one_or_two_navigation_taps(void) {
    const int mods[]={-1,35,67,68,66,69};
    for(int win=0;win<2;win++)for(size_t m=0;m<ARRAY_SIZE(mods);m++)
    for(int count=1;count<=2;count++)for(int overlap=0;overlap<4;overlap++) {
        fresh();windows_mode=win;
        key(1000,29,true);
        if(overlap==0)key(1005,29,false);
        if(mods[m]>=0)key(1010,mods[m],true);
        key(1020,38,true);
        if(overlap==1)key(1025,29,false);
        key(1030,38,false);
        if(overlap==2)key(1035,29,false);
        if(count==2){key(1040,38,true);key(1050,38,false);}
        if(overlap==3)key(1055,29,false);
        if(mods[m]>=0)key(1060,mods[m],false);
        settled();CHECK(downs(A,0)==1);CHECK(downs(BACKSPACE,-1)==(size_t)count);
        CHECK(downs(KP_PLUS,-1)==0);
    }
}

static void test_repeated_fast_plus_rolls(void) {
    for(int win=0;win<2;win++)for(int order=0;order<2;order++) {
        fresh();windows_mode=win;
        for(int i=0;i<8;i++) {
            int t=1000+i*70;
            key(t,38,true);key(t+15,29,true);
            key(t+30,order?29:38,false);key(t+45,order?38:29,false);
        }
        settled();CHECK(downs(KP_PLUS,-1)==8);CHECK(downs(A,-1)==0);
        CHECK(downs(BACKSPACE,-1)==0);
    }
}

static void test_unused_alt_holds_do_not_delete(void) {
    const int alts[]={35,38},durations[]={89,90,91,200,2000};
    const uint32_t codes[]={DELETE,BACKSPACE};
    for(int win=0;win<2;win++)for(size_t a=0;a<2;a++)for(size_t d=0;d<ARRAY_SIZE(durations);d++) {
        fresh();windows_mode=win;
        key(3000,alts[a],true);advance(3000+durations[d]);
        CHECK(downs(codes[a],-1)==0);CHECK(!hid_keys[codes[a]&0xffff]);
        key(3000+durations[d],alts[a],false);
        CHECK(downs(codes[a],-1)==(durations[d]<90?1u:0u));
        if(durations[d]>=90) {
            /* A long unused hold must not arm deletion repeat for next hold. */
            key(3010+durations[d],alts[a],true);advance(3300+durations[d]);
            CHECK(downs(codes[a],-1)==0);CHECK(!hid_keys[codes[a]&0xffff]);
            key(3310+durations[d],alts[a],false);
        }
        settled();
        if(durations[d]>=90)CHECK(downs(codes[a],-1)==0);
    }
    /* An older letter's release can promote the long-held Alt without any
     * new action using it. This HOLDING path must not retro-delete either. */
    for(size_t a=0;a<2;a++) {
        fresh();key(1000,29,true);key(1010,alts[a],true);key(1120,29,false);
        key(1300,alts[a],false);settled();CHECK(downs(codes[a],-1)==0);
    }
}

static void test_navigation_input_time_survives_delayed_dispatch(void) {
    const int alts[]={35,38};const uint32_t codes[]={DELETE,BACKSPACE};
    for(size_t a=0;a<2;a++)for(int held=0;held<2;held++) {
        fresh();now_ms=5000;
        batch_key_at(alts[a],true,1000);
        batch_key_at(alts[a],false,held?3000:1040);
        CHECK(downs(codes[a],-1)==(held?0u:1u));
        /* Two seconds between input presses is not a quick repeat just
         * because their queued handlers execute back-to-back. */
        batch_key_at(alts[a],true,held?5010:3050);
        CHECK(downs(codes[a],-1)==(held?0u:1u));
        advance(6000);
        batch_key_at(alts[a],false,held?5300:3340);
        settled();CHECK(downs(codes[a],-1)==(held?0u:1u));
    }
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
    run("word then held modifier and five Backspaces over 60 timings",test_word_then_modified_backspace_sweep);
    run("word tail release overlaps modifier and Backspace",test_word_tail_overlaps_modified_backspace);
    run("word correction with blocking HID report delays",test_modified_corrections_with_report_delays);
    run("each modified Backspace behind double-letter barrier",test_correction_behind_pulse_barrier);
    run("pending rolled word must precede modified Backspace",test_pending_word_precedes_correction);
    run("50000 mixed events without resolver reset",test_long_session_without_reset);
    run("batched word corrections without intervening timer work",test_batched_word_corrections);
    run("batched mixed input leaves no held keys or queued events",test_batched_mixed_cleanup);
    run("nested replay preserves distinct letter presses and navigation releases",test_nested_replay_keeps_distinct_events);
    run("nested replay timing and chronology sweep",test_nested_replay_timing_sweep);
    run("nested replay preserves FIFO tail and rejects queue overflow",test_nested_replay_fifo_contract);
    run("NUM media and both Shift keys in macOS and Windows",test_num_media_both_os);
    run("NUM media keeps its press binding through Shift and NUM release rolls",test_num_media_rolls);
    run("all Alt symbols retain early-release modifiers, including Alt+Shift",test_alt_symbol_rolls);
    run("all Alt+Shift symbol press and release orders",test_alt_shift_symbol_release_orders);
    run("navigation repeat is cancelled by an intervening key",test_navigation_repeat_requires_consecutive_taps);
    run("letter first then one or two Backspace taps retains exact count",test_letter_then_one_or_two_navigation_taps);
    run("repeated fast Alt+A rolls emit one plus each",test_repeated_fast_plus_rolls);
    run("unused Alt holds never delete or arm deletion repeat",test_unused_alt_holds_do_not_delete);
    run("navigation tap duration and repeat use own input-edge timestamps",test_navigation_input_time_survives_delayed_dispatch);
    printf("%u failed assertions\n",failures);
    return failures?1:0;
}
