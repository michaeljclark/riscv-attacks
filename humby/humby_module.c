/*
 * Humby - Physical Memory Protection for Berkeley Boot Loader
 *
 * - riscv-linux firmware disassembler: `cat /proc/bbl`
 * - runtime firmware patching: `echo patch > /proc/bbl`
 * - SBI_PROTECT trap patch: `echo protect > /proc/bbl`
 *
 * Copyright (c) 2019 Michael Clark <michaeljclark@mac.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#if defined CONFIG_RISCV
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include <asm-generic/io.h>
#include <asm/csr.h>

#include "humby_disasm.h"

unsigned char humby_tramp[] = {
#include "humby_tramp_src.h"
};

unsigned char humby_patch[] = {
#include "humby_patch_src.h"
};

#define HUMBY_MOD "humby: "

#define BOOT_BASE 0x80000000
#define BOOT_TRAMP 0x80000004
#define BOOT_PATCH 0x80020000
#define BOOT_LEN 0x200000

struct humby_module_data
{
	char *buf;
	struct proc_dir_entry *proc;
	int patched;
	int protected;
};

static struct humby_module_data data;

static void inst_fetch(void __iomem *addr, rv_inst *inst, size_t *len)
{
	*inst = readl(addr);
	if ((*inst & 0b11) != 0b11) {
		*inst &= 0xffff; /* RVC */
		*len = 2;
	} else {
		*len = 4;
	}
}

static void humby_disasm_log(const char *label, void __iomem *bbl,
	uint64_t pc, size_t limit)
{
	char buf[128];
	size_t offset = 0;

	while (offset < limit) {
		rv_inst inst;
		size_t len;
		inst_fetch(bbl + offset, &inst, &len);
		disasm_inst(buf, sizeof(buf), rv64, pc + offset, inst);
		printk(KERN_INFO "%s: %llx:\t%s\n", label, pc + offset, buf);
		offset += len;
	}
}

static void humby_disasm_buf(char *buffer, size_t buflen, void __iomem *bbl,
	uint64_t pc, size_t limit)
{
	char buf[128];

	size_t offset = 0;
	buffer[0] = '\0';
	while (offset < limit) {
		rv_inst inst;
		size_t len;
		inst_fetch(bbl + offset, &inst, &len);
		disasm_inst(buf, sizeof(buf), rv64, pc + offset, inst);
		snprintf(buffer + strlen(buffer), buflen - strlen(buffer),
			"%llx:\t%s\n", pc + offset, buf);
		offset += len;
	}
}

static int humby_proc_open(struct inode *inode, struct file *file)
{
	try_module_get(THIS_MODULE);
	return 0;
}

static int humby_proc_close(struct inode *inode, struct file *file)
{
	module_put(THIS_MODULE);
	return 0;
}

/* Use PDE_DATA(file_inode(filp)) to access proc entry userdata */

static ssize_t humby_proc_read(struct file *filp, char __user *buffer,
	size_t count, loff_t *pos)
{
	size_t len;
	void __iomem *bbl;

	/* disassemble firmware */
	bbl = ioremap(BOOT_BASE, BOOT_LEN);
	if (!bbl) {
		return -ENOMEM;
	}
	humby_disasm_buf(data.buf, PAGE_SIZE, bbl, BOOT_BASE, 8);
	len = strlen(data.buf);
	iounmap(bbl);

	/* check length */
	if (count == 0 || *pos > len) {
		return 0;
	} else if (count + *pos > len) {
		count = len - *pos;
	}

	/* copy result to user-space */
	if (copy_to_user(buffer, data.buf + *pos, count)) {
		return -EFAULT;
	}
	*pos += count;

	return count;
}

static void humby_cmd_patch(void)
{
	void __iomem *bbl;

	if (data.patched) {
		return;
	}

	bbl = ioremap(BOOT_BASE, BOOT_LEN);
	if (!bbl) {
		printk(KERN_ERR HUMBY_MOD "ioremap failed\n");
		return;
	}

	/* install trap patch */
	memcpy_toio(bbl + 0x20000, humby_patch, sizeof(humby_patch));
	printk(KERN_INFO HUMBY_MOD "+ installed bbl-patch (len=%zu)\n",
		sizeof(humby_patch));
	humby_disasm_log("bbl-patch ", bbl + 0x20000, BOOT_PATCH,
		sizeof(humby_patch));

	/* install trampoline to trap patch */
	memcpy_toio(bbl + 4, humby_tramp, sizeof(humby_tramp));
	printk(KERN_INFO HUMBY_MOD "+ installed bbl-tramp (len=%zu)\n",
		sizeof(humby_tramp));
	humby_disasm_log("bbl-tramp ", bbl, BOOT_BASE, 8);

	iounmap(bbl);

	data.patched = 1;
}

static void humby_cmd_protect(void)
{
	if (data.patched && data.protected) {
		return;
	}
	asm volatile("li a0, 9\necall\n");
	printk(KERN_INFO HUMBY_MOD "protected\n");

	data.protected =1;
}

static ssize_t humby_proc_write(struct file *filp, const char __user *buffer,
	size_t count, loff_t *pos)
{
	static char write_buf[17] = { 0 };

	if (*pos + count > sizeof(write_buf)-1) {
		return -EFAULT;
	}

	if (copy_from_user(write_buf + *pos, buffer, count)) {
		return -EFAULT;
	}
	write_buf[*pos + count] = '\0';
	*pos += count;

	if (strncmp(write_buf, "patch\n", sizeof(write_buf)) == 0) {
		humby_cmd_patch();
	} else if (strncmp(write_buf, "protect\n", sizeof(write_buf)) == 0) {
		humby_cmd_protect();
	}

	return count;
}

static struct file_operations humby_proc_fops = {
	.open = humby_proc_open,
	.release = humby_proc_close,
	.read = humby_proc_read,
	.write = humby_proc_write,
};

static int humby_init(void)
{
	data.buf = (char*)get_zeroed_page(GFP_KERNEL);
	if (!data.buf) {
		return -ENOMEM;
	}

	data.proc = proc_create_data("bbl", 0666, NULL, &humby_proc_fops, NULL);
	if (!data.proc) {
		free_page((unsigned long)data.buf);
		return -EFAULT;
	}

	printk(KERN_INFO HUMBY_MOD "initialized\n");

	return 0;
}

static void humby_exit(void)
{
	printk(KERN_INFO HUMBY_MOD "exiting\n");
	remove_proc_entry("bbl", NULL);
	free_page((unsigned long)data.buf);
}

module_init(humby_init);
module_exit(humby_exit);

MODULE_LICENSE("MIT");
MODULE_DESCRIPTION("Humby - Hotpatch for BBL");
MODULE_AUTHOR("Michael <michael@anarch128.org>");
#endif
