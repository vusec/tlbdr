#ifndef _PGTABLE_H_
#define _PGTABLE_H_

#include <linux/syscalls.h>
#include <linux/version.h>

#define MMUCTL_PGD (1 << 0)
#define MMUCTL_P4D (1 << 1)
#define MMUCTL_PUD (1 << 2)
#define MMUCTL_PMD (1 << 3)
#define MMUCTL_PTE (1 << 4)
#define NXBIT (1ULL << 63)

struct ptwalk {
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

int resolve_va(size_t addr, struct ptwalk *entry, int lock);
void clear_nx(pgd_t *p);

static inline __attribute__((always_inline)) void switch_pages(pte_t *pte1, pte_t *pte2)
{
	u64 ptev = pte1->pte;
	pte1->pte = pte2->pte;
	pte2->pte = ptev;
}

#endif
