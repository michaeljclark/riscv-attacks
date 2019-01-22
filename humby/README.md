# Humby

## Overview

Humby is an experimental linux-kernel module that live patches firmware,
adding physical memory protection for systems running RISC-V Linux and
_BBL (Berkeley Boot Loader)_.

## Introduction

The RISC-V Privileged ISA provides two bits to hold privilege level:

Level  | Name | Abbreviation
-----  | ---- | ------------
0      | U    | User
1      | S    | Supervisor
2      |      | _Reserved_
3      | M    | Machine

In the RISC-V Privileged Architecture, the Supervisor OS is run in a lower
privileged mode than the secure monitor. The Supervisor OS typically uses
page-based address translation PTE (Page Table Entry) flags to protect itself
from user processes and user processes from one another. On the other hand,
the secure monior runs using flat addressing in M-mode. In the RISC-V
architecture, the M-mode monitor is essential to provide emulation and
virtualization capabilities, using features such as `mstatus.MPRV` to allow
M-mode to perform loads and stores on behalf of the Supervisor or User mode
using the address translation mode of the Supervisor. There are several
features in the Privileged ISA that are used for emulation of misaligned
loads and stores, floating-point emulation and hypervisor functions.

In versions of Privileged ISA prior to v1.9, M-mode had no ability to prevent
S-mode from mapping the memory of the monitor, thereby allowing S-mode to
access and modify the monitor's code. Privileged ISA v1.9 addresses this by
adding PMP (Physical Memory Protection). The PMP capability allows the monitor
to protect sections of physical memory thereby allowing the monitor to control
what parts of physical memory the Supervisor has access to. Interestingly,
PMP functionality is designed in such a way, that it is mandatory to configure
so that the processor implementation can transition to lower privileged modes.
The default is to only allow M-mode to have access to physical memory.

While PMP functionality is now present in the current RISC-V Privileged ISA,
the popular RISC-V boot loader, _BBL (Berkeley Boot Loader)_, does not use PMP
and instead configures the PMP table with an "allow all" rule. This opens a
security hole, whereby the Supervisor can install a _trap patch_, changing the
code pointed to by the `mtvec` register (M-mode registers are otherwise
protected from S-mode, however, PMP is required to protect the footprint of
the monitor). This vulnerability has already been outlined in a blog post by
Don A. Bailey, in April 2017. See here:

