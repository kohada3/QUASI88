#include "input-api-protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *current;
    const char *end;
} parser_t;

static void skip_space(parser_t *parser)
{
    while (parser->current < parser->end &&
           isspace((unsigned char)*parser->current)) {
        parser->current++;
    }
}

static int consume(parser_t *parser, char expected)
{
    if (parser->current == parser->end || *parser->current != expected) {
        return 0;
    }
    parser->current++;
    return 1;
}

static int parse_simple_string(
    parser_t *parser, char *value, size_t size, int allow_escapes)
{
    size_t length = 0;

    if (!consume(parser, '"')) {
        return 0;
    }
    while (parser->current < parser->end && *parser->current != '"') {
        unsigned int decoded;
        unsigned char ch = (unsigned char)*parser->current++;

        if (ch == '\\') {
            int i;
            if (!allow_escapes || parser->current == parser->end) {
                return 0;
            }
            ch = (unsigned char)*parser->current++;
            switch (ch) {
            case '"': decoded = '"'; break;
            case '\\': decoded = '\\'; break;
            case '/': decoded = '/'; break;
            case 'b': decoded = '\b'; break;
            case 'f': decoded = '\f'; break;
            case 'n': decoded = '\n'; break;
            case 'r': decoded = '\r'; break;
            case 't': decoded = '\t'; break;
            case 'u':
                decoded = 0;
                for (i = 0; i < 4; i++) {
                    int digit;
                    if (parser->current == parser->end) {
                        return 0;
                    }
                    ch = (unsigned char)*parser->current++;
                    if (ch >= '0' && ch <= '9') digit = ch - '0';
                    else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
                    else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
                    else return 0;
                    decoded = decoded * 16u + (unsigned int)digit;
                }
                break;
            default: return 0;
            }
        } else {
            decoded = ch;
        }
        if (decoded < 0x20 || decoded > 0x7e || length + 1 >= size) {
            return 0;
        }
        value[length++] = (char)decoded;
    }
    if (!consume(parser, '"')) {
        return 0;
    }
    value[length] = '\0';
    return 1;
}

static int parse_uint32(parser_t *parser, uint32_t *value)
{
    uint32_t result = 0;

    if (parser->current == parser->end ||
        *parser->current < '0' || *parser->current > '9') {
        return 0;
    }
    if (*parser->current == '0' && parser->current + 1 < parser->end &&
        parser->current[1] >= '0' && parser->current[1] <= '9') {
        return 0;
    }
    while (parser->current < parser->end &&
           *parser->current >= '0' && *parser->current <= '9') {
        uint32_t digit = (uint32_t)(*parser->current++ - '0');
        if (result > UINT32_MAX / 10u ||
            (result == UINT32_MAX / 10u && digit > UINT32_MAX % 10u)) {
            return 0;
        }
        result = result * 10u + digit;
    }
    *value = result;
    return 1;
}

