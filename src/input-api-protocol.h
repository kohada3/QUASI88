#ifndef INPUT_API_PROTOCOL_H
#define INPUT_API_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define INPUT_API_MAX_LINE 1024
#define INPUT_API_DEFAULT_HOLD_MS 80

typedef enum {
    INPUT_API_OP_PING = 0,
    INPUT_API_OP_TAP,
    INPUT_API_OP_KEYDOWN,
    INPUT_API_OP_KEYUP
} input_api_operation_t;

typedef struct {
    uint32_t id;
    input_api_operation_t op;
    char key[32];
    uint32_t hold_ms;
} input_api_command_t;

typedef enum {
    INPUT_API_PARSE_OK = 0,
    INPUT_API_PARSE_MALFORMED,
    INPUT_API_PARSE_MISSING_FIELD,
    INPUT_API_PARSE_DUPLICATE_FIELD,
    INPUT_API_PARSE_UNKNOWN_FIELD,
    INPUT_API_PARSE_INVALID_VALUE,
    INPUT_API_PARSE_LINE_TOO_LONG
} input_api_parse_result_t;

input_api_parse_result_t input_api_parse_command(
    const char *line, size_t length, input_api_command_t *out);

int input_api_format_response(
    char *dst, size_t size, uint32_t id, int ok,
    const char *status_or_error);

#endif
