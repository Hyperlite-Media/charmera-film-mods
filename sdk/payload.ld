/* Payloads are linked at 0 and relocated by crt0 (GOT only). Avoid static
   tables of pointers in C: those would need relocations crt0 does not apply.
   .bss is not in HACK.BIN; it lives in the rest of the loader's 64 KB buffer
   and crt0 zeroes it. */
ENTRY(_start)
SECTIONS
{
    . = 0;
    .text : { KEEP(*(.text.start)) *(.text*) *(.rodata*) }
    .got  : { __got_start = .; *(.got.plt) *(.got) __got_end = .; }
    .data : { *(.data*) . = ALIGN(4); }
    .bss  : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
