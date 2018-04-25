#include <platform/platform.h>
#include <platform/unit_test.h>
#include <platform/tests.h>

struct test_state {
    bool called;
};

static void nabto_test_callback(void* data)
{
    struct test_state* state = (struct test_state*)data;
    state->called = true;
}

static void nabto_timed_event_test_callback(const nabto_error_code ec, void* data)
{
    struct test_state* state = (struct test_state*)data;
    state->called = true;
}

void nabto_platform_test_post_event()
{
    struct nabto_platform pl;

    nabto_platform_init(&pl);

    NABTO_TEST_CHECK(nabto_event_queue_is_event_queue_empty(&pl));

    struct test_state state; 
    state.called = false;

    struct nabto_event event;

    nabto_event_queue_post(&pl, &event, &nabto_test_callback, &state);

    NABTO_TEST_CHECK(!nabto_event_queue_is_event_queue_empty(&pl));

    nabto_event_queue_poll_one(&pl);

    NABTO_TEST_CHECK(nabto_event_queue_is_event_queue_empty(&pl));

    NABTO_TEST_CHECK(state.called);
}

nabto_timestamp time;
bool nabto_platform_test_ts_passed_or_now(nabto_timestamp* timestamp)
{
    return (time >= *timestamp);
}

void nabto_platform_test_ts_now(nabto_timestamp* ts)
{
    *ts = time;
}
bool nabto_platform_test_ts_less_or_equal(nabto_timestamp* t1, nabto_timestamp* t2)
{
    return (*t1 <= *t2);
}

void nabto_platform_test_ts_set_future_timestamp(nabto_timestamp* ts, uint32_t milliseconds)
{
    *ts = time + milliseconds;
}

void nabto_platform_test_post_timed_event()
{
    struct nabto_platform pl;
    nabto_platform_init(&pl);

    time = 0;
    pl.ts.passed_or_now = &nabto_platform_test_ts_passed_or_now;
    pl.ts.less_or_equal = &nabto_platform_test_ts_less_or_equal;
    pl.ts.now = &nabto_platform_test_ts_now;
    pl.ts.set_future_timestamp = &nabto_platform_test_ts_set_future_timestamp;

    NABTO_TEST_CHECK(!nabto_event_queue_has_timed_event(&pl));

    struct nabto_timed_event event;
    struct test_state state;
    state.called = false;

    nabto_event_queue_post_timed_event(&pl, &event, 50, &nabto_timed_event_test_callback, &state);

    NABTO_TEST_CHECK(nabto_event_queue_has_timed_event(&pl));

    NABTO_TEST_CHECK(!nabto_event_queue_has_ready_timed_event(&pl));

    time += 100;

    NABTO_TEST_CHECK(nabto_event_queue_has_ready_timed_event(&pl));

    nabto_event_queue_poll_one_timed_event(&pl);

    NABTO_TEST_CHECK(!nabto_event_queue_has_ready_timed_event(&pl));
    NABTO_TEST_CHECK(!nabto_event_queue_has_timed_event(&pl));

    NABTO_TEST_CHECK(state.called = true);
}

void nabto_event_queue_tests()
{
    nabto_platform_test_post_event();
    nabto_platform_test_post_timed_event();
}
