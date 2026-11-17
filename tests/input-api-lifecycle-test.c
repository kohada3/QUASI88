#include "input-api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define closesocket close
#endif

extern int input_api_port;

typedef struct { int key88; int pressed; } event_t;
typedef struct { event_t events[16]; size_t count; } recorder_t;
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int resolve_key(const char *name) { (void)name; return 40; }
static void record_key(int key88, int pressed, void *opaque)
{
    recorder_t *recorder = (recorder_t *)opaque;
    recorder->events[recorder->count].key88 = key88;
    recorder->events[recorder->count].pressed = pressed;
    recorder->count++;
}

static void test_disabled_default(void)
{
    CHECK(input_api_port == 0);
}

static void test_occupied_port_fails(void)
{
    SOCKET occupied = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in address;
    int length = (int)sizeof(address);
    input_api_t api;
    recorder_t recorder = { 0 };
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    CHECK(bind(occupied, (struct sockaddr *)&address, sizeof(address)) == 0);
    CHECK(listen(occupied, 1) == 0);
    CHECK(getsockname(occupied, (struct sockaddr *)&address, &length) == 0);
    CHECK(!input_api_init(&api, ntohs(address.sin_port), resolve_key, record_key, &recorder));
    closesocket(occupied);
}

static void test_shutdown_releases_and_discards(void)
{
    input_api_t api;
    recorder_t recorder = { 0 };
    input_api_action_t action;
    CHECK(input_api_init(&api, 0, resolve_key, record_key, &recorder));
    action.type = INPUT_API_ACTION_DOWN; action.hold_ms = 0;
    action.key88 = 40; CHECK(input_api_core_submit(&api.core, &action));
    action.key88 = 41; CHECK(input_api_core_submit(&api.core, &action));
    input_api_core_tick(&api.core, 10, record_key, &recorder);
    action.type = INPUT_API_ACTION_TAP; action.key88 = 42; action.hold_ms = 80;
    CHECK(input_api_core_submit(&api.core, &action));
    input_api_shutdown(&api);
    CHECK(recorder.count == 4);
    CHECK(recorder.events[2].key88 == 40 && recorder.events[2].pressed == 0);
    CHECK(recorder.events[3].key88 == 41 && recorder.events[3].pressed == 0);
    CHECK(input_api_core_pending(&api.core) == 0);
    input_api_shutdown(&api);
    CHECK(recorder.count == 4);
}

int main(void)
{
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return EXIT_FAILURE;
#endif
    test_disabled_default();
    test_occupied_port_fails();
    test_shutdown_releases_and_discards();
#ifdef _WIN32
    WSACleanup();
#endif
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
