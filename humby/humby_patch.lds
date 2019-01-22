/*
 * Humby BBL trap patch linker script
 *
 * This linker script contains the start address and trap reentry address
 * for relative linking of Humby's BBL SBI_PROTECT trap patch.
 */

OUTPUT_ARCH( "riscv" )

ENTRY( humby_patch )

MEMORY
{
  patch   (wxa!ri) : ORIGIN = 0x80020000, LENGTH = 128K
}

PHDRS
{
  text PT_LOAD;
}

SECTIONS
{
  .text : {
    *(.text.init) *(.text .text.*)
  } >patch AT>patch :text

  PROVIDE(trap_reentry = 0x80000008);
}
