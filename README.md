# Cryptographic Engineering Assignments

This repository provides the course infrastructure for implementing
cryptographic primitives on the Nucleo‑L4R5ZI (ARM Cortex‑M4). You write
the core algorithms in [Jasmin](https://github.com/jasmin-lang/jasmin) and
verify them through shared C firmware, Python reference code, and automated
tests.

The Nucleo‑L476RG (`PLATFORM=stm32l476rg`), Nucleo‑L432KC
(`PLATFORM=stm32l432kc`) and STM32F4DISCOVERY (`PLATFORM=stm32f4discovery`)
are supported as alternative boards, and `PLATFORM=qemu` needs no hardware at
all. See
[common/TESTING_GUIDE.md](common/TESTING_GUIDE.md).

## Getting Started
### Method 1 (Recommended)
Use our provided VM. You can obtain it from: https://nce.mpi-sp.org/index.php/s/y4ddz4cwYgxjKD7
Instructions on the setup can be found next to the VM image files.

### Method 2
1. Install the **Nix** package manager.
2. Clone this repository and open a shell inside the root directory.
3. Enter the development environment and build:

   ```bash
   # brings Jasmin 2025.06.3, ARM GCC, OpenOCD, Python tools
   nix --extra-experimental-features nix-command --extra-experimental-features flakes develop
   git submodule init && git submodule update # get libopencm3
   make -C common/libopencm3 TARGETS="stm32/l4 stm32/f4" # build libopencm3
   cd assignment0-sum # enter any assignment directory
   make # builds firmware for the current assignment
   ```

4. Run the test suite following the instructions in
   [common/TESTING_GUIDE.md](common/TESTING_GUIDE.md).
5. Edit the Jasmin sources under each assignment's `src/` directory and iterate.

## Code Quality Checks

Set up the commit-time linters once per clone:

```bash
nix develop
pre-commit install
```

The hook formats Python files with Black, enforces Flake8 style rules (aligned
with Black), and runs clang-format on C sources while skipping third-party
libraries. Use `pre-commit run --all-files` to lint the whole tree manually.

## Repository Tour

### Playgrounds (Testing & Debugging)
- `playground0-print/` - minimal UART output demo for printf-style debugging
- `playground1-bench/` - simple C benchmarking demo using the cycle counter
- `playground2-jasmin/` - Jasmin integration demo for testing Jasmin snippets

Each playground supports `make test-qemu` for instant testing without hardware.
Use these to quickly test code snippets, debug issues, or experiment with ideas.

### Assignments
- `assignment0-sum/` - first assignment (sum); contains Jasmin sources,
  firmware, tests, and an assignment-specific README.
- `assignment1-chacha20/` - second assignment (ChaCha20); contains Jasmin
  sources, reference Python models, firmware, tests, and an assignment-specific
  README.
- `assignment2-curve25519/` - third assignment (Curve25519); mirrors the layout
  of assignment 1.

### Infrastructure
- `common/` - shared infrastructure: hardware abstraction layer, testing
  harness, linker scripts, and Makefile rules.
- `flake.nix` - Nix environment definition that pins toolchain versions.
- `common/TESTING_GUIDE.md` - explains how to run the automated tests on
  hardware or under QEMU.

## Working on an Assignment

1. Read the assignment README inside the corresponding directory for learning
   goals and implementation guidance.
2. Implement the required Jasmin routines (and any supporting C glue if
   specified).
3. Build the firmware with `make` while inside the assignment directory.
4. Run the provided Python tests (board or QEMU) as described in the testing
   guide.
5. Record results and prepare any deliverables requested by the instructor.

## Documentation Map

- **Assignment guides** - `assignment*/README.md`
  Focus on specification details, required entry points, and grading
  expectations.
- **Playgrounds** - `playground*/README.md`
  Quick-start demos for testing and debugging that can help you while solving the assignments.
- **Testing guide** - `common/TESTING_GUIDE.md`
  Explains the shared UART/QEMU test harness and troubleshooting steps.
- **Source references** - each assignment's `reference/` directory
  Provides executable Python models and vectors for validation.
