#ifndef _PCID_H_
#define _PCID_H_

#include <pgtable.h>
#include <helpers.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <mem_access.h>
#include <address_generation.h>
#include "../../settings.h"

int stlb_pcid_limit(int pcid_writes, int no_flush);
int dtlb_pcid_limit(int pcid_writes, int no_flush);
int itlb_pcid_limit(int pcid_writes, int no_flush);

void detect_stlb_pcid_permutation(int vector_index, int vector[], int *agreement, int no_flush);

#endif