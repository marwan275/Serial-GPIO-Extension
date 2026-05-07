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

- Successful digital, analog-output, and servo writes do not produce a firmware response frame (fire and forget). Only read operations and explicit errors currently emit serial responses.
- The ROS 2 server therefore treats writes as successful as soon as the host enqueues the request; there is no end-to-end acknowledgement from the Teensy for a successful write.
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

## Action contract

`API/action/GpioFrame.action` currently exposes:

- Goal fields: `client_id`, `request_id`, `type`, `pin_type`, `priority`, `pin_number`, `value`
- Result fields: `success`, `result_value`, `error_code`, `message`

The server currently allocates its own serial request ID for reads and waits for a matching serial response before completing the action.

