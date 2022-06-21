#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include "mmuctl.h"
#include "xbs.h"

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define PFNBITS (~((1L << 63) | 0xfffL))

#define PTEIDX_BITS (0x1ff000)

static inline uintptr_t pteidx(void *addr)
{
	return ((uintptr_t)addr & PTEIDX_BITS) >> 12;
}

static inline uintptr_t pteaddr(void *addr, unsigned long pmd)
{
	return (pmd & PFNBITS) + pteidx(addr) * 8;
}

#define CHLINE(x) ((x) >> 6)

#define L3SLMASK (7)
static int l3slice(uintptr_t p)
{
	int s0 = xbs64(p & 0x1b5f575440);
	int s1 = xbs64(p & 0x0eb5faa880) << 1;
	int s2 = xbs64(p & 0x1cccc93100) << 2;
	return (s2|s1|s0) & L3SLMASK;
}

static inline uintptr_t CL1(uintptr_t pa) {return CHLINE(pa) & 0x3f;}
static inline uintptr_t CL2(uintptr_t pa) {return CHLINE(pa) & 0x3ff;}
static inline uintptr_t CL3(uintptr_t pa)
{
	return (CHLINE(pa) & 0x3ff) | (l3slice(pa) << 10);
}

#define TLLINE(x) ((x) >> 12)

#if 0 // Haswell

static inline uintptr_t TDL1(uintptr_t va) {return TLLINE(va) & 0xf;}
static inline uintptr_t TIL1(uintptr_t va) {return TLLINE(va) & 0x7;}
static inline uintptr_t TSL2(uintptr_t va) {return TLLINE(va) & 0x7f;}

#else // Kabylake

static inline uintptr_t TDL1(uintptr_t va) {return TLLINE(va) & 0xf;}
static inline uintptr_t TIL1(uintptr_t va) {return TLLINE(va) & 0x7;}
static inline uintptr_t TSL2(uintptr_t va)
{
	uintptr_t p = TLLINE(va);
	return (p ^ (p >> 7)) & 0x7f;
}

#endif


int32_t tload(void **addr)
{
	int32_t ret;
	asm volatile (
		"lfence\n"
		"rdtsc\n"
		"mov %%eax, %%esi\n"
		"mov (%[probe]),%[probe]\n"
		"lfence\n"
		"rdtscp\n"
		"sub %%esi, %%eax\n"
		: [probe]"+r" (addr), "=a" (ret)
		:
		: "rdx", "rsi", "rcx", "cc"
	);
	return ret;
}

#define PAGESZ (4096)
#define EVBN (1L << 16)
#define EVBSZ (EVBN * PAGESZ)

#define _K (1024)
#define _M (_K * _K)
#define _G (_K * _M)

#define MAP_HUGE_2MB    (21 << MAP_HUGE_SHIFT)
#define MAP_HUGE_1GB    (30 << MAP_HUGE_SHIFT)

