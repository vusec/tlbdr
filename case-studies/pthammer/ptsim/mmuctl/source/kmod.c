#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/highmem.h>
#include <linux/memory.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/version.h>

#include <ioctl.h>
#include <mmuctl.h>

MODULE_AUTHOR("Stephan van Schaik");
MODULE_DESCRIPTION("A kernel module to control the MMU translation structures");
MODULE_LICENSE("GPL");

static int device_busy = 0;
static int mmap_sem_locked = 0;

struct ptwalk {
	int pid;
	pgd_t *pgd;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	p4d_t *p4d;
#else
	unsigned long *p4d;
#endif
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned valid;
};

#define from_user copy_from_user
#define to_user copy_to_user

static int
device_open(struct inode *inode, struct file *file)
{
	if (device_busy)
		return -EBUSY;

	try_module_get(THIS_MODULE);
	device_busy = 1;

	return 0;
}

static int
device_release(struct inode *inode, struct file *file)
{
	device_busy = 0;
	module_put(THIS_MODULE);

	return 0;
}

static inline
unsigned long size_inside_page(unsigned long start, unsigned long size)
{
	unsigned long sz;

	sz = PAGE_SIZE - (start & (PAGE_SIZE - 1));

	return min(sz, size);
}

void *
xlate_mem_ptr(phys_addr_t phys)
{
	void *addr = NULL;
	unsigned long start = phys & PAGE_MASK;
	unsigned long pfn = PFN_DOWN(phys);

	/* If page is RAM, we can use __va. Otherwise ioremap and unmap. */
	if (!page_is_ram(start >> PAGE_SHIFT)) {
		addr = (void __force *)ioremap(start, PAGE_SIZE);

		if (addr)
			addr = (void *)((unsigned long)addr | (phys & ~PAGE_MASK));

		return addr;
	}

	if (!PageHighMem(pfn_to_page(pfn)))
		return __va(phys);

	addr = kmap(pfn_to_page(pfn));
	return addr;
}

void
unxlate_mem_ptr(phys_addr_t phys, void *addr)
{
	unsigned long pfn = PFN_DOWN(phys);

	if (!page_is_ram(phys >> PAGE_SHIFT)) {
		iounmap((void __iomem *)((unsigned long)addr & PAGE_MASK));
		return;
	}

	if (!PageHighMem(pfn_to_page(pfn)))
		return;

	kunmap(pfn_to_page(pfn));
}

static ssize_t
device_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	phys_addr_t p = *ppos;
	ssize_t read, sz;
	char *ptr;

	read = 0;

#ifdef __ARCH_HAS_NO_PAGE_ZERO_MAPPED
	if (p < PAGE_SIZE) {
		sz = size_inside_page(p, count);

		if (sz > 0) {
			if (clear_user(buf, sz))
				return -EFAULT;

			buf += sz;
			p += sz;
			count -= sz;
			read += sz;
		}
	}
#endif

	while (count > 0) {
		unsigned long remaining;

		sz = size_inside_page(p, count);

		if (!(ptr = xlate_mem_ptr(p)))
			return -EFAULT;

		remaining = copy_to_user(buf, ptr, sz);
		unxlate_mem_ptr(p, ptr);

		if (remaining)
			return -EFAULT;

		buf += sz;
		p += sz;
		count -= sz;
		read += sz;
	}

	*ppos += read;

	return read;
}

static ssize_t
device_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	phys_addr_t p = *ppos;
	ssize_t written, sz;
	char *ptr;

	if (p != *ppos)
		return -EFBIG;

	written = 0;

#ifdef __ARCH_HAS_NO_PAGE_ZERO_MAPPED
	if (p < PAGE_SIZE) {
		sz = size_inside_page(p, count);

		buf += sz;
		p += sz;
		count -= sz;
		written += sz;
	}
#endif

	while (count > 0) {
		unsigned long remaining;

		sz = size_inside_page(p, count);

		if (!(ptr = xlate_mem_ptr(p))) {
			if (written)
				break;

			return -EFAULT;
		}

		remaining = copy_from_user(ptr, buf, sz);
		unxlate_mem_ptr(p, ptr);

		if (remaining) {
			written += sz - remaining;

			if (written)
				break;

			return -EFAULT;
		}

		buf += sz;
		p += sz;
		count -= sz;
		written += sz;
	}

	*ppos += written;

	return written;
}

int __weak phys_mem_access_prot_allowed(struct file *file,
	unsigned long pfn, unsigned long size, pgprot_t *vma_prot)
{
	return 1;
}

#ifndef __HAVE_PHYS_MEM_ACCESS_PROT
/*
 * Architectures vary in how they handle caching for addresses
 * outside of main memory.
 *
 */
#ifdef pgprot_noncached
static int
uncached_access(struct file *file, phys_addr_t addr)
{
#if defined(CONFIG_IA64)
	/*
	 * On ia64, we ignore O_DSYNC because we cannot tolerate memory
	 * attribute aliases.
	 */
	return !(efi_mem_attributes(addr) & EFI_MEMORY_WB);
#elif defined(CONFIG_MIPS)
	{
		extern int __uncached_access(struct file *file,
						 unsigned long addr);

		return __uncached_access(file, addr);
	}
#else
	/*
	 * Accessing memory above the top the kernel knows about or through a
	 * file pointer
	 * that was marked O_DSYNC will be done non-cached.
	 */
	if (file->f_flags & O_DSYNC)
		return 1;
	return addr >= __pa(high_memory);
#endif
}
#endif

static pgprot_t
phys_mem_access_prot(struct file *file, unsigned long pfn, unsigned long size,
	pgprot_t vma_prot)
{

#ifdef pgprot_noncached
	phys_addr_t offset = pfn << PAGE_SHIFT;

	if (uncached_access(file, offset))
		return pgprot_noncached(vma_prot);
#endif
	return vma_prot;
}
#endif

#ifndef CONFIG_MMU
static unsigned long
get_unmapped_area_mem(struct file *file, unsigned long addr, unsigned long len,
	unsigned long pgoff, unsigned long flags)
{
	return pgoff << PAGE_SHIFT;
}

/* permit direct mmap, for read, write or exec */
static unsigned
memory_mmap_capabilities(struct file *file)
{
	return NOMMU_MAP_DIRECT | NOMMU_MAP_READ | NOMMU_MAP_WRITE |
		NOMMU_MAP_EXEC;
}

/* can't do an in-place private mapping if there's no MMU */
static inline int
private_mapping_ok(struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_MAYSHARE;
}
#else
static inline
int private_mapping_ok(struct vm_area_struct *vma)
{
	return 1;
}
#endif

static const struct vm_operations_struct mmap_mem_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys
#endif
};

static int
device_mmap(struct file *file, struct vm_area_struct *vma)
{
	size_t size = vma->vm_end - vma->vm_start;

	if (!private_mapping_ok(vma))
		return -ENOSYS;

	if (!phys_mem_access_prot_allowed(file, vma->vm_pgoff, size,
						&vma->vm_page_prot))
		return -EINVAL;

	vma->vm_ops = &mmap_mem_ops;

	/* Remap-pfn-range will mark the range VM_IO */
	if (remap_pfn_range(vma,
				vma->vm_start,
				vma->vm_pgoff,
				size,
				vma->vm_page_prot)) {
		return -EAGAIN;
	}

	return 0;
}

static loff_t
device_lseek(struct file *file, loff_t offset, int orig)
{
	loff_t ret;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	mutex_lock(&file->f_dentry->d_inode->i_mutex);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,5,0)
	mutex_lock(&file->f_path.dentry->d_inode->i_mutex);
#else
	inode_lock(file_inode(file));
#endif

	switch (orig) {
	case SEEK_CUR:
		offset += file->f_pos;
	case SEEK_SET:
		/* to avoid userland mistaking f_pos=-9 as -EBADF=-9 */
		if ((unsigned long long)offset >= -MAX_ERRNO) {
			ret = -EOVERFLOW;
			break;
		}
		file->f_pos = offset;
		ret = file->f_pos;
		force_successful_syscall_return();
		break;
	default:
		ret = -EINVAL;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	mutex_unlock(&file->f_dentry->d_inode->i_mutex);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,5,0)
	mutex_unlock(&file->f_path.dentry->d_inode->i_mutex);
#else
	inode_unlock(file_inode(file));
#endif

	return ret;
}

static struct mm_struct *
get_mm(int pid)
{
	struct task_struct *task;
	struct pid *vpid;

	task = current;

	if (pid != 0) {
		vpid = find_vpid(pid);

		if (!vpid)
			return NULL;

		task = pid_task(vpid, PIDTYPE_PID);

		if (!task)
			return NULL;
	}

	if (task->mm)
		return task->mm;

	return task->active_mm;
}

