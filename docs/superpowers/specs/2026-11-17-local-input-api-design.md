# Local Input API Design

## Intent

Add an opt-in, localhost-only input API to QUASI88 so automation can deliver
PC-8801 key presses reliably without depending on Windows focus or synthetic
desktop input.

## Goals

- Enable the API only when `-input-api <port>` is supplied.
- Bind only to IPv4 loopback (`127.0.0.1`).
- Accept ordered NDJSON commands for `tap`, `keydown`, `keyup`, and `ping`.
- Convert QUASI88 key names through the existing `keyboard_str2key88()` path.
- Deliver key transitions through the existing `quasi88_key()` path.
- Acknowledge accepted or rejected commands with the caller-provided `id`.
- Keep networking non-blocking so a slow or broken client cannot stall emulation.

## Non-goals

- No LAN or Internet listening, authentication, TLS, or multiple clients.
- No screen capture, disk control, reset, save-state, or other emulator control.
- No replacement of physical, SDL, touch-key, record, or playback input.

## Constraints

- The API is disabled by default.
- Only one client may be connected at a time.
- Input lines are at most 1024 bytes and the command queue holds at most 256 items.
- `tap` defaults to an 80 ms hold and must span separate poll iterations.
- Disconnect and shutdown release every key held through the API.
- The implementation adds no JSON or networking runtime dependency.
- Windows and POSIX socket implementations share the same public API.

## Chosen Approach

Use a non-blocking TCP listener and a strict, limited NDJSON parser. Poll it once
per QUASI88 frame immediately after the normal system event update. Parsed key
commands enter a bounded FIFO; a scheduler emits key-down and key-up transitions
through an injected key sink. Production supplies `quasi88_key()` as that sink,
while tests supply a recording sink.

Alternatives considered:

- Windows named pipes: safe and local, but Windows-specific.
- Extending `-playback`: smaller, but lacks acknowledgements and interactive
  timing/error handling.

## Protocol

Requests contain one JSON object per line:

```json
{"id":1,"op":"tap","key":"KEY88_1","hold_ms":80}
{"id":2,"op":"keydown","key":"KEY88_SHIFT"}
{"id":3,"op":"keyup","key":"KEY88_SHIFT"}
{"id":4,"op":"ping"}
```

Responses also contain one object per line:

```json
{"id":1,"ok":true,"status":"queued"}
{"id":4,"ok":true,"status":"ready"}
{"id":5,"ok":false,"error":"invalid_key"}
```

`id` is an unsigned 32-bit integer. Key commands require `key`. `tap` accepts
`hold_ms` from 1 through 5000; omission uses 80. Unknown fields, duplicate fields,
malformed JSON, missing required fields, oversized lines, and a full queue are
rejected without changing emulator input state.

## Lifecycle and Failure Handling

Listener setup occurs after QUASI88 core initialization and before the main loop.
Failure to open an explicitly requested port is a startup error. Polling accepts
at most one client, consumes complete lines, queues commands, advances timed key
releases, and writes responses without blocking. A second connection is rejected.
Client disconnect, parser failure requiring connection closure, emulator reset,
and shutdown release all API-held keys.

## Verification

Unit tests cover parsing, queue order, hold timing, duplicate commands, full queues,
oversized input, and disconnect release. Loopback integration tests cover bind,
connect, acknowledgements, command delivery, single-client enforcement, and clean
reconnection. A small Python standard-library client provides the manual smoke test.