static void *setup_tlbevbuf(void)
{
	void *ret = mmap(NULL, EVBSZ, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
	if (ret == MAP_FAILED) {
		perror("TLBEVB fail!");
		exit(1);
	}
	return ret;
}
static void *setup_cacheevbuf(void)
{
	void *ret = mmap(NULL, 1*_G, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB|MAP_HUGE_1GB|MAP_POPULATE, -1, 0);
	//void *ret = mmap(NULL, 64*_M, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB|MAP_HUGE_2MB|MAP_POPULATE, -1, 0);
	//void *ret = mmap(NULL, 64*_M, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
	if (ret == MAP_FAILED) {
		perror("CACHEEVB fail!");
		exit(1);
	}
	return ret;
}

/* TLB ev */

typedef void *(nexthit_f)(void *, uintptr_t, uintptr_t);

static void *tlb_nexthit(void *cur, uintptr_t l1t, uintptr_t l2t)
{
	uintptr_t p = (uintptr_t)cur;
	do {
		p += PAGESZ;
	} while (TDL1(p) != l1t || TSL2(p) != l2t);
	return (void *)p;
}
//static void *nexthitl1(void *cur, uintptr_t l1t, uintptr_t l2t)
//{
	//uintptr_t p = (uintptr_t)cur;
	//do {
		//p += PAGESZ;
	//} while (TDL1(p) != l1t || TSL2(p) == l2t);
	//return (void *)p;
//}
//static void *nexthitl2(void *cur, uintptr_t l1t, uintptr_t l2t)
//{
	//uintptr_t p = (uintptr_t)cur;
	//do {
		//p += PAGESZ;
	//} while (TDL1(p) == l1t || TSL2(p) != l2t);
	//return (void *)p;
//}

static inline void tlb_evrun(void *base, uintptr_t targ, size_t n, nexthit_f nhf)
{
	uintptr_t l1t = TDL1(targ);
	uintptr_t l2t = TSL2(targ);
	void *p = base;
	for (size_t i = 0; i < n; i++) {
		p = nhf(p, l1t, l2t);
		//fprintf(stderr, "*%p\n", p);
		//(void)tload((void **)p);
		*(volatile char *)p;
	}
}

/* Cache ev */

#define CACHE_W1 (8)
#define CACHE_W2 (4)
#define CACHE_W3 (17)

static uintptr_t cevpa = 0;
#define cev2p(x) (((x) & 0x3fffffff) | cevpa)

static void *cache_nexthit(void *cur, uintptr_t l1t, uintptr_t l2t, uintptr_t l3t,
                           unsigned *l1n, unsigned *l2n, unsigned *l3n)
{
	uintptr_t p = (uintptr_t)cur;
	do {
		p += PAGESZ;
		int b1, b2, b3;
		b1 = b2 = b3 = 0;
		int m1 = CL1(cev2p(p)) == l1t;
		int m2 = CL2(cev2p(p)) == l2t;
		int m3 = CL3(cev2p(p)) == l3t;
		if (*l1n && m1) {
			(*l1n)--;
			b1 = 1;
		}
		if (*l2n && m2) {
			(*l2n)--;
			b2 = 1;
		}
		if (*l3n && m3) {
			(*l3n)--;
			b3 = 1;
		}
		if (b1 || b2 || b3)
			break;
	//} while (CL1(p) != l1t || CL2(p) != l2t || CL3(p) != l3t);
	} while (1);
	return (void *)p;
}
static void *cache_nexthita(void *cur, uintptr_t l1t, uintptr_t l2t, uintptr_t l3t)
{
	uintptr_t p = (uintptr_t)cur;
	do {
		p += PAGESZ;
		int m1 = CL1(cev2p(p)) == l1t;
		int m2 = CL2(cev2p(p)) == l2t;
		int m3 = CL3(cev2p(p)) == l3t;
		if (m1 && m2 && m3) {
			//if (*l1n && m1)
				//(*l1n)--;
			//if (*l2n && m2)
				//(*l2n)--;
			//if (*l3n && m3)
				//(*l3n)--;
			break;
		}
	} while (1);
	return (void *)p;
}

static inline void cache_evrun(void *base, uintptr_t targ)
{
	size_t cnt = 0;
	uintptr_t l1t = CL1(targ);
	uintptr_t l2t = CL2(targ);
	uintptr_t l3t = CL3(targ);
	unsigned l1w = CACHE_W1;
	unsigned l2w = CACHE_W2;
	unsigned l3w = CACHE_W3;
	void *p = base;
	//while (l1w || l2w || l3w) {
	while (l3w) {
		//p = cache_nexthit(p, l1t, l2t, l3t, &l1w, &l2w, &l3w);
		p = cache_nexthita(p, l1t, l2t, l3t);
		//p = cache_nexthita(p, l1t, l2t, l3t);
		l3w--;
		//fprintf(stderr, "*%p\n", p);
		//fprintf(stderr, "# %u %u %u\n", l1w, l2w, l3w);
		//(void)tload((void **)p);
		*(volatile char *)p;
	}
	//fprintf(stderr, "&%zu\n", cnt);
}

/* TLB Prep */

#define TLB_PREPSZ (12)

static void **tlb_prepnaive(void *base, uintptr_t targ)
{
	uintptr_t l1t = TDL1(targ);
	uintptr_t l2t = TSL2(targ);
	fprintf(stderr, "%" PRIxPTR " %" PRIxPTR "\n", l1t, l2t);
	void *p = base;
	void **ev[TLB_PREPSZ];
	for (int i = 0; i < TLB_PREPSZ; i++) {
		ev[i] = (void **)tlb_nexthit(p, l1t, l2t);
		p = ev[i];
	}
	for (int i = 0; i < TLB_PREPSZ - 1; i++) {
		*(ev[i]) = ev[i+1];
	}
	*(ev[TLB_PREPSZ-1]) = ev[0];
	assert((uintptr_t)base + EVBSZ > (uintptr_t)ev[TLB_PREPSZ-1]);
	return ev[0];
}

#if 0 // Haswell BFS ninja

static void **tlb_prepninja(void *base, uintptr_t targ)
{
	uintptr_t l1t = TDL1(targ);
	uintptr_t l2t = TSL2(targ);
	void *p = base;
	void **ev[8];
	for (int i = 0; i < 8; i++) {
		p = tlb_nexthit(p, l1t, l2t);
		ev[i] = (void **)p;
	}
	ev[0][0] = &ev[1][0];
	ev[0][1] = &ev[6][0];
	ev[0][2] = &ev[1][2];
	ev[0][3] = &ev[6][2];

	ev[1][0] = &ev[2][0];
	ev[1][1] = &ev[4][2];
	ev[1][2] = &ev[2][2];

	ev[2][0] = &ev[3][0];
	ev[2][1] = &ev[4][1];
	ev[2][2] = &ev[3][2];

	ev[3][0] = &ev[4][0];
	ev[3][1] = &ev[7][1];
	ev[3][2] = &ev[5][2];

	ev[4][0] = &ev[5][0];
	ev[4][1] = &ev[5][1];
	ev[4][2] = &ev[3][1];

	ev[5][0] = &ev[0][1];
	ev[5][1] = &ev[7][0];
	ev[5][2] = &ev[0][3];

	ev[6][0] = &ev[2][1];
	ev[6][1] = &ev[1][1];
	ev[6][2] = &ev[2][1];

	ev[7][0] = &ev[6][1];
	ev[7][1] = &ev[0][2];


	//ev[0][0] = &ev[1][0];
	//ev[0][1] = &ev[6][0];
	//ev[0][2] = &ev[1][2];

	//ev[1][0] = &ev[2][0];
	//ev[1][1] = &ev[4][2];
	//ev[1][2] = &ev[2][2];

	//ev[2][0] = &ev[3][0];
	//ev[2][1] = &ev[4][1];
	//ev[2][2] = &ev[3][2];

	//ev[3][0] = &ev[4][0];
	//ev[3][1] = &ev[7][1];
	//ev[3][2] = &ev[5][0]; // <-- looplink here

	//ev[4][0] = &ev[5][0];
	//ev[4][1] = &ev[5][1];
	//ev[4][2] = &ev[3][1];

	//ev[5][0] = &ev[0][1];
	//ev[5][1] = &ev[7][0];

	//ev[6][0] = &ev[2][1];
	//ev[6][1] = &ev[1][1];

	//ev[7][0] = &ev[6][1];
	//ev[7][1] = &ev[0][2];

	return ev[0];
}

#elif 0 // Haswell A* ninja

static void **tlb_prepninja(void *base, uintptr_t targ)
{
	uintptr_t l1t = TDL1(targ);
	uintptr_t l2t = TSL2(targ);
	void *p = base;
	void **ev[7];
	for (int i = 0; i < 7; i++) {
		p = tlb_nexthit(p, l1t, l2t);
		ev[i] = (void **)p;
	}
	ev[0][0] = &ev[1][0];
	ev[0][1] = &ev[2][0];
	ev[0][2] = &ev[3][0];
	ev[0][3] = &ev[1][2];
	ev[0][4] = &ev[3][2];
	ev[0][5] = &ev[5][2];
	ev[0][6] = &ev[1][7];
	ev[0][7] = &ev[6][4];

	ev[1][0] = &ev[0][1];
	ev[1][1] = &ev[4][0];
	ev[1][2] = &ev[6][0];
	ev[1][3] = &ev[6][1];
	ev[1][4] = &ev[0][4];
	ev[1][5] = &ev[0][5];
	ev[1][6] = &ev[2][3];
	ev[1][7] = &ev[2][4];
	ev[1][8] = &ev[4][4];

	ev[2][0] = &ev[0][2];
	ev[2][1] = &ev[0][3];
	ev[2][2] = &ev[1][4];
	ev[2][3] = &ev[5][3];
	ev[2][4] = &ev[3][3];
	ev[2][5] = &ev[1][2]; // <-- looplink here

	ev[3][0] = &ev[1][1];
	ev[3][1] = &ev[4][2];
	ev[3][2] = &ev[6][2];
	ev[3][3] = &ev[0][7];
	ev[3][4] = &ev[2][5];

	ev[4][0] = &ev[5][0];
	ev[4][1] = &ev[1][3];
	ev[4][2] = &ev[2][2];
	ev[4][3] = &ev[1][6];
	ev[4][4] = &ev[3][4];

	ev[5][0] = &ev[2][1];
	ev[5][1] = &ev[4][1];
	ev[5][2] = &ev[6][3];
	ev[5][3] = &ev[0][6];

	ev[6][0] = &ev[5][1];
	ev[6][1] = &ev[3][1];
	ev[6][2] = &ev[1][5];
	ev[6][3] = &ev[4][3];
	ev[6][4] = &ev[1][8];

	return ev[0];
}

#else // Kabylake

static void **tlb_prepninja(void *base, uintptr_t targ)
{
	uintptr_t l1t = TDL1(targ);
	uintptr_t l2t = TSL2(targ);
	void *p = base;
	void **ev[9];
	for (int i = 0; i < 9; i++) {
		p = tlb_nexthit(p, l1t, l2t);
		ev[i] = (void **)p;
	}
	ev[0][0] = &ev[1][0];
	ev[0][1] = &ev[2][1];
	ev[0][2] = &ev[2][2];
	ev[0][3] = &ev[8][0];
	ev[0][4] = &ev[2][4];
	ev[0][5] = &ev[4][2];

	ev[1][0] = &ev[2][0];
	ev[1][1] = &ev[6][0];
	ev[1][2] = &ev[0][3];
	ev[1][3] = &ev[5][1];
	ev[1][4] = &ev[0][5];
	ev[1][5] = &ev[6][0]; // <-- looplink here

	ev[2][0] = &ev[3][0];
	ev[2][1] = &ev[1][1];
	ev[2][2] = &ev[7][0];
	ev[2][3] = &ev[1][3];
	ev[2][4] = &ev[3][2];
	ev[2][5] = &ev[1][5];

	ev[3][0] = &ev[4][0];
	ev[3][1] = &ev[0][2];
	ev[3][2] = &ev[8][1];

	ev[4][0] = &ev[5][0];
	ev[4][1] = &ev[1][2];
	ev[4][2] = &ev[5][2];

	ev[5][0] = &ev[0][1];
	ev[5][1] = &ev[7][1];
	ev[5][2] = &ev[2][1]; // was [2][5]

	ev[6][0] = &ev[3][1];
	ev[6][1] = &ev[2][3];

	ev[7][0] = &ev[4][1];
	ev[7][1] = &ev[0][4];

	ev[8][0] = &ev[6][1];
	ev[8][1] = &ev[1][4];

	return ev[0];
}

#endif

/* Cache prep */

#define CACHE_PREPSZ (CACHE_W3)

static void **cache_prepnaive(void *base, uintptr_t targ)
{
	uintptr_t l1t = CL1(targ);
	uintptr_t l2t = CL2(targ);
	uintptr_t l3t = CL3(targ);
	fprintf(stderr, "%" PRIxPTR " %" PRIxPTR " %" PRIxPTR "\n", l1t, l2t, l3t);
	void *p = base;
	void **ev[CACHE_PREPSZ];
	for (int i = 0; i < CACHE_PREPSZ; i++) {
		ev[i] = (void **)cache_nexthita(p, l1t, l2t, l3t);
		p = ev[i];
	}
	for (int i = 0; i < CACHE_PREPSZ - 1; i++) {
		*(ev[i]) = ev[i+1];
	}
	*(ev[TLB_PREPSZ-1]) = ev[0];

	return ev[0];
}

static void **cache_prepnotlb(void *base, uintptr_t targ, uintptr_t tt1, uintptr_t tt2)
{
	uintptr_t l1t = CL1(targ);
	uintptr_t l2t = CL2(targ);
	uintptr_t l3t = CL3(targ);
	uintptr_t t11 = TDL1(tt1);
	uintptr_t t12 = TSL2(tt1);
	uintptr_t t21 = TDL1(tt2);
	uintptr_t t22 = TSL2(tt2);
	fprintf(stderr, "%" PRIxPTR " %" PRIxPTR " %" PRIxPTR " | %" PRIxPTR " %" PRIxPTR " %" PRIxPTR " %" PRIxPTR "\n",
			l1t, l2t, l3t, t11, t12, t21, t22);
	void *p = base;
	void **ev[CACHE_PREPSZ];
	int i = CACHE_PREPSZ;
	for (int i = 0; i < CACHE_PREPSZ;) {
		p = cache_nexthita(p, l1t, l2t, l3t);
		uintptr_t nl1 = TDL1((uintptr_t)p);
		uintptr_t nl2 = TSL2((uintptr_t)p);
		if (nl1 == t11 || nl1 == t21 || nl2 == t12 || nl2 == t22)
			continue;
		ev[i++] = p;
	}
	for (int i = 0; i < CACHE_PREPSZ - 1; i++) {
		*(ev[i]) = ev[i+1];
	}
	*(ev[TLB_PREPSZ-1]) = ev[0];

	return ev[0];
}

/* Pointer chasers */

static int32_t pchase(void *head, size_t n)
{
	int32_t ret;
	asm volatile (
		"lfence\n"
		"rdtsc\n"
		"mov %%eax, %%edi\n"
		"1:"
		"mov (%[probe]),%[probe]\n"
		"loop 1b\n"
		"lfence\n"
		"rdtscp\n"
		"sub %%edi, %%eax\n"

		: [probe]"+r" (head), "=a" (ret), "+c" (n)
		:
		: "rdx", "rdi", "cc"
	);
	return ret;
}

static void **qchase(void **head, size_t n)
{
	while (n--) {
		head = (void **)(*head);
	}
	return head;
}

static void pprint(void **head, size_t n)
{
	while (n--) {
		fprintf(stderr, "%p ", head);
		head = *head;
	}
	fputc('\n', stderr);
}

/* Hamfuncs */

struct htiming {
	uint32_t ham;
	uint32_t tot;
};

typedef struct htiming (hamtime_f)(void *, void *);

static inline uint32_t htime(void *a1, void *a2)
{
	uint32_t ret;
	asm volatile (
		"mov %[a1], %%r8 \n"
		"mov %[a2], %%r9 \n"
		"lfence \n"
		"mfence \n"
		"rdtscp \n"
		"mov %%eax, %%edi \n"
		"mov (%%r8), %%rax \n"
		"mov (%%r9), %%rax \n"
		"lfence \n"
		"rdtscp \n"
		"sub %%edi, %%eax \n"

		: "=a" (ret)
		: [a1]"r" (a1), [a2]"r" (a2)
		: "rdx", "rcx", "rdi", "r8", "r9", "cc", "memory"
	);
	return ret;
}
//static inline uint32_t fhtime(void *a1, void *a2)
//{
	//uint32_t ret;
	//asm volatile (
		//"clflush (%[a1]) \n"
		//"clflush (%[a2]) \n"
		//"mov %[a1], %%r8 \n"
		//"mov %[a2], %%r9 \n"
		//"lfence \n"
		//"mfence \n"
		//"rdtscp \n"
		//"mov %%eax, %%edi \n"
		//"mov (%%r8), %%rax \n"
		//"mov (%%r9), %%rax \n"
		//"lfence \n"
		//"rdtscp \n"
		//"sub %%edi, %%eax \n"

		//: "=a" (ret)
		//: [a1]"r" (a1), [a2]"r" (a2)
		//: "rdx", "rcx", "rdi", "r8", "r9", "cc", "memory"
	//);
	//return ret;
//}
static inline void hamham(void *a1, void *a2)
{
	asm volatile ("lfence");
	(void)*((volatile size_t *)a1);
	//asm volatile ("lfence");
	(void)*((volatile size_t *)a2);
	asm volatile ("lfence");
}

#define REPS (250)
//#define CLFLUSH(a) asm volatile ("lfence\nmfence\nclflushopt (%0)" :: "r" (a))
#define CLFLUSH(a) asm volatile ("lfence\nsfence\nmfence\nclflush (%0)\nlfence\nsfence\nmfence" :: "r" (a))

static struct htiming hamtime(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}
static struct htiming fhamtime(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		CLFLUSH(a1);
		CLFLUSH(a2);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}

#define PMDCNT (32L)
#define BASESZ (PMDCNT << (12 + 9 + 1))
#define NXPMD (512 * 0x1000)
#define pmdidx(a) ((((char *)(a)) - base)/NXPMD)
static char *tebuf = NULL;
static char *cebuf = NULL;
static char *base = NULL;
static struct mmuctl ctx;
static unsigned long pmds[PMDCNT];
static uintptr_t tpa[2];

static struct htiming evhamtime(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		cache_evrun(cebuf, tpa[0]);
		cache_evrun(cebuf, tpa[1]);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}

static struct htiming itham(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		tlb_evrun(tebuf, (uintptr_t)a1, TLB_PREPSZ, tlb_nexthit);
		tlb_evrun(tebuf, (uintptr_t)a2, TLB_PREPSZ, tlb_nexthit);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}
static struct htiming iiham(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		mmuctl_invlpg(&ctx, a1);
		mmuctl_invlpg(&ctx, a2);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}
static struct htiming iftham(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		CLFLUSH(a1);
		CLFLUSH(a2);
		tlb_evrun(tebuf, (uintptr_t)a1, TLB_PREPSZ, tlb_nexthit);
		tlb_evrun(tebuf, (uintptr_t)a2, TLB_PREPSZ, tlb_nexthit);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}
static struct htiming itfham(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		tlb_evrun(tebuf, (uintptr_t)a1, TLB_PREPSZ, tlb_nexthit);
		tlb_evrun(tebuf, (uintptr_t)a2, TLB_PREPSZ, tlb_nexthit);
		CLFLUSH(a1);
		CLFLUSH(a2);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}
static struct htiming ifiham(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		CLFLUSH(a1);
		CLFLUSH(a2);
		mmuctl_invlpg(&ctx, a1);
		mmuctl_invlpg(&ctx, a2);
		//CLFLUSH(a1);
		//CLFLUSH(a2);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}
static struct htiming icham(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		cache_evrun(cebuf, pmds[pmdidx(a1)]);
		cache_evrun(cebuf, pmds[pmdidx(a2)]);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}
static struct htiming ifcham(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		CLFLUSH(a1);
		CLFLUSH(a2);
		cache_evrun(cebuf, pmds[pmdidx(a1)]);
		cache_evrun(cebuf, pmds[pmdidx(a2)]);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}
static struct htiming itcham(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		tlb_evrun(tebuf, (uintptr_t)a1, TLB_PREPSZ, tlb_nexthit);
		tlb_evrun(tebuf, (uintptr_t)a2, TLB_PREPSZ, tlb_nexthit);
		cache_evrun(cebuf, pmds[pmdidx(a1)]);
		cache_evrun(cebuf, pmds[pmdidx(a2)]);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}
static struct htiming iicham(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		mmuctl_invlpg(&ctx, a1);
		mmuctl_invlpg(&ctx, a2);
		cache_evrun(cebuf, pmds[pmdidx(a1)]);
		cache_evrun(cebuf, pmds[pmdidx(a2)]);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}
static struct htiming iftcham(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		CLFLUSH(a1);
		CLFLUSH(a2);
		tlb_evrun(tebuf, (uintptr_t)a1, TLB_PREPSZ, tlb_nexthit);
		tlb_evrun(tebuf, (uintptr_t)a2, TLB_PREPSZ, tlb_nexthit);
		cache_evrun(cebuf, pmds[pmdidx(a1)]);
		cache_evrun(cebuf, pmds[pmdidx(a2)]);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}
static struct htiming ictham(void *a1, void *a2)
{
	uint32_t acc = 0;
	for (int i = 0; i < REPS; i++) {
		cache_evrun(cebuf, pmds[pmdidx(a1)]);
		cache_evrun(cebuf, pmds[pmdidx(a2)]);
		tlb_evrun(tebuf, (uintptr_t)a1, TLB_PREPSZ, tlb_nexthit);
		tlb_evrun(tebuf, (uintptr_t)a2, TLB_PREPSZ, tlb_nexthit);
		acc += htime(a1, a2);
	}
	return (struct htiming){acc/REPS, 0};
}

/* PC hamfuncs */

static void **t1head = NULL;
static void **t2head = NULL;
static void **c1head = NULL;
static void **c2head = NULL;

//static struct timespec it_t0, it_t, ic_t0, ic_t, itc_t0, itc_t, ict_t0, ict_t;
//static uint32_t it_tsc0, it_tsc, ic_tsc0, ic_tsc, itc_tsc0, itc_tsc, ict_tsc0, ict_tsc;

#define rdtscp(v) asm volatile ("lfence\nmfence\nrdtscp\nshl $32,%%rdx\nor %%rdx,%%rax" : "=a" (v) :: "rdx", "rcx")

static struct htiming pc_itham(void *a1, void *a2)
{
	uint64_t t0, t;
	uint32_t acc = 0;
	//clock_gettime(CLOCK_REALTIME, &it_t0);
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		t1head = qchase(t1head, TLB_PREPSZ);
		t2head = qchase(t2head, TLB_PREPSZ);
		acc += htime(a1, a2);
	}
	rdtscp(t);
	//clock_gettime(CLOCK_REALTIME, &it_t);
	return (struct htiming){acc/REPS, (t - t0)/REPS};
}
static struct htiming pc_icham(void *a1, void *a2)
{
	uint64_t t0, t;
	uint32_t acc = 0;
	//clock_gettime(CLOCK_REALTIME, &ic_t0);
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		c1head = qchase(c1head, CACHE_PREPSZ);
		c2head = qchase(c2head, CACHE_PREPSZ);
		acc += htime(a1, a2);
	}
	rdtscp(t);
	//clock_gettime(CLOCK_REALTIME, &ic_t);
	return (struct htiming){acc/REPS, (t - t0)/REPS};
}
static struct htiming pc_itcham(void *a1, void *a2)
{
	uint64_t t0, t;
	uint32_t acc = 0;
	//clock_gettime(CLOCK_REALTIME, &itc_t0);
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		t1head = qchase(t1head, TLB_PREPSZ);
		t2head = qchase(t2head, TLB_PREPSZ);
		c1head = qchase(c1head, CACHE_PREPSZ);
		c2head = qchase(c2head, CACHE_PREPSZ);
		acc += htime(a1, a2);
	}
	rdtscp(t);
	//clock_gettime(CLOCK_REALTIME, &itc_t);
	return (struct htiming){acc/REPS, (t - t0)/REPS};
}
static struct htiming pc_ictham(void *a1, void *a2)
{
	uint64_t t0, t;
	uint32_t acc = 0;
	//clock_gettime(CLOCK_REALTIME, &ict_t0);
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		c1head = qchase(c1head, CACHE_PREPSZ);
		c2head = qchase(c2head, CACHE_PREPSZ);
		t1head = qchase(t1head, TLB_PREPSZ);
		t2head = qchase(t2head, TLB_PREPSZ);
		acc += htime(a1, a2);
	}
	rdtscp(t);
	//clock_gettime(CLOCK_REALTIME, &ict_t);
	return (struct htiming){acc/REPS, (t - t0)/REPS};
}
/*--TOTALS--*/
static struct htiming pct_itham(void *a1, void *a2)
{
	uint64_t t0, t;
	//clock_gettime(CLOCK_REALTIME, &it_t0);
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		t1head = qchase(t1head, TLB_PREPSZ);
		t2head = qchase(t2head, TLB_PREPSZ);
		hamham(a1, a2);
	}
	rdtscp(t);
	//clock_gettime(CLOCK_REALTIME, &it_t);
	return (struct htiming){0, (t - t0)/REPS};
}
static struct htiming pct_icham(void *a1, void *a2)
{
	uint64_t t0, t;
	//clock_gettime(CLOCK_REALTIME, &ic_t0);
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		c1head = qchase(c1head, CACHE_PREPSZ);
		c2head = qchase(c2head, CACHE_PREPSZ);
		hamham(a1, a2);
	}
	rdtscp(t);
	//clock_gettime(CLOCK_REALTIME, &ic_t);
	return (struct htiming){0, (t - t0)/REPS};
}
static struct htiming pct_itcham(void *a1, void *a2)
{
	uint64_t t0, t;
	//clock_gettime(CLOCK_REALTIME, &itc_t0);
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		t1head = qchase(t1head, TLB_PREPSZ);
		t2head = qchase(t2head, TLB_PREPSZ);
		c1head = qchase(c1head, CACHE_PREPSZ);
		c2head = qchase(c2head, CACHE_PREPSZ);
		hamham(a1, a2);
	}
	rdtscp(t);
	//clock_gettime(CLOCK_REALTIME, &itc_t);
	return (struct htiming){0, (t - t0)/REPS};
}
static struct htiming pct_ictham(void *a1, void *a2)
{
	uint64_t t0, t;
	//clock_gettime(CLOCK_REALTIME, &ict_t0);
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		c1head = qchase(c1head, CACHE_PREPSZ);
		c2head = qchase(c2head, CACHE_PREPSZ);
		t1head = qchase(t1head, TLB_PREPSZ);
		t2head = qchase(t2head, TLB_PREPSZ);
		hamham(a1, a2);
	}
	rdtscp(t);
	//clock_gettime(CLOCK_REALTIME, &ict_t);
	return (struct htiming){0, (t - t0)/REPS};
}