static void
local_invlpg(void *addr)
{
#if defined(__i386__) || defined(__x86_64__)
	int pcid;

	if (cpu_feature_enabled(X86_FEATURE_INVPCID_SINGLE)) {
		for (pcid = 0; pcid < 4096; pcid++) {
			invpcid_flush_one(pcid, (long unsigned int)addr);
		}
	} else {
		asm volatile("invlpg (%0)\n" :: "r" (addr));
	}
#elif defined(__arch64__)
	asm volatile("dsb ishst\n");
	asm volatile("tlbi vmalle1is\n");
	asm volatile("dsb ish\n");
	asm volatile("isb\n");
#else
#error not implemented
#endif
}

static void
invlpg(void *addr)
{
	on_each_cpu(local_invlpg, addr, 1);
}

static int
resolve_va(size_t addr, struct ptwalk *entry, int lock)
{
	struct mm_struct *mm;
	pgd_t *pgd;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	p4d_t *p4d;
#else
	size_t *p4d;
#endif
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	if (!entry)
		return -EINVAL;

	entry->pgd = NULL;
	entry->p4d = NULL;
	entry->pud = NULL;
	entry->pmd = NULL;
	entry->pte = NULL;
	entry->valid = 0;

	mm = get_mm(entry->pid);

	if (!mm)
		return -EINVAL;

	if (lock)
		//down_read(&mm->mmap_sem);
		mmap_read_lock(mm);

	pgd = pgd_offset(mm, addr);

	if (pgd_none(*pgd) || pgd_bad(*pgd))
		goto err;

	entry->pgd = pgd;
	entry->valid |= MMUCTL_PGD;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	p4d = p4d_offset(entry->pgd, addr);

	if (p4d_none(*p4d) || p4d_bad(*p4d))
		goto err;

	entry->p4d = p4d;
	entry->valid |= MMUCTL_P4D;

	pud = pud_offset(entry->p4d, addr);
#else
	pud = pud_offset(pgd, addr);
#endif
	if (!pud_large(*pud) && (pud_none(*pud) || pud_bad(*pud)))
		goto err;

	entry->pud = pud;
	entry->valid |= MMUCTL_PUD;

	if (pud_large(*pud))
		goto err;

	pmd = pmd_offset(pud, addr);

	if (!pmd_large(*pmd) && (pmd_none(*pmd) || pmd_bad(*pmd)))
		goto err;

	entry->pmd = pmd;
	entry->valid |= MMUCTL_PMD;

	if (pmd_large(*pmd))
		goto err;

	pte = pte_offset_map(pmd, addr);

	entry->pte = pte;
	entry->valid |= MMUCTL_PTE;

	pte_unmap(pte);

	if (lock)
		mmap_read_unlock(mm);
		//up_read(&mm->mmap_sem);

	return 0;

err:
	if (lock)
		mmap_read_unlock(mm);
		//up_read(&mm->mmap_sem);

	return -1;
}

#if defined(__aarch64__)
static inline pte_t
native_make_pte(pteval_t val)
{
	return __pte(val);
}

static inline pgd_t
native_make_pgd(pgdval_t val)
{
	return __pgd(val);
}

static inline pud_t
native_make_pud(pudval_t val)
{
	return __pud(val);
}

static inline pmd_t
native_make_pmd(pmdval_t val)
{
	return __pmd(val);
}

static inline pteval_t
native_pte_val(pte_t pte)
{
	return pte_val(pte);
}
#endif

static int
update_va(struct mmuctl_ptwalk *entry, int lock)
{
	struct mm_struct *mm = get_mm(entry->pid);
	struct ptwalk old;
	unsigned long addr = entry->va;

	if (!mm)
		return -EINVAL;

	old.pid = entry->pid;

	if (lock)
		mmap_read_lock(mm);
		//down_read(&mm->mmap_sem);

	resolve_va(addr, &old, 0);

	if ((old.valid & MMUCTL_PGD) && (entry->valid & MMUCTL_PGD))
		set_pgd(old.pgd, native_make_pgd(entry->pgd));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	if ((old.valid & MMUCTL_P4D) && (entry->valid & MMUCTL_P4D))
		set_p4d(old.p4d, native_make_p4d(entry->p4d));
#endif

	if ((old.valid & MMUCTL_PUD) && (entry->valid & MMUCTL_PUD))
		set_pud(old.pud, native_make_pud(entry->pud));

	if ((old.valid & MMUCTL_PMD) && (entry->valid & MMUCTL_PMD))
		set_pmd(old.pmd, native_make_pmd(entry->pmd));

	if ((old.valid & MMUCTL_PTE) && (entry->valid & MMUCTL_PTE))
		set_pte(old.pte, native_make_pte(entry->pte));

	invlpg((void *)addr);

	if (lock)
		mmap_read_unlock(mm);
		//up_read(&mm->mmap_sem);

	return 0;
}

