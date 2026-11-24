/*
 * STM32H743: place baltic_embedded grid data in internal Flash.
 *
 * Include this fragment in the application linker script.  The generated
 * grid arrays carry __attribute__((section(".baltic_grid"))), so they are
 * independent of the location of .rodata.
 *
 * Tested layout assumption: FLASH starts at 0x08000000 and is 2 MiB.
 */

.baltic_grid : ALIGN(32)
{
    __baltic_grid_start = .;
    KEEP(*(.baltic_grid))
    KEEP(*(.baltic_grid.*))
    __baltic_grid_end = .;
} > FLASH

ASSERT(__baltic_grid_end - __baltic_grid_start < 2M,
       "baltic grid exceeds internal Flash")
