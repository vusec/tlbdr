#ifndef _PERMUTATION_H_
#define _PERMUTATION_H_

#include <linux/syscalls.h>
#include <linux/version.h>
#include "../../settings.h"
#include <helpers.h>
#include <address_generation.h> 
#include <pgtable.h>
#include <mem_access.h>

volatile struct experiment_info{
    volatile unsigned int original;
    volatile unsigned int curr;
    volatile unsigned int i;
    volatile unsigned int number_of_washings;
    volatile unsigned int ways;
    volatile unsigned int position;
    volatile unsigned int iteration;
    volatile unsigned long p;
    volatile unsigned int element;
    volatile unsigned int target_stlb_set;
    volatile unsigned int target_dtlb_set;
    volatile unsigned int target_itlb_set;
    volatile unsigned int vector_index;
};

int __attribute__((optimize("O0"))) stlb_vector_evicted(volatile struct experiment_info info);
void __attribute__((optimize("O0"))) detect_stlb_vector(volatile unsigned int vector_index, volatile int vector[], volatile unsigned int *agreement, volatile unsigned int set_mistakes_early[][tlb.shared_component->ways], volatile unsigned int set_mistakes_late[][tlb.shared_component->ways], int set_attempts[]);

int __attribute__((optimize("O0"))) dtlb_vector_evicted(volatile struct experiment_info info);
void __attribute__((optimize("O0"))) detect_dtlb_vector(volatile unsigned int vector_index, volatile int vector[], volatile unsigned int *agreement, volatile unsigned int set_mistakes_early[][tlb.split_component_data->ways], volatile unsigned int set_mistakes_late[][tlb.split_component_data->ways], int set_attempts[]);

int __attribute__((optimize("O0"))) itlb_vector_evicted(volatile struct experiment_info info);
void __attribute__((optimize("O0"))) detect_itlb_vector(volatile unsigned int vector_index, volatile int vector[], volatile unsigned int *agreement, volatile unsigned int set_mistakes_early[][tlb.split_component_instruction->ways], volatile unsigned int set_mistakes_late[][tlb.split_component_instruction->ways], int set_attempts[]);

void __attribute__((optimize("O0"))) walk_dtlb_chain(volatile struct experiment_info *info, volatile pte_t *pte);
void __attribute__((optimize("O0"))) walk_stlb_chain(volatile struct experiment_info *info, volatile pte_t *pte);
void __attribute__((optimize("O0"))) walk_itlb_chain(volatile struct experiment_info *info, volatile pte_t *pte);

#endif
