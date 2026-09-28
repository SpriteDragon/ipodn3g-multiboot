/***************************************************************************
 * custom_payload entry stub -- iPod Nano 3G (S5L8702)
 *
 * Contract with the bootmenu bootloader (ipod-s5l87xx-menu.c):
 *   - loaded at 0x08000000 (DRAM), first 8 MB max
 *   - first instruction must be an ARM branch (0xEAxxxxxx) so the
 *     bootmenu recognises this as a raw ARM image
 *   - entered in ARM state, SVC mode, IRQs disabled, caches flushed
 *   - bl-called as `int entry(void)`; returning an int is ALLOWED
 *     (the boot menu reappears -- perfect for quick experiments)
 *   - if it never returns, it simply IS your firmware
 *
 * Memory layout to respect while the menu is still resident:
 *   0x08000000 .. 0x08800000   image area (8 MB) -- free for you
 *   0x08800000 .. TTB          boot menu .bss    -- DO NOT WRITE
 *                              (not even the stack/this stub's .bss)
 *   top 16 KB of DRAM          translation table
 ***************************************************************************/
    .syntax unified
    .text
    .global _start

_start:
    b       reset           /* 0xEA... marker at offset 0 */
    ldr     pc, =hang       /* fill the rest of the vector slots */
    ldr     pc, =hang
    ldr     pc, =hang
    ldr     pc, =hang
    ldr     pc, =hang
    ldr     pc, =hang
    ldr     pc, =hang

reset:
    msr     cpsr_c, #0xd3   /* SVC, IRQ+FIQ off (already, but be sure) */
    ldr     sp, =0x08400000 /* stack INSIDE the free first 8 MB */

    /* zero our .bss */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    bl      main            /* int main(void) */

    /* return the int to the boot menu */
    bx      lr

hang:
    b       hang
