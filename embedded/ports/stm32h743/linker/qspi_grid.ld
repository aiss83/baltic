/*
 * STM32H743: place baltic_embedded grid data in memory-mapped QSPI Flash.
 *
 * Add a QSPI memory region to MEMORY, for example:
 *
 *   QSPI (rx) : ORIGIN = 0x90000000, LENGTH = 32M
 *
 * The QSPI peripheral must be configured in memory-mapped mode and the
 * external loader must program the .baltic_grid section.  Enable the
 * D-cache/MPU for the QSPI region (read-only, cacheable) for best
 * performance.
 */

.baltic_grid : ALIGN(32)
{
    __baltic_grid_start = .;
    KEEP(*(.baltic_grid))
    KEEP(*(.baltic_grid.*))
    __baltic_grid_end = .;
} > QSPI
