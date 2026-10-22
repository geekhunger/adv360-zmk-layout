/* Exercise the same resolver with diagnostic hooks compiled in. The recorder
 * itself is tested separately by trace_test.c, including its consent gate.
 */
#define CONFIG_ADV360_SCOPED_TRACE 1
#define main resolver_main
#include "resolver_test.c"
#undef main

static bool inspect_trace;
struct observed_trace { uint8_t stage; int32_t p; bool down; int32_t a, b; };
static struct observed_trace observed[128];
static size_t observed_count;

void adv360_trace_position(uint8_t stage, int32_t p, bool down, int32_t a, int32_t b) {
    if (!inspect_trace || !adv360_trace_allowed_position(p)) return;
    if (observed_count == ARRAY_SIZE(observed)) abort();
    observed[observed_count++] = (struct observed_trace){stage, p, down, a, b};
}
void adv360_trace_emit(uint32_t code, bool down, uint8_t mods) {}

static void test_trace_decision_metadata(void) {
    fresh(); inspect_trace = true; observed_count = 0;
    key(1000,38,true); key(3000,38,false); settled();
    unsigned inputs = 0;
    for (size_t i=0;i<observed_count;i++) {
        struct observed_trace *r = &observed[i];
        CHECK(r->p == 38);
        if (r->stage == ADV_TRACE_MOD_TAP_INPUT) {
            inputs++;
            CHECK(r->b == (r->down ? 60001 : 2000));
        }
    }
    CHECK(inputs == 2); CHECK(downs(BACKSPACE,-1) == 0);
    observed_count = 0;
    key(4000,38,true); key(4020,38,false); key(4060,38,true);
    key(5000,38,false); settled();
    bool repeat_gap = false;
    for (size_t i=0;i<observed_count;i++) {
        struct observed_trace *r = &observed[i];
        if (r->stage == ADV_TRACE_MOD_TAP_INPUT && r->down && r->b == 40) repeat_gap=true;
    }
    CHECK(repeat_gap); CHECK(downs(BACKSPACE,-1) == 2);
    fresh(); observed_count = 0;
    key(1000,70,true); key(1020,38,true); key(2000,38,false); key(2100,70,false);
    settled();
    bool num_seen = false;
    for (size_t i=0;i<observed_count;i++) {
        struct observed_trace *r = &observed[i];
        if (r->p == 38 && r->stage == ADV_TRACE_RESOLVER_IN && r->down) {
            CHECK(r->b & (1<<17)); num_seen = true;
        }
        CHECK(r->stage != ADV_TRACE_MOD_TAP_INPUT);
    }
    CHECK(num_seen); CHECK(downs(BACKSPACE,-1) == 1);
    inspect_trace = false;
}
int main(void) {
    int result = resolver_main();
    run("trace distinguishes long hold, repeat interval and direct NUM path",test_trace_decision_metadata);
    return result || failures ? 1 : 0;
}
