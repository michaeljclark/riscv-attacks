#
# humby_patch - trap patch to protect BBL firmware
#
# We use the gas, ld, objcopy and xxd to make an array with our patch.
# The linker script zero_tramp.lds provides context for relative link.
#
# - gcc -nostartfiles -Wl,-T,zero_patch.lds zero_patch.S -o zero_patch_link.o
# - objcopy -O binary -j .text zero_patch_link.o zero_patch_bin.o
# - xxd -i zero_patch_bin.o zero_patch_src.h
#
# Humby's BBL patch is placed just after the text at (+512KiB) so that
# the trampoline can be composed of a single jump instruction (+/-1MiB)
#
# |                   |
# --------------------|
# | 0x8000_0000 BBL   |
# | 0x8008_0000 Humby |
# | 0x801f_ffff       |
# --------------------|
# | 0x8020_0000 Linux |
# |                   |
#

.globl humby_patch
humby_patch:
    # We overwrite the mscratch swap instruction with a jump to this patch
    # so we need to perform that instruction here instead.
    csrrw   sp,mscratch,sp

    # sp is zero for traps from machine mode so we jump back to regular
    # trap handler at the instruction after our patched jump. we need to
    # duplicate this test here because we overwrote that instruction
    bnez    sp, 1f
    j       trap_reentry

1:
    # The goal is to patch SBI ecall from S-mode and add our own SBI handler.
    # This is somewhat like the MacOS 9 method of enhancing the system
    # using so-called "trap patches".

    # We need a couple of scratch registers to do our work.
    sd      a0,80(sp)
    sd      a1,88(sp)

    # We need to check the trap cause for S-mode ecall.
    csrr    a0,mcause
    li      a1,9          # supervisor_ecall -> 9
    beq     a0,a1,2f      # okay we have an ecall instruction
    j       restore_regs

2:
    # We want to intercept the SBI_HACK SBI interface.
    ld      a0,80(sp)     # restore a0 as it holds SBI function
    beq     a0,a1,3f      # okay we have SBI_HACK (a0 -> 9)
    j       9f            # we can skip restore of a0

3:
    # We want to be nice!
    #
    # So we set up PMP (Program Physical Memory Protection) so that from now
    # on only M mode can read BBL firmware.
    #
    # pmpaddr0   0x80000000 - 0x80200000
    # pmpcfg0    NAPOT|NONE   (No R|W|X flags means only M-mode)
    #
    li      a0,0x2003ffff
    csrrw   zero,pmpaddr0,a0
    li      a0,0x18
    csrrw   zero,pmpcfg0,a0

restore_regs:
    # Restore scratch registers before jumping back to reentry symbol.
    ld      a0,80(sp)
9:
    ld      a1,88(sp)
    j       trap_reentry
