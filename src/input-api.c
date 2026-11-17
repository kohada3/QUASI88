#include "input-api.h"
#include "input-api-protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET native_socket_t;
typedef int native_socket_length_t;
#define CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int native_socket_t;
typedef socklen_t native_socket_length_t;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define CLOSE_SOCKET close
#endif

static native_socket_t native_socket(intptr_t value)
{
    return (native_socket_t)(uintptr_t)value;
}

static int socket_invalid(intptr_t value)
{
    return native_socket(value) == INVALID_SOCKET;
}

static int would_block(void)
{
#ifdef _WIN32
    int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int set_nonblocking(native_socket_t socket_value)
{
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(socket_value, FIONBIO, &enabled) == 0;
#else
    int flags = fcntl(socket_value, F_GETFL, 0);
    return flags >= 0 && fcntl(socket_value, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void compact_tx(input_api_t *api)
{
    if (api->tx_offset != 0 && api->tx_length != 0) {
        memmove(api->tx, api->tx + api->tx_offset, api->tx_length);
    }
    api->tx_offset = 0;
}

static int queue_response(input_api_t *api, uint32_t id, int ok, const char *message)
{
    char response[128];
    int length = input_api_format_response(
        response, sizeof(response), id, ok, message);
    if (length < 0) return 0;
    if (api->tx_offset + api->tx_length + (size_t)length > INPUT_API_TX_CAPACITY)
        compact_tx(api);
    if (api->tx_length + (size_t)length > INPUT_API_TX_CAPACITY) return 0;
    memcpy(api->tx + api->tx_offset + api->tx_length, response, (size_t)length);
    api->tx_length += (size_t)length;
    return 1;
}

static uint32_t best_effort_id(const char *line, size_t length)
{
    const char *found;
    char copy[INPUT_API_MAX_LINE + 1];
    unsigned long value;
    if (length > INPUT_API_MAX_LINE) return 0;
    memcpy(copy, line, length);
    copy[length] = '\0';
    found = strstr(copy, "\"id\"");
    if (found == NULL || (found = strchr(found, ':')) == NULL) return 0;
    value = strtoul(found + 1, NULL, 10);
    return value > UINT32_MAX ? 0 : (uint32_t)value;
}

static const char *parse_error(input_api_parse_result_t result)
{
    switch (result) {
    case INPUT_API_PARSE_MISSING_FIELD: return "missing_field";
    case INPUT_API_PARSE_DUPLICATE_FIELD: return "duplicate_field";
    case INPUT_API_PARSE_UNKNOWN_FIELD: return "unknown_field";
    case INPUT_API_PARSE_INVALID_VALUE: return "invalid_value";
    case INPUT_API_PARSE_LINE_TOO_LONG: return "line_too_long";
    default: return "malformed_json";
    }
}

static void process_line(input_api_t *api, const char *line, size_t length)
{
    input_api_command_t command;
    input_api_parse_result_t parsed = input_api_parse_command(line, length, &command);
    input_api_action_t action;
    int key88;
    if (parsed != INPUT_API_PARSE_OK) {
        queue_response(api, best_effort_id(line, length), 0, parse_error(parsed));
        return;
    }
    if (command.op == INPUT_API_OP_PING) {
        queue_response(api, command.id, 1, "ready");
        return;
    }
    key88 = api->resolver(command.key);
    if (key88 < INPUT_API_KEY_MIN || key88 > INPUT_API_KEY_MAX) {
        queue_response(api, command.id, 0, "invalid_key");
        return;
    }
    action.key88 = key88;
    action.hold_ms = command.hold_ms;
    action.type = command.op == INPUT_API_OP_TAP ? INPUT_API_ACTION_TAP
        : command.op == INPUT_API_OP_KEYDOWN ? INPUT_API_ACTION_DOWN
        : INPUT_API_ACTION_UP;
    if (!input_api_core_submit(&api->core, &action)) {
        queue_response(api, command.id, 0, "queue_full");
    } else {
        queue_response(api, command.id, 1, "queued");
    }
}

static void cleanup_client(input_api_t *api)
{
    input_api_core_release_all(&api->core, api->sink, api->opaque);
    if (!socket_invalid(api->client)) CLOSE_SOCKET(native_socket(api->client));
    api->client = (intptr_t)INVALID_SOCKET;
    api->rx_length = 0;
    api->discarding_line = 0;
    api->tx_offset = 0;
    api->tx_length = 0;
}

static void flush_tx(input_api_t *api)
{
    while (!socket_invalid(api->client) && api->tx_length != 0) {
        int sent = send(native_socket(api->client), api->tx + api->tx_offset,
                        (int)api->tx_length, 0);
        if (sent > 0) {
            api->tx_offset += (size_t)sent;
            api->tx_length -= (size_t)sent;
            if (api->tx_length == 0) api->tx_offset = 0;
        } else if (sent == SOCKET_ERROR && would_block()) {
            return;
        } else {
            cleanup_client(api);
            return;
        }
    }
}

int input_api_init(
    input_api_t *api, uint16_t port,
    input_api_key_resolver_t resolver,
    input_api_key_sink_t sink, void *opaque)
{
    native_socket_t listener;
    struct sockaddr_in address;
    int reuse = 1;
    native_socket_length_t address_length = (native_socket_length_t)sizeof(address);
#ifdef _WIN32
    WSADATA data;
#endif
    if (api == NULL || resolver == NULL || sink == NULL) return 0;
    memset(api, 0, sizeof(*api));
    api->listener = (intptr_t)INVALID_SOCKET;
    api->client = (intptr_t)INVALID_SOCKET;
    api->resolver = resolver;
    api->sink = sink;
    api->opaque = opaque;
    input_api_core_init(&api->core);
#ifdef _WIN32
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 0;
    api->sockets_started = 1;
#endif
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) goto fail;
    api->listener = (intptr_t)listener;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR || !set_nonblocking(listener) ||
        getsockname(listener, (struct sockaddr *)&address, &address_length) == SOCKET_ERROR)
        goto fail;
    api->bound_port = ntohs(address.sin_port);
    return 1;
fail:
    input_api_shutdown(api);
    return 0;
}

void input_api_poll(input_api_t *api, uint64_t now_ms)
{
    native_socket_t accepted;
    char received[2048];
    int count;
    int i;
    if (api == NULL || socket_invalid(api->listener)) return;
    for (;;) {
        accepted = accept(native_socket(api->listener), NULL, NULL);
        if (accepted == INVALID_SOCKET) break;
        set_nonblocking(accepted);
        if (socket_invalid(api->client)) api->client = (intptr_t)accepted;
        else CLOSE_SOCKET(accepted);
    }
    if (socket_invalid(api->client)) return;
    flush_tx(api);
    if (socket_invalid(api->client)) return;
    for (;;) {
        count = recv(native_socket(api->client), received, sizeof(received), 0);
        if (count == 0) { cleanup_client(api); return; }
        if (count == SOCKET_ERROR) {
            if (would_block()) break;
            cleanup_client(api); return;
        }
        for (i = 0; i < count; i++) {
            char ch = received[i];
            if (api->discarding_line) {
                if (ch == '\n') api->discarding_line = 0;
            } else if (ch == '\n') {
                size_t length = api->rx_length;
                if (length != 0 && api->rx[length - 1] == '\r') length--;
                process_line(api, api->rx, length);
                api->rx_length = 0;
            } else if (api->rx_length == INPUT_API_MAX_LINE) {
                queue_response(api, 0, 0, "line_too_long");
                api->rx_length = 0;
                api->discarding_line = 1;
            } else {
                api->rx[api->rx_length++] = ch;
            }
        }
    }
    input_api_core_tick(&api->core, now_ms, api->sink, api->opaque);
    flush_tx(api);
}

void input_api_shutdown(input_api_t *api)
{
    if (api == NULL) return;
    if (!socket_invalid(api->client)) cleanup_client(api);
    else input_api_core_release_all(&api->core, api->sink, api->opaque);
    if (!socket_invalid(api->listener)) CLOSE_SOCKET(native_socket(api->listener));
    api->listener = (intptr_t)INVALID_SOCKET;
    api->bound_port = 0;
#ifdef _WIN32
    if (api->sockets_started) {
        WSACleanup();
        api->sockets_started = 0;
    }
#endif
}

uint16_t input_api_bound_port(const input_api_t *api)
{
    return api->bound_port;
}
