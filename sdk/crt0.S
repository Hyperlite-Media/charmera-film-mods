@ Payload entry. The loader calls offset 0 with r0 = load address.
@ Payloads are linked at 0 and built with -fPIE, so startup only has to add the
@ load address to each GOT entry and zero .bss before any C code runs.

        .arm
        .section .text.start, "ax"
        .global _start
_start:
        push    {r4, r5, r6, lr}
        mov     r4, r0
        ldr     r1, =__got_start
        ldr     r2, =__got_end
        add     r1, r1, r4
        add     r2, r2, r4
1:      cmp     r1, r2
        ldrlo   r3, [r1]
        addlo   r3, r3, r4
        strlo   r3, [r1], #4
        blo     1b

        ldr     r1, =__bss_start
        ldr     r2, =__bss_end
        add     r1, r1, r4
        add     r2, r2, r4
        mov     r3, #0
2:      cmp     r1, r2
        strlo   r3, [r1], #4
        blo     2b

        mov     r0, r4
        bl      main                    @ int main(unsigned load_address)
        pop     {r4, r5, r6, lr}
        bx      lr
        .ltorg
