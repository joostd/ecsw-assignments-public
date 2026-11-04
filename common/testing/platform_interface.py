"""
Platform Interface for Testing

Provides a unified interface for testing on both physical board and QEMU emulation.
This allows the same test suite to run on either platform without modification.

The platform interface provides only low-level communication (connect, disconnect,
send_command). Algorithm-specific test protocols should be built on top by subclassing
CommandProtocol (see common/testing/command_protocol.py).

Usage:
    from common.testing.platform_interface import get_platform
    from common.testing.command_protocol import CommandProtocol

    # Auto-detect and create appropriate platform
    platform = get_platform()  # or get_platform('qemu') or get_platform('board')

    # Wrap in a protocol (or use algorithm-specific protocol like ChaCha20Protocol)
    protocol = CommandProtocol(platform)

    with protocol:
        response = protocol.send_command("CUSTOM_COMMAND")
        # ... run tests
"""

import logging
import os
import socket
import subprocess
import time
from contextlib import suppress
from typing import Optional
from abc import ABC, abstractmethod

logger = logging.getLogger(__name__)


class PlatformInterface(ABC):
    """Abstract base class for platform communication."""

    @abstractmethod
    def connect(self) -> bool:
        """Establish connection to the platform."""
        pass

    @abstractmethod
    def disconnect(self):
        """Close connection to the platform."""
        pass

    @abstractmethod
    def send_command(
        self, command: str, timeout: Optional[float] = None
    ) -> Optional[str]:
        """
        Send a command and wait for response.

        Args:
            command: Command string (without newline)
            timeout: Seconds to wait for the response. None uses the
                platform's configured default.

        Returns:
            Response string or None on error
        """
        pass

    def __enter__(self):
        """Context manager entry."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.disconnect()


# USB vendor IDs of serial adapters used with the course boards. The Nucleo
# reaches the host through its on-board ST-LINK; the STM32F4DISCOVERY has no
# usable virtual COM port, so its USART2 (PA2/PA3) needs an external adapter,
# which may be a Raspberry Pi Debug Probe or any generic USB-TTL bridge.
KNOWN_SERIAL_VIDS = {
    0x0483: "ST-LINK virtual COM port",
    0x2E8A: "Raspberry Pi Debug Probe",
    0x0403: "FTDI USB-serial adapter",
    0x10C4: "Silicon Labs CP210x adapter",
    0x1A86: "WCH CH340 adapter",
    0x067B: "Prolific PL2303 adapter",
}

# Seconds to wait for a reply before giving up. Ordinary commands answer in
# milliseconds, so a short default turns an unresponsive board into a quick
# failure instead of an apparent hang. Benchmarks loop thousands of times on
# the target and pass their own, much longer, timeout.
DEFAULT_COMMAND_TIMEOUT = 15.0
DEFAULT_BENCHMARK_TIMEOUT = 600.0


def find_candidate_ports():
    """
    List serial ports that look like a connected board or UART adapter.

    Returns:
        List of (device, label, description) tuples.
    """
    import serial.tools.list_ports

    candidates = []
    for port in serial.tools.list_ports.comports():
        label = KNOWN_SERIAL_VIDS.get(port.vid)
        if label is None and "STM" in (port.description or ""):
            label = "STM device"
        if label:
            candidates.append((port.device, label, port.description))
    return candidates


class BoardPlatform(PlatformInterface):
    """Physical board communication via UART."""

    def __init__(
        self,
        port: Optional[str] = None,
        baudrate: int = 115200,
        timeout: float = 5.0,
        command_timeout: Optional[float] = None,
    ):
        """
        Initialize board platform.

        Args:
            port: Serial port path (falls back to $SERIAL_PORT, then auto-detect)
            baudrate: Communication speed
            timeout: Read timeout in seconds
            command_timeout: Seconds to wait for a command response (falls back
                to $COMMAND_TIMEOUT, then DEFAULT_COMMAND_TIMEOUT)
        """
        self.port = port or os.environ.get("SERIAL_PORT") or None
        self.baudrate = baudrate
        self.timeout = timeout
        if command_timeout is None:
            command_timeout = float(
                os.environ.get("COMMAND_TIMEOUT") or DEFAULT_COMMAND_TIMEOUT
            )
        self.command_timeout = command_timeout
        self.serial = None

    def connect(self) -> bool:
        """Establish UART connection."""
        import serial
        import serial.tools.list_ports

        # Auto-detect port if not specified
        if self.port is None:
            candidates = find_candidate_ports()

            if not candidates:
                logger.error(
                    "No board or UART adapter found. "
                    "Connect one, or set SERIAL_PORT to the port to use."
                )
                return False

            if len(candidates) > 1:
                # Guessing here is how you end up talking to the wrong device:
                # on the STM32F4DISCOVERY the ST-LINK enumerates a virtual COM
                # port that is not wired to the target's USART at all.
                logger.error(
                    "Multiple candidate serial ports found; "
                    "set SERIAL_PORT to choose one:"
                )
                for device, label, description in candidates:
                    logger.error(f"    {device}  -  {label} ({description})")
                return False

            self.port, label, _ = candidates[0]
            logger.info(f"Auto-detected {label} at {self.port}")

        try:
            self.serial = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
            )
            time.sleep(0.5)  # Wait for connection to stabilize
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()
            logger.info(f"Connected to board at {self.port}")
            return True
        except serial.SerialException as e:
            logger.error(f"Failed to connect to board: {e}")
            return False

    def disconnect(self):
        """Close UART connection."""
        if self.serial and self.serial.is_open:
            self.serial.close()
            logger.info("Disconnected from board")

    def send_command(
        self, command: str, timeout: Optional[float] = None
    ) -> Optional[str]:
        """Send command via UART and receive response."""
        if not self.serial or not self.serial.is_open:
            logger.error("Board not connected")
            return None

        try:
            # Send command
            cmd_bytes = (command + "\n").encode("ascii")
            self.serial.write(cmd_bytes)
            self.serial.flush()
            logger.debug(f"Sent to board: {command}")

            # Read response
            self.serial.timeout = self.command_timeout if timeout is None else timeout
            response = self.serial.readline().decode("ascii").strip()
            logger.debug(f"Received from board: {response}")

            if not response:
                logger.error(
                    f"No response to '{command}' after {self.serial.timeout:g}s. "
                    "Check that the board is flashed and that the UART is wired "
                    "to the port in use."
                )
                return None

            return response
        except Exception as e:
            logger.error(f"Board communication error: {e}")
            return None


class QEMUPlatform(PlatformInterface):
    """QEMU emulation communication via TCP socket."""

    def __init__(
        self,
        elf_path: str,
        qemu_binary: str = "qemu-system-arm",
        machine: str = "mps2-an386",
        port: int = 5555,
    ):
        """
        Initialize QEMU platform.

        Args:
            elf_path: Path to ELF file to run
            qemu_binary: QEMU binary name
            machine: QEMU machine type
            port: TCP port for UART connection
        """
        self.elf_path = elf_path
        self.qemu_binary = qemu_binary
        self.machine = machine
        self.port = port
        self.qemu_process = None
        self.socket = None

    def connect(self) -> bool:
        """Start QEMU and establish TCP connection."""
        try:
            # Start QEMU with UART connected to TCP socket
            # -serial tcp::5555,server,nowait - Creates TCP server on port 5555
            qemu_args = [
                self.qemu_binary,
                "-M",
                self.machine,
                "-nographic",
                "-semihosting",
                "-kernel",
                self.elf_path,
                "-serial",
                f"tcp::{self.port},server,nowait",
            ]

            logger.info(f"Starting QEMU: {' '.join(qemu_args)}")
            self.qemu_process = subprocess.Popen(
                qemu_args,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            # Wait a bit for QEMU to start
            time.sleep(1.0)

            # Connect to QEMU's TCP serial port
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(5.0)

            max_retries = 10
            for attempt in range(max_retries):
                try:
                    self.socket.connect(("localhost", self.port))
                    logger.info(f"Connected to QEMU on port {self.port}")

                    # Read and discard startup banner
                    time.sleep(0.5)
                    try:
                        self.socket.settimeout(0.1)
                        while True:
                            data = self.socket.recv(1024)
                            if not data:
                                break
                            logger.debug(
                                f"Startup out: {data.decode('ascii', errors='ignore')}"
                            )
                    except socket.timeout:
                        pass

                    self.socket.settimeout(5.0)
                    return True
                except (ConnectionRefusedError, socket.timeout):
                    if attempt < max_retries - 1:
                        time.sleep(0.5)
                    else:
                        raise

        except Exception as e:
            logger.error(f"Failed to start QEMU: {e}")
            self.cleanup()
            return False

    def disconnect(self):
        """Stop QEMU and close socket."""
        self.cleanup()

    def cleanup(self):
        """Clean up resources."""
        if self.socket:
            with suppress(OSError):
                self.socket.close()
            self.socket = None

        if self.qemu_process:
            try:
                self.qemu_process.terminate()
                self.qemu_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                with suppress(OSError):
                    self.qemu_process.kill()
            except OSError as error:
                logger.warning("Failed to terminate QEMU cleanly: %s", error)
            self.qemu_process = None

        logger.info("QEMU disconnected")

    def send_command(
        self, command: str, timeout: Optional[float] = None
    ) -> Optional[str]:
        """Send command via TCP socket and receive response."""
        if not self.socket:
            logger.error("QEMU not connected")
            return None

        try:
            if timeout is not None:
                self.socket.settimeout(timeout)

            # Send command
            cmd_bytes = (command + "\n").encode("ascii")
            self.socket.sendall(cmd_bytes)
            logger.debug(f"Sent to QEMU: {command}")

            # Read response line by line until we get OK or ERR
            response_lines = []
            while True:
                # Read one byte at a time to find newline
                line_bytes = b""
                while True:
                    byte = self.socket.recv(1)
                    if not byte:
                        raise ConnectionError("QEMU connection closed")
                    if byte == b"\n":
                        break
                    line_bytes += byte

                line = line_bytes.decode("ascii").strip()
                if not line:
                    continue

                logger.debug(f"Received from QEMU: {line}")

                # Check if this is a response line
                if line.startswith("OK ") or line.startswith("ERR "):
                    return line

                response_lines.append(line)

        except socket.timeout:
            logger.error("QEMU communication timeout")
            return None
        except Exception as e:
            logger.error(f"QEMU communication error: {e}")
            return None


def get_platform(platform_type: Optional[str] = None, **kwargs) -> PlatformInterface:
    """
    Get a platform interface instance.

    Args:
        platform_type: 'board', 'qemu', or None (auto-detect)
        **kwargs: Platform-specific arguments

    Returns:
        PlatformInterface instance

    Examples:
        # Auto-detect (prefers board if available)
        platform = get_platform()

        # Explicit board
        platform = get_platform('board', port='/dev/ttyACM0')

        # QEMU
        platform = get_platform('qemu', elf_path='build/chacha20.elf')
    """
    if platform_type is None:
        # Auto-detect: check if board is available
        import serial.tools.list_ports

        if os.environ.get("SERIAL_PORT"):
            logger.info("Using board at $SERIAL_PORT")
            return BoardPlatform(**kwargs)

        for port in serial.tools.list_ports.comports():
            if "STM" in port.description:
                logger.info("Auto-detected physical board")
                return BoardPlatform(**kwargs)

        # No board found, check for QEMU requirements
        if "elf_path" in kwargs:
            logger.info("No board detected, using QEMU")
            return QEMUPlatform(**kwargs)
        else:
            logger.error("No board found and no elf_path specified for QEMU")
            raise ValueError(
                "Cannot auto-detect platform. Specify 'board' or 'qemu' explicitly."
            )

    elif platform_type.lower() == "board":
        return BoardPlatform(**kwargs)

    elif platform_type.lower() == "qemu":
        if "elf_path" not in kwargs:
            raise ValueError("elf_path required for QEMU platform")
        return QEMUPlatform(**kwargs)

    else:
        raise ValueError(f"Unknown platform type: {platform_type}")