input_api_parse_result_t input_api_parse_command(
    const char *line, size_t length, input_api_command_t *out)
{
    parser_t parser;
    input_api_command_t command;
    unsigned int seen = 0;
    enum { SEEN_ID = 1, SEEN_OP = 2, SEEN_KEY = 4, SEEN_HOLD = 8 };

    if (length > INPUT_API_MAX_LINE) return INPUT_API_PARSE_LINE_TOO_LONG;
    if (line == NULL || out == NULL) return INPUT_API_PARSE_MALFORMED;
    memset(&command, 0, sizeof(command));
    parser.current = line;
    parser.end = line + length;
    skip_space(&parser);
    if (!consume(&parser, '{')) return INPUT_API_PARSE_MALFORMED;
    skip_space(&parser);
    if (consume(&parser, '}')) return INPUT_API_PARSE_MISSING_FIELD;

    for (;;) {
        char name[16];
        unsigned int field;

        if (!parse_simple_string(&parser, name, sizeof(name), 0))
            return INPUT_API_PARSE_MALFORMED;
        if (strcmp(name, "id") == 0) field = SEEN_ID;
        else if (strcmp(name, "op") == 0) field = SEEN_OP;
        else if (strcmp(name, "key") == 0) field = SEEN_KEY;
        else if (strcmp(name, "hold_ms") == 0) field = SEEN_HOLD;
        else return INPUT_API_PARSE_UNKNOWN_FIELD;
        if (seen & field) return INPUT_API_PARSE_DUPLICATE_FIELD;
        seen |= field;
        skip_space(&parser);
        if (!consume(&parser, ':')) return INPUT_API_PARSE_MALFORMED;
        skip_space(&parser);

        if (field == SEEN_ID) {
            if (!parse_uint32(&parser, &command.id))
                return INPUT_API_PARSE_INVALID_VALUE;
        } else if (field == SEEN_HOLD) {
            if (!parse_uint32(&parser, &command.hold_ms))
                return INPUT_API_PARSE_INVALID_VALUE;
        } else if (field == SEEN_KEY) {
            if (!parse_simple_string(&parser, command.key, sizeof(command.key), 1))
                return INPUT_API_PARSE_INVALID_VALUE;
        } else {
            char operation[16];
            if (!parse_simple_string(&parser, operation, sizeof(operation), 0))
                return INPUT_API_PARSE_INVALID_VALUE;
            if (strcmp(operation, "ping") == 0) command.op = INPUT_API_OP_PING;
            else if (strcmp(operation, "tap") == 0) command.op = INPUT_API_OP_TAP;
            else if (strcmp(operation, "keydown") == 0) command.op = INPUT_API_OP_KEYDOWN;
            else if (strcmp(operation, "keyup") == 0) command.op = INPUT_API_OP_KEYUP;
            else return INPUT_API_PARSE_INVALID_VALUE;
        }
        skip_space(&parser);
        if (consume(&parser, '}')) break;
        if (!consume(&parser, ',')) return INPUT_API_PARSE_MALFORMED;
        skip_space(&parser);
    }
    skip_space(&parser);
    if (parser.current != parser.end) return INPUT_API_PARSE_MALFORMED;
    if ((seen & (SEEN_ID | SEEN_OP)) != (SEEN_ID | SEEN_OP))
        return INPUT_API_PARSE_MISSING_FIELD;

    if (command.op == INPUT_API_OP_PING) {
        if (seen & (SEEN_KEY | SEEN_HOLD)) return INPUT_API_PARSE_INVALID_VALUE;
    } else {
        if (!(seen & SEEN_KEY)) return INPUT_API_PARSE_MISSING_FIELD;
        if (command.op == INPUT_API_OP_TAP) {
            if (!(seen & SEEN_HOLD)) command.hold_ms = INPUT_API_DEFAULT_HOLD_MS;
            if (command.hold_ms < 1 || command.hold_ms > 5000)
                return INPUT_API_PARSE_INVALID_VALUE;
        } else if (seen & SEEN_HOLD) {
            return INPUT_API_PARSE_INVALID_VALUE;
        }
    }
    *out = command;
    return INPUT_API_PARSE_OK;
}

int input_api_format_response(
    char *dst, size_t size, uint32_t id, int ok,
    const char *status_or_error)
{
    const unsigned char *scan;
    int written;

    if (dst == NULL || size == 0 || status_or_error == NULL) return -1;
    for (scan = (const unsigned char *)status_or_error; *scan; scan++) {
        if (*scan < 0x20 || *scan > 0x7e || *scan == '"' || *scan == '\\')
            return -1;
    }
    written = snprintf(dst, size,
        ok ? "{\"id\":%u,\"ok\":true,\"status\":\"%s\"}\n"
           : "{\"id\":%u,\"ok\":false,\"error\":\"%s\"}\n",
        (unsigned int)id, status_or_error);
    if (written < 0 || (size_t)written >= size) return -1;
    return written;
}