/* Ninja PC hamfuncs */

static void *tjbuf = NULL;

static void **t1njhead = NULL;
static void **t2njhead = NULL;
#define NINJA_THRESH (1024)

// A* ninja
//#define NINJA_INIT (9)
//#define NINJA_INIT (13)
//#define NINJA_STEP (4)
//#define NINJA_STEP (4 + (i & 1))
//#define NINJA_STEP (5 - (i & 1))

// BFS ninja
#define NINJA_INIT (18)
#define NINJA_STEP (4)

#define NINJA_BOILERPLATE \
static void **c1 = NULL; \
static void **c2 = NULL; \
size_t cnt = NINJA_THRESH; \
if (cnt >= NINJA_THRESH) { \
	cnt = 0; \
	tlb_evrun(tebuf, (uintptr_t)a1, TLB_PREPSZ, tlb_nexthit); \
	tlb_evrun(tebuf, (uintptr_t)a2, TLB_PREPSZ, tlb_nexthit); \
	c1 = qchase(t1njhead, NINJA_INIT); \
	c2 = qchase(t2njhead, NINJA_INIT); \
}

static struct htiming ntpc_itham(void *a1, void *a2)
{
	NINJA_BOILERPLATE

	uint64_t t0, t;
	uint32_t acc = 0;
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		c1 = qchase(c1, NINJA_STEP);
		c2 = qchase(c2, NINJA_STEP);
		acc += htime(a1, a2);
	}
	rdtscp(t);
	cnt += REPS;
	return (struct htiming){acc/REPS, (t - t0)/REPS};
}
static struct htiming ntpct_itham(void *a1, void *a2)
{
	NINJA_BOILERPLATE

