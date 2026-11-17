#ifndef INPUT_API_H
#define INPUT_API_H

#include "input-api-core.h"

#include <stddef.h>
#include <stdint.h>

#define INPUT_API_RX_CAPACITY 1025
#define INPUT_API_TX_CAPACITY 65536

typedef int (*input_api_key_resolver_t)(const char *name);

extern int input_api_port;

typedef struct {
    intptr_t listener;
    intptr_t client;
    int sockets_started;
    uint16_t bound_port;
    char rx[INPUT_API_RX_CAPACITY];
    size_t rx_length;
    int discarding_line;
    char tx[INPUT_API_TX_CAPACITY];
    size_t tx_offset;
    size_t tx_length;
    input_api_core_t core;
    input_api_key_resolver_t resolver;
    input_api_key_sink_t sink;
    void *opaque;
} input_api_t;

int input_api_init(
    input_api_t *api, uint16_t port,
    input_api_key_resolver_t resolver,
    input_api_key_sink_t sink, void *opaque);
void input_api_poll(input_api_t *api, uint64_t now_ms);
void input_api_release_all(input_api_t *api);
void input_api_shutdown(input_api_t *api);
uint16_t input_api_bound_port(const input_api_t *api);

#endif
