import serial
from serial.tools import list_ports
from serial.tools.list_ports_common import ListPortInfo

class SerialPortError(Exception):
    pass


class TeensySerialHandler:
    TEENSY_VID = 0x16C0

    def __init__(self,logger):
        self.logger = logger
        self.serial_port: serial.Serial | None = None
        self._rx_buffer = bytearray()

    @staticmethod
    def looks_like_teensy(port_info: ListPortInfo) -> bool:
        text_fields = [
            port_info.description or "",
            port_info.manufacturer or "",
            port_info.product or "",
            port_info.hwid or "",
        ]

        joined_text = " ".join(text_fields).lower()

        return (
            "teensy" in joined_text or port_info.vid == TeensySerialHandler.TEENSY_VID
        )

    def resolve_port(self, requested_port: str | None = None) -> str:
        if requested_port:
            return requested_port

        available_ports = list(list_ports.comports())

        if not available_ports:
            raise SerialPortError(
                "No serial ports found. Connect the board or specify a port."
            )

        preferred_ports = [
            port_info
            for port_info in available_ports
            if self.looks_like_teensy(port_info)
        ]

        if len(preferred_ports) == 1:
            selected_port = preferred_ports[0]

            self.logger.info(
                f"Auto-detected Teensy port: "
                f"{selected_port.device} "
                f"({selected_port.description})"
            )

            return selected_port.device

        if len(available_ports) == 1:
            selected_port = available_ports[0]

            self.logger.info(
                f"Auto-detected serial port: "
                f"{selected_port.device} "
                f"({selected_port.description})"
            )

            return selected_port.device

        port_lines = [
            f"  - {port_info.device}: {port_info.description}"
            for port_info in available_ports
        ]

        raise SerialPortError(
            "Multiple serial ports found.\n"
            "Specify the port explicitly.\n\n"
            "Available ports:\n" + "\n".join(port_lines)
        )

    def connect(
        self,
        baudrate: int = 115200,
        requested_port: str | None = None,
        timeout: float = 1.0,
    ) -> serial.Serial:

        port = self.resolve_port(requested_port)

        try:
            self.serial_port = serial.Serial(
                port=port,
                baudrate=baudrate,
                timeout=timeout,
            )

            self.logger.info(f"Connected to {port}")

            return self.serial_port

        except serial.SerialException as exc:
            raise SerialPortError(f"Failed to open serial port {port}: {exc}") from exc

    def disconnect(self):
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            self.reset_rx()
            self.logger.info("Serial port disconnected")

    def reset_rx(self):
        self._rx_buffer.clear()

    def send(self, frame: str) -> None:
        """
        Send ASCII frame matching MCU expected format:
        @W,2,D,H,13,11;\n
        """

        if not self.serial_port or not self.serial_port.is_open:
            raise SerialPortError("Serial port is not connected")

        try:
            # Ensure frame has correct structure
            frame = frame.strip()

            if not frame.startswith("@"):
                raise SerialPortError(f"Frame must start with '@': {frame}")

            if not frame.endswith(";"):
                raise SerialPortError(f"Frame must end with ';': {frame}")

            # Add newline ONLY for UART stream separation (MCU uses it for framing end)
            wire_frame = frame + "\n"

            data = wire_frame.encode("utf-8")

            written = self.serial_port.write(data)

            if written != len(data):
                raise SerialPortError(f"Incomplete TX write ({written}/{len(data)})")

        except serial.SerialException as exc:
            raise SerialPortError(f"Failed to send frame: {exc}") from exc

    def receive(self) -> str | None:
        """
        Stream-based frame ingester (RX thread safe).
        """

        if not self.serial_port or not self.serial_port.is_open:
            raise SerialPortError("Serial port is not connected")

        try:
            chunk = self.serial_port.read(self.serial_port.in_waiting or 1)

            if chunk:
                self._rx_buffer.extend(chunk)

            if len(self._rx_buffer) > 4096:
                self._rx_buffer.clear()
                return None

            first_start = self._rx_buffer.find(b"@")
            if first_start == -1:
                # no frame start → drop garbage occasionally
                if len(self._rx_buffer) > 1024:
                    self._rx_buffer.clear()
                return None

            if first_start > 0:
                # keep only latest plausible start
                del self._rx_buffer[:first_start]

            return self._extract_frame()

        except serial.SerialException as exc:
            raise SerialPortError(f"Serial RX failed: {exc}") from exc

    def _extract_frame(self) -> str | None:
        """
        Extract one complete frame if available.
        """

        start = self._rx_buffer.find(b"@")

        if start == -1:
            if len(self._rx_buffer) > 4096:
                self._rx_buffer.clear()
            return None

        end = self._rx_buffer.find(b";", start + 1)

        if end == -1:
            return None  # incomplete frame

        frame_bytes = self._rx_buffer[start : end + 1]

        del self._rx_buffer[: end + 1]

        try:
            frame = frame_bytes.decode("utf-8")
        except UnicodeDecodeError:
            return None

        if not frame.startswith("@") or not frame.endswith(";"):
            return None

        return frame
