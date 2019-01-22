#
# humby_tramp - trampoline instruction to jump to trap patch
#
# We use the gas, ld, objcopy and xxd to make an array with our patch.
# The linker script zero_tramp.lds provides context for relative link.
#
# - gcc -nostartfiles -Wl,-T,zero_tramp.lds zero_tramp.S -o zero_tramp_link.o
# - objcopy -O binary -j .text zero_tramp_link.o zero_tramp_bin.o
# - xxd -i zero_tramp_bin.o zero_tramp_src.h
#
# + BBL (unpatched) 
#
#         _start 80000000:   1f80006f            j   800001f8 <do_reset>
#    trap_vector 80000004:   34011173            csrrw   sp,mscratch,sp
#                80000008:   1a010863            beqz    sp,800001b8 <restore_regs+0x80>
#
# + BBL (patched) 
#
#        _start 80000000:   1f80006f            j   800001f8 <do_reset>
#   trap_vector 80000004:   7fd1f06f            j   80020000 <trap_patch>
#               80000008:   1a010863            beqz    sp,800001b8 <restore_regs+0x80>
#

.globl humby_tramp
humby_tramp:
    j       trap_patch
