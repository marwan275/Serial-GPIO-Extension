import queue
import threading
from .teensy_serial_handler import SerialPortError, TeensySerialHandler
from .sgpio_frame_handler import SGPIOFrameHandler
from .frame_codec import RequestFrame


class SGPIOFrameSender:
    def __init__(
        self,
        serial_handler: TeensySerialHandler,
        frames_queue: queue.Queue,
        stop_token: threading.Event,
        logger,
    ):
        self.logger = logger
        self.serial_handler = serial_handler
        self.frames_queue = frames_queue
        self.stop_token = stop_token
        self.frame_converter = SGPIOFrameHandler(logger=self.logger)

        self._thread = threading.Thread(
            target=self._sender_loop,
            daemon=True,
            name="sender_thread",
        )

    def start(self):
        """Starts the sender thread. This will continuously read frames from the queue and send them over serial."""
        self._thread.start()

    def join(self, timeout=None):
        self._thread.join(timeout)

    def _sender_loop(self):
        while not self.stop_token.is_set():
            try:
                frame = self.frames_queue.get(timeout=0.1)
            except queue.Empty:
                continue
            try:
                if isinstance(frame, RequestFrame):
                    frame_str = self.frame_converter.stringFromFrame(frame)
                    self.serial_handler.send(frame_str)
                else:
                    self.logger.warning(
                        f"Warning: Unsupported frame type in sender loop: {type(frame)}"
                    )
            except Exception as e:
                if self.stop_token.is_set():
                    break
                if isinstance(e, SerialPortError):
                    self.logger.error(f"Sender loop stopping after serial error: {e}")
                    self.stop_token.set()
                    break
                self.logger.error(f"Error in sender loop: {e}")
                continue
