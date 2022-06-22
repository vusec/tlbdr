#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <linux/highmem.h>
#include <linux/memory.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/version.h>
#include <ioctl.h>
#include <mmuctl.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/kthread.h>

MODULE_AUTHOR("Daniel Trujillo, with contributions from Stephan van Schaik and Andrei Tatar");
MODULE_DESCRIPTION("A kernel module for testing TLB properties");
MODULE_LICENSE("GPL");

#define BASE ((void *)0x133800000000ULL)

/* IMPORTANT: define the two logical cores here that are sharing the TLB*/
#define CORE1 0
#define CORE2 2

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

typedef u64 (*retf_t)(void);

static struct mm_struct * get_mm(int pid){
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

static int resolve_va(size_t addr, struct ptwalk *entry, int lock){
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
		down_read(&mm->mmap_sem);

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
	if (pud_none(*pud) || pud_bad(*pud))
		goto err;

	entry->pud = pud;
	entry->valid |= MMUCTL_PUD;

	pmd = pmd_offset(pud, addr);

	if (pmd_none(*pmd) || pmd_bad(*pmd))
		goto err;

	entry->pmd = pmd;
	entry->valid = MMUCTL_PMD;

	pte = pte_offset_map(pmd, addr);

	entry->pte = pte;
	entry->valid |= MMUCTL_PTE;

	pte_unmap(pte);

	if (lock)
		up_read(&mm->mmap_sem);

	return 0;

err:
	if (lock)
		up_read(&mm->mmap_sem);

	return -1;
}

static inline void switch_pages(pte_t *pte1, pte_t *pte2)
{
	u64 ptev = pte1->pte;
	pte1->pte = pte2->pte;
	pte2->pte = ptev;
}

static inline u64 setcr3(u64 val)
{
	u64 r;
	asm volatile ("mov %0, %%cr3" :: "r" (val));
	asm volatile ("mov %%cr3, %0" : "=r" (r));
	return r;
}

static inline u64 getcr3(void){
	u64 cr3k;
	asm volatile ("mov %%cr3, %0" : "=r" (cr3k));
	return cr3k;
}

static inline void disable_smep(void){
	u64 cr4v;
	asm volatile ("mov %%cr4, %0" : "=r" (cr4v));
	asm volatile ("mov %0, %%cr4" :: "r" (cr4v & (~(1ULL << 20))));
}

static inline void claim_cpu(void){
	unsigned long flags;
	preempt_disable();
	raw_local_irq_save(flags);
}

static inline void give_up_cpu(void){
	unsigned long flags;
	raw_local_irq_restore(flags);
	preempt_enable();
}


int pid1 = 1;
int pid2 = 2;

static DECLARE_COMPLETION(thread_done_dtlb);
static DECLARE_COMPLETION(thread_done_stlb);
static DECLARE_COMPLETION(thread_done_itlb);

volatile int condition;

struct task_struct *task1;
struct task_struct *task2;

unsigned char *addr;

volatile int cpu_thread_0_claimed = 0;
volatile int cpu_thread_1_claimed = 0;

volatile int success = 0;
volatile int val, total;

/*
	This function determines whether PCID entries of iTLB entries are shared across threads.
	This function is called by two hyperthreads.
	Hyperthread 2 is trying to evict a TLB entry of hyperthread 1
	out of the iTLB by switching to all possible PCIDs.
*/
static int pcid_itlb(void *data){
	int i;

	claim_cpu();
	disable_smep();

    	int *thread_id = (int*)data;

	if(*thread_id == 1){
		cpu_thread_1_claimed = 1;

		while(cpu_thread_0_claimed != 1){
		}
	}else{
		cpu_thread_0_claimed = 1;

		while(cpu_thread_1_claimed != 1){
		}
	}

	//At this point, both threads have claimed their CPU

	while(total < 1000){
		if(*thread_id == 1)
		{
				while(condition != 0){

				}

				volatile u64 cr3k = getcr3();

				//Get a random page, which is not at the end of a page table
				volatile unsigned long rand_addr;
				get_random_bytes(&rand_addr, sizeof(rand_addr));
				rand_addr = addr + (4096 * (rand_addr % 100));

				int difference = (rand_addr - (unsigned long)BASE) / 4096;
				while(difference % 512 == 511){
					get_random_bytes(&rand_addr, sizeof(rand_addr));
					rand_addr = addr + (4096 * (rand_addr % 100));
					difference = (rand_addr - (unsigned long)BASE) / 4096;
				}

				//Perform the page walk
				volatile struct ptwalk walk;
				walk.pid = 0;
				resolve_va(rand_addr, &walk, 0);

				//We prime using PCID 0
				//Read the current value of the page
				setcr3(((cr3k >> 12) << 12) | 0);
				val = ((retf_t)rand_addr)();
				//We directly switch to PCID 1 (using NOFLUSH as iTLB will be flushed otherwise)
				setcr3(((cr3k >> 12) << 12) | CR3_NOFLUSH | 1);

				asm volatile("mfence\n\t" ::: "memory");
				//Desync the TLB
				switch_pages(walk.pte, walk.pte + 1);

				//Let the other hyperthread enumerate over all PCIDs
				condition = 1;

				while(condition != 0){
					((retf_t)rand_addr)();
				}

				//Evict TLB entry from sTLB
				for(i = 0; i < 10000; i++){
					*((volatile unsigned long *)(addr + (4096 * (i + 100))));
				}

				asm volatile("mfence\n\t" ::: "memory");

				//We get back to PCID 0, which could have been evicted from the PCID cache by PCID switches on other thread
				//as PCID 0 was not active on this thread
				setcr3(((cr3k >> 12) << 12) | CR3_NOFLUSH | 0);


				//Check if the TLB entry was still cached in the iTLB
				volatile int curr = ((retf_t)rand_addr)();

				if(curr == val){
					success++;
				}

				setcr3(cr3k);

				asm volatile("mfence\n\t" ::: "memory");
				switch_pages(walk.pte, walk.pte + 1);
		}else{
				while(condition != 1){

				}

				volatile u64 cr3k = getcr3();

				//Enumerate over all PCIDs to try kick out the thread's entry
				unsigned long pcid;
				for(pcid = 0; pcid < 4096; pcid++){
					setcr3((cr3k >> 12) << 12 | pcid);
				}

				setcr3(cr3k);

				total++;
				condition = 0;
		}
	}

	give_up_cpu();

	if(*thread_id == 1){
		cpu_thread_1_claimed = 0;

		while(cpu_thread_0_claimed != 0){
		}
	}else{
		cpu_thread_0_claimed = 0;

		while(cpu_thread_1_claimed != 0){
		}
	}

	complete_and_exit(&thread_done_itlb, 0);

	return 0;
}

/*
	This function determines whether PCID entries of dTLB entries are shared across threads.
	This function is called by two hyperthreads.
	Hyperthread 2 is trying to evict a TLB entry of hyperthread 1
	out of the dTLB by switching to all possible PCIDs.
*/
static int pcid_dtlb(void *data){
	int i;

	claim_cpu();
	disable_smep();

	int *thread_id = (int*)data;

	if(*thread_id == 1){
		cpu_thread_1_claimed = 1;

		while(cpu_thread_0_claimed != 1){
		}
	}else{
		cpu_thread_0_claimed = 1;

		while(cpu_thread_1_claimed != 1){
		}
	}

	//At this point, both threads have claimed their CPU

	while(total < 1000){
		if(*thread_id == 1)
		{
				while(condition != 0){

				}

				volatile u64 cr3k = getcr3();

				//Get a random page, which is not at the end of a page table
				volatile unsigned long rand_addr;
				get_random_bytes(&rand_addr, sizeof(rand_addr));
				rand_addr = addr + (4096 * (rand_addr % 100));

				int difference = (rand_addr - (unsigned long)BASE) / 4096;
				while(difference % 512 == 511){
					get_random_bytes(&rand_addr, sizeof(rand_addr));
					rand_addr = addr + (4096 * (rand_addr % 100));
					difference = (rand_addr - (unsigned long)BASE) / 4096;
				}

				//Perform the page walk
				volatile struct ptwalk walk;
				walk.pid = 0;
				resolve_va(rand_addr, &walk, 0);

				//Read the current value of the page
				val = *((volatile uint64_t *)&((volatile char *)(volatile void *)rand_addr)[4]);

				asm volatile("mfence\n\t" ::: "memory");

				//Desync the TLB
				switch_pages(walk.pte, walk.pte + 1);

				//Let the other hyperthread enumerate over all PCIDs
				condition = 1;

				while(condition != 0){
					*((volatile unsigned long *)rand_addr);
				}

				//Evict TLB entry from sTLB
				for(i = 0; i < 10000; i++){
					((retf_t)(addr + (4096 * (i + 100))))();
				}

				//Check if the TLB entry was still cached in the dTLB
				asm volatile("mfence\n\t" ::: "memory");
				volatile int curr = *((volatile uint64_t *)&((volatile char *)(volatile void *)rand_addr)[4]);

				if(curr == val){
					success++;
				}

				setcr3(cr3k);

				asm volatile("mfence\n\t" ::: "memory");
				switch_pages(walk.pte, walk.pte + 1);
		}else{
				while(condition != 1){

				}

				volatile u64 cr3k = getcr3();

				//Enumerate over all PCIDs to try kick out the thread's entry
				unsigned long pcid;
				for(pcid = 0; pcid < 4096; pcid++){
					setcr3((cr3k >> 12) << 12 | pcid);
				}

				setcr3(cr3k);

				total++;
				condition = 0;
		}
	}

	give_up_cpu();

	if(*thread_id == 1){
		cpu_thread_1_claimed = 0;

		while(cpu_thread_0_claimed != 0){
		}
	}else{
		cpu_thread_0_claimed = 0;

		while(cpu_thread_1_claimed != 0){
		}
	}

	complete_and_exit(&thread_done_dtlb, 0);

	return 0;
}

/*
	This function determines whether PCID entries of sTLB entries are shared across threads.
	This function is called by two hyperthreads.
	Hyperthread 2 is trying to evict a TLB entry of hyperthread 1
	out of the sTLB by switching to all possible PCIDs.
*/
static int pcid_stlb(void *data){
	int i;

	claim_cpu();
	disable_smep();

    	int *thread_id = (int*)data;

	if(*thread_id == 1){
		cpu_thread_1_claimed = 1;

		while(cpu_thread_0_claimed != 1){
		}
	}else{
		cpu_thread_0_claimed = 1;

		while(cpu_thread_1_claimed != 1){
		}
	}

	//At this point, both threads have claimed their CPU

	while(total < 1000){
		if(*thread_id == 1)
		{
				while(condition != 0){

				}

				volatile u64 cr3k = getcr3();

				//Get a random page, which is not at the end of a page table				
				volatile unsigned long rand_addr;
				get_random_bytes(&rand_addr, sizeof(rand_addr));
				rand_addr = addr + (4096 * (rand_addr % 100));

				int difference = (rand_addr - (unsigned long)BASE) / 4096;
				while(difference % 512 == 511){
					get_random_bytes(&rand_addr, sizeof(rand_addr));
					rand_addr = addr + (4096 * (rand_addr % 100));
					difference = (rand_addr - (unsigned long)BASE) / 4096;
				}

				//Perform the page walk
				volatile struct ptwalk walk;
				walk.pid = 0;
				int r = resolve_va(rand_addr, &walk, 0);

				//We prime using PCID 0
				//Read the current value of the page
				setcr3(((cr3k >> 12) << 12) | 0);
				val = *((volatile uint64_t *)&((volatile char *)(volatile void *)rand_addr)[4]);
				//We set PCID 1 to make PCID 0 vulnerable to eviction
				setcr3(((cr3k >> 12) << 12) | CR3_NOFLUSH | 1);

				asm volatile("mfence\n\t" ::: "memory");
				//Desync the TLB
				switch_pages(walk.pte, walk.pte + 1);

				condition = 1;

				while(condition != 0){
					*((volatile unsigned long *)rand_addr);
				}

				asm volatile("mfence\n\t" ::: "memory");

				//We swtich back to PCID 0 which was not active while other thread was switching PCIDs
				//PCID 0 was hence vulnerable for eviction from the PCID cache
				setcr3(((cr3k >> 12) << 12) | CR3_NOFLUSH | 0);

				//Check if the TLB entry was still cached in the sTLB
				volatile int curr = ((retf_t)rand_addr)();

				asm volatile (
					"clflush (%0)\n\t"
					"invlpg (%0)\n\t"
					:: "r" (rand_addr)
				);

				if(curr == val){
					success++;
				}

				setcr3(cr3k);

				asm volatile("mfence\n\t" ::: "memory");
				switch_pages(walk.pte, walk.pte + 1);

		}else{
				while(condition != 1){

				}

				volatile u64 cr3k = getcr3();

				//Enumerate over all PCIDs to try kick out the thread's entry
				unsigned long pcid;
				for(pcid = 0; pcid < 4096; pcid++){
					setcr3((cr3k >> 12) << 12 | pcid);
				}

				setcr3(cr3k);

				total++;
				condition = 0;
		}
	}

	give_up_cpu();

	if(*thread_id == 1){
		cpu_thread_1_claimed = 0;

		while(cpu_thread_0_claimed != 0){
		}
	}else{
		cpu_thread_0_claimed = 0;

		while(cpu_thread_1_claimed != 0){
		}
	}

	complete_and_exit(&thread_done_stlb, 0);

	return 0;
}

/*
	This function tests whether the iTLB shares its PCID entries across hyperthreads.
	This function is a wrapper function of pcid_itlb(). It creates the two threads
	on co-resident hyperthreads and starts them.
*/
void test_pcid_itlb(void){
	//Thread 0 is supposed to start
	condition = 0;
	total = 0;
	success = 0;

	//Create thread 1
	task1 = kthread_create(&pcid_itlb, (void *)&pid1, "PCID");

	if (IS_ERR(task1)) {
	    	complete(&thread_done_itlb);
	    	int ret = PTR_ERR(task1);
	    	return ret;
	}

	kthread_bind(task1, CORE1);

	//Create thread 2
	task2 = kthread_create(&pcid_itlb, (void *)&pid2, "PCID");

	if (IS_ERR(task2)) {
	    	complete(&thread_done_itlb);
	    	int ret = PTR_ERR(task2);
	    	return ret;
	}

	kthread_bind(task2, CORE2);

	//Wake them up
	wake_up_process(task1);
	wake_up_process(task2);

	wait_for_completion(&thread_done_itlb);

	printk("Success with ITLB is %d\n", success);
}

/*
	This function tests whether the dTLB shares its PCID entries across hyperthreads.
	This function is a wrapper function of pcid_dtlb(). It creates the two threads
	on co-resident hyperthreads and starts them.
*/
void test_pcid_dtlb(void){
	//Thread 0 is supposed to start
	condition = 0;
	total = 0;
	success = 0;

	//Create thread 1
	task1 = kthread_create(&pcid_dtlb, (void *)&pid1, "PCID");

	if (IS_ERR(task1)) {
	    	complete(&thread_done_dtlb);
	    	int ret = PTR_ERR(task1);
	    	return ret;
	}

	kthread_bind(task1, CORE1);

	//Create thread 2
	task2 = kthread_create(&pcid_dtlb, (void *)&pid2, "PCID");

	if (IS_ERR(task2)) {
	    	complete(&thread_done_dtlb);
	    	int ret = PTR_ERR(task2);
	    	return ret;
	}

	kthread_bind(task2, CORE2);

	//Wake them up
	wake_up_process(task1);
	wake_up_process(task2);

	wait_for_completion(&thread_done_dtlb);

	printk("Success with DTLB is %d\n", success);
}

/*
	This function tests whether the sTLB shares its PCID entries across hyperthreads.
	This function is a wrapper function of pcid_stlb(). It creates the two threads
	on co-resident hyperthreads and starts them.
*/
void test_pcid_stlb(void){
	//Thread 0 is supposed to start
	condition = 0;
	total = 0;
	success = 0;

	//Create thread 1
	task1 = kthread_create(&pcid_stlb, (void *)&pid1, "PCID");

	if (IS_ERR(task1)) {
	    	complete(&thread_done_stlb);
	    	int ret = PTR_ERR(task1);
	    	return ret;
	}

	kthread_bind(task1, CORE1);

	//Create thread 2
	task2 = kthread_create(&pcid_stlb, (void *)&pid2, "PCID");

	if (IS_ERR(task2)) {
	    	complete(&thread_done_stlb);
	    	int ret = PTR_ERR(task2);
	    	return ret;
	}

	kthread_bind(task2, CORE2);

	//Wake them up
	wake_up_process(task1);
	wake_up_process(task2);

	wait_for_completion(&thread_done_stlb);

	printk("Success with STLB is %d\n", success);
}


/*
	This is the entry point of the experiments.
	It finds whether the dTLB, sTLB and iTLB shares their PCID entries.
*/
static int device_open(struct inode *inode, struct file *file){
	addr = __vmalloc(4096 * 10100, GFP_KERNEL, PAGE_KERNEL_EXEC);
	volatile unsigned int i;

	//Write an identifier to each page
	for(i = 0; i < 10100; i++){
		volatile unsigned char *p1 = addr + (4096 * i);
		*(uint16_t *)p1 = 0x9090;
		p1[2] = 0x48; p1[3] = 0xb8;
		*(uint64_t *)(&p1[4]) = i;
		p1[12] = 0xc3;
	}

	//Testing whether there is dTLB PCID interference
	test_pcid_dtlb();

	//Testing whether there is sTLB PCID interference
	test_pcid_stlb();

	//Testing whether there is iTLB PCID interference
	test_pcid_itlb();

	vfree(addr);

	return 0;
}

static struct file_operations fops = {
	.open = device_open,
	.release = NULL,
	.read = NULL,
	.write = NULL,
	.mmap = NULL,
	.llseek = NULL,
	.unlocked_ioctl = NULL,
};

static struct miscdevice misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mmuctl",
	.fops = &fops,
	.mode = S_IRWXUGO,
};

int init_module(void){
	int ret;

	ret = misc_register(&misc_dev);
	if (ret != 0) {
		printk(KERN_ALERT "mmuctl: failed to register device with %d\n", ret);
		return -1;
	}

	printk(KERN_INFO "mmuctl: initialized.\n");

	return 0;
}

void cleanup_module(void){
	misc_deregister(&misc_dev);
	printk(KERN_INFO "mmuctl: cleaned up.\n");
}
