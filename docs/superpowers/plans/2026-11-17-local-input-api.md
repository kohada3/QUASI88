# Local Input API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in localhost TCP API that reliably injects PC-8801 key transitions into QUASI88.

**Architecture:** A strict NDJSON parser feeds a bounded, time-aware input core. A non-blocking loopback TCP server owns connection and buffering state, while QUASI88 integrates it at the shared per-frame event boundary and supplies the existing key resolver and key sink.

**Tech Stack:** C, CMake/CTest, Winsock2 on Windows, BSD sockets on POSIX, Python 3 standard library for the smoke client.

**Spec:** `docs/superpowers/specs/2026-11-17-local-input-api-design.md`

## Global Constraints

- The API is disabled unless `-input-api <port>` is supplied.
- Bind only to IPv4 `127.0.0.1`; do not offer a configurable bind address.
- Support exactly one client, 1024-byte input lines, and a 256-command FIFO.
- Support only `tap`, `keydown`, `keyup`, and `ping` in v1.
- `tap` defaults to 80 ms; valid `hold_ms` values are 1 through 5000.
- Add no JSON or networking runtime dependency.
- Release all API-held keys on disconnect, shutdown, and failed connection cleanup.
- Keep physical, SDL, touch-key, record, and playback behavior unchanged.

## Review Focus

- A client sends a valid line in several TCP fragments: buffer it until newline and execute exactly once (Task 3 integration test).
- A client sends several lines in one packet: preserve order and return one response per line (Task 3 integration test).
- A client disconnects while SHIFT and another key are down: release both exactly once (Task 2 and Task 3 tests).
- The response socket cannot currently accept all bytes: retain pending response bytes without blocking or truncation (Task 3 integration test).
- The emulator is shut down with queued taps and held keys: release held keys and discard future transitions (Task 4 lifecycle test).

---

### Task 1: Strict NDJSON Command Parser

**Files:**
- Create: `src/input-api-protocol.h`
- Create: `src/input-api-protocol.c`
- Create: `tests/input-api-protocol-test.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: newline-free UTF-8 bytes from the future socket server.
- Produces: `input_api_parse_command(const char *line, size_t length, input_api_command_t *out)` returning `input_api_parse_result_t`; `input_api_format_response(char *dst, size_t size, uint32_t id, int ok, const char *status_or_error)` returning encoded byte count or `-1`.

- [ ] **Step 1: Add parser types and declarations**

Define `INPUT_API_OP_PING`, `INPUT_API_OP_TAP`, `INPUT_API_OP_KEYDOWN`, and `INPUT_API_OP_KEYUP`; define `input_api_command_t` with `uint32_t id`, operation, `char key[32]`, and `uint32_t hold_ms`; define result values for valid, malformed, missing field, duplicate field, unknown field, invalid value, and line too long.

- [ ] **Step 2: Add the failing parser test executable**

Cover the four valid request shapes and assert exact output fields. Add failure cases for malformed JSON, missing `id`/`op`/`key`, duplicate fields, unknown fields, negative or overflowing `id`, a key longer than 31 bytes, `hold_ms` values 0 and 5001, and 1025-byte input. Assert the exact success and error response strings, including the trailing newline.

- [ ] **Step 3: Register the test with CTest**

Add `include(CTest)`, an `input_api_protocol_tests` executable built only from the parser and its test, and `add_test(NAME input_api_protocol_tests ...)` under `if(BUILD_TESTING)`.

- [ ] **Step 4: Run the test to verify it fails**

Run: `cmake -S . -B build-test -DBUILD_TESTING=ON -DUSE_SOUND=OFF -DUSE_FMGEN=OFF && cmake --build build-test --target input_api_protocol_tests && ctest --test-dir build-test -R input_api_protocol_tests --output-on-failure`

Expected: build or test failure because parser behavior is not implemented.

- [ ] **Step 5: Implement the minimal strict parser and response formatter**

Parse only a flat object containing JSON strings and unsigned decimal integers. Reject escapes in field names; support standard JSON escapes in `key` only when they decode to a printable ASCII byte sequence. Do not accept trailing non-whitespace bytes.

- [ ] **Step 6: Run the parser tests**

Run the Step 4 command.

Expected: `100% tests passed` for `input_api_protocol_tests`.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/input-api-protocol.c src/input-api-protocol.h tests/input-api-protocol-test.c
git commit -m "feat: add local input API protocol parser"
```

### Task 2: Bounded Key Queue and Tap Scheduler

