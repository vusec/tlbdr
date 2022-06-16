#ifndef _REPLACEMENT_H_
#define _REPLACEMENT_H_

#include <helpers.h>
#include <mem_access.h>
#include <pgtable.h>
#include <linux/random.h>
#include <address_generation.h>
#include "../../settings.h"

#define nmru3plru4_evict_length (10)
#define nmru3plru4_noevict_length (69)
static int nmru3plru4_evict[nmru3plru4_evict_length] = {0, 1, 2, 3, 2, 4, 3, 2, 5, 0};
static int nmru3plru4_noevict[nmru3plru4_noevict_length] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 1, 12, 4, 13, 7, 14, 10, 15, 1, 16, 4, 17, 7, 18, 10, 19, 1, 20, 4, 21, 7, 22, 10, 23, 1, 24, 4, 25, 7, 26, 10, 27, 1, 28, 4, 29, 7, 30, 10, 31, 1, 32, 4, 33, 7, 34, 10, 35, 1, 36, 4, 37, 7, 38, 10, 39, 0};

#define plru4_evict_length (6)
#define plru4_noevict_length (77)
static int plru4_evict[plru4_evict_length] = {0, 1, 2, 1, 3, 0};
static int plru4_noevict[plru4_noevict_length] = {0, 1, 2, 3, 2, 4, 2, 5, 2, 6, 2, 7, 2, 8, 2, 9, 2, 10, 2, 11, 2, 12, 2, 13, 2, 14, 2, 15, 2, 16, 2, 17, 2, 18, 2, 19, 2, 20, 2, 21, 2, 22, 2, 23, 2, 24, 2, 25, 2, 26, 2, 27, 2, 28, 2, 29, 2, 30, 2, 31, 2, 32, 2, 33, 2, 34, 2, 35, 2, 36, 2, 37, 2, 38, 2, 39, 0};

#define lru4_evict_length (6)
#define lru4_noevict_length (5)
static int lru4_evict[plru4_evict_length] = {0, 1, 2, 3, 4, 0};
static int lru4_noevict[plru4_noevict_length] = {0, 1, 2, 3, 0};

#define plru8_evict_length (9)
#define plru8_noevict_length (73)
static int plru8_evict[plru8_evict_length] = {0, 1, 2, 1, 3, 2, 1, 4, 0};
static int plru8_noevict[plru8_noevict_length] = {0, 1, 2, 3, 4, 5, 6, 7, 2, 8, 4, 9, 6, 10, 2, 11, 4, 12, 6, 13, 2, 14, 4, 15, 6, 16, 2, 17, 4, 18, 6, 19, 2, 20, 4, 21, 6, 22, 2, 23, 4, 24, 6, 25, 2, 26, 4, 27, 6, 28, 2, 29, 4, 30, 6, 31, 2, 32, 4, 33, 6, 34, 2, 35, 4, 36, 6, 37, 2, 38, 4, 39, 0};

int test_shared_replacement(int sequence[], int length, int failure_distribution[], int distribution[], int expect_eviction);
int test_split_data_replacement(int sequence[], int length, int failure_distribution[], int distribution[], int expect_eviction);
int test_split_instruction_replacement(int sequence[], int length, int failure_distribution[], int distribution[], int expect_eviction);

void test_nmru3plru(int (*test_function)(int[], int, int[], int[], int), int *short_succ, int *long_succ, int failure_distribution[], int distribution[]);
void test_plru4(int (*test_function)(int[], int, int[], int[], int), int *short_succ, int *long_succ, int failure_distribution[], int distribution[]);
void test_lru4(int (*test_function)(int[], int, int[], int[], int), int *short_succ, int *long_succ, int failure_distribution[], int distribution[]);
void test_plru8(int (*test_function)(int[], int, int[], int[], int), int *short_succ, int *long_succ, int failure_distribution[], int distribution[]);
void build_sequence_string(char buffer[], int buff_length, int sequence[], int sequence_length);

void build_failures_string(char buffer[], int buff_length, int failures[], int total[], int number_of_sets);

void build_plru4_message(char message[], char *component, int short_succ, int long_succ, int failure_distribution[], int distribution[], int number_of_sets);
void build_plru8_message(char message[], char *component, int short_succ, int long_succ, int failure_distribution[], int distribution[], int number_of_sets);
void build_nmru3plru_message(char message[], char *component, int short_succ, int long_succ, int failure_distribution[], int distribution[], int number_of_sets);
void build_lru4_message(char message[], char *component, int short_succ, int long_succ, int failure_distribution[], int distribution[], int number_of_sets);
#endif
