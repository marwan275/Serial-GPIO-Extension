# Request Frame Parser Algorithm

This document explains how the request parser works today.
It is based on the implementation in `request_frame_parser.cpp` and the interface in `request_frame_parser.h`.

The parser accepts request frames from a `Stream`, validates the wire format, converts the payload into a `Frame::RequestFrame`, and pushes valid frames into the global request queue.

## Wire Format

The parser expects this ASCII request shape:

```text
@<type>,<requestId>,<pinType>,<priority>,<pin>,<value>;
```

The control characters have fixed meaning:

- `@` starts a frame
- `,` separates fields
- `;` ends the payload
- `\n` must appear after `;`
- `\r` is accepted only between `;` and `\n`

Everything between `@` and `;` is stored as payload text and later decoded into a typed request struct.
The effective request operation is derived from the `(type, pinType)` pair after decoding.

## High-Level Design

The parser is intentionally split into two steps:

1. `fillBuffer()` reads raw bytes, finds frame boundaries, and decides when one complete payload has arrived.
2. `constructFrame()` parses the completed payload and fills a `Frame::RequestFrame`.

That split keeps the hot serial-ingress path simple.
`fillBuffer()` only worries about framing state, while `constructFrame()` only worries about field decoding.

## Parser State

The parser keeps a small amount of persistent state between calls:

- `buffer_` stores the payload bytes between `@` and `;`
- `bufferLength_` tracks how many payload bytes are currently stored
- `inFrame_` tells whether the parser is currently collecting a frame
- `awaitingLineEnd_` tells whether `;` has been seen and the parser is waiting for the required `\n`
- `statistics_` counts bytes consumed, queued frames, malformed frames, overflow drops, and queue send failures

This makes the parser incremental.
If only part of a frame arrives in one call, the next call continues from the saved state instead of restarting.

## Step 1: `parse()`

`parse()` is intentionally small.
It just calls `fillBuffer()`.

That keeps the public API simple while leaving all parsing state inside the class.

## Step 2: `fillBuffer()`

`fillBuffer()` is the framing engine.
It reads from the input stream in bounded chunks instead of reading one byte at a time.

### Why chunked reads are used

On Teensy USB serial, repeated single-byte reads are more expensive than block reads.
This implementation uses:

- a fixed read chunk size
- a fixed per-call byte budget
- `setTimeout(0)` so `readBytes()` behaves as a non-blocking burst read

This keeps the parser fast while also preventing one call from monopolizing the CPU.

### `fillBuffer()` flow

For each call:

1. Stop immediately if there is no input stream.
2. Repeatedly check how many bytes are available.
3. Read up to the smaller of:
   - available bytes
   - the temporary read buffer size
   - the per-call parse budget
4. For each byte in that chunk, update the parser state.

### Byte handling state machine

When `inFrame_` is false:

- ignore every byte until `@` appears
- when `@` appears, clear the payload buffer and start a new frame

When `inFrame_` is true and `awaitingLineEnd_` is false:

- another `@` means the current partial frame is invalid, so drop it and restart from the new start marker
- `;` means the payload is complete, so switch to `awaitingLineEnd_`
- `\n` or `\r` before `;` is invalid and drops the frame
- any other byte is appended to the payload buffer
- if the payload buffer is full, drop the frame as an overflow

When `awaitingLineEnd_` is true:

- `\r` is ignored
- `\n` finalizes the frame and triggers `constructFrame()`
- any other byte makes the frame malformed

### What happens when a frame completes

When `\n` is received after `;`:

1. Create a temporary `Frame::RequestFrame`.
2. Call `constructFrame()` using the payload stored in `buffer_`.
3. If parsing succeeds, push the frame into the request queue with `xQueueSend(..., 0)`.
4. Update statistics for success or failure.
5. Reset parser state for the next frame.

Using a zero-timeout queue send keeps the RX path non-blocking.
If the queue is full, the parser records that as a queue send failure and moves on.

## Step 3: `constructFrame()`

`constructFrame()` converts the payload text into a typed request.
It does not read from the serial stream.
It only parses the bytes already collected by `fillBuffer()`.

### Parsing strategy

The function performs one linear scan across the payload.
Its runtime is $O(n)$ in the payload length.

It tracks:

- `fieldIndex` for which field is being parsed
- `fieldLength` for validating empty or multi-character symbolic fields
- `numericValue` for decimal fields such as request id, pin number, and value

At each comma, the current field is committed into a temporary `parsedFrame`.

### Field rules

The request frame has exactly six fields:

1. frame type
2. request id
3. pin type
4. priority
5. pin number
6. value

The symbolic fields must be exactly one character long:

- type
- pin type
- priority

The numeric fields must contain only decimal digits:

- request id fits in `uint16_t`
- pin number fits in `uint8_t`
- value fits in `uint16_t`

If a field is empty, too long, contains invalid characters, overflows its destination type, or appears in the wrong position, parsing fails.

### Enum decoding

Single-character symbolic fields are translated with the codec helpers:

- `FrameTypeCodec::frameTypeFromChar(...)`
- `PinTypeCodec::pinTypeFromChar(...)`
- `PriorityCodec::PriorityFromChar(...)`

If any codec returns its `kNone` value, the payload is treated as malformed.
After the individual fields decode successfully, the parser validates that the `(type, pinType)` pair represents a supported request shape.

### Final validation

After the scan finishes, `constructFrame()` checks that exactly six fields were parsed.
Only then does it copy the temporary `parsedFrame` into the output frame.

This temporary-then-commit pattern avoids partially written output on parse failure.

## Failure Handling

The parser is designed to fail fast and recover quickly.

It drops frames when it sees:

- unexpected bytes outside the allowed frame order
- a new `@` before the current frame finished
- line endings before `;`
- payload overflow
- empty fields
- non-digit characters in numeric fields
- numeric overflow
- unknown enum codes
- unsupported request type and pin type combinations
- the wrong number of fields
- a full request queue

The recovery rule is simple: reset local frame state and keep scanning the stream for the next valid `@`.

### Notes from current implementation

- The parser is intentionally conservative and records several counters so the runtime can distinguish malformed input from queue backpressure. These counters are exposed on the `RequestFrameParser::Statistics` object and are useful when running the host test harness.
- The current hosted test harness uses `tests/host_shims` so the parser implementation can be exercised without flashing a Teensy. See the top-level `README.md` for build instructions for the test app.
- If the system is stressed and the global request queue fills, the parser records a `queue_send_failures` counter and continues parsing; this protects the RX path at the cost of dropping otherwise-valid requests. Consider adding a bounded policy and a telemetry-backed flow-control signal if host reliability is a priority.

## Statistics and Observability

The parser keeps counters for:

- `bytes_consumed`
- `frames_queued`
- `malformed_frames`
- `overflow_drops`
- `queue_send_failures`

These counters are useful for measuring parser health under load and for distinguishing syntax problems from queue backpressure.

## Why This Algorithm Fits The Project

This parser design matches the current firmware direction well because it is:

- incremental, so it works naturally in the superloop or a periodic FreeRTOS task
- non-blocking on both serial input and queue output
- bounded, because each call has a fixed read budget
- strict, because malformed frames are rejected early
- efficient, because framing and field parsing both use simple linear logic

If higher throughput is needed later, the next step would be a fully streaming parser that writes directly into a request struct without storing the full payload first.
The current version keeps the logic easier to reason about while still being fast enough for the present architecture.