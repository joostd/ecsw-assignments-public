/*
 * STM32L476RG linker script (Nucleo-L476RG)
 *
 * Memory layout:
 * - Flash: 1MB at 0x08000000
 * - RAM: 96KB at 0x20000000 (SRAM1 only)
 *
 * The 32KB SRAM2 at 0x10000000 is not used. Unlike the L4+ parts (such as the
 * L4R5ZI, whose 640KB is one contiguous block), the STM32L476 does not alias
 * SRAM2 directly above SRAM1, so RAM cannot simply be declared as 128KB:
 * 0x20018000 onwards is unmapped and faults on access.
 */

MEMORY
{
    rom (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
    ram (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}

/* Include the common STM32 linker script */
INCLUDE cortex-m-generic.ld

/* Additional symbols required by some code */
PROVIDE(_estack = ORIGIN(ram) + LENGTH(ram));
PROVIDE(_end = end);
