#ifndef _HASH_H_
#define _HASH_H_

#include <helpers.h>
#include <mem_access.h>
#include <pgtable.h>
#include <linux/random.h>
#include <address_generation.h>
#include "../../settings.h"
#include <linux/vmalloc.h>

int detect_bits_dtlb(int set_bits, int bit, int ways);
int detect_bits_itlb(int set_bits, int bit, int ways);

int determine_inclusivity_instructions(int set_bits, int ways);
int determine_inclusivity_data(int set_bits, int ways);

int seperate_dtlb_and_itlb(int ways);
int seperate_itlb_and_dtlb(int ways);

#endif
