#include "input-api-core.h"
#include "input-api-protocol.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int key88;
    int pressed;
} event_t;

typedef struct {
    event_t events[1024];
    size_t count;
} recorder_t;

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void record_key(int key88, int pressed, void *opaque)
{
    recorder_t *recorder = (recorder_t *)opaque;
    recorder->events[recorder->count].key88 = key88;
    recorder->events[recorder->count].pressed = pressed;
    recorder->count++;
}

static input_api_action_t action(input_api_action_type_t type, int key88, uint32_t hold_ms)
{
    input_api_action_t value;
    value.type = type;
    value.key88 = key88;
    value.hold_ms = hold_ms;
    return value;
}

static void submit(input_api_core_t *core, input_api_action_t value)
{
    CHECK(input_api_core_submit(core, &value));
}

static void test_fifo_and_down_up(void)
{
    input_api_core_t core;
    recorder_t recorder = { 0 };

    input_api_core_init(&core);
    submit(&core, action(INPUT_API_ACTION_DOWN, 40, 0));
    submit(&core, action(INPUT_API_ACTION_DOWN, 41, 0));
    submit(&core, action(INPUT_API_ACTION_UP, 40, 0));
    CHECK(input_api_core_pending(&core) == 3);
    input_api_core_tick(&core, 10, record_key, &recorder);
    CHECK(input_api_core_pending(&core) == 0);
    CHECK(recorder.count == 3);
    CHECK(recorder.events[0].key88 == 40 && recorder.events[0].pressed == 1);
    CHECK(recorder.events[1].key88 == 41 && recorder.events[1].pressed == 1);
    CHECK(recorder.events[2].key88 == 40 && recorder.events[2].pressed == 0);
}

static void test_queue_full_is_unchanged(void)
{
    input_api_core_t core;
    input_api_action_t value = action(INPUT_API_ACTION_DOWN, 40, 0);
    size_t i;

    input_api_core_init(&core);
    for (i = 0; i < INPUT_API_QUEUE_CAPACITY; i++) {
        CHECK(input_api_core_submit(&core, &value));
    }
    CHECK(input_api_core_pending(&core) == INPUT_API_QUEUE_CAPACITY);
    CHECK(!input_api_core_submit(&core, &value));
    CHECK(input_api_core_pending(&core) == INPUT_API_QUEUE_CAPACITY);
}

static void test_default_tap_timing_and_unrelated_progress(void)
{
    input_api_core_t core;
    recorder_t recorder = { 0 };

    input_api_core_init(&core);
    submit(&core, action(INPUT_API_ACTION_TAP, 50, INPUT_API_DEFAULT_HOLD_MS));
    submit(&core, action(INPUT_API_ACTION_DOWN, 51, 0));
    input_api_core_tick(&core, 100, record_key, &recorder);
    CHECK(recorder.count == 2);
    CHECK(recorder.events[0].key88 == 50 && recorder.events[0].pressed == 1);
    CHECK(recorder.events[1].key88 == 51 && recorder.events[1].pressed == 1);
    input_api_core_tick(&core, 179, record_key, &recorder);
    CHECK(recorder.count == 2);
    input_api_core_tick(&core, 180, record_key, &recorder);
    CHECK(recorder.count == 3);
    CHECK(recorder.events[2].key88 == 50 && recorder.events[2].pressed == 0);
}

static void test_tap_boundaries(void)
{
    input_api_core_t core;
    recorder_t recorder = { 0 };

    input_api_core_init(&core);
    submit(&core, action(INPUT_API_ACTION_TAP, 60, 1));
    submit(&core, action(INPUT_API_ACTION_TAP, 61, 5000));
    input_api_core_tick(&core, 200, record_key, &recorder);
    CHECK(recorder.count == 2);
    input_api_core_tick(&core, 201, record_key, &recorder);
    CHECK(recorder.count == 3);
    CHECK(recorder.events[2].key88 == 60 && recorder.events[2].pressed == 0);
    input_api_core_tick(&core, 5199, record_key, &recorder);
    CHECK(recorder.count == 3);
    input_api_core_tick(&core, 5200, record_key, &recorder);
    CHECK(recorder.count == 4);
    CHECK(recorder.events[3].key88 == 61 && recorder.events[3].pressed == 0);
}

