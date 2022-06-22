#ifndef FUNCTIONS_H_INCLUDED
#define FUNCTIONS_H_INCLUDED

#include <linux/mm.h>
#include "../../settings.h"
#include <linux/random.h>
#include <linux/uaccess.h>

static volatile unsigned long flags;

long int set_bits_to_sets(int set_bits);
void claim_cpu(void);
void give_up_cpu(void);
u64 setcr3(u64 val);
u64 getcr3(void);
void disable_smep(void);
int get_itlb_set(int max, int force_random);
int get_dtlb_set(int max, int force_random);
int unsafe_address(unsigned long addr);
void shuffle(unsigned long list[], int length);

#endif
