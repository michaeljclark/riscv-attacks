/*
 * Humby BBL trampoline linker script
 *
 * This linker script contains the start address and trap patch address
 * for relative linking of Humby's BBL trampoline jump instruction.
 */

OUTPUT_ARCH( "riscv" )

ENTRY( humby_tramp )

MEMORY
{
  boot   (wxa!ri) : ORIGIN = 0x80000004, LENGTH = 4
}

PHDRS
{
  text PT_LOAD;
}

SECTIONS
{
  .text : {
    *(.text.init) *(.text .text.*)
  } >boot AT>boot :text

  PROVIDE(trap_patch = 0x80020000);
}
