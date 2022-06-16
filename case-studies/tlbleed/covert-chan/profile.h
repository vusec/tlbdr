/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>

typedef uint64_t cycles_t;

static inline void code_barrier(void)
{
	asm volatile("cpuid\n" :: "a" (0) : "%rbx", "%rcx", "%rdx");
}

static inline void data_barrier(void)
{
	asm volatile("mfence\n" ::: "memory");
}

static inline cycles_t rdtsc(void)
{
	cycles_t cycles_lo, cycles_hi;
	
	asm volatile("rdtscp\n" :
		"=a" (cycles_lo), "=d" (cycles_hi) ::
		"%rcx");

	return ((uint64_t)cycles_hi << 32) | cycles_lo;
}


static inline uint64_t profile_access(volatile char *p)
{
	uint64_t past, now;

	data_barrier();
	code_barrier();
	past = rdtsc();
	data_barrier();

	*p;
	
	data_barrier();
	now = rdtsc();
	code_barrier();
	data_barrier();

	return now - past;
}

static inline uint64_t profile_call_return(void (*p)(void))
{
	uint64_t past, now;

	data_barrier();
	code_barrier();
	past = rdtsc();
	data_barrier();

	p();
	
	data_barrier();
	now = rdtsc();
	code_barrier();
	data_barrier();

	return now - past;
}

#define PROFILE_ACCESS(code, cycles) do { \
	uint64_t _past, _now; \
	data_barrier(); \
	code_barrier(); \
	_past = rdtsc(); \
	data_barrier(); \
    do { code } while(0);   \
	data_barrier(); \
	_now = rdtsc(); \
	code_barrier(); \
	data_barrier(); \
    cycles = _now-_past; \
} while(0)
