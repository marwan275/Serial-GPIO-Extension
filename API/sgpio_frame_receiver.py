import queue
import threading
from .teensy_serial_handler import TeensySerialHandler
from .sgpio_frame_handler import SGPIOFrameHandler
from .frame_codec import ResponseFrame


class SGPIOFrameReceiver:
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
            target=self._receiver_loop,
            daemon=True,
            name="receiver_thread",
        )

    def start(self):
        """Starts the receiver thread. This will continuously read frames from the serial connection and put them in the queue."""
        self._thread.start()

    def _receiver_loop(self):
        while not self.stop_token.is_set():
            try:
                frame_str = (
                    self.serial_handler.receive()
                )  # returns None if nothing available
                if frame_str is None:
                    continue  # no data → loop again

                frame_obj = self.frame_converter.frameFromString(frame_str)
                if isinstance(frame_obj, ResponseFrame):
                    self.frames_queue.put(frame_obj)
                else:
                    # log unexpected frame types (e.g., a RequestFrame echoed back)
                    self.logger.debug(f"Ignored unsupported frame: {type(frame_obj)}")
            except Exception as e:
                self.logger.error(f"Error in receiver loop: {e}")
                continue
