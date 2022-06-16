#ifndef _HASH_H_
#define _HASH_H_

#include <helpers.h>
#include <mem_access.h>
#include <pgtable.h>
#include <linux/random.h>
#include <address_generation.h>
#include "../../settings.h"

int test_lin_stlb(int set_bits, int ways);
int test_xor_stlb(int set_bits, int ways);

int test_lin_itlb_stlb_xor(int set_bits, int ways);
int test_lin_itlb_stlb_lin(int set_bits, int ways);

int test_lin_dtlb_stlb_lin(int set_bits, int ways);
int test_lin_dtlb_stlb_xor(int set_bits, int ways);

#endif