- [blog.securitymouse.com/2017/04/the-risc-v-files-supervisor-machine.html](http://blog.securitymouse.com/2017/04/the-risc-v-files-supervisor-machine.html)

## BBL Memory Protection

Humby extends the exploit procedure outlined by Don A. Bailey, but instead
of using the vulnerability for demonstration or worse, malicious purposes,
Humby implements SBI _trap patching_ by adding a new `SBI_PROTECT` method to
the SBI (Supervisor Based Interface). 

Nunmber  | Name        | Description
-------- | ----------- | -----------
9        | SBI_PROTECT | Protect Firmware using PMP rules

Humby exposes an inteface via the proc filesystem which can be used to call
the new `SBI_PROTECT` interface and enable firmware physical memory protection.

Humby PMP protection for BBL can be configured with three commands:

```
# insmod /lib/modules/4.20.0/humby.ko 
# echo patch > /proc/bbl
# echo protect > /proc/bbl
```

Humby uses a small area of memory after the monitor to install its modified
trap handler. Fortunately, the BBL firmware is small enough such that the
first instruction pointed to by `mtvec` can be modified with a near jump to 
Humby's trap patch. Humby trap patch checks whether the call is an S-mode
ecall, with  the new `SBI_PROTECT` function _(a0=9)_, and if so transfers
control to the modified trap handler, otherwise it returns to the instruction
following the trampoline instruction.

## Memory Layout

The memory layout (including Humby patch) for RISC-V Linux running on RISC-V
QEMU is as follows:

```
|                   |
--------------------|
| 0x8000_0000 BBL   |
| 0x8002_0000 Humby |
| 0x801f_ffff       |
--------------------|
| 0x8020_0000 Linux |
|                   |
```

## Linux kernel module build

Humby is provided as an out-of-tree Linux kernel module. The Makefile lets you
specify your compiler prefix and linux source tree.

```
make CROSS_COMPILE=riscv64-unknown-linux-gnu- RISCV_LINUX=../../riscv-linux
```

## Linux kernel module installation

After building the kernel module, copy it into your kernel rootfs and
after boot, use `insmod` to load it.

```
# insmod /lib/modules/4.20.0/humby.ko 
[   17.952000] humby: loading out-of-tree module taints kernel.
[   17.952000] humby: module license 'MIT' taints kernel.
[   17.956000] humby: initialized
```

## Runtime firmware disassembly

Upon installation of Humby's kernel module, a new proc filesystem entry
`/proc/bbl` is added. The proc entry is how the user interacts with Humby.
Reading `/proc/bbl` will use `ioremap` to map the monitor firmware's physical
memory to a temporary virtual address and then it will disassembly the first
two instructions. e.g.

```
# cat /proc/bbl 
80000000:	1f80006f          j             504                             # 0x800001f8
80000004:	34011173          csrrw         sp,mscratch,sp
```

## Live firmware patching

Humby will patch BBL upon writing `patch` to `/proc/bbl` e.g.

```
# echo patch > /proc/bbl
[   30.300000] humby: + installed bbl-patch (len=62)
[   30.300000] bbl-patch : 80020000:	34011173          csrrw         sp,mscratch,sp
[   30.300000] bbl-patch : 80020004:	00011463          bnez          sp,8                            # 0x8002000c
[   30.304000] bbl-patch : 80020008:	800e006f          j             -131072                         # 0x80000008
[   30.304000] bbl-patch : 8002000c:	e8aa              sd            a0,80(sp)
[   30.304000] bbl-patch : 8002000e:	ecae              sd            a1,88(sp)
[   30.304000] bbl-patch : 80020010:	34202573          csrrs         a0,mcause,zero
[   30.304000] bbl-patch : 80020014:	45a5              addi          a1,zero,9
[   30.308000] bbl-patch : 80020016:	00b50363          beq           a0,a1,6                         # 0x8002001c
[   30.308000] bbl-patch : 8002001a:	a831              j             28                              # 0x80020036
[   30.308000] bbl-patch : 8002001c:	6546              ld            a0,80(sp)
[   30.308000] bbl-patch : 8002001e:	00b50363          beq           a0,a1,6                         # 0x80020024
[   30.308000] bbl-patch : 80020022:	a819              j             22                              # 0x80020038
[   30.308000] bbl-patch : 80020024:	20040537          lui           a0,537133056
[   30.312000] bbl-patch : 80020028:	fff5051b          addiw         a0,a0,-1
[   30.312000] bbl-patch : 8002002c:	3b051073          csrrw         zero,pmpaddr0,a0
[   30.312000] bbl-patch : 80020030:	4561              addi          a0,zero,24
[   30.312000] bbl-patch : 80020032:	3a051073          csrrw         zero,pmpcfg0,a0
[   30.312000] bbl-patch : 80020036:	6546              ld            a0,80(sp)
[   30.312000] bbl-patch : 80020038:	65e6              ld            a1,88(sp)
[   30.312000] bbl-patch : 8002003a:	fcfdf06f          j             -131122                         # 0x80000008
[   30.312000] humby: + installed bbl-tramp (len=4)
[   30.312000] bbl-tramp : 80000000:	1f80006f          j             504                             # 0x800001f8
[   30.312000] bbl-tramp : 80000004:	7fd1f06f          j             131068                          # 0x80020000
```

If one checks the disassembly, one can note the _mscratch_ CSR swap
is replaced with a jump to Humby at `0x8002_0000`.

```
# cat /proc/bbl 
80000000:	1f80006f          j             504                             # 0x800001f8
80000004:	7fd1f06f          j             131068                          # 0x80020000
```

## Live memory protection

Humby will protect BBL by loading PMP rules upon writing `protect` to `/proc/bbl` e.g.

```
# echo protect > /proc/bbl
[   57.000000] humby: protected
```

After protection has been enabled, it should no longer be possible to
disassemble firmware. Attempting to read the firmware should cause a address
fault and may even hang the kernel due to bugs in riscv-linux trap handling.

```
# cat /proc/bbl
<Linux HANGS or PANICS>
```

## Future work

Use Humby for Linux based testing of Physical Memory Protection, live patching
amd Hypervisor functionality.
