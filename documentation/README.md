# serial_gpio_extension

serial_gpio_extension is a Teensy 4.1 + FreeRTOS runtime that exposes GPIO operations over framed USB serial, plus a ROS 2 host-side Python layer that wraps the serial protocol in an action-based API.

This README describes the implementation that exists in the repository today, not an aspirational design.

## Repository layout

- Firmware sources live at the repository root in the `.ino`, `.cpp`, and `.h` files.
- Host-side ROS 2 Python sources live under `API/`.
- The action definition lives at `API/action/GpioFrame.action`.

## Firmware runtime

The firmware boot path is:

1. `serial_gpio_extension.ino` starts USB serial, prints any Teensy crash report, creates the global queues, creates the long-lived tasks, and starts the FreeRTOS scheduler.
2. `SerialRxTask` reads bytes from `Serial` and feeds them into `RequestFrameParser`.
3. `RequestFrameParser` recognizes framed ASCII requests and pushes validated `Frame::RequestFrame` objects into the global request queue.
4. `CommandDispatcherTask` validates the request semantics, lazily creates a `PinSession` for the target pin when needed, and routes work into the per-pin session queue.
5. Each concrete `PinSession` owns the hardware interaction for one pin.
6. `SerialTxTask` serializes outbound `Frame::ResponseFrame` values back onto USB serial.

## Serial protocol

Inbound request format:

```text
@<type>,<requestId>,<pinType>,<priority>,<pin>,<value>;
```

Examples:

```text
@W,2,D,H,13,1;
@R,7,A,L,34,0;
```

On the wire the host sender appends a trailing newline, so the Teensy actually receives `@...;\n`.

Outbound response format:

```text
@<type>,<requestId>,<value>,<error>;
```

Examples:

```text
@S,7,812,255;
@E,9,0,1;
```

Protocol enums currently map as follows:

- Frame types: `R` read, `W` write, `S` response, `E` error, `B` debug
- Pin types: `D` digital, `A` analog, `S` servo, `G` MCP4725 DAC
- Priorities: `L` low, `H` high

## Current supported behavior

- Digital read
- Digital write
- Analog read
- Analog write through `AnalogPinSession`
- Servo write
- MCP4725 DAC write through `MCP4725PinSession`
- High-priority request enqueue that clears any pending requests already queued for the same pin session

## Pin session model

- Sessions are created lazily from the first valid request for a pin.
- Most sessions are indexed by physical pin number and currently allow pins `1..41`.
- I2C-backed sessions are indexed separately by I2C address and use the request `pin_number` field as that address.
- There is exactly one live `PinSession` per physical pin or I2C address.
- A later request is accepted only if it matches the existing session's derived configuration.
- If a request does not match the existing session configuration, the dispatcher returns `ErrorCode::kUnsupported`.

Current session mapping:

- Digital read creates an input-mode digital session.
- Digital write creates an output-mode digital session.
- Analog read creates an input-mode analog session.
- Analog write creates an output-mode analog session.
- Servo supports write only.
- MCP4725 is the first I2C-backed session type. It supports write only and initializes the addressed DAC with a hardcoded 5000 mV reference.

## Important current limitations

- Successful reads and writes both produce a firmware response frame. Reads return the sampled value, while writes return the value that was actually applied by the session.
- High-priority requests can supersede older queued requests for the same pin session. Discarded queued requests now emit `ErrorCode::kCancelled` so the host can distinguish supersession from a transport timeout.
- Analog sessions currently differentiate only between input mode and output mode; they do not carry per-session frequency or resolution settings.
- `FrameType::kDebug` exists for outbound debug traffic, but inbound debug requests are not part of the dispatcher request semantics.

## ROS 2 host-side package layout

The Python files under `API/` are source files intended to be copied into a ROS 2 Python package directory named `serial_gpio`.

That is why the package's internal imports use relative paths such as:

```python
from .sgpio_lib import SGPIO
from .teensy_serial_handler import TeensySerialHandler
```

The ROS action interface import remains absolute because it comes from a different package:

```python
from serial_gpio_interfaces.action import GpioFrame
```

The action file belongs in the interfaces package, typically:

```text
serial_gpio_interfaces/action/GpioFrame.action
```

Current host-side roles:

- `sgpio_server.py` is the main ROS 2 action server that bridges ROS goals to serial frames.
- `sgpio_lib.py` is the client helper used by ROS nodes.
- `sgpio_frame_sender.py` and `sgpio_frame_receiver.py` provide the serial transport worker threads.
- `sgpio_frame_handler.py` converts between Python frame objects and the ASCII wire format.
- `teensy_serial_handler.py` owns serial-port discovery, send, and receive buffering.
- `node_example.py`, `ping_node.py`, and `brust_test.py` are example or diagnostic ROS nodes.

## SGPIO server runtime

`API/sgpio_server.py` is now a response-driven action server rather than a fire-and-forget write bridge.

Startup flow:

1. `GpioServer` creates the ROS 2 `ActionServer` for `sgpio/frame`.
2. It opens the Teensy serial connection through `TeensySerialHandler`.
3. It starts one sender worker (`SGPIOFrameSender`) and one receiver worker (`SGPIOFrameReceiver`).
4. It also starts a local `_receiver_loop` thread that consumes decoded `ResponseFrame` objects and matches them back to waiting ROS goals.

Per-request flow:

1. `execute_callback` converts the incoming ROS action goal into a serial `RequestFrame`.
2. The server allocates its own serial request ID with `_allocate_serial_id()` for every request, including writes.
3. It creates a `threading.Event` and stores `(goal_handle, completion_event)` in `serial_id_map`, keyed by that serial request ID.
4. The request frame is queued to `SGPIOFrameSender`, which serializes it onto USB serial.
5. `execute_callback` then blocks on the event for up to 2 seconds instead of immediately succeeding writes.
6. When the Teensy sends back a `ResponseFrame`, `_receiver_loop` looks up the matching pending request by `response.request_id`, attaches the response to the goal handle, and sets the event.
7. `execute_callback` resumes, builds the `GpioFrame.Result`, and completes the action based on the firmware response.

Current completion semantics:

- `ErrorCode.NONE` means the request reached the device and executed successfully. For reads, `result_value` is the sampled value. For writes, `result_value` is the value actually applied by the firmware.
- `ErrorCode.CANCELLED` means the request was accepted earlier but later discarded on the device when a higher-priority request cleared that pin session's queue.
- Any other error code is returned as an action failure.
- A missing response within 2 seconds is treated as a timeout and the action is aborted.

This means the host no longer treats writes as successful when they are merely enqueued. A write action now completes only after the Teensy reports either success or an explicit error.

Shutdown flow:

- `destroy_node()` sets the stop token, destroys the action server, joins the sender thread, joins the frame receiver thread, joins the local response-matching thread, and then disconnects the serial port.
- This keeps teardown ordered so background workers stop before the serial transport is closed.

## Action contract

`API/action/GpioFrame.action` currently exposes:

- Goal fields: `client_id`, `request_id`, `type`, `pin_type`, `priority`, `pin_number`, `value`
- Result fields: `success`, `result_value`, `error_code`, `message`

The server currently allocates its own serial request ID for every request and waits for a matching serial response before completing the action. Responses are correlated only by the server-managed serial request ID, not by the original ROS action `request_id` field.

