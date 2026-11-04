"""
Generic Command Protocol Base Class

Provides a reusable foundation for algorithm-specific test protocols.
Eliminates code duplication by handling the common pattern of:
  1. Format command with hex-encoded inputs
  2. Send command via platform
  3. Parse response (OK/ERR)
  4. Convert hex response to bytes
  5. Validate response length

Subclasses (e.g., ChaCha20Protocol) inherit this and define specific test
methods that call _send_test_command() with appropriate command strings.
"""

import logging
from typing import Optional
from common.testing.platform_interface import (
    DEFAULT_BENCHMARK_TIMEOUT,
    PlatformInterface,
)

logger = logging.getLogger(__name__)


class CommandProtocol:
    """
    Base class for algorithm-specific test protocols.

    Wraps a PlatformInterface and provides helper methods for sending
    test commands with automatic encoding/decoding and error handling.
    """

    def __init__(self, platform: PlatformInterface):
        """
        Initialize protocol with a platform instance.

        Args:
            platform: PlatformInterface instance (BoardPlatform or QEMUPlatform)
        """
        self.platform = platform

    def connect(self) -> bool:
        """Establish connection to the platform."""
        return self.platform.connect()

    def disconnect(self):
        """Close connection to the platform."""
        self.platform.disconnect()

    def send_command(self, command: str) -> Optional[str]:
        """
        Send raw command and get raw response.

        This is a low-level method exposed for advanced use cases.
        Most test methods should use _send_test_command() instead.

        Args:
            command: Command string (without newline)

        Returns:
            Response string or None on error
        """
        return self.platform.send_command(command)

    def _send_test_command(
        self, command: str, expected_length: Optional[int] = None
    ) -> Optional[bytes]:
        """
        Send a test command and parse the response.

        This method handles the common pattern used by all test commands:
        1. Send command string
        2. Parse "OK <hex>" or "ERR <message>" response
        3. Convert hex to bytes
        4. Validate response length if expected_length is provided

        Args:
            command: Complete command string (e.g., "TEST_KEYSETUP <hex>")
            expected_length: Expected response length in bytes (optional validation)

        Returns:
            Response bytes on success, None on error
        """
        response = self.platform.send_command(command)

        if not response:
            logger.error(f"No response for command: {command}")
            return None

        if response.startswith("OK "):
            result_hex = response[3:]
            try:
                result_bytes = bytes.fromhex(result_hex)

                # Validate length if specified
                if expected_length is not None and len(result_bytes) != expected_length:
                    logger.error(
                        f"Invalid response length: got {len(result_bytes)}, "
                        f"expected {expected_length}"
                    )
                    return None

                return result_bytes
            except ValueError as e:
                logger.error(f"Invalid hex in response: {e}")
                return None

        elif response.startswith("ERR "):
            logger.error(f"Platform error: {response[4:]}")
            return None
        else:
            logger.error(f"Invalid response format: {response}")
            return None

    def _send_benchmark_command(self, command: str) -> Optional[int]:
        """
        Send a benchmark command and parse cycle count.

        Benchmark commands return "OK cycles=<number>" format.

        Args:
            command: Complete benchmark command string

        Returns:
            Cycle count as integer, or None on error
        """
        # Benchmarks loop many times on the target, so they get a far longer
        # budget than the default used for ordinary commands.
        response = self.platform.send_command(
            command, timeout=DEFAULT_BENCHMARK_TIMEOUT
        )

        if not response:
            logger.error(f"No response for benchmark: {command}")
            return None

        if response.startswith("OK "):
            try:
                # Expected format: "OK cycles=12345"
                parts = response[3:].split()
                cycles = int(parts[0].split("=")[1], 0)
                return cycles
            except (ValueError, IndexError) as e:
                logger.error(f"Invalid benchmark response format: {e}")
                logger.error(f"Response was: {response}")
                return None

        elif response.startswith("ERR "):
            logger.error(f"Benchmark error: {response[4:]}")
            return None
        else:
            logger.error(f"Invalid response format: {response}")
            return None

    def reset(self) -> bool:
        """
        Reset platform state.

        Returns:
            True if successful, False otherwise
        """
        response = self.send_command("RESET")
        return response == "OK"

    def __enter__(self):
        """Context manager entry - establishes connection."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit - closes connection."""
        self.disconnect()

    # For compatibility with code that checks serial attribute
    @property
    def serial(self):
        """Access underlying serial port (if using BoardPlatform)."""
        if hasattr(self.platform, "serial"):
            return self.platform.serial
        return None