	uint64_t t0, t;
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		c1 = qchase(c1, NINJA_STEP);
		c2 = qchase(c2, NINJA_STEP);
		hamham(a1, a2);
	}
	rdtscp(t);
	cnt += REPS;
	return (struct htiming){0, (t - t0)/REPS};
}
static struct htiming ntpc_itcham(void *a1, void *a2)
{
	NINJA_BOILERPLATE

	uint64_t t0, t;
	uint32_t acc = 0;
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		c1 = qchase(c1, NINJA_STEP);
		c2 = qchase(c2, NINJA_STEP);
		c1head = qchase(c1head, CACHE_PREPSZ);
		c2head = qchase(c2head, CACHE_PREPSZ);
		acc += htime(a1, a2);
	}
	rdtscp(t);
	cnt += REPS;
	return (struct htiming){acc/REPS, (t - t0)/REPS};
}
static struct htiming ntpct_itcham(void *a1, void *a2)
{
	NINJA_BOILERPLATE

	uint64_t t0, t;
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		c1 = qchase(c1, NINJA_STEP);
		c2 = qchase(c2, NINJA_STEP);
		c1head = qchase(c1head, CACHE_PREPSZ);
		c2head = qchase(c2head, CACHE_PREPSZ);
		hamham(a1, a2);
	}
	rdtscp(t);
	cnt += REPS;
	return (struct htiming){0, (t - t0)/REPS};
}
static struct htiming ntpc_ictham(void *a1, void *a2)
{
	NINJA_BOILERPLATE