static void
ptwalk_to_user(struct mmuctl_ptwalk *usr, struct ptwalk *ptwalk)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#if CONFIG_PGTABLE_LEVELS > 4
	usr->p4d = (ptwalk->p4d) ? (ptwalk->p4d)->p4d : 0;
#else
	usr->p4d = (ptwalk->p4d) ? (ptwalk->p4d)->pgd.pgd : 0;
#endif
#endif

#if defined(__i386__) || defined(__x86_64__)
	usr->pgd = (ptwalk->pgd) ? (ptwalk->pgd)->pgd : 0;
	usr->pud = (ptwalk->pud) ? (ptwalk->pud)->pud : 0;
	usr->pmd = (ptwalk->pmd) ? (ptwalk->pmd)->pmd : 0;
	usr->pte = (ptwalk->pte) ? (ptwalk->pte)->pte : 0;
#elif defined(__aarch64__)
	usr->pgd = (ptwalk->pgd) ? pgd_val(*(ptwalk->pgd)) : 0;
	usr->pud = (ptwalk->pud) ? pud_val(*(ptwalk->pud)) : 0;
	usr->pmd = (ptwalk->pmd) ? pmd_val(*(ptwalk->pmd)) : 0;
	usr->pte = (ptwalk->pte) ? pte_val(*(ptwalk->pte)) : 0;
#endif

	usr->valid = ptwalk->valid;
}

static long
ioctl_lock(void)
{
	struct mm_struct *mm = current->active_mm;

	if (mmap_sem_locked) {
		printk("mmuctl: mmap_sem already locked.\n");
		return -EINVAL;
	}

	mmap_read_lock(mm);
	//down_read(&mm->mmap_sem);
	mmap_sem_locked = 1;

	return 0;
}

static long
ioctl_unlock(void)
{
	struct mm_struct *mm = current->active_mm;

	if (mmap_sem_locked) {
		printk("mmuctl: mmap_sem is not locked.\n");
		return -EINVAL;
	}

	mmap_read_unlock(mm);
	//up_read(&mm->mmap_sem);
	mmap_sem_locked = 0;

	return 0;
}

static long
ioctl_get_root(unsigned long param)
{
	struct mm_struct *mm;
	struct mmuctl_root root;
	long ret;

	ret = from_user(&root, (void *)param, sizeof root);

	if (ret < 0)
		return ret;

	mm = get_mm(root.pid);

	if (!mm)
		return -EINVAL;

	if (!mmap_sem_locked)
		mmap_read_lock(mm);
		//down_read(&mm->mmap_sem);

	root.root = virt_to_phys(mm->pgd);

	if (!mmap_sem_locked)
		mmap_read_unlock(mm);
		//up_read(&mm->mmap_sem);

	return to_user((void *)param, &root, sizeof root);
}

static long
ioctl_set_root(unsigned long param)
{
	struct mm_struct *mm;
	struct mmuctl_root root = {0};
	long ret;

	ret = from_user(&root, (void *)param, sizeof root);

	mm = get_mm(root.pid);

	if (!mm)
		return -EINVAL;

	if (!mmap_sem_locked)
		mmap_read_lock(mm);
		//down_read(&mm->mmap_sem);

	mm->pgd = (pgd_t *)phys_to_virt(root.root);

	if (!mmap_sem_locked)
		mmap_read_unlock(mm);
		//up_read(&mm->mmap_sem);

	return 0;
}

static long
ioctl_resolve(unsigned long param)
{
	struct mmuctl_ptwalk usr_ptwalk;
	struct ptwalk ptwalk;
	long ret;

	ret = from_user(&usr_ptwalk, (void *)param, sizeof usr_ptwalk);

	if (ret < 0)
		return ret;

	ptwalk.pid = usr_ptwalk.pid;

	resolve_va(usr_ptwalk.va, &ptwalk, !mmap_sem_locked);
	ptwalk_to_user(&usr_ptwalk, &ptwalk);

	ret = to_user((void *)param, &usr_ptwalk, sizeof usr_ptwalk);
	if (ret < 0)
		return ret;

	return 0;
}

