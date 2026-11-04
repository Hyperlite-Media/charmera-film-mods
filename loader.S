@ Charmera SD payload loader.
@
@ Replaces the body of the stock update check (RAM 0x21624), which the SD mount
@ handler calls once the card is mounted. Once per boot it reads C:\HACK.BIN
@ (up to 64 KB) into a malloc'd buffer and calls it with r0 = load address.
@ It always returns 0, so the stock SD updater (which erased flash block
@ 0x10000 in testing) never runs.
@
@ Payload ABI: ARM code, position independent, entry at offset 0, called with
@ r0 = its own load address; must preserve r4-r11 and sp and return via lr.
@ The buffer is never freed, so a payload may leave code resident.
@
@ The CPU is ARMv4 with no caches in use, so no cache maintenance is needed and
@ indirect calls use "mov lr, pc; mov pc, rX".

        .arm
        .section .text
        .global loader
loader:
        push    {r4, r5, r6, lr}
        adr     r0, ran_once
        ldr     r1, [r0]
        cmp     r1, #0
        bne     out
        mov     r1, #1
        str     r1, [r0]

        adr     r0, path
        mov     r1, #0                  @ O_RDONLY
        bl      fw_open
        movs    r4, r0                  @ r4 = fd
        bmi     out

        mov     r0, #0x10000
        bl      fw_malloc
        movs    r5, r0                  @ r5 = buffer
        beq     close

        mov     r0, r4
        mov     r1, r5
        mov     r2, #0x10000
        bl      fw_read
        mov     r6, r0                  @ r6 = bytes read
        mov     r0, r4
        bl      fw_close

        cmp     r6, #4
        blt     discard
        mov     r0, r5
        mov     lr, pc
        mov     pc, r5                  @ payload(r0 = load address)
        b       out

discard:
        mov     r0, r5
        bl      fw_free
        b       out
close:
        mov     r0, r4
        bl      fw_close
out:
        mov     r0, #0                  @ never report a stock update
        pop     {r4, r5, r6, pc}

ran_once:
        .word   0
path:
        .asciz  "C:\\HACK.BIN"
        .align  2
