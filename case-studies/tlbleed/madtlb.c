#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

#include <time.h>
#include <unistd.h>
#include <sys/mman.h>


#define USE_L2 (0)
#define NINJA (0)

#define L1_SHORT_NINJA (0)

#define NMEAS (20)

static inline long usecdiff(struct timespec *t0, struct timespec *t)
{
	long usec = (t->tv_nsec - t0->tv_nsec) / 1000;
	usec += (t->tv_sec - t0->tv_sec) * 1000000;
	return usec;
}

#define _K (1024L)
#define _M (_K * _K)
#define _G (_K * _M)

#define PAGESZ (4*_K)
#define EVBN (1L << 18)
#define EVBSZ (EVBN * PAGESZ)

#define TBUFBASE ((void *)0x13370000000L)
#define TARGOFF (0x887L * PAGESZ)
#define TARGET ((char *)TBUFBASE + TARGOFF)

static void *setup_evbuf(void)
{
	return mmap(NULL, EVBSZ, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
}
static void *setup_tbuf(void)
{
	assert((char *)TBUFBASE + EVBSZ > TARGET);
	return mmap(TBUFBASE, EVBSZ, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
}

#define TLLINE(x) ((x) >> 12)

static inline uintptr_t TDL1(uintptr_t va) {return TLLINE(va) & 0xf;}
static inline uintptr_t TIL1(uintptr_t va) {return TLLINE(va) & 0x7;}
static inline uintptr_t TSL2(uintptr_t va)
{
	uintptr_t p = TLLINE(va);
	return (p ^ (p >> 7)) & 0x7f;
}

static void *tlb_nexthit(void *cur, uintptr_t l1t, uintptr_t l2t)
{
	uintptr_t p = (uintptr_t)cur;
	do {
		p += PAGESZ;
	} while (TDL1(p) != l1t || TSL2(p) != l2t);
	return (void *)p;
}
static void *tlb_nexthit_l1(void *cur, uintptr_t l1t)
{
	uintptr_t p = (uintptr_t)cur;
	do {
		p += PAGESZ;
	} while (TDL1(p) != l1t);
	return (void *)p;
}

static inline void tlb_evrun(void *base, uintptr_t targ, size_t n)
{
	uintptr_t l1t = TDL1(targ);
	uintptr_t l2t = TSL2(targ);
	void *p = base;
	for (size_t i = 0; i < n; i++) {
		p = tlb_nexthit(p, l1t, l2t);
		//fprintf(stderr, "*%p\n", p);
		//(void)tload((void **)p);
		*(volatile char *)p;
	}
}

#define TLB_PREPSZ_L1 (4)
#define TLB_PREPSZ (12)

static void **tlb_prep_l1(void *base, uintptr_t targ)
{
	uintptr_t l1t = TDL1(targ);
	//fprintf(stderr, "%"PRIxPTR"\n", l1t);
	void *p = base;
	void **ev[TLB_PREPSZ_L1];
	for (int i = 0; i < TLB_PREPSZ_L1; i++) {
		ev[i] = (void **)tlb_nexthit_l1(p, l1t);
		p = ev[i];
	}
	for (int i = 0; i < TLB_PREPSZ_L1 - 1; i++) {
		*(ev[i]) = ev[i+1];
	}
	*(ev[TLB_PREPSZ_L1-1]) = ev[0];
	return ev[0];
}

#if (L1_SHORT_NINJA)
/*|-------------| <-- lead-in
 * 0 1 0 2 0 2 3 (2 3 1 | 3 1 0 | 1 0 2 | 0 2 3) */
/* Implemented as (0 1 0 2 0 2 3 2 3 1 3 1) */
#define NINJA_L1_INIT (7)
#define NINJA_L1_STEP (3)
static void **tlb_prepninja_l1(void *base, uintptr_t targ)
{
	uintptr_t l1t = TDL1(targ);
	//fprintf(stderr, "%"PRIxPTR"\n", l1t);
	void *p = base;
	void **ev[4];
	for (int i = 0; i < 4; i++) {
		ev[i] = tlb_nexthit_l1(p, l1t);
		p = ev[i];
	}
	ev[0][0] = &ev[1][0];
	ev[0][1] = &ev[2][0];
	ev[0][2] = &ev[2][1];

	ev[1][0] = &ev[0][1];
	ev[1][1] = &ev[3][2];
	ev[1][2] = &ev[0][0];

	ev[2][0] = &ev[0][2];
	ev[2][1] = &ev[3][0];
	ev[2][2] = &ev[3][1];

	ev[3][0] = &ev[2][2];
	ev[3][1] = &ev[1][1];
	ev[3][2] = &ev[1][2];
	return ev[0];
}
#else
/*|-------| <-- lead-in
 * 0 1 2 3 (2 3 0 | 3 0 1 | 0 1 2 | 1 2 3)
 * Impl as (0 | 1 2 3 2 3 0 3 0 1 0 1 2) */
#define NINJA_L1_INIT (4)
#define NINJA_L1_STEP (3)
static void **tlb_prepninja_l1(void *base, uintptr_t targ)
{
	uintptr_t l1t = TDL1(targ);
	//fprintf(stderr, "%"PRIxPTR"\n", l1t);
	void *p = base;
	void **ev[4];
	for (int i = 0; i < 4; i++) {
		ev[i] = tlb_nexthit_l1(p, l1t);
		p = ev[i];
	}
	ev[0][0] = &ev[1][0];
	ev[0][1] = &ev[3][2];
	ev[0][2] = &ev[1][1];
	ev[0][3] = &ev[1][2];

	ev[1][0] = &ev[2][0];
	ev[1][1] = &ev[0][3];
	ev[1][2] = &ev[2][2];

	ev[2][0] = &ev[3][0];
	ev[2][1] = &ev[3][1];
	ev[2][2] = &ev[1][0];

	ev[3][0] = &ev[2][1];
	ev[3][1] = &ev[0][1];
	ev[3][2] = &ev[0][2];
	return ev[0];
}
#endif

static void **tlb_prepnaive(void *base, uintptr_t targ)
{
	uintptr_t l1t = TDL1(targ);
	uintptr_t l2t = TSL2(targ);
	//fprintf(stderr, "%" PRIxPTR " %" PRIxPTR "\n", l1t, l2t);
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

/*|---------------------| <-- min. lead-in
 * 0 1 2 3 4 5 0 (2 1 6 | 3 0 2 7 | 4 1 0 8 | 6 2 1 5 | 7 0 2 3 | 8 1 0 4 | 5)
 *|-----------------------------------------| <-- full lead-in */
#define NINJA_L2_INIT (18)
#define NINJA_L2_STEP (4)
static void **tlb_prepninja(void *base, uintptr_t targ)
{
	uintptr_t l1t = TDL1(targ);
	uintptr_t l2t = TSL2(targ);
	//fprintf(stderr, "%" PRIxPTR " %" PRIxPTR "\n", l1t, l2t);
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

/*|-----------------------------------------| <-- lead-in
 * 0 1 2 1 3 4 5 2 1 6 | 0 3 2 7 | 0327 4138 | 4138 6215 | 6215 7320 | 7320 8134 | 8134 5216 |
 * looplink -> ^-----------------------------------------------------------------------------| */
#define NINJA_SPLICE_INIT (22)
#define NINJA_SPLICE_L1_STEP (4)
#define NINJA_SPLICE_L2_STEP (4)
static void **tlb_prepninja_splice(void *base, uintptr_t targ)
{
	uintptr_t l1t = TDL1(targ);
	uintptr_t l2t = TSL2(targ);
	//fprintf(stderr, "%" PRIxPTR " %" PRIxPTR "\n", l1t, l2t);
	void *p = base;
	void **ev[9];
	for (int i = 0; i < 9; i++) {
		p = tlb_nexthit(p, l1t, l2t);
		ev[i] = (void **)p;
	}
	ev[0][0] = &ev[1][0];
	ev[0][1] = &ev[3][1];
	ev[0][2] = &ev[3][2];
	ev[0][3] = &ev[7][3];
	ev[0][4] = &ev[8][2];

	ev[1][0] = &ev[2][0];
	ev[1][1] = &ev[3][0];
	ev[1][2] = &ev[6][0];
	ev[1][3] = &ev[3][3];
	ev[1][4] = &ev[3][4];
	ev[1][5] = &ev[5][1];
	ev[1][6] = &ev[5][2];
	ev[1][7] = &ev[3][7];
	ev[1][8] = &ev[3][8];
	ev[1][9] = &ev[6][3];

	ev[2][0] = &ev[1][1];
	ev[2][1] = &ev[1][2];
	ev[2][2] = &ev[7][0];
	ev[2][3] = &ev[7][1];
	ev[2][4] = &ev[1][5];
	ev[2][5] = &ev[1][6];
	ev[2][6] = &ev[0][3];
	ev[2][7] = &ev[0][4];
	ev[2][8] = &ev[1][9];

	ev[3][0] = &ev[4][0];
	ev[3][1] = &ev[2][2];
	ev[3][2] = &ev[2][3];
	ev[3][3] = &ev[8][0];
	ev[3][4] = &ev[8][1];
	ev[3][5] = &ev[2][6];
	ev[3][6] = &ev[2][7];
	ev[3][7] = &ev[4][3];
	ev[3][8] = &ev[4][4];

	ev[4][0] = &ev[5][0];
	ev[4][1] = &ev[1][3];
	ev[4][2] = &ev[1][4];
	ev[4][3] = &ev[8][3];
	ev[4][4] = &ev[5][3];

	ev[5][0] = &ev[2][1];
	ev[5][1] = &ev[6][2];
	ev[5][2] = &ev[7][2];
	ev[5][3] = &ev[2][8];

	ev[6][0] = &ev[0][1];
	ev[6][1] = &ev[2][4];
	ev[6][2] = &ev[2][5];
	ev[6][3] = &ev[5][0]; // <- looplink here

	ev[7][0] = &ev[0][2];
	ev[7][1] = &ev[4][1];
	ev[7][2] = &ev[3][5];
	ev[7][3] = &ev[3][6];

	ev[8][0] = &ev[4][2];
	ev[8][1] = &ev[6][1];
	ev[8][2] = &ev[1][7];
	ev[8][3] = &ev[1][8];

	return ev[0];
}


#define NAIVE_SPLICE_PRIME (12)
#define NAIVE_SPLICE_L1 (4)
#define NAIVE_SPLICE_L2 (12)
static void **tlb_prepnaive_splice(void *base, uintptr_t targ)
{
	uintptr_t l1t = TDL1(targ);
	uintptr_t l2t = TSL2(targ);
	//fprintf(stderr, "%" PRIxPTR " %" PRIxPTR "\n", l1t, l2t);
	void *p = base;
	void **ev[24];
	for (int i = 0; i < 24; i++) {
		ev[i] = (void **)tlb_nexthit(p, l1t, l2t);
		p = ev[i];
	}
	ev[0][0] = &ev[1][0];
	ev[0][1] = &ev[1][1];

	ev[1][0] = &ev[2][0];
	ev[1][1] = &ev[2][1];

	ev[2][0] = &ev[3][0];
	ev[2][1] = &ev[3][1];

	ev[3][0] = &ev[4][0];
	ev[3][1] = &ev[4][1];

	ev[4][0] = &ev[5][0];
	ev[4][1] = &ev[5][1];

	ev[5][0] = &ev[6][0];
	ev[5][1] = &ev[6][1];

	ev[6][0] = &ev[7][0];
	ev[6][1] = &ev[7][1];

	ev[7][0] = &ev[8][0];
	ev[7][1] = &ev[8][2];

	ev[8][0] = &ev[9][0];
	ev[8][1] = &ev[9][1];
	ev[8][2] = &ev[9][2];

	ev[9][0] = &ev[10][0];
	ev[9][1] = &ev[10][1];
	ev[9][2] = &ev[10][2];

	ev[10][0] = &ev[11][0];
	ev[10][1] = &ev[11][1];
	ev[10][2] = &ev[11][2];

	ev[11][0] = &ev[8][1];
	ev[11][1] = &ev[0][1];
	ev[11][2] = &ev[12][0];

	ev[12][0] = &ev[13][0];
	ev[12][1] = &ev[13][1];

	ev[13][0] = &ev[14][0];
	ev[13][1] = &ev[14][1];

	ev[14][0] = &ev[15][0];
	ev[14][1] = &ev[15][1];

	ev[15][0] = &ev[16][0];
	ev[15][1] = &ev[16][1];

	ev[16][0] = &ev[17][0];
	ev[16][1] = &ev[17][1];

	ev[17][0] = &ev[18][0];
	ev[17][1] = &ev[18][1];

	ev[18][0] = &ev[19][0];
	ev[18][1] = &ev[19][1];

	ev[19][0] = &ev[20][0];
	ev[19][1] = &ev[20][2];

	ev[20][0] = &ev[21][0];
	ev[20][1] = &ev[21][1];
	ev[20][2] = &ev[21][2];

	ev[21][0] = &ev[22][0];
	ev[21][1] = &ev[22][1];
	ev[21][2] = &ev[22][2];

	ev[22][0] = &ev[23][0];
	ev[22][1] = &ev[23][1];
	ev[22][2] = &ev[23][2];

	ev[23][0] = &ev[20][1];
	ev[23][1] = &ev[12][1];
	ev[23][2] = &ev[0][0];

	return ev[0];
}


#define ipfwd(head, niter) asm volatile (\
	"mov %%ecx, %%esi\n"\
	"lfence\n"\
	"1:"\
	"mov (%[probe]),%[probe]\n"\
	"loop 1b\n"\
	"lfence\n"\
	"mov %%esi, %%ecx\n"\
	: [probe]"+r" (head) : "c" (niter) : "rsi", "cc"\
)

#define ipchase(head, niter, tim) asm volatile (\
	"mov %%ecx, %%esi\n"\
	"lfence\n"\
	"rdtsc\n"\
	"mov %%eax, %%edi\n"\
	"1:"\
	"mov (%[probe]),%[probe]\n"\
	"loop 1b\n"\
	"lfence\n"\
	"rdtscp\n"\
	"sub %%edi, %%eax\n"\
	"mov %%esi, %%ecx\n"\
	: [probe]"+r" (head), "=a" (tim) : "c" (niter) : "rdx", "rdi", "rsi", "cc"\
)

static void pprint(void **head, size_t n)
{
	while (n--) {
		fprintf(stderr, "%p\n", head);
		head = *head;
	}
	fputc('\n', stderr);
}

#define rdtscp(v) asm volatile ("lfence\nmfence\nrdtscp\nshl $32,%%rdx\nor %%rdx,%%rax" : "=a" (v) :: "rdx", "rcx")

#define NINJA_ROUNDS (16*_K)
#define NINJA_THRESH (2048)
#define TIMSZ (32)

//static void prtim(uint32_t tim[TIMSZ])
//{
	//for (size_t i = 0; i < TIMSZ; i++) {
		//printf("%"PRIu32" ", tim[i]);
	//}
	//putchar('\n');
//}

static inline uint16_t nxr(uint16_t x)
{
	x ^= x >> 7;
	x ^= x << 9;
	x ^= x >> 13;
	return x;
}


#define SYNCPULSE (9)
//#define DO_DELAY {rn = nxr(rn);}
//#define DO_DELAY {rn = nxr(rn);rn = nxr(rn);}
#define DO_DELAY {rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);}
//#define DO_DELAY {rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);}
//#define DO_DELAY {rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);}
//#define DO_DELAY {rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);}
//#define DO_DELAY {rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);}
//#define DO_DELAY {rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);}

static void do_send(void *tebuf)
{
	uint16_t rn = 0xace1;
	//int v = 0;
	//volatile char *targ = (volatile char *)TARGET;
	//uintptr_t tl1 = TDL1((uintptr_t)TARGET);
	//uintptr_t tl2 = TSL2((uintptr_t)TARGET);
	uintptr_t tl1 = TDL1((uintptr_t)TARGET ^ (1 << 18));
	uintptr_t tl2 = TSL2((uintptr_t)TARGET ^ (1 << 18));
	volatile char *targ = (volatile char *)tlb_nexthit(tebuf, tl1, tl2);
	fprintf(stderr, "Sending on target: %p\n%zd %zd\n", targ, tl1, tl2);

	for (;;) {
		//for (size_t i = 0; i < (1L << (SYNCPULSE + 1)); i++) {
			//DO_DELAY;
			//if (i & (1L << SYNCPULSE)) {
				//*(volatile char *)(targ + (rn & 8));
			//}
		//}
		//for (size_t i = 0; i < (1L << (SYNCPULSE)); i++) {
			//DO_DELAY;
			//if (i & (1L << SYNCPULSE)) {
				//*(volatile char *)(targ + (rn & 8));
			//}
		//}

		for (size_t i = 0;; i++) {
		//for (size_t i = 0; i < (1L << (SYNCPULSE + 2)); i++) {
			DO_DELAY;
			//rn ^= 1;
			//if (rand() & 1) {
			//if (0) {
			//if (1) {
			//if (rn & 1) {
			//if (i & (1L << 6)) {
				*(volatile char *)(targ + (rn & 8));
				//if (v > 0) {
					//*(volatile char *)(targ + (rn & 8));
					//continue;
				//}
				//if (v < 0) {
					//v = rn & 1;
				//}

				//if (!v) {
					//v = 1;
					//*(volatile char *)(targ + (rn & 8));
					//puts("OY\n");
				//}
				//continue;
			//}
			//} else if (v >= 0) {
				//v = -1;
			//}
			//if (v) {
				//puts("EY\n");
				//*(volatile char *)(targ + (rn & 8));
				//v = 0;
			//}
		}
	}
}

#undef DO_DELAY

static void do_walkup(void *tebuf)
{
	//uint16_t rn = 0xace1;
	fputs("Walking up sets...\n", stderr);
	for (;;) {
		for (int i = 0; i < 128; i++) {
			volatile char *targ = (volatile char *)TBUFBASE + i*PAGESZ;
			for (int rep = 4096; rep-- >0;) *targ;
			//for (int bw = 1000; bw-- > 0;) rn = nxr(rn);
		}
	}
}

static void do_recv(void *tebuf, const int oneshot, const int use_l2, const int use_ninja)
{
	//volatile char const *dummy = tlb_nexthit(tebuf, TDL1((uintptr_t)TARGET), TSL2((uintptr_t)TARGET));
	//#define DUMMY() do { (void)*dummy; } while (0)
	//#define DUMMY() do { (void)*(volatile char *)TARGET; } while (0)
	#define DUMMY() do { } while (0)

	void **njhead, **njcur;
	char *evmode;
	int evpplen;
	long int it = NMEAS;

	if (use_l2) {
		if (use_ninja) {
			evmode = "Ninja L2";
			evpplen = 19;
			njhead = tlb_prepninja(TBUFBASE, (uintptr_t)TARGET);
		} else {
			evmode = "Naive L2";
			evpplen = 13;
			njhead = tlb_prepnaive(TBUFBASE, (uintptr_t)TARGET);
		}
	} else {
		if (use_ninja) {
			evmode = "Ninja L1";
			evpplen = 13;
			njhead = tlb_prepninja_l1(TBUFBASE, (uintptr_t)TARGET);
		} else {
			evmode = "Naive L1 (TLBleed-like)";
			evpplen = 5;
			njhead = tlb_prep_l1(TBUFBASE, (uintptr_t)TARGET);
		}
	}
	njcur = njhead;

	if (!oneshot) {
		fprintf(stderr, "%s mode:\n", evmode);
		pprint(njhead, evpplen);
		//fputs("Ready to recv...", stderr);
		//(void)getchar();
	}

	do {
		uint32_t tim[TIMSZ];
		struct timespec t0, t;
		long diff;
		clock_gettime(CLOCK_REALTIME, &t0);

		if (use_l2) {
			if (use_ninja) {
				for (size_t r = NINJA_ROUNDS; r-- > 0;) {
					njcur = njhead;
					tlb_evrun(tebuf, (uintptr_t)TARGET, TLB_PREPSZ);
					ipchase(njcur, NINJA_L2_INIT, tim[0]);

					for (size_t n = NINJA_THRESH/TIMSZ; n-- > 0;) {
						for (size_t ti = 0; ti < TIMSZ; ti++) {
							DUMMY();
							ipchase(njcur, NINJA_L2_STEP, tim[ti]);
						}
						//prtim(tim);
					}
					//fputs("--------\n", stdout);
				}
			} else {
				for (size_t r = NINJA_ROUNDS*NINJA_THRESH/TIMSZ; r-- > 0;) {
					for (size_t ti = 0; ti < TIMSZ; ti++) {
						DUMMY();
						ipchase(njcur, TLB_PREPSZ, tim[ti]);
					}
				}
			}
		} else {
			if (use_ninja) {
				for (size_t r = NINJA_ROUNDS; r-- > 0;) {
					njcur = njhead;
					ipchase(njcur, NINJA_L1_INIT, tim[0]);

					for (size_t n = NINJA_THRESH/TIMSZ; n-- > 0;) {
						for (size_t ti = 0; ti < TIMSZ; ti++) {
							DUMMY();
							ipchase(njcur, NINJA_L1_STEP, tim[ti]);
						}
					}
				}
			} else {
				for (size_t r = NINJA_ROUNDS*NINJA_THRESH/TIMSZ; r-- > 0;) {
					for (size_t ti = 0; ti < TIMSZ; ti++) {
						DUMMY();
						ipchase(njcur, TLB_PREPSZ_L1, tim[ti]);
					}
				}
			}
		}
		clock_gettime(CLOCK_REALTIME, &t);
		diff = usecdiff(&t0, &t);
		long rps = NINJA_ROUNDS*NINJA_THRESH*1000000L/diff;
		double mps = (double)(NINJA_ROUNDS*NINJA_THRESH)/diff;
		if (!oneshot)
			fprintf(stderr, "Recv'd %lu bits in %ld us (rate: %ld / sec ~= %4.1f M / sec)\n",
		            NINJA_ROUNDS*NINJA_THRESH, diff, rps, mps);
	} while (!oneshot && --it);
}


#define L1DSETS (16)
#define L2SSETS (128)

#define PROBEROUNDS (4096)

#include <alloca.h>
#include <fcntl.h>

static void do_probe(void *tebuf, const int use_l2, const int use_ninja, int ofd)
{
	void ***njhead, ***njcur;
	uint32_t *timbuf;
	int nsets;
	long ninjacnt = -1;
	long int it = NMEAS;

	if (use_l2) {
		njhead = alloca(L2SSETS * sizeof(*njhead));
		njcur = alloca(L2SSETS * sizeof(*njcur));
		nsets = L2SSETS;
		if (use_ninja) {
			fputs("Ninja L2 probe\n", stderr);
			for (int i = 0; i < L2SSETS; i++) {
				njhead[i] = tlb_prepninja(TBUFBASE, (uintptr_t)TBUFBASE + i * PAGESZ);
				njcur[i] = njhead[i];
			}
		} else {
			fputs("Naive L2 probe\n", stderr);
			for (int i = 0; i < L2SSETS; i++) {
				njhead[i] = tlb_prepnaive(TBUFBASE, (uintptr_t)TBUFBASE + i * PAGESZ);
				njcur[i] = njhead[i];
			}
		}
	} else {
		njhead = alloca(L1DSETS * sizeof(*njhead));
		njcur = alloca(L1DSETS * sizeof(*njcur));
		nsets = L1DSETS;
		if (use_ninja) {
			fputs("Ninja L1 probe\n", stderr);
			for (int i = 0; i < L1DSETS; i++) {
				njhead[i] = tlb_prepninja_l1(TBUFBASE, (uintptr_t)TBUFBASE + i * PAGESZ);
				njcur[i] = njhead[i];
			}
		} else {
			fputs("Naive L1 (TLBleed-like) probe\n", stderr);
			for (int i = 0; i < L1DSETS; i++) {
				njhead[i] = tlb_prep_l1(TBUFBASE, (uintptr_t)TBUFBASE + i * PAGESZ);
				njcur[i] = njhead[i];
			}
		}
	}
	timbuf = calloc(nsets * PROBEROUNDS, sizeof(*timbuf));

	fputs("Ready to probe...", stderr);
	(void)getchar();

	struct timespec t0, t;
	long diff;
	clock_gettime(CLOCK_REALTIME, &t0);

	do {
		for (int rnd = 0; rnd < PROBEROUNDS; rnd++) {
			uint32_t *tim = timbuf + rnd * nsets;
			for (int set = 0; set < nsets; set++) {
				if (use_l2) {
					if (use_ninja) {
						if (ninjacnt < 0) {
							// Re-sync
							njcur[set] = njhead[set];
							tlb_evrun(tebuf, (uintptr_t)TBUFBASE + set * PAGESZ, TLB_PREPSZ);
							ipchase(njcur[set], NINJA_L2_INIT, tim[set]);
						} else {
							ipchase(njcur[set], NINJA_L2_STEP, tim[set]);
						}
					} else {
						ipchase(njcur[set], TLB_PREPSZ, tim[set]);
					}
				} else {
					if (use_ninja) {
						if (ninjacnt < 0) {
							// Re-sync
							njcur[set] = njhead[set];
							ipchase(njcur[set], NINJA_L1_INIT, tim[set]);
						} else {
							ipchase(njcur[set], NINJA_L1_STEP, tim[set]);
						}
					} else {
						ipchase(njcur[set], TLB_PREPSZ_L1, tim[set]);
					}
				}
			}
			if (ninjacnt < 0)
				ninjacnt = NINJA_THRESH;
			ninjacnt--;
		}
		// Output timbuf
		write(ofd, timbuf, nsets * PROBEROUNDS * sizeof(*timbuf));
	} while (--it);

	clock_gettime(CLOCK_REALTIME, &t);
	diff = usecdiff(&t0, &t);
	fprintf(stderr, "Probed for %lu us (%lu us/rnd)\n", diff, diff/NMEAS/PROBEROUNDS);
}

#define SPROBEROUNDS (PROBEROUNDS)
#define SPROBEIT (4096)

static void do_setprobe(void *tebuf, const int use_l2, const int use_ninja, int ofd, int l1s, int l2s)
{
	void **njhead, **njcur;
	uint32_t *timbuf;
	long ninjacnt = -1;
	const uintptr_t SPTARG = (uintptr_t)tlb_nexthit(TBUFBASE, l1s, l2s);

	if (use_l2) {
		if (use_ninja) {
			fputs("Ninja L2 probe\n", stderr);
			njhead = tlb_prepninja(TBUFBASE, SPTARG);
			njcur = njhead;
		} else {
			fputs("Naive L2 probe\n", stderr);
			njhead = tlb_prepnaive(TBUFBASE, SPTARG);
			njcur = njhead;
		}
	} else {
		if (use_ninja) {
			fputs("Ninja L1 probe\n", stderr);
			njhead = tlb_prepninja_l1(TBUFBASE, SPTARG);
			njcur = njhead;
		} else {
			fputs("Naive L1 (TLBleed-like) probe\n", stderr);
			njhead = tlb_prep_l1(TBUFBASE, SPTARG);
			njcur = njhead;
		}
	}
	timbuf = calloc(SPROBEROUNDS, sizeof(*timbuf));

	//fputs("Ready to probe...", stderr);
	//(void)getchar();

	size_t measit = NMEAS;

	do {
		long int it = SPROBEIT;

		struct timespec t0, t;
		long diff;
		clock_gettime(CLOCK_REALTIME, &t0);

		do {
			for (int rnd = 0; rnd < SPROBEROUNDS; rnd++) {
				//for (int set = 0; set < nsets; set++) {
					if (use_l2) {
						if (use_ninja) {
							if (ninjacnt < 0) {
								// Re-sync
								njcur = njhead;
								tlb_evrun(tebuf, SPTARG, TLB_PREPSZ);
								ipchase(njcur, NINJA_L2_INIT, timbuf[rnd]);
							} else {
								/* HACK: time last access only */
								ipchase(njcur, NINJA_L2_STEP, timbuf[rnd]);
								//ipchase(njcur, NINJA_L2_STEP - 1, timbuf[rnd]);
								//ipchase(njcur, 1, timbuf[rnd]);
							}
						} else {
							ipchase(njcur, TLB_PREPSZ, timbuf[rnd]);
						}
					} else {
						if (use_ninja) {
							if (ninjacnt < 0) {
								// Re-sync
								njcur = njhead;
								ipchase(njcur, NINJA_L1_INIT, timbuf[rnd]);
							} else {
								ipchase(njcur, NINJA_L1_STEP, timbuf[rnd]);
							}
						} else {
							ipchase(njcur, TLB_PREPSZ_L1, timbuf[rnd]);
						}
					}
				//}
				if (ninjacnt < 0)
					ninjacnt = NINJA_THRESH;
				ninjacnt--;
			}
			// Output timbuf
			write(ofd, timbuf, SPROBEROUNDS * sizeof(*timbuf));
		} while (--it);

		clock_gettime(CLOCK_REALTIME, &t);
		diff = usecdiff(&t0, &t);
		fprintf(stderr, "Probed for %lu us (%lu ns/rnd ; %f M/sec)\n", diff, diff*1000/SPROBEIT/SPROBEROUNDS, SPROBEIT*SPROBEROUNDS*1.0/diff);
	} while (--measit);
}


static void do_splice_setprobe_ninja(void *tebuf, int ofd, int l1s, int l2s)
{
	void **njhead, **njcur;
	uint32_t *timbuf;
	long ninjacnt = -1;
	const uintptr_t SPTARG = (uintptr_t)tlb_nexthit(TBUFBASE, l1s, l2s);

	fputs("Ninja Spliced L1+L2 probe\n", stderr);
	njhead = tlb_prepninja_splice(TBUFBASE, SPTARG);
	njcur = njhead;

	timbuf = calloc(2 * SPROBEROUNDS, sizeof(*timbuf));

	//fputs("Ready to probe...", stderr);
	//(void)getchar();

	size_t measit = NMEAS;

	do {
		long int it = SPROBEIT;

		struct timespec t0, t;
		long diff;
		clock_gettime(CLOCK_REALTIME, &t0);

		do {
			for (int rnd = 0; rnd < SPROBEROUNDS; rnd++) {
				if (ninjacnt < 0) {
					// Re-sync
					njcur = njhead;
					tlb_evrun(tebuf, SPTARG, TLB_PREPSZ);
					ipchase(njcur, NINJA_SPLICE_INIT, timbuf[2*rnd]);
					timbuf[2*rnd + 1] = 0;
					ninjacnt = NINJA_THRESH;
				} else {
					ipchase(njcur, NINJA_SPLICE_L1_STEP, timbuf[2*rnd]);
					ipchase(njcur, NINJA_SPLICE_L2_STEP, timbuf[2*rnd + 1]);
				}
				ninjacnt--;
			}
			// Output timbuf
			write(ofd, timbuf, 2 * SPROBEROUNDS * sizeof(*timbuf));
		} while (--it);

		clock_gettime(CLOCK_REALTIME, &t);
		diff = usecdiff(&t0, &t);
		fprintf(stderr, "Probed for %lu us (%lu ns/rnd ; %f M/sec)\n", diff, diff*1000/SPROBEIT/SPROBEROUNDS, SPROBEIT*SPROBEROUNDS*1.0/diff);
	} while (--measit);
}

static void do_splice_setprobe_naive(void *tebuf, int ofd, int l1s, int l2s)
{
	void **cur;
	uint32_t *timbuf;
	const uintptr_t SPTARG = (uintptr_t)tlb_nexthit(TBUFBASE, l1s, l2s);

	fputs("Naive Spliced L1+L2 probe\n", stderr);
	cur = tlb_prepnaive_splice(TBUFBASE, SPTARG);

	timbuf = calloc(2 * SPROBEROUNDS, sizeof(*timbuf));

	//fputs("Ready to probe...", stderr);
	//(void)getchar();

	size_t measit = NMEAS;

	do {
		long int it = SPROBEIT;

		struct timespec t0, t;
		long diff;
		clock_gettime(CLOCK_REALTIME, &t0);

		do {
			for (int rnd = 0; rnd < SPROBEROUNDS; rnd++) {
				ipfwd(cur, NAIVE_SPLICE_PRIME);
				ipchase(cur, NAIVE_SPLICE_L1, timbuf[2*rnd]);
				ipchase(cur, NAIVE_SPLICE_L2, timbuf[2*rnd + 1]);
			}
			// Output timbuf
			write(ofd, timbuf, 2 * SPROBEROUNDS * sizeof(*timbuf));
		} while (--it);

		clock_gettime(CLOCK_REALTIME, &t);
		diff = usecdiff(&t0, &t);
		fprintf(stderr, "Probed for %lu us (%lu ns/rnd ; %f M/sec)\n", diff, diff*1000/SPROBEIT/SPROBEROUNDS, SPROBEIT*SPROBEROUNDS*1.0/diff);
	} while (--measit);
}



#define HIST_BSZ (10)
#define HIST_BNR (80)
#define HIST_SAMPLES (100000)

static void do_hist(void *tebuf, const int use_l2, const int use_ninja)
{
	void ***njhead, ***njcur;
	uint32_t *timbuf;
	int nsets;
	long ninjacnt = -1;
	long int it = NMEAS;

	if (use_l2) {
		njhead = alloca(L2SSETS * sizeof(*njhead));
		njcur = alloca(L2SSETS * sizeof(*njcur));
		nsets = L2SSETS / 2;
		if (use_ninja) {
			fputs("Ninja L2 hist\n", stderr);
			for (int i = 0; i < L2SSETS; i++) {
				njhead[i] = tlb_prepninja(TBUFBASE, (uintptr_t)TBUFBASE + i * PAGESZ);
				njcur[i] = njhead[i];
			}
		} else {
			fputs("Naive L2 hist\n", stderr);
			for (int i = 0; i < L2SSETS; i++) {
				njhead[i] = tlb_prepnaive(TBUFBASE, (uintptr_t)TBUFBASE + i * PAGESZ);
				njcur[i] = njhead[i];
			}
		}
	} else {
		njhead = alloca(L1DSETS * sizeof(*njhead));
		njcur = alloca(L1DSETS * sizeof(*njcur));
		nsets = L1DSETS;
		if (use_ninja) {
			fputs("Ninja L1 hist\n", stderr);
			for (int i = 0; i < L1DSETS; i++) {
				njhead[i] = tlb_prepninja_l1(TBUFBASE, (uintptr_t)TBUFBASE + i * PAGESZ);
				njcur[i] = njhead[i];
			}
		} else {
			fputs("Naive L1 (TLBleed-like) hist\n", stderr);
			for (int i = 0; i < L1DSETS; i++) {
				njhead[i] = tlb_prep_l1(TBUFBASE, (uintptr_t)TBUFBASE + i * PAGESZ);
				njcur[i] = njhead[i];
			}
		}
	}
	timbuf = calloc(nsets * HIST_BNR, sizeof(*timbuf));

	fputs("Ready to probe...", stderr);
	(void)getchar();

	//struct timespec t0, t;
	//long diff;
	//clock_gettime(CLOCK_REALTIME, &t0);

	do {
		for (int rnd = 0; rnd < HIST_SAMPLES; rnd++) {
			for (int set = 0; set < nsets; set++) {
				uint32_t tim;
				if (use_l2) {
					if (use_ninja) {
						if (ninjacnt < 0) {
							// Re-sync
							njcur[set] = njhead[set];
							tlb_evrun(tebuf, (uintptr_t)TBUFBASE + set * PAGESZ, TLB_PREPSZ);
							ipchase(njcur[set], NINJA_L2_INIT, tim);
						} else {
							ipchase(njcur[set], NINJA_L2_STEP, tim);
						}
					} else {
						ipchase(njcur[set], TLB_PREPSZ, tim);
					}
				} else {
					if (use_ninja) {
						if (ninjacnt < 0) {
							// Re-sync
							njcur[set] = njhead[set];
							ipchase(njcur[set], NINJA_L1_INIT, tim);
						} else {
							ipchase(njcur[set], NINJA_L1_STEP, tim);
						}
					} else {
						ipchase(njcur[set], TLB_PREPSZ_L1, tim);
					}
				}
				int buck = tim/HIST_BSZ;
				if (buck < HIST_BNR) {
					timbuf[set*HIST_BNR + buck]++;
				}
			}
			if (ninjacnt < 0)
				ninjacnt = NINJA_THRESH;
			ninjacnt--;
		}
		// Output timbuf
		for (int set = 0; set < nsets; set++) {
			for (int b = 0; b < HIST_BNR; b++) {
				printf("%5u ", timbuf[set * HIST_BNR + b]);
			}
			putchar('\n');
		}
		putchar('\n');
		memset(timbuf, 0, nsets * HIST_BNR * sizeof(*timbuf));
	} while (--it);

	//clock_gettime(CLOCK_REALTIME, &t);
	//diff = usecdiff(&t0, &t);
	//fprintf(stderr, "Probed for %lu us (%lu us/rnd)\n", diff, diff/NMEAS/PROBEROUNDS);
}

static void do_sethist(void *tebuf, const int use_l2, const int use_ninja, int l1s, int l2s)
{
	void **njhead, **njcur;
	uint32_t *timbuf;
	long ninjacnt = -1;
	long int it = -1;
	const uintptr_t SPTARG = (uintptr_t)tlb_nexthit(TBUFBASE, l1s, l2s);

	if (use_l2) {
		if (use_ninja) {
			fputs("Ninja L2 probe\n", stderr);
			njhead = tlb_prepninja(TBUFBASE, SPTARG);
			njcur = njhead;
		} else {
			fputs("Naive L2 probe\n", stderr);
			njhead = tlb_prepnaive(TBUFBASE, SPTARG);
			njcur = njhead;
		}
	} else {
		if (use_ninja) {
			fputs("Ninja L1 probe\n", stderr);
			njhead = tlb_prepninja_l1(TBUFBASE, SPTARG);
			njcur = njhead;
		} else {
			fputs("Naive L1 (TLBleed-like) probe\n", stderr);
			njhead = tlb_prep_l1(TBUFBASE, SPTARG);
			njcur = njhead;
		}
	}
	timbuf = calloc(HIST_BNR, sizeof(*timbuf));

	fputs("Ready to probe...", stderr);
	(void)getchar();

	//struct timespec t0, t;
	//long diff;
	//clock_gettime(CLOCK_REALTIME, &t0);

	do {
		for (int rnd = 0; rnd < HIST_SAMPLES; rnd++) {
			uint32_t tim;
			if (use_l2) {
				if (use_ninja) {
					if (ninjacnt < 0) {
						// Re-sync
						njcur = njhead;
						tlb_evrun(tebuf, SPTARG, TLB_PREPSZ);
						ipchase(njcur, NINJA_L2_INIT, tim);
					} else {
						ipchase(njcur, NINJA_L2_STEP, tim);
					}
				} else {
					ipchase(njcur, TLB_PREPSZ, tim);
				}
			} else {
				if (use_ninja) {
					if (ninjacnt < 0) {
						// Re-sync
						njcur = njhead;
						ipchase(njcur, NINJA_L1_INIT, tim);
					} else {
						ipchase(njcur, NINJA_L1_STEP, tim);
					}
				} else {
					ipchase(njcur, TLB_PREPSZ_L1, tim);
				}
			}
			int buck = tim/HIST_BSZ;
			if (buck < HIST_BNR) {
				timbuf[buck]++;
			}
			if (ninjacnt < 0)
				ninjacnt = NINJA_THRESH;
			ninjacnt--;
		}
		// Output timbuf
		for (int b = 0; b < HIST_BNR; b++) {
			printf("%5u ", timbuf[b]);
		}
		putchar('\n');
		memset(timbuf, 0, HIST_BNR * sizeof(*timbuf));
	} while (--it);

	//clock_gettime(CLOCK_REALTIME, &t);
	//diff = usecdiff(&t0, &t);
	//fprintf(stderr, "Probed for %lu us (%lu us/rnd)\n", diff, diff/NMEAS/PROBEROUNDS);
}



const char USAGE[] = "usage:\n'%s -s' : run as sender\n'%s -r[nj12]' : run as receiver\n'%s -h' : print this message\n";

int main(int argc, char *argv[])
{
	if (argc < 2 || !strcmp(argv[1], "-h")) {
		goto err_usage;
	} else {
		void *tebuf;
		int use_l2 = USE_L2;
		int use_ninja = NINJA;

		if (setup_tbuf() == MAP_FAILED) {
			perror("Error mapping target buffer");
			return 2;
		}
		if ((tebuf = setup_evbuf()) == MAP_FAILED) {
			perror("Error setting up eviction buf");
			return 2;
		}

		if (argv[1][0] == '-') {
			switch (argv[1][1]) {
				case 'w':
					do_walkup(tebuf);
					break;
				case 's':
					if (!argv[1][2]) {
						do_send(tebuf);
						break;
					}
					// fall through
				default:
					fprintf(stderr, "Unknown option: '%s'\n", argv[1]);
					goto err_usage;
				case 'r':
				case 'c':
				case 'p':
				case 'P':
				case 'h':
				case 'H':
					for (char *c = &argv[1][2]; *c; c++) {
						switch (*c) {
							case 'n': case 'N': use_ninja = 0; break;
							case 'j': case 'J': use_ninja = 1; break;
							case '1': use_l2 = 0; break;
							case '2': use_l2 = 1; break;
							default:
								fprintf(stderr, "Unknown suboption to -%c: '%s'\n", argv[1][1], c);
								goto err_usage;
						}
					}
					switch (argv[1][1]) {
						case 'r':
						case 'c':
							do_recv(tebuf, argv[1][1] == 'c', use_l2, use_ninja);
							break;
						case 'p':
						case 'P':
						{
							int fd = open(argv[2], O_RDWR|O_CREAT|O_TRUNC, 0664);
							if (fd < 0) {
								perror("Unable to open output file");
								goto err_usage;
							}
							if (argv[1][1] == 'p') {
								do_probe(tebuf, use_l2, use_ninja, fd);
							} else {
								do_setprobe(tebuf, use_l2, use_ninja, fd, atoi(argv[3]), atoi(argv[4]));
							}
							close(fd);
						}
							break;
						case 'h':
							do_hist(tebuf, use_l2, use_ninja);
							break;
						case 'H':
							do_sethist(tebuf, use_l2, use_ninja, atoi(argv[2]), atoi(argv[3]));
							break;
					}
					break;
				case 'S':
				{
					int fd = open(argv[2], O_RDWR|O_CREAT|O_TRUNC, 0664);
					if (fd < 0) {
						perror("Unable to open output file");
						goto err_usage;
					}
					switch (argv[1][2]) {
						case 'j':
							do_splice_setprobe_ninja(tebuf, fd, atoi(argv[3]), atoi(argv[4]));
							break;
						case 'n':
							do_splice_setprobe_naive(tebuf, fd, atoi(argv[3]), atoi(argv[4]));
							break;
					}
					close(fd);
				}
					break;
			}
		} else {
			fprintf(stderr, "Unknown argument: '%s'\n", argv[1]);
			goto err_usage;
		}
	}
	return 0;
err_usage:
	fprintf(stderr, USAGE, argv[0], argv[0], argv[0]);
	return 1;
}