static long
ioctl_update(unsigned long param)
{
	struct mmuctl_ptwalk usr_ptwalk = {0};
	long ret;

	ret = copy_from_user(&usr_ptwalk, (void *)param, sizeof usr_ptwalk);

	if (ret < 0)
		return ret;

	printk(KERN_INFO "mmuctl: got pmd=%p\n", (void *)usr_ptwalk.pmd);

	return update_va(&usr_ptwalk, !mmap_sem_locked);
}

static long
ioctl_invlpg(unsigned long param)
{
	invlpg((void *)param);
	return 0;
}

static long
ioctl_local_invlpg(unsigned long param)
{
	local_invlpg((void *)param);
	return 0;
}

static long
ioctl_get_pat(unsigned long param)
{
#if defined(__i386__) || defined(__x86_64__)
	uint32_t lo, hi;
	uint64_t pat;

	asm volatile("rdmsr\n"
		: "=a" (lo), "=d" (hi)
		: "c" (0x277));

	pat = lo | (((uint64_t)hi) << 32);

	return to_user((void *)param, &pat, sizeof pat);
#elif defined(__aarch64__)
	uint64_t val;

	asm volatile("mrs %0, mair_el1\n"
		: "=r" (val));

	return to_user((void *)param, &val, sizeof val);
#endif
}

static void
set_pat(void *udata)
{
#if defined(__i386__) || defined(__x86_64__)
	uint32_t lo, hi;
	uint64_t pat = *(uint64_t *)udata;

	lo = pat & 0xffffffff;
	hi = (pat >> 32) & 0xffffffff;

	asm volatile("wrmsr\n"
		:: "a" (lo), "d" (hi), "c" (0x277));
#elif defined(__aarch64__)
	uint64_t pat = *(uint64_t *)pat;

	asm volatile("msr mair_el1, %0\n"
		:: "r" (pat));
#endif
}

static long
ioctl_set_pat(unsigned long param)
{
	uint64_t val = param;

	on_each_cpu(set_pat, &val, 1);

	return 0;
}

static long
ioctl_touch(unsigned long param)
{
	struct mmuctl_buf buf;
	size_t i;
	long ret;

	ret = copy_from_user(&buf, (void *)param, sizeof buf);

	if (ret < 0)
		return ret;

	for (i = 0; i < buf.len; ++i) {
		*(volatile char *)(buf.addr);
	}

	return 0;
}

static long
device_ioctl(struct file *file, unsigned int num, unsigned long param)
{
	switch (num) {
	case MMUCTL_GET_PAGE_SIZE: return PAGE_SIZE;
	case MMUCTL_LOCK: return ioctl_lock();
	case MMUCTL_UNLOCK: return ioctl_unlock();
	case MMUCTL_GET_ROOT: return ioctl_get_root(param);
	case MMUCTL_SET_ROOT: return ioctl_set_root(param);
	case MMUCTL_RESOLVE: return ioctl_resolve(param);
	case MMUCTL_UPDATE: return ioctl_update(param);
	case MMUCTL_INVLPG: return ioctl_invlpg(param);
	case MMUCTL_LOCAL_INVLPG: return ioctl_local_invlpg(param);
	case MMUCTL_GET_PAT: return ioctl_get_pat(param);
	case MMUCTL_SET_PAT: return ioctl_set_pat(param);
	case MMUCTL_TOUCH: return ioctl_touch(param);
	default: return -EINVAL;
	}
}

static struct file_operations fops = {
	.open = device_open,
	.release = device_release,
	.read = device_read,
	.write = device_write,
	.mmap = device_mmap,
	.llseek = device_lseek,
	.unlocked_ioctl = device_ioctl,
};

static struct miscdevice misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mmuctl",
	.fops = &fops,
	.mode = S_IRWXUGO,
};

int
init_module(void)
{
	int ret;

	ret = misc_register(&misc_dev);
	if (ret != 0) {
		printk(KERN_ALERT "mmuctl: failed to register device with %d\n", ret);
		return -1;
	}

	printk(KERN_INFO "mmuctl: initialized.\n");

	return 0;
}

void
cleanup_module(void)
{
	misc_deregister(&misc_dev);
	printk(KERN_INFO "mmuctl: cleaned up.\n");
}