	uint64_t t0, t;
	uint32_t acc = 0;
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		c1head = qchase(c1head, CACHE_PREPSZ);
		c2head = qchase(c2head, CACHE_PREPSZ);
		c1 = qchase(c1, NINJA_STEP);
		c2 = qchase(c2, NINJA_STEP);
		acc += htime(a1, a2);
	}
	rdtscp(t);
	cnt += REPS;
	return (struct htiming){acc/REPS, (t - t0)/REPS};
}
static struct htiming ntpct_ictham(void *a1, void *a2)
{
	NINJA_BOILERPLATE

	uint64_t t0, t;
	rdtscp(t0);
	for (int i = 0; i < REPS; i++) {
		c1head = qchase(c1head, CACHE_PREPSZ);
		c2head = qchase(c2head, CACHE_PREPSZ);
		c1 = qchase(c1, NINJA_STEP);
		c2 = qchase(c2, NINJA_STEP);
		hamham(a1, a2);
	}
	rdtscp(t);
	cnt += REPS;
	return (struct htiming){0, (t - t0)/REPS};
}

/* Utils & showtime! */

#define PRREP (8192)
static void prham(void *p1, void *p2, hamtime_f hf)
{
	struct htiming r[PRREP];
	for (int i = 0; i < PRREP; i++) {
		r[i] = hf(p1, p2);
	}
	for (int i = 0; i < PRREP; i++) {
		fprintf(stderr, "%5u ", r[i].ham);
	}
	fputc('\n', stderr);
	for (int i = 0; i < PRREP; i++) {
		fprintf(stderr, "%5u ", r[i].tot);
	}
	fputc('\n', stderr);
}

