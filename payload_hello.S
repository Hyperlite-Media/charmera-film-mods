@ Test payload: writes C:\HACKLOG.TXT with its load address, then returns.
@ Position independent; calls firmware functions through absolute addresses.

        .arm
        .section .text
        .global payload
payload:
        push    {r4, r5, r6, lr}
        sub     sp, sp, #64
        mov     r5, r0                  @ r5 = load address

        mov     r0, sp                  @ sprintf(buf, msg, load address)
        adr     r1, msg
        mov     r2, r5
        ldr     r3, =FW_SPRINTF
        mov     lr, pc
        mov     pc, r3
        mov     r6, r0                  @ r6 = length

        adr     r0, logpath             @ open(path, O_RDWR|O_CREAT|O_TRUNC)
        ldr     r1, =0x602
        ldr     r3, =FW_OPEN
        mov     lr, pc
        mov     pc, r3
        movs    r4, r0
        bmi     done

        mov     r0, r4                  @ write(fd, buf, len)
        mov     r1, sp
        mov     r2, r6
        ldr     r3, =FW_WRITE
        mov     lr, pc
        mov     pc, r3

        mov     r0, #5                  @ same short delay the firmware uses before close
        ldr     r3, =FW_DELAY
        mov     lr, pc
        mov     pc, r3

        mov     r0, r4
        ldr     r3, =FW_CLOSE
        mov     lr, pc
        mov     pc, r3
done:
        add     sp, sp, #64
        pop     {r4, r5, r6, pc}

logpath:
        .asciz  "C:\\HACKLOG.TXT"
msg:
        .asciz  "Charmera payload ran, loaded at 0x%08x\r\n"
        .align  2
        .ltorg
