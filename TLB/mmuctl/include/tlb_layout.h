#ifndef _TLB_LAYOUT_H_
#define _TLB_LAYOUT_H_

#include <helpers.h>
#include <mem_access.h>
#include <pgtable.h>
#include <linux/random.h>
#include <address_generation.h>
#include "../../settings.h"

int non_inclusivity(void);
int non_exclusivity(void);
int reinsert_itlb(void);
int reinsert_dtlb(void);
int reinsert_stlb_data(void);
int reinsert_stlb_instruction(void);
int reinsert_stlb_itlb_eviction(void);
int reinsert_stlb_dtlb_eviction(void);
#endif