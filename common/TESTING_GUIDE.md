# Testing Guide (Board & QEMU)

Use this guide whenever you need to run the automated tests for an assignment.
The same Python harness talks to the firmware whether it is running on the STM32
board or inside QEMU.

## Prerequisites

- Build the assignment firmware (`make` inside the assignment directory).  
- Open a Nix development shell so Jasmin, the ARM toolchain, QEMU, and Python
  dependencies are available.  
- For hardware runs, connect the Nucleo-L4R5ZI (or the STM32F4DISCOVERY, see
  below) and identify the serial device name.

## Running the Tests

We will give details about running the tests for chacha20 here but the same
principles apply to any other assignment.

### QEMU (fast feedback)

```bash
cd assignment1-chacha20
make test-qemu
```

This target rebuilds the firmware for `PLATFORM=qemu`, launches
`qemu-system-arm` (MPS2-AN386) with the UART exposed on TCP port 5555, and
executes `tests/test_chacha20.py --platform qemu --elf build/chacha20.elf`.

### Physical board

```bash
cd assignment1-chacha20
make clean flash          # build and program the STM32 firmware
make test-board           # runs tests/test_chacha20.py --platform board
```

If automatic port detection fails, supply the serial path explicitly (e.g.,
`--port /dev/ttyACM0` on Linux or `/dev/cu.usbmodem*` on macOS).

### Nucleo-L476RG (STM32L476RG)

Unlike the STM32F4DISCOVERY, this board needs no external adapter: its ST-LINK
virtual COM port is wired to USART2 on PA2/PA3, so flashing and the serial
console share one USB cable.

```bash
cd assignment1-chacha20
make PLATFORM=stm32l476rg test-board
```

The benchmark clock is 16 MHz, the fastest this part runs with zero flash wait
states in VCORE range 1 (the L4R5ZI manages 20 MHz, the F407 24 MHz), so cycle
counts are not skewed by flash stalls. `CLOCK_FAST` is 80 MHz, the STM32L476's
maximum — the Nucleo-L4R5ZI's 120 MHz is an L4+ figure and does not apply here.

A factory board may ship read-out protected (OpenOCD reports `RDP level 1` and
flashing fails with `stm32x device protected`). Clear it once with
`openocd -f board/st_nucleo_l4.cfg -c init -c halt -c "stm32l4x unlock 0" -c shutdown`,
then **power-cycle the board** so the new option bytes take effect. This
mass-erases the flash.

### STM32F4DISCOVERY (STM32F407)

The STM32F4DISCOVERY has no USB serial port of its own, so the UART goes
through an external 3.3V USB-UART adapter (TX and RX cross over, adapter VCC
stays unconnected; the board is powered through its ST-LINK USB port):

| Board | Adapter |
| --- | --- |
| PA2 (USART2 TX) | RX |
| PA3 (USART2 RX) | TX |
| GND | GND |

Select the board with `PLATFORM=stm32f4discovery` and point the tests at the
adapter with `SERIAL_PORT` (it cannot be auto-detected):

```bash
cd assignment1-chacha20
make PLATFORM=stm32f4discovery SERIAL_PORT=/dev/ttyUSB0 test-board
```

The benchmark clock is 24 MHz so that flash runs without wait states (like the
20 MHz benchmark clock on the Nucleo). If more than one ST-LINK is connected, add
`OPENOCD_SERIAL=<serial>` (shown by `st-info --probe`) to flash the right one.
Switching `PLATFORM` rebuilds the firmware automatically.

## Useful Script Flags

`tests/test_chacha20.py` accepts:

- `--platform board|qemu` - choose the transport.  
- `--elf <file>` - required for QEMU; ignored on hardware.  
- `--port <serial>` - override board auto-detection.  

Any failing test stops the run immediately and prints the mismatching inputs and outputs.

## Troubleshooting

| Symptom | Likely Cause | Suggested Action |
| --- | --- | --- |
| `Failed to connect to QEMU` | Port 5555 busy or QEMU crashed | `killall qemu-system-arm`, rerun `make test-qemu` |
| `QEMU communication timeout` | Firmware hung or asserted | Rebuild with `make PLATFORM=qemu clean all`, rerun to capture console output |
| `No board found` | Serial device not detected | List ports (`ls /dev/ttyACM*` / `ls /dev/cu.*`) and pass `--port` |
| `Permission denied: '/dev/ttyACM0'` | Missing serial permissions | Add your user to `dialout` (Linux) or use `sudo` temporarily |
| `ERR ...` responses from firmware | Malformed command or stale build | Confirm buffer lengths, clean and rebuild (`make clean` + `make`) |
| Any symptom | - | Re-connect USB cable  |

## Tips

- Iterate in QEMU first: it is deterministic and avoids flashing delays.  
- The same pattern applies to other assignments; only the directory and test
  script name change (for example, `assignment0-sum/tests/test_sum.py` or
  `assignment2-ed25519/tests/test_ed25519.py`).
