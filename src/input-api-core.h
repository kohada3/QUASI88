#ifndef INPUT_API_CORE_H
#define INPUT_API_CORE_H

#include <stddef.h>
#include <stdint.h>

#define INPUT_API_QUEUE_CAPACITY 256
#define INPUT_API_KEY_MIN 32
#define INPUT_API_KEY_MAX 247
#define INPUT_API_KEY_COUNT (INPUT_API_KEY_MAX - INPUT_API_KEY_MIN + 1)
#define INPUT_API_KEY_BYTES ((INPUT_API_KEY_COUNT + 7) / 8)

typedef enum {
    INPUT_API_ACTION_TAP = 0,
    INPUT_API_ACTION_DOWN,
    INPUT_API_ACTION_UP
} input_api_action_type_t;

typedef struct {
    input_api_action_type_t type;
    int key88;
    uint32_t hold_ms;
} input_api_action_t;

typedef void (*input_api_key_sink_t)(int key88, int pressed, void *opaque);

typedef struct {
    input_api_action_t queue[INPUT_API_QUEUE_CAPACITY];
    size_t head;
    size_t count;
    unsigned char held[INPUT_API_KEY_BYTES];
    uint64_t release_at[INPUT_API_KEY_COUNT];
} input_api_core_t;

void input_api_core_init(input_api_core_t *core);
int input_api_core_submit(input_api_core_t *core, const input_api_action_t *action);
void input_api_core_tick(
    input_api_core_t *core, uint64_t now_ms,
    input_api_key_sink_t sink, void *opaque);
void input_api_core_release_all(
    input_api_core_t *core, input_api_key_sink_t sink, void *opaque);
size_t input_api_core_pending(const input_api_core_t *core);

#endif
