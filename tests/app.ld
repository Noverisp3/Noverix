/* FILE: app.ld */
ENTRY(main)
SECTIONS
{
    . = 0x00800000; /* Load tại mốc 8MB */

    .text : {
        *(.text)
    }
    .data : {
        *(.data)
    }
    .bss : {
        *(.bss)
    }
}
