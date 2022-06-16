#ifndef _MEM_ACCESS_H_
#define _MEM_ACCESS_H_

#include <linux/mm.h>
#include <linux/uaccess.h>

typedef u64 (*retf_t)(void);

/*
	Helper function. Returns the location at which we should write
	the next address of the pointer chain.
*/
static inline __attribute__((always_inline)) unsigned long get_exec_pointer_address(volatile unsigned long addr, volatile unsigned int offset){
    return addr + 4096 - (offset * 13);
}

/*
	Writes the next address ('goal') of the pointer chain on the 'start'
	address, taking the given offset into account.
*/
static inline __attribute__((always_inline)) void write_instruction_chain(volatile unsigned long start, volatile unsigned int *offset, volatile unsigned long goal){
	__uaccess_begin_nospec();
    volatile unsigned char *p1 = get_exec_pointer_address(start, *(offset));
    *(uint16_t *)p1 = 0x9090;
    p1[2] = 0x48; p1[3] = 0xb8;
    *(uint64_t *)(&p1[4]) = goal;
    p1[12] = 0xc3;	
	__uaccess_end();

    *(offset) += 1;
}

/*
	Returns the identifier of the page indicated by 'addr'.
	'addr' is accessed using a data load.
*/
static inline __attribute__((always_inline)) unsigned long read(volatile unsigned long addr){	
	volatile unsigned long val;
	__uaccess_begin_nospec();
	asm volatile("mfence\nlfence\n" ::: "memory");
	val = *((volatile uint64_t *)&((volatile char *)(volatile void *)addr)[4]);
	__uaccess_end();

	return val;
}

/*
	Returns the next address of the pointer chain, indicated
	by 'addr' and 'offset'. 
	'addr' is accessed using a data load.
*/
static inline __attribute__((always_inline)) unsigned long read_walk(volatile unsigned long addr, volatile unsigned int *offset){	
	volatile unsigned long val;
	__uaccess_begin_nospec();
	asm volatile("mfence\nlfence\n" ::: "memory");
	val = *((volatile uint64_t *)&((volatile char *)(volatile void *)(addr + 4096 - (*(offset) * 13)))[4]);
	__uaccess_end();
	
	*(offset) += 1;

	return val;
}

/*
	Returns the identifier of the page indicated by 'addr'.
	'addr' is accessed using an instruction fetch.
*/
static inline __attribute__((always_inline)) unsigned long execute(volatile unsigned long addr){
	volatile unsigned long val;
	__uaccess_begin_nospec();
	asm volatile("mfence\nlfence\n" ::: "memory");
	val = ((volatile retf_t)addr)();
	__uaccess_end();

	return val;
}

/*
	Returns the next address of the pointer chain, indicated
	by 'addr' and 'offset'. 
	'addr' is accessed using an instruction fetch.
*/
static inline __attribute__((always_inline)) unsigned long execute_walk(volatile unsigned long addr, volatile unsigned int *offset){
	volatile unsigned long val;
	__uaccess_begin_nospec();
	asm volatile("mfence\nlfence\n" ::: "memory");
	val = ((volatile retf_t)(addr + 4096 - (*(offset) * 13)))();
	__uaccess_end();

	*(offset) += 1;

	return val;
}

#endif

