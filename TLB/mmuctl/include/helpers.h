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
void merge(unsigned long list1[], unsigned long list2[], int length, unsigned long result[]);
int get_stlb_set(int max, int force_random);
int get_itlb_set(int max, int force_random);
int get_dtlb_set(int max, int force_random);
void spirt(u64 *p);
int unsafe_address(unsigned long addr);
void get_random_pcids(unsigned long pcids[]);
int compute_xor_set(unsigned long addr, int set_bits);
int compute_lin_set(unsigned long addr, int set_bits);
int max_index(int list[], int length);
void shuffle(unsigned long list[], int length);

#endif
