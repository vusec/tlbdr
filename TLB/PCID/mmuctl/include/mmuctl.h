#pragma once

#if defined(__i386__) || defined(__x86_64__)
#define PAGE_PRESENT       (1 << 0)
#define PAGE_WRITE         (1 << 1)
#define PAGE_USER          (1 << 2)
#define PAGE_WRITE_THROUGH (1 << 3)
#define PAGE_NO_CACHE      (1 << 4)
#define PAGE_ACCESSED      (1 << 5)
#define PAGE_DIRTY         (1 << 6)
#define PAGE_HUGE          (1 << 7)
#define PAGE_PAT           (1 << 7)
#define PAGE_GLOBAL        (1 << 8)
#define PAGE_NO_EXEC       (1ULL << 63)
#elif defined(__aarch64__)
#define PAGE_MAIR(x)          (((x) >> 2) & 0x7)
#define PAGE_SET_MAIR(x, t)   (((t) & 0x7) << 2)
#define PAGE_ACCESS(x)        (((x) >> 6)  & 0x3)
#define PAGE_SET_ACCESS(x, t) (((t) & 0x3) << 6)
#define PAGE_SHARED(x)        (((x) >> 8) & 0x3)
#define PAGE_SET_SHARED(x, t) (((t) & 0x3) << 8)
#define PAGE_ACCESSED         (1 << 10)
#define PAGE_NOT_GLOBAL       (1 << 11)
#define PAGE_CONTIGUOUS       (1 << 52)
#define PAGE_PRIV_EXEC_NEVER  (1 << 53)
#define PAGE_EXEC_NEVER       (1 << 54)
#endif

#if defined(__i386__) || defined(__x86_64__)
#define MEM_UNCACHEABLE     0
#define MEM_WRITE_COMBINE   1
#define MEM_WRITE_THROUGH   4
#define MEM_WRITE_PROTECT   5
#define MEM_WRITE_BACK      6
#define MEM_UNCACHEABLE_MIN 7
#elif defined(__aarch64__)
#define MEM_UNCACHEABLE     0x44
#define MEM_WRITE_THROUGH   0xbb
#define MEM_WRITE_BACK      0xff
#endif

#define MMUCTL_PGD (1 << 0)
#define MMUCTL_P4D (1 << 1)
#define MMUCTL_PUD (1 << 2)
#define MMUCTL_PMD (1 << 3)
#define MMUCTL_PTE (1 << 4)

struct mmuctl {
	int fd;
};

struct mmuctl_page {
	uint64_t addr;
};

struct mmuctl_root {
	int pid;
	unsigned long root;
};

struct mmuctl_ptwalk {
	int pid;
	unsigned long va;
	unsigned long pgd;
	unsigned long p4d;
	unsigned long pud;
	unsigned long pmd;
	unsigned long pte;
	unsigned valid;
};

struct mmuctl_buf {
	void *addr;
	size_t len;
};

int
mmuctl_init(struct mmuctl *ctx);
void
mmuctl_cleanup(struct mmuctl *ctx);
size_t
mmuctl_get_page_size(struct mmuctl *ctx);
int
mmuctl_lock(struct mmuctl *ctx);
int
mmuctl_unlock(struct mmuctl *ctx);
ssize_t
mmuctl_read(struct mmuctl *ctx, void *buf, size_t len, uint64_t addr);
ssize_t
mmuctl_write(struct mmuctl *ctx, const void *buf, size_t len, uint64_t addr);
uint64_t
mmuctl_get_root(struct mmuctl *ctx, pid_t pid);
int
mmuctl_set_root(struct mmuctl *ctx, pid_t pid, uint64_t pa);
int
mmuctl_resolve(struct mmuctl *ctx, struct mmuctl_ptwalk *ptwalk, void *va,
	pid_t pid);
int
mmuctl_update(struct mmuctl *ctx, struct mmuctl_ptwalk *ptwalk, void *va,
	pid_t pid);
int
mmuctl_invlpg(struct mmuctl *ctx, void *addr);
int
mmuctl_local_invlpg(struct mmuctl *ctx, void *addr);
unsigned long
mmuctl_get_pat(struct mmuctl *ctx);
int mmuctl_set_pat(struct mmuctl *ctx, unsigned long pat);
unsigned char
mmuctl_find_mem_type(struct mmuctl *ctx, unsigned char mem_type);
unsigned char
mmuctl_get_mem_type(unsigned long entry);
unsigned long
mmuctl_set_mem_type(unsigned long entry, unsigned char mem_type);
int
mmuctl_touch(struct mmuctl *ctx, void *addr, size_t len);
void
mmuctl_print_walk(struct mmuctl_ptwalk *walk);
