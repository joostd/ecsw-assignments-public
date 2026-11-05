/*
 * STM32L432KC linker script (Nucleo-L432KC)
 *
 * Memory layout:
 * - Flash: 256KB at 0x08000000
 * - RAM: 64KB at 0x20000000, as one contiguous block. SRAM1 is 48KB at
 *   0x20000000 and the 16KB SRAM2 is aliased directly above it at 0x2000C000
 *   (SRAM2 is also visible at 0x10000000). Note this differs from the
 *   STM32L476, where SRAM2 is not aliased above SRAM1.
 */

MEMORY
{
    rom (rx) : ORIGIN = 0x08000000, LENGTH = 256K
    ram (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

/* Include the common STM32 linker script */
INCLUDE cortex-m-generic.ld

/* Additional symbols required by some code */
PROVIDE(_estack = ORIGIN(ram) + LENGTH(ram));
PROVIDE(_end = end);
