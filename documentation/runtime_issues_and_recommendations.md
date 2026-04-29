# Runtime Issues and Recommendations

This document summarizes observed runtime issues in the current codebase and recommended mitigations. It is intended as developer-facing guidance to prioritize fixes and improve stability on resource-constrained targets.

## Observed Issues

1. Large per-pin queue allocations
   - Some pin factories allocate per-pin request queues with depths of 1024 entries. At ~8 bytes per request this is hundreds or thousands of bytes per session, which quickly exhausts heap if many pins become active.

2. Task creation and priority choices
   - Factories often create worker tasks with `configMAX_PRIORITIES - 1` (very high priority). This can starve other system tasks and cause scheduling problems.

3. Direct `Serial` writes from helpers
   - `SerialSendDebugFrame()` writes directly to `Serial` rather than enqueuing a `ResponseFrame` to the global response queue. This can interleave with the `SerialTxTask` and corrupt frames under concurrent access.

4. No standardized session teardown
   - The code does not currently implement a single, reliable pattern for terminating worker tasks and reclaiming per-session queues. Abrupt deletion risks use-after-free and orphaned tasks.

5. Partial cleanup on create failures
   - `createQueues()` and similar helpers may return on error without cleaning previously-allocated resources, leaving partial state.

6. Lack of global limits on sessions
   - The dispatcher will create sessions on demand without enforcing a global limit or checking heap usage first. A hostile host can therefore exhaust device resources.

## Short-term Mitigations (high impact, low effort)

- Reduce per-pin queue defaults to 4–16 entries across factories.
- Lower worker task priority to be below `SerialRxTask` but above `SerialTxTask`; define constants like `PRIO_RX`, `PRIO_DISPATCH`, `PRIO_TX`, `PRIO_WORKER` in a central header.
- Change `SerialSendDebugFrame()` to call `sendDebugResponse()` (enqueue to `GlobalResponseQueue`) and let the single `SerialTxTask` do all writing.
- Ensure `createQueues()` and `createTasks()` free previously allocated resources on partial failure.

## Medium-term Fixes (recommended)

- Implement a sentinel-based or notification-based session shutdown: worker tasks should exit cleanly after observing a sentinel request or a task notification; the owning code should wait for confirmation (or a bounded timeout) before freeing the queue and session object.
- Add a global maximum session count and check `xPortGetFreeHeapSize()` before creating new sessions. Consider rejecting session creation early with `ErrorCode::kQueueFull` or `kUnsupported` when resources are low.
- Use `xQueueCreateStatic()` and `xTaskCreateStatic()` for deterministic memory usage in constrained builds; provide a compile-time option to choose static vs dynamic allocation.

## Long-term Improvements

- Centralize resource policy in a configuration header and document the per-session memory/stack cost for the target board.
- Add telemetry endpoints and a debug command that returns current heap usage, session count, per-task high water marks, and queue lengths.
- Consider a shared worker pool model for certain pin families (e.g., read-only analog sensors) where strict per-pin ordering is not required.

## Developer Checklist for Patches

- Verify no direct `Serial.write` calls remain outside `SerialTxTask` in production builds (allow `SerialSendDebugFrame` under `#ifdef DEBUG` for development only).
- Document per-pin memory cost after any change to queue depth or stack size.
- Add unit/host tests that create many sessions to validate heap and ensure no leaks; use the `tests/host_shims` harness to run these quickly on desktop.

*** End of file
