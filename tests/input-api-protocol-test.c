#include "input-api-protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static input_api_parse_result_t parse(const char *json, input_api_command_t *command)
{
    memset(command, 0xa5, sizeof(*command));
    return input_api_parse_command(json, strlen(json), command);
}

static void test_valid_commands(void)
{
    input_api_command_t command;

    CHECK(parse("{\"id\":1,\"op\":\"ping\"}", &command) == INPUT_API_PARSE_OK);
    CHECK(command.id == 1 && command.op == INPUT_API_OP_PING);
    CHECK(command.key[0] == '\0' && command.hold_ms == 0);

    CHECK(parse(" { \"op\" : \"tap\", \"key\" : \"KEY88_1\", \"id\" : 2 } ", &command) == INPUT_API_PARSE_OK);
    CHECK(command.id == 2 && command.op == INPUT_API_OP_TAP);
    CHECK(strcmp(command.key, "KEY88_1") == 0 && command.hold_ms == 80);

    CHECK(parse("{\"id\":3,\"op\":\"tap\",\"key\":\"KEY88_RETURN\",\"hold_ms\":120}", &command) == INPUT_API_PARSE_OK);
    CHECK(command.hold_ms == 120);

    CHECK(parse("{\"id\":4,\"op\":\"keydown\",\"key\":\"KEY88_SHIFT\"}", &command) == INPUT_API_PARSE_OK);
    CHECK(command.op == INPUT_API_OP_KEYDOWN && command.hold_ms == 0);

    CHECK(parse("{\"id\":5,\"op\":\"keyup\",\"key\":\"KEY88_SHIFT\"}", &command) == INPUT_API_PARSE_OK);
    CHECK(command.op == INPUT_API_OP_KEYUP);

    CHECK(parse("{\"id\":6,\"op\":\"tap\",\"key\":\"KEY88_\\u0031\"}", &command) == INPUT_API_PARSE_OK);
    CHECK(strcmp(command.key, "KEY88_1") == 0);
}

static void expect(const char *json, input_api_parse_result_t expected)
{
    input_api_command_t command;
    CHECK(parse(json, &command) == expected);
}

static void test_invalid_commands(void)
{
    expect("", INPUT_API_PARSE_MALFORMED);
    expect("[]", INPUT_API_PARSE_MALFORMED);
    expect("{\"id\":1,", INPUT_API_PARSE_MALFORMED);
    expect("{\"op\":\"ping\"}", INPUT_API_PARSE_MISSING_FIELD);
    expect("{\"id\":1}", INPUT_API_PARSE_MISSING_FIELD);
    expect("{\"id\":1,\"op\":\"tap\"}", INPUT_API_PARSE_MISSING_FIELD);
    expect("{\"id\":1,\"id\":2,\"op\":\"ping\"}", INPUT_API_PARSE_DUPLICATE_FIELD);
    expect("{\"id\":1,\"op\":\"ping\",\"extra\":0}", INPUT_API_PARSE_UNKNOWN_FIELD);
    expect("{\"i\\u0064\":1,\"op\":\"ping\"}", INPUT_API_PARSE_MALFORMED);
    expect("{\"id\":-1,\"op\":\"ping\"}", INPUT_API_PARSE_INVALID_VALUE);
    expect("{\"id\":4294967296,\"op\":\"ping\"}", INPUT_API_PARSE_INVALID_VALUE);
    expect("{\"id\":1,\"op\":\"other\"}", INPUT_API_PARSE_INVALID_VALUE);
    expect("{\"id\":1,\"op\":\"ping\",\"key\":\"KEY88_1\"}", INPUT_API_PARSE_INVALID_VALUE);
    expect("{\"id\":1,\"op\":\"keydown\",\"key\":\"KEY88_1\",\"hold_ms\":1}", INPUT_API_PARSE_INVALID_VALUE);
    expect("{\"id\":1,\"op\":\"tap\",\"key\":\"12345678901234567890123456789012\"}", INPUT_API_PARSE_INVALID_VALUE);
    expect("{\"id\":1,\"op\":\"tap\",\"key\":\"KEY88_1\",\"hold_ms\":0}", INPUT_API_PARSE_INVALID_VALUE);
    expect("{\"id\":1,\"op\":\"tap\",\"key\":\"KEY88_1\",\"hold_ms\":5001}", INPUT_API_PARSE_INVALID_VALUE);
    expect("{\"id\":1,\"op\":\"tap\",\"key\":\"KEY88_\\n\"}", INPUT_API_PARSE_INVALID_VALUE);
    expect("{\"id\":1,\"op\":\"ping\"} trailing", INPUT_API_PARSE_MALFORMED);
}

static void test_line_limit(void)
{
    char line[1026];
    input_api_command_t command;
    memset(line, ' ', 1025);
    line[1025] = '\0';
    CHECK(input_api_parse_command(line, 1025, &command) == INPUT_API_PARSE_LINE_TOO_LONG);
}

static void test_responses(void)
{
    char output[128];
    int length;
    length = input_api_format_response(output, sizeof(output), 1, 1, "queued");
    CHECK(length == (int)strlen("{\"id\":1,\"ok\":true,\"status\":\"queued\"}\n"));
    CHECK(strcmp(output, "{\"id\":1,\"ok\":true,\"status\":\"queued\"}\n") == 0);
    length = input_api_format_response(output, sizeof(output), 5, 0, "invalid_key");
    CHECK(length == (int)strlen("{\"id\":5,\"ok\":false,\"error\":\"invalid_key\"}\n"));
    CHECK(strcmp(output, "{\"id\":5,\"ok\":false,\"error\":\"invalid_key\"}\n") == 0);
    CHECK(input_api_format_response(output, 8, 1, 1, "queued") == -1);
    CHECK(input_api_format_response(output, sizeof(output), 1, 1, "bad\"value") == -1);
}

int main(void)
{
    test_valid_commands();
    test_invalid_commands();
    test_line_limit();
    test_responses();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