static void test_repeated_taps_of_same_key_stay_distinct(void)
{
    input_api_core_t core;
    recorder_t recorder = { 0 };

    input_api_core_init(&core);
    submit(&core, action(INPUT_API_ACTION_TAP, 50, 80));
    submit(&core, action(INPUT_API_ACTION_TAP, 50, 80));
    submit(&core, action(INPUT_API_ACTION_DOWN, 51, 0));

    input_api_core_tick(&core, 100, record_key, &recorder);
    CHECK(recorder.count == 2);
    CHECK(recorder.events[0].key88 == 50 && recorder.events[0].pressed == 1);
    CHECK(recorder.events[1].key88 == 51 && recorder.events[1].pressed == 1);
    CHECK(input_api_core_pending(&core) == 1);

    input_api_core_tick(&core, 179, record_key, &recorder);
    CHECK(recorder.count == 2);
    input_api_core_tick(&core, 180, record_key, &recorder);
    CHECK(recorder.count == 4);
    CHECK(recorder.events[2].key88 == 50 && recorder.events[2].pressed == 0);
    CHECK(recorder.events[3].key88 == 50 && recorder.events[3].pressed == 1);
    CHECK(input_api_core_pending(&core) == 0);

    input_api_core_tick(&core, 260, record_key, &recorder);
    CHECK(recorder.count == 5);
    CHECK(recorder.events[4].key88 == 50 && recorder.events[4].pressed == 0);
}

static void test_idempotent_transitions(void)
{
    input_api_core_t core;
    recorder_t recorder = { 0 };

    input_api_core_init(&core);
    submit(&core, action(INPUT_API_ACTION_UP, 70, 0));
    submit(&core, action(INPUT_API_ACTION_DOWN, 70, 0));
    submit(&core, action(INPUT_API_ACTION_DOWN, 70, 0));
    submit(&core, action(INPUT_API_ACTION_UP, 70, 0));
    submit(&core, action(INPUT_API_ACTION_UP, 70, 0));
    input_api_core_tick(&core, 0, record_key, &recorder);
    CHECK(recorder.count == 2);
    CHECK(recorder.events[0].pressed == 1);
    CHECK(recorder.events[1].pressed == 0);
}

static void test_release_all_once_and_discard_queue(void)
{
    input_api_core_t core;
    recorder_t recorder = { 0 };

    input_api_core_init(&core);
    submit(&core, action(INPUT_API_ACTION_DOWN, 80, 0));
    submit(&core, action(INPUT_API_ACTION_DOWN, 81, 0));
    input_api_core_tick(&core, 0, record_key, &recorder);
    submit(&core, action(INPUT_API_ACTION_TAP, 82, 80));
    input_api_core_release_all(&core, record_key, &recorder);
    CHECK(recorder.count == 4);
    CHECK(recorder.events[2].key88 == 80 && recorder.events[2].pressed == 0);
    CHECK(recorder.events[3].key88 == 81 && recorder.events[3].pressed == 0);
    CHECK(input_api_core_pending(&core) == 0);
    input_api_core_release_all(&core, record_key, &recorder);
    input_api_core_tick(&core, 100, record_key, &recorder);
    CHECK(recorder.count == 4);
}

int main(void)
{
    test_fifo_and_down_up();
    test_queue_full_is_unchanged();
    test_default_tap_timing_and_unrelated_progress();
    test_tap_boundaries();
    test_repeated_taps_of_same_key_stay_distinct();
    test_idempotent_transitions();
    test_release_all_once_and_discard_queue();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
