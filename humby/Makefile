ifneq ($(KERNELRELEASE),)

obj-m  := humby.o
humby-y := humby_module.o humby_disasm.o
clean-files := humby_tramp_src.h humby_patch_src.h

$(obj)/humby_tramp_link.o: $(src)/humby_tramp.lds $(obj)/humby_tramp.o ; $(call if_changed,humbylink)
$(obj)/humby_patch_link.o: $(src)/humby_patch.lds $(obj)/humby_patch.o ; $(call if_changed,humbylink)
$(obj)/humby_tramp_bin.o: $(obj)/humby_tramp_link.o ; $(call if_changed,humbybin) # binary files
$(obj)/humby_patch_bin.o: $(obj)/humby_patch_link.o ; $(call if_changed,humbybin) # binary files
$(src)/humby_tramp_src.h: $(obj)/humby_tramp_bin.o ; $(call if_changed,humbysrc) # C arrays
$(src)/humby_patch_src.h: $(obj)/humby_patch_bin.o ; $(call if_changed,humbysrc) # C arrays
$(obj)/humby_module.o: $(src)/humby_tramp_src.h $(src)/humby_patch_src.h

quiet_cmd_humbylink = Humby.LINK $@
      cmd_humbylink = $(CC) -nostdlib -nostartfiles -Wl,-T,$^ -o $@

quiet_cmd_humbybin = Humby.MKBIN $@
      cmd_humbybin = $(OBJCOPY) -O binary -j .text $^ $@

quiet_cmd_humbysrc = Humby.MKSRC $@
      cmd_humbysrc = xxd -i < $^ > $@

else

CROSS_COMPILE ?= riscv64-unknown-linux-gnu-
RISCV_LINUX ?= ../../riscv-linux

all:
	make -C $(RISCV_LINUX) ARCH=riscv CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) modules

clean:
	make -C $(RISCV_LINUX) ARCH=riscv CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean

endif