static inline long usecdiff(struct timespec *t0, struct timespec *t)
{
	long usec = (t->tv_nsec - t0->tv_nsec) / 1000;
	usec += (t->tv_sec - t0->tv_sec) * 1000000;
	return usec;
}

static inline long cyclediff(uint64_t t0, uint64_t t)
{
	return (t - t0)/REPS;
}


/* Arbitrary, distinct constant values */
#define ARVAL1 (0)
#define ARVAL2 (3)


int main(int argc, char *argv[])
{
	struct mmuctl_ptwalk ptw;
	pid_t pid = getpid();
	uintptr_t caddr;

	size_t t1, t2;
	char *t1p, *t2p;
	uintptr_t ca1, ca2;

	fprintf(stderr, "PID: %u\n", pid);
	fprintf(stderr, "REPS: %u; TLB ESZ: %u; CACHE ESZ: %u\n", REPS, TLB_PREPSZ, CACHE_PREPSZ);

	tebuf = setup_tlbevbuf();
	cebuf = setup_cacheevbuf();
	tjbuf = setup_tlbevbuf();
	//fprintf(stderr, "TEBUF: %p\n", tebuf);
	//fprintf(stderr, "CEBUF: %p\n", cebuf);
	//fprintf(stderr, "TJBUF: %p\n", tjbuf);
	//(void)tload((void **)cebuf+100);
	//cebuf[0] = '@';

	if (mmuctl_init(&ctx)) {
		perror("Error mmuctl init");
		return 1;
	}

	if (mmuctl_resolve(&ctx, &ptw, cebuf, pid)) {
		perror("Error resolving cebuf");
		return 1;
	}
	//fprintf(stderr, "Res %p %x: %09lx %09lx %09lx %09lx %09lx\n", cebuf, ptw.valid,
	        //ptw.pgd, ptw.p4d, ptw.pud, ptw.pmd, ptw.pte);
	//mmuctl_read(&ctx, &cevpa, 8, ptw.pgd & PFNBITS
	cevpa = ptw.pud & PFNBITS;
	//fprintf(stderr, "CEVPA: %lx\n", cevpa);

	if ((base = mmap(NULL, BASESZ, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0)) == MAP_FAILED) {
		perror("Error mapping target buf");
		return 1;
	}
	base = (char *)((((uintptr_t)base >> (12 + 9)) + 1) << (12 + 9));

	for (size_t i = 0; i < PMDCNT; i++) {
		if (mmuctl_resolve(&ctx, &ptw, base + i * NXPMD, pid)) {
			perror("Error resolving addr");
			return 1;
		}
		pmds[i] = ptw.pmd & PFNBITS;
		printf("%lx\n", pmds[i] & 0x3ff000);
	}

	fflush(stdout);
	while (scanf("%zu %zu", &t1, &t2) != 2) {
		perror("Bad input");
	}
	(void)getchar();
	t1p = base + t1 * NXPMD + ARVAL1 * PAGESZ;
	t2p = base + t2 * NXPMD + ARVAL2 * PAGESZ;
	ca1 = pteaddr(t1p, pmds[t1]);
	ca2 = pteaddr(t2p, pmds[t2]);
	fprintf(stderr, "Targets are:\n[%2zu] = %lx (%p) -> %lx\n[%2zu] = %lx (%p) -> %lx\n",
	        t1, pmds[t1], t1p, ca1, t2, pmds[t2], t2p, ca2);
	fprintf(stderr, "TLB sets:\n%lx %lx\n%lx %lx\n",
	        TDL1((uintptr_t)t1p), TSL2((uintptr_t)t1p),
	        TDL1((uintptr_t)t2p), TSL2((uintptr_t)t2p));
	fprintf(stderr, "Cache sets:\n%lx %lx %lx %lx\n%lx %lx %lx %lx\n",
	        CL1(pmds[t1]), CL2(pmds[t1]), CL3(pmds[t1]) & 0x3ff, CL3(pmds[t1]) >> 10,
	        CL1(pmds[t2]), CL2(pmds[t2]), CL3(pmds[t2]) & 0x3ff, CL3(pmds[t2]) >> 10);
	fprintf(stderr, "VA Cache sets:\n%lx %lx %lx %lx\n%lx %lx %lx %lx\n",
	        CL1((uintptr_t)t1p), CL2((uintptr_t)t1p), CL3((uintptr_t)t1p) & 0x3ff, CL3((uintptr_t)t1p) >> 10,
	        CL1((uintptr_t)t2p), CL2((uintptr_t)t2p), CL3((uintptr_t)t2p) & 0x3ff, CL3((uintptr_t)t2p) >> 10);

	if (mmuctl_resolve(&ctx, &ptw, t1p, pid)) {
		perror("Error resolving T1");
		return 1;
	}
	tpa[0] = (ptw.pte & PFNBITS) | ((uintptr_t)t1p & 0xfff);
	if (mmuctl_resolve(&ctx, &ptw, t2p, pid)) {
		perror("Error resolving T2");
		return 1;
	}
	tpa[1] = (ptw.pte & PFNBITS) | ((uintptr_t)t2p & 0xfff);
	fprintf(stderr, "Target PAs: %lx %lx\n", tpa[0], tpa[1]);
	fprintf(stderr, "PA Cache sets:\n%lx %lx %lx %lx\n%lx %lx %lx %lx\n",
	        CL1(tpa[0]), CL2(tpa[0]), CL3(tpa[0]) & 0x3ff, CL3(tpa[0]) >> 10,
	        CL1(tpa[1]), CL2(tpa[1]), CL3(tpa[1]) & 0x3ff, CL3(tpa[1]) >> 10);

	t1head = tlb_prepnaive(tebuf, (uintptr_t)t1p);
	t2head = tlb_prepnaive(tebuf, (uintptr_t)t2p);
	//c1head = cache_prepnaive(cebuf, (uintptr_t)pmds[t1]);
	//c2head = cache_prepnaive(cebuf, (uintptr_t)pmds[t2]);

	t1njhead = tlb_prepninja(tjbuf, (uintptr_t)t1p);
	t2njhead = tlb_prepninja(tjbuf, (uintptr_t)t2p);
	c1head = cache_prepnotlb(cebuf, (uintptr_t)pmds[t1], (uintptr_t)t1p, (uintptr_t)t2p);
	c2head = cache_prepnotlb(cebuf, (uintptr_t)pmds[t2], (uintptr_t)t1p, (uintptr_t)t2p);

#if 1
// Sanity checks, disable for faster testing
	fputc('\n', stderr);
	fputs("Normal timing run\n", stderr);
	prham(t1p, t2p, hamtime);
	fputs("Flush timing run\n", stderr);
	prham(t1p, t2p, fhamtime);
	fputs("CacheEv timing run\n", stderr);
	prham(t1p, t2p, evhamtime);

	fputc('\n', stderr);
	fputs("Naive(T) timing run\n", stderr);
	prham(t1p, t2p, itham);
	fputs("Naive(T) PC timing run\n", stderr);
	prham(t1p, t2p, pc_itham);
	fputs("Naive(T) PCT timing run\n", stderr);
	prham(t1p, t2p, pct_itham);

	fputc('\n', stderr);
	fputs("Naive(T+C) timing run\n", stderr);
	prham(t1p, t2p, itcham);
	fputs("Naive(T+C) PC timing run\n", stderr);
	prham(t1p, t2p, pc_itcham);
	fputs("Naive(T+C) PCT timing run\n", stderr);
	prham(t1p, t2p, pct_itcham);

	fputc('\n', stderr);
	fputs("Ninja(T) PC timing run\n", stderr);
	prham(t1p, t2p, ntpc_itham);
	fputs("Ninja(T) PCT timing run\n", stderr);
	prham(t1p, t2p, ntpct_itham);

	fputc('\n', stderr);
	fputs("Ninja(T+C) PC timing run\n", stderr);
	prham(t1p, t2p, ntpc_itcham);
	fputs("Ninja(T+C) PCT timing run\n", stderr);
	prham(t1p, t2p, ntpct_itcham);

#endif

#if 1
	size_t it = 10;

	do {
	//fputc('\n', stderr);
	//fputs("Naive(T) timing run\n", stderr);
	//prham(t1p, t2p, itham);
	//fputs("Naive(T) PC timing run\n", stderr);
	//prham(t1p, t2p, pc_itham);
	//fputs("Naive(T) PCT timing run\n", stderr);
	//prham(t1p, t2p, pct_itham);

	//fprintf(stderr, "%lu us\n", usecdiff(&it_t0, &it_t));
	//fprintf(stderr, "%ld cycles/rep\n", cyclediff(it_tsc0, it_tsc));
	//fputs("nNaive(I) timing run\n", stderr);
	//prham(t1p, t2p, iiham);
	//fputs("Naive(F+T) timing run\n", stderr);
	//prham(t1p, t2p, iftham);
	//fputs("Naive(T+F) timing run\n", stderr);
	//prham(t1p, t2p, itfham);
	//fputs("Naive(F+I) timing run\n", stderr);
	//prham(t1p, t2p, ifiham);

	//fputc('\n', stderr);
	//fputs("Naive(C) timing run\n", stderr);
	//prham(t1p, t2p, icham);
	//fputs("Naive(C) PC timing run\n", stderr);
	//prham(t1p, t2p, pc_icham);
	//fputs("Naive(C) PCT timing run\n", stderr);
	//prham(t1p, t2p, pct_icham);
	//fprintf(stderr, "%lu us\n", usecdiff(&ic_t0, &ic_t));
	//fprintf(stderr, "%ld cycles/rep\n", cyclediff(ic_tsc0, ic_tsc));
	//fputs("Naive(F+C) timing run\n", stderr);
	//prham(t1p, t2p, ifcham);

	//fputc('\n', stderr);
	//fputs("Naive(T+C) timing run\n", stderr);
	//prham(t1p, t2p, itcham);
	//fputs("Naive(T+C) PC timing run\n", stderr);
	//prham(t1p, t2p, pc_itcham);
	//fputs("Naive(T+C) PCT timing run\n", stderr);
	//prham(t1p, t2p, pct_itcham);

	//fprintf(stderr, "%lu us\n", usecdiff(&itc_t0, &itc_t));
	//fprintf(stderr, "%ld cycles/rep\n", cyclediff(itc_tsc0, itc_tsc));
	//fputs("Naive(I+C) timing run\n", stderr);
	//prham(t1p, t2p, iicham);
	//fputs("Naive(F+T+C) timing run\n", stderr);
	//prham(t1p, t2p, iftcham);

	//fputc('\n', stderr);
	//fputs("Naive(C+T) timing run\n", stderr);
	//prham(t1p, t2p, ictham);
	//fputs("Naive(C+T) PC timing run\n", stderr);
	//prham(t1p, t2p, pc_ictham);
	//fputs("Naive(C+T) PCT timing run\n", stderr);

	//prham(t1p, t2p, pct_ictham);
	//fprintf(stderr, "%lu us\n", usecdiff(&ict_t0, &ict_t));
	//fprintf(stderr, "%ld cycles/rep\n", cyclediff(ict_tsc0, ict_tsc));

	//fputc('\n', stderr);
	//fputs("Ninja timing run\n", stderr);

	//fputc('\n', stderr);
	//fputs("Ninja(T) PC timing run\n", stderr);
	//prham(t1p, t2p, ntpc_itham);
	//fputs("Ninja(T) PCT timing run\n", stderr);
	//prham(t1p, t2p, ntpct_itham);

	//fputc('\n', stderr);
	//fputs("Ninja(T+C) PC timing run\n", stderr);
	//prham(t1p, t2p, ntpc_itcham);
	//fputs("Ninja(T+C) PCT timing run\n", stderr);
	//prham(t1p, t2p, ntpct_itcham);

	//fputc('\n', stderr);
	//fputs("Ninja(C+T) PC timing run\n", stderr);
	//prham(t1p, t2p, ntpc_ictham);
	//fputs("Ninja(C+T) PCT timing run\n", stderr);
	//prham(t1p, t2p, ntpct_ictham);

	/**/

	//fputc('\n', stderr);
	//fputs("Naive(T+C) PC timing run\n", stderr);
	//prham(t1p, t2p, pc_itcham);
	//fputs("Ninja(T+C) PC timing run\n", stderr);
	//prham(t1p, t2p, ntpc_itcham);

	//fputs("Naive(C+T) PC timing run\n", stderr);
	//prham(t1p, t2p, pc_ictham);
	//fputs("Ninja(C+T) PC timing run\n", stderr);
	//prham(t1p, t2p, ntpc_ictham);

	fputc('\n', stderr);
	fputs("Naive(T+C) PCT timing run\n", stderr);
	prham(t1p, t2p, pct_itcham);
	fputs("Ninja(T+C) PCT timing run\n", stderr);
	prham(t1p, t2p, ntpct_itcham);

	//fputs("Naive(C+T) PCT timing run\n", stderr);
	//prham(t1p, t2p, pct_ictham);
	//fputs("Ninja(C+T) PCT timing run\n", stderr);
	//prham(t1p, t2p, ntpct_ictham);

	/**/

	//fputc('\n', stderr);
	//fputs("Naive(C+T) PC timing run\n", stderr);
	//prham(t1p, t2p, pc_ictham);
	//fputs("Ninja(C+T) PC timing run\n", stderr);
	//prham(t1p, t2p, ntpc_ictham);

	//fputs("Naive(T+C) PC timing run\n", stderr);
	//prham(t1p, t2p, pc_itcham);
	//fputs("Ninja(T+C) PC timing run\n", stderr);
	//prham(t1p, t2p, ntpc_itcham);

	//fputc('\n', stderr);
	//fputs("Naive(C+T) PCT timing run\n", stderr);
	//prham(t1p, t2p, pct_ictham);
	//fputs("Ninja(C+T) PCT timing run\n", stderr);
	//prham(t1p, t2p, ntpct_ictham);

	//fputs("Naive(T+C) PCT timing run\n", stderr);
	//prham(t1p, t2p, pct_itcham);
	//fputs("Ninja(T+C) PCT timing run\n", stderr);
	//prham(t1p, t2p, ntpct_itcham);

	//fputs("Again?\n", stderr);
	//(void)getchar();
	} while (--it);
#endif

	return 0;
}
