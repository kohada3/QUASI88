#include "input-api-core.h"

#include <string.h>

static size_t key_index(int key88)
{
    return (size_t)(key88 - INPUT_API_KEY_MIN);
}

static int is_held(const input_api_core_t *core, size_t index)
{
    return (core->held[index / 8] & (1u << (index % 8))) != 0;
}

static void set_held(input_api_core_t *core, size_t index, int held)
{
    unsigned char mask = (unsigned char)(1u << (index % 8));
    if (held) core->held[index / 8] |= mask;
    else core->held[index / 8] &= (unsigned char)~mask;
}

static void emit_down(
    input_api_core_t *core, size_t index,
    input_api_key_sink_t sink, void *opaque)
{
    if (!is_held(core, index)) {
        set_held(core, index, 1);
        sink((int)index + INPUT_API_KEY_MIN, 1, opaque);
    }
}

static void emit_up(
    input_api_core_t *core, size_t index,
    input_api_key_sink_t sink, void *opaque)
{
    if (is_held(core, index)) {
        set_held(core, index, 0);
        core->release_at[index] = 0;
        sink((int)index + INPUT_API_KEY_MIN, 0, opaque);
    }
}

void input_api_core_init(input_api_core_t *core)
{
    memset(core, 0, sizeof(*core));
}

int input_api_core_submit(input_api_core_t *core, const input_api_action_t *action)
{
    size_t tail;

    if (core == NULL || action == NULL ||
        action->key88 < INPUT_API_KEY_MIN || action->key88 > INPUT_API_KEY_MAX ||
        action->type < INPUT_API_ACTION_TAP || action->type > INPUT_API_ACTION_UP ||
        (action->type == INPUT_API_ACTION_TAP &&
         (action->hold_ms < 1 || action->hold_ms > 5000)) ||
        core->count == INPUT_API_QUEUE_CAPACITY) {
        return 0;
    }
    tail = (core->head + core->count) % INPUT_API_QUEUE_CAPACITY;
    core->queue[tail] = *action;
    core->count++;
    return 1;
}

void input_api_core_tick(
    input_api_core_t *core, uint64_t now_ms,
    input_api_key_sink_t sink, void *opaque)
{
    size_t index;

    if (core == NULL || sink == NULL) return;
    for (index = 0; index < INPUT_API_KEY_COUNT; index++) {
        if (core->release_at[index] != 0 &&
            core->release_at[index] <= now_ms) {
            emit_up(core, index, sink, opaque);
        }
    }
    while (core->count != 0) {
        input_api_action_t action = core->queue[core->head];
        uint64_t due;

        core->head = (core->head + 1) % INPUT_API_QUEUE_CAPACITY;
        core->count--;
        index = key_index(action.key88);
        if (action.type == INPUT_API_ACTION_DOWN) {
            emit_down(core, index, sink, opaque);
            core->release_at[index] = 0;
        } else if (action.type == INPUT_API_ACTION_UP) {
            emit_up(core, index, sink, opaque);
        } else if (!is_held(core, index) || core->release_at[index] != 0) {
            emit_down(core, index, sink, opaque);
            due = UINT64_MAX - now_ms < action.hold_ms
                ? UINT64_MAX : now_ms + action.hold_ms;
            if (core->release_at[index] == 0 || due > core->release_at[index]) {
                core->release_at[index] = due;
            }
        }
    }
}

void input_api_core_release_all(
    input_api_core_t *core, input_api_key_sink_t sink, void *opaque)
{
    size_t index;

    if (core == NULL || sink == NULL) return;
    core->head = 0;
    core->count = 0;
    for (index = 0; index < INPUT_API_KEY_COUNT; index++) {
        emit_up(core, index, sink, opaque);
        core->release_at[index] = 0;
    }
}

size_t input_api_core_pending(const input_api_core_t *core)
{
    return core == NULL ? 0 : core->count;
}
