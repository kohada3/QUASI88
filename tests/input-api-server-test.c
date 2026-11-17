#include "input-api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSESOCKET closesocket
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define CLOSESOCKET close
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

typedef struct { int key88; int pressed; } key_event_t;
typedef struct { key_event_t events[1024]; size_t count; } recorder_t;
static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } \
} while (0)

static int resolve_key(const char *name)
{
    if (strcmp(name, "KEY88_A") == 0) return 65;
    if (strcmp(name, "KEY88_SHIFT") == 0) return 66;
    return -1;
}

static void record_key(int key88, int pressed, void *opaque)
{
    recorder_t *recorder = (recorder_t *)opaque;
    recorder->events[recorder->count].key88 = key88;
    recorder->events[recorder->count].pressed = pressed;
    recorder->count++;
}

static SOCKET connect_client(uint16_t port)
{
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in address;
#ifdef _WIN32
    DWORD timeout = 20;
#else
    struct timeval timeout = { 0, 20000 };
#endif
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(client, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        CLOSESOCKET(client);
        return INVALID_SOCKET;
    }
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
               (const char *)&timeout, sizeof(timeout));
    return client;
}

static int receive_lines(
    input_api_t *api, SOCKET client, char *output, size_t size,
    int wanted_lines, uint64_t *now_ms)
{
    size_t length = 0;
    int lines = 0;
    int attempts;
    for (attempts = 0; attempts < 5000 && lines < wanted_lines; attempts++) {
        int received;
        input_api_poll(api, (*now_ms)++);
        received = recv(client, output + length, (int)(size - length - 1), 0);
        if (received > 0) {
            int i;
            for (i = 0; i < received; i++) if (output[length + i] == '\n') lines++;
            length += (size_t)received;
        }
        if (lines < wanted_lines) SLEEP_MS(1);
    }
    output[length] = '\0';
    return lines;
}

static void send_text(SOCKET client, const char *text)
{
    size_t length = strlen(text);
    size_t sent = 0;
    while (sent < length) {
        int result = send(client, text + sent, (int)(length - sent), 0);
        CHECK(result > 0);
        if (result <= 0) return;
        sent += (size_t)result;
    }
}

static void test_server(void)
{
    input_api_t api;
    recorder_t recorder = { 0 };
    SOCKET client;
    SOCKET second;
    char response[70000];
    uint64_t now_ms = 100;
    int i;

    if (!input_api_init(&api, 0, resolve_key, record_key, &recorder)) {
        CHECK(0 && "input_api_init");
        return;
    }
    CHECK(input_api_bound_port(&api) != 0);
    client = connect_client(input_api_bound_port(&api));
    CHECK(client != INVALID_SOCKET);

    send_text(client, "{\"id\":1,\"op\":\"pi");
    input_api_poll(&api, now_ms++);
    send_text(client, "ng\"}\n");
    CHECK(receive_lines(&api, client, response, sizeof(response), 1, &now_ms) == 1);
    CHECK(strcmp(response, "{\"id\":1,\"ok\":true,\"status\":\"ready\"}\n") == 0);

    send_text(client, "{\"id\":2,\"op\":\"keydown\",\"key\":\"KEY88_A\"}\n{\"id\":3,\"op\":\"keyup\",\"key\":\"KEY88_A\"}\n");
    CHECK(receive_lines(&api, client, response, sizeof(response), 2, &now_ms) == 2);
    CHECK(strstr(response, "{\"id\":2,\"ok\":true,\"status\":\"queued\"}\n") != NULL);
    CHECK(strstr(response, "{\"id\":3,\"ok\":true,\"status\":\"queued\"}\n") != NULL);
    CHECK(recorder.count == 2 && recorder.events[0].pressed == 1 && recorder.events[1].pressed == 0);

    send_text(client, "{\"id\":4,\"op\":\"tap\",\"key\":\"NOPE\"}\n");
    CHECK(receive_lines(&api, client, response, sizeof(response), 1, &now_ms) == 1);
    CHECK(strstr(response, "\"id\":4,\"ok\":false,\"error\":\"invalid_key\"") != NULL);

    {
        int small_buffer = 1024;
        setsockopt(client, SOL_SOCKET, SO_RCVBUF,
                   (const char *)&small_buffer, sizeof(small_buffer));
        for (i = 0; i < 1000; i++) {
            char line[40];
            sprintf(line, "{\"id\":%d,\"op\":\"ping\"}\n", 2000 + i);
            send_text(client, line);
        }
        CHECK(receive_lines(&api, client, response, sizeof(response), 1000, &now_ms) == 1000);
        CHECK(strstr(response, "\"id\":2000,\"ok\":true") != NULL);
        CHECK(strstr(response, "\"id\":2999,\"ok\":true") != NULL);
    }

    second = connect_client(input_api_bound_port(&api));
    CHECK(second != INVALID_SOCKET);
    input_api_poll(&api, now_ms++);
    SLEEP_MS(5);
    CHECK(send(second, "x", 1, 0) == SOCKET_ERROR || recv(second, response, 1, 0) <= 0);
    CLOSESOCKET(second);

    for (i = 0; i < 300; i++) {
        char line[64];
        sprintf(line, "{\"id\":%d,\"op\":\"keydown\",\"key\":\"KEY88_A\"}\n", 1000 + i);
        send_text(client, line);
    }
    CHECK(receive_lines(&api, client, response, sizeof(response), 300, &now_ms) == 300);
    CHECK(strstr(response, "\"error\":\"queue_full\"") != NULL);

    {
        char oversized[1027];
        memset(oversized, ' ', 1025);
        oversized[1025] = '\n'; oversized[1026] = '\0';
        send_text(client, oversized);
        CHECK(receive_lines(&api, client, response, sizeof(response), 1, &now_ms) == 1);
        CHECK(strstr(response, "\"error\":\"line_too_long\"") != NULL);
    }

    send_text(client, "{\"id\":5,\"op\":\"keydown\",\"key\":\"KEY88_SHIFT\"}\n");
    CHECK(receive_lines(&api, client, response, sizeof(response), 1, &now_ms) == 1);
    CLOSESOCKET(client);
    for (i = 0; i < 20; i++) { input_api_poll(&api, now_ms++); SLEEP_MS(1); }
    CHECK(recorder.events[recorder.count - 1].key88 == 66);
    CHECK(recorder.events[recorder.count - 1].pressed == 0);

    client = connect_client(input_api_bound_port(&api));
    CHECK(client != INVALID_SOCKET);
    send_text(client, "{\"id\":6,\"op\":\"ping\"}\n");
    CHECK(receive_lines(&api, client, response, sizeof(response), 1, &now_ms) == 1);
    CHECK(strstr(response, "\"id\":6,\"ok\":true") != NULL);
    CLOSESOCKET(client);
    input_api_shutdown(&api);
    input_api_shutdown(&api);
}

int main(void)
{
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return EXIT_FAILURE;
#endif
    test_server();
#ifdef _WIN32
    WSACleanup();
#endif
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