**Files:**
- Create: `src/input-api-core.h`
- Create: `src/input-api-core.c`
- Create: `tests/input-api-core-test.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: validated numeric key codes and commands through `input_api_core_submit(input_api_core_t *, const input_api_action_t *)`.
- Produces: `input_api_core_init`, `input_api_core_tick(input_api_core_t *, uint64_t now_ms, input_api_key_sink_t sink, void *opaque)`, `input_api_core_release_all`, and `input_api_core_pending`; calls `sink(int key88, int pressed, void *opaque)` for transitions.

- [ ] **Step 1: Add the core interface and constants**

Define a 256-entry FIFO, an API-held-key bitset covering valid QUASI88 key codes 32 through 247, and action types for tap/down/up. Keep socket and JSON types out of this header.

- [ ] **Step 2: Add failing scheduler tests**

Assert FIFO ordering, queue-full rejection without mutation, down/up delivery, default 80 ms tap timing across separate ticks, caller-specified 1 ms and 5000 ms timing, repeated keydown idempotence, keyup of a non-held key as a no-op, and disconnect release of multiple held keys exactly once.

- [ ] **Step 3: Register and run the core test**

Run: `cmake --build build-test --target input_api_core_tests && ctest --test-dir build-test -R input_api_core_tests --output-on-failure`

Expected: failure because the scheduler is not implemented.

- [ ] **Step 4: Implement FIFO and scheduler behavior**

Process queued actions in insertion order. A tap emits keydown when dequeued and schedules its keyup no earlier than `now_ms + hold_ms`; scheduled releases must not prevent later unrelated key actions from progressing.

- [ ] **Step 5: Run the core tests**

Run the Step 3 command.

Expected: `100% tests passed` for `input_api_core_tests`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/input-api-core.c src/input-api-core.h tests/input-api-core-test.c
git commit -m "feat: add input API key scheduler"
```

### Task 3: Non-blocking Loopback TCP Server

**Files:**
- Create: `src/input-api.h`
- Create: `src/input-api.c`
- Create: `tests/input-api-server-test.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `input_api_parse_command`, `input_api_format_response`, input-core APIs, `input_api_key_resolver_t(const char *)`, and `input_api_key_sink_t`.
- Produces: `input_api_init(input_api_t *, uint16_t port, input_api_key_resolver_t resolver, input_api_key_sink_t sink, void *opaque)`, `input_api_poll(input_api_t *, uint64_t now_ms)`, `input_api_shutdown(input_api_t *)`, and `input_api_bound_port(const input_api_t *)`.

- [ ] **Step 1: Add the server interface and platform socket aliases**

Keep Windows `SOCKET`/`WSAStartup` and POSIX descriptor details private to `input-api.c`. Port `0` is permitted only in tests so the OS can select a free loopback port.

- [ ] **Step 2: Add failing loopback integration tests**

Test loopback-only bind, connect and ping ACK, fragmented request assembly, multiple requests in one send, valid key resolution and queued ACK, invalid key rejection, 1025-byte line rejection, 256-entry queue exhaustion, second-client rejection, partial response writes, disconnect release, and reconnect after cleanup.

- [ ] **Step 3: Link the test for Windows and POSIX**

Add `ws2_32` only for Windows test and production targets. Use the normal socket library without an extra dependency on POSIX.

- [ ] **Step 4: Run the server tests to verify they fail**

Run: `cmake --build build-test --target input_api_server_tests && ctest --test-dir build-test -R input_api_server_tests --output-on-failure`

Expected: failure because listener, buffering, and response handling are not implemented.

- [ ] **Step 5: Implement the non-blocking server**

Bind an IPv4 socket to `htonl(INADDR_LOOPBACK)`, listen with backlog 1, set listener and accepted socket non-blocking, keep fixed receive and transmit buffers, and process complete newline-delimited commands without blocking. On connection cleanup call `input_api_core_release_all` before closing the client.

- [ ] **Step 6: Run protocol, core, and server tests**

Run: `ctest --test-dir build-test --output-on-failure`

Expected: all three test executables pass.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/input-api.c src/input-api.h tests/input-api-server-test.c
git commit -m "feat: add loopback input API server"
```

### Task 4: QUASI88 Lifecycle, Configuration, and Build Integration

**Files:**
- Modify: `src/getconf.c`
- Modify: `src/help.h`
- Modify: `src/quasi88.c`
- Modify: `CMakeLists.txt`
- Modify: `visualc/QUASI88.vcxproj`
- Modify: `doc/manual.txt`
- Create: `tests/input-api-lifecycle-test.c`

**Interfaces:**
- Consumes: Task 3 server API, `keyboard_str2key88(const char *)`, and `quasi88_key(int code, int on)`.
- Produces: `-input-api <port>` with range 1 through 65535 and a once-per-frame poll integrated immediately after `event_update()`.

- [ ] **Step 1: Add a failing lifecycle test with fake resolver and sink**

Exercise initialization disabled at port 0, initialization failure on an occupied requested port, shutdown with queued taps, shutdown with multiple held keys, and repeat shutdown safety.

- [ ] **Step 2: Add the configuration variable and option**

Expose `int input_api_port` from `input-api.h`, default it to 0, and add an `X_INT` option named `input-api` with bounds 1 and 65535 in the debug/system option range. Add the matching English help line and Japanese manual entry, explicitly stating `127.0.0.1` only and disabled by default.

- [ ] **Step 3: Integrate startup, polling, and shutdown**

Initialize after PC-88 and record/playback initialization. Treat failure of an explicitly requested listener as startup failure with an error message containing the port. Poll immediately after the shared `event_update()` call in `quasi88_loop()`. Shut down before `key_record_playback_exit()`.

