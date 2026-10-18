@ Dead Wars startup code
    .section .crt0, "ax"
    .arm
    .global _start
_start:
    b       rom_start
    @ cartridge header (logo, title, checksums are filled in by tools/gbafix.py)
    .fill   156, 1, 0
    .ascii  "DEAD WARS   "          @ title (12)
    .ascii  "ZDWE"                  @ game code
    .ascii  "01"                    @ maker
    .byte   0x96, 0, 0              @ fixed, unit code, device type
    .fill   7, 1, 0
    .byte   0                       @ version
    .byte   0                       @ complement check (gbafix)
    .byte   0, 0

rom_start:
    mov     r0, #0x12               @ IRQ mode
    msr     cpsr_c, r0
    ldr     sp, =__sp_irq
    mov     r0, #0x1f               @ System mode
    msr     cpsr_c, r0
    ldr     sp, =__sp_usr

    @ copy .data (and .iwram code) from ROM to IWRAM
    ldr     r0, =__data_lma
    ldr     r1, =__data_start
    ldr     r2, =__data_end
1:  cmp     r1, r2
    ldrlo   r3, [r0], #4
    strlo   r3, [r1], #4
    blo     1b
    @ copy .ewram_data
    ldr     r0, =__ewram_lma
    ldr     r1, =__ewram_start
    ldr     r2, =__ewram_end
1:  cmp     r1, r2
    ldrlo   r3, [r0], #4
    strlo   r3, [r1], #4
    blo     1b
    @ clear .bss
    mov     r3, #0
    ldr     r1, =__bss_start
    ldr     r2, =__bss_end
1:  cmp     r1, r2
    strlo   r3, [r1], #4
    blo     1b
    @ clear .ewram_bss
    ldr     r1, =__ewram_bss_start
    ldr     r2, =__ewram_bss_end
1:  cmp     r1, r2
    strlo   r3, [r1], #4
    blo     1b

    @ install IRQ handler
    ldr     r0, =irq_handler
    ldr     r1, =0x03007FFC
    str     r0, [r1]

    ldr     r0, =main
    mov     lr, pc
    bx      r0
    b       .

    @ minimal IRQ handler: acknowledge and flag for VBlankIntrWait
    .section .iwram, "ax"
    .arm
    .global irq_handler
irq_handler:
    mov     r0, #0x04000000
    ldr     r1, [r0, #0x200]        @ IE | IF<<16
    and     r1, r1, r1, lsr #16     @ IE & IF
    add     r2, r0, #0x200
    strh    r1, [r2, #2]            @ ack IF
    ldr     r2, =0x03007FF8
    ldrh    r3, [r2]
    orr     r3, r3, r1
    strh    r3, [r2]                @ ack BIOS IF
    bx      lr
    .pool