- [ ] **Step 4: Add sources and Windows socket linkage to both build systems**

Add the three new implementation units to `SOURCES` in CMake and to `visualc/QUASI88.vcxproj`. Link `ws2_32` for every Windows configuration without removing inherited libraries.

- [ ] **Step 5: Run focused tests and a no-sound QUASI88 build**

Run: `cmake -S . -B build-test -DBUILD_TESTING=ON -DUSE_SOUND=OFF -DUSE_FMGEN=OFF && cmake --build build-test && ctest --test-dir build-test --output-on-failure`

Expected: QUASI88 and all API tests build; all tests pass.

- [ ] **Step 6: Verify CLI validation manually**

Run the built executable once with `-input-api 0`, once with `-input-api 65536`, and once with an occupied valid port.

Expected: invalid ranges are rejected by configuration parsing; occupied valid port produces a startup error and does not enter the emulation loop.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt visualc/QUASI88.vcxproj src/getconf.c src/help.h src/quasi88.c doc/manual.txt tests/input-api-lifecycle-test.c
git commit -m "feat: integrate local input API with QUASI88"
```

### Task 5: Client Tool and End-to-End Smoke Test

**Files:**
- Create: `tools/quasi88-input.py`
- Create: `tests/quasi88-input-client-test.py`
- Modify: `doc/manual.txt`

**Interfaces:**
- Consumes: host/port plus `ping`, `tap`, `keydown`, or `keyup` CLI arguments.
- Produces: one NDJSON request, validates the matching response `id`, prints the response, and exits 0 only for `ok:true`.

- [ ] **Step 1: Add failing Python client tests**

Use a standard-library fake TCP server to assert exact request JSON, default 80 ms tap behavior, custom hold duration, special key names, matching response IDs, error-response exit status, timeout handling, and connection-refused handling.

- [ ] **Step 2: Run the client tests to verify they fail**

Run: `python -m unittest tests/quasi88-input-client-test.py -v`

Expected: failure because the client does not exist.

- [ ] **Step 3: Implement the standard-library client**

CLI examples must work exactly as follows:

```text
python tools/quasi88-input.py --port 18801 ping
python tools/quasi88-input.py --port 18801 tap KEY88_1
python tools/quasi88-input.py --port 18801 tap KEY88_RETURN --hold-ms 120
python tools/quasi88-input.py --port 18801 keydown KEY88_SHIFT
python tools/quasi88-input.py --port 18801 keyup KEY88_SHIFT
```

- [ ] **Step 4: Run client and C tests**

Run: `python -m unittest tests/quasi88-input-client-test.py -v && ctest --test-dir build-test --output-on-failure`

Expected: Python tests and all CTest tests pass.

- [ ] **Step 5: Run the end-to-end smoke test against QUASI88**

Start QUASI88 with the existing Murder Club disks plus `-input-api 18801`. Send `ping`, then `tap KEY88_1`, `tap KEY88_A`, and `tap KEY88_RETURN`; verify each response has the matching ID and `ok:true`, and visually confirm each command advances or affects the game exactly once.

- [ ] **Step 6: Document operation and limitations**

Add protocol examples, loopback-only behavior, disabled-by-default behavior, single-client limit, line/queue limits, valid key-name examples, and clean client shutdown guidance to `doc/manual.txt`.

- [ ] **Step 7: Commit**

```bash
git add tools/quasi88-input.py tests/quasi88-input-client-test.py doc/manual.txt
git commit -m "docs: add local input API client and usage"
```

### Task 6: Final Verification

**Files:**
- Verify only; modify files only if a failing check exposes a defect within this feature.

**Interfaces:**
- Consumes: all outputs from Tasks 1 through 5.
- Produces: a release-build executable and recorded verification evidence.

- [ ] **Step 1: Run every automated test from a clean build directory**

Run: `cmake -S . -B build-verify -DBUILD_TESTING=ON -DUSE_SOUND=OFF -DUSE_FMGEN=OFF && cmake --build build-verify --config Release && ctest --test-dir build-verify -C Release --output-on-failure && python -m unittest tests/quasi88-input-client-test.py -v`

Expected: all builds and tests pass with zero failures.

- [ ] **Step 2: Run the Murder Club reliability check**

Send at least 30 sequential `tap` commands using keys required by the game, waiting for each ACK. Confirm no missing or duplicate selection, then disconnect while SHIFT is down and verify a subsequent physical key does not remain shifted.

- [ ] **Step 3: Check disabled-by-default behavior**

Start QUASI88 without `-input-api` and verify connecting to the previously used port fails while normal keyboard and touch-key input still work.

- [ ] **Step 4: Review the final diff**

Run: `git diff --check HEAD~5..HEAD && git status --short`

Expected: no whitespace errors and no unintended files.

- [ ] **Step 5: Commit any verification-only correction**

If no correction was needed, do not create an empty commit. Otherwise commit only the verified correction with a message describing the defect fixed.
