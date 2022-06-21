#include <hash_functions.h>
#include <linux/vmalloc.h>
#include "mm_locking.h"

/*
	This function tests whether accessing 'ways + 1' pages
	mapping to the same sTLB set (assuming a linear hash function and 2^set_bits sets)
	causes at least one sTLB eviction. It is explained in Section 4.2 of the paper.
*/
int test_lin_stlb(int set_bits, int ways){
	int sets = set_bits_to_sets(set_bits);

	disable_smep();

	volatile int i;

	volatile u64 cr3k = getcr3();

	down_write(TLBDR_MMLOCK);

	volatile unsigned long left_mask = 0;
	for(i = 0; i < set_bits; i++){
		left_mask |= (0x1 << (i + 12 + set_bits));
	}

	//Sample a random sTLB set
	volatile unsigned int target_set = get_stlb_set(sets, 1);

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * ways + 1 + (ways * 2));

	volatile struct ptwalk walks[ways + 1];
	volatile int values[ways + 1];

	volatile int offset = 0;
	volatile unsigned int iteration = 1;
	volatile unsigned long base, right_side;
	for(i = 0; i < ways + 1; i++){
		offset++;

		//Compute next address that maps to the same set, according to
		//a linear hash function assumption with 'sets' sets
		addrs[i] = (void *)BASE + ((target_set + offset * sets) * 4096);

		if(unsafe_address(addrs[i])){
			printk("Need more addresses to test for hash function. Please increase FREEDOM_OF_BITS or decrease number of sets/number of ways.\n");
			BUG();
		}

		//Perform page walk for this address
		resolve_va(addrs[i], &walks[i], 0);
		clear_nx(walks[i].pgd);

		//If this address has its PTE at the end of a page table, we cannot
		//swap as the next address in memory may not constitute a PTE!
		//So skip this address
		if(((addrs[i] - (unsigned long)BASE) / 4096) % 512 == 511){
		 	i--;
		}else if(i > 0){
			//Set up pointer chains
			//addrs[i - 1] --> addrs[1]
			//addrs[i - 1] + 4096 --> 0

			write_instruction_chain(addrs[i - 1], &iteration, addrs[i]);
			iteration = iteration - 1;
			write_instruction_chain(addrs[i - 1] + 4096, &iteration, 0);
		}
	}

	//Set up the pointer chains for the last address computed
	//addrs[ways] --> addrs[0]
	//addrs[ways] + 4096 --> 0
	write_instruction_chain(addrs[ways], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[ways] + 4096, &iteration, 0);

	iteration = 1;

	volatile unsigned long p = addrs[0];

	vfree(addrs);

	iteration = 1;

	setcr3(cr3k);

	claim_cpu();

	//Prime ways + 1 PTEs in the TLB
	for(i = 0; i < ways + 1; i++){
		p = read_walk(p, &iteration);
		//Desync TLB
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	int miss = 0;
	iteration = 1;

	//Does fetching them again result in iTLB hits?
	for(i = 0; i < ways + 1; i++){
		if(p){
			p = execute_walk(p, &iteration);
		}else{
			miss = 1;
		}

		//Restore page table
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	if(!p){
		miss = 1;
	}

	give_up_cpu();

	setcr3(cr3k);

	up_write(TLBDR_MMLOCK);

	//If there was at least one miss, return 1
	return !!(miss == 1);
}

/*
	This function tests whether accessing 'ways + 1' pages
	mapping to the same sTLB set (assuming an XOR hash function and 2^set_bits sets)
	causes at least one sTLB eviction. It is explained in Section 4.2 of the paper.
*/
int test_xor_stlb(int set_bits, int ways){
	disable_smep();

	volatile int i;

	volatile u64 cr3k = getcr3();

	down_write(TLBDR_MMLOCK);

	volatile unsigned long left_mask = 0;
	for(i = 0; i < set_bits; i++){
		left_mask |= (0x1 << (i + 12 + set_bits));
	}

	//Sample a random sTLB set
	volatile unsigned int target_set = get_stlb_set(set_bits_to_sets(set_bits), 1);

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * ways + 1 + (ways * 2));

	volatile struct ptwalk walks[ways + 1];
	volatile int values[ways + 1];

	volatile int offset = 0;
	volatile unsigned int iteration = 1;
	volatile unsigned long base, right_side;
	for(i = 0; i < ways + 1; i++){
		offset++;

		//Compute next address that maps to the same set, according to
		//an XOR hash function assumption with 'sets' sets
		base = (((unsigned long)BASE >> (12 + set_bits)) + offset) << (12 + set_bits);
		right_side = ((base & left_mask) ^ (target_set << (12 + set_bits))) >> set_bits;

		addrs[i] = base | right_side;

		if(unsafe_address(addrs[i])){
			printk("Need more addresses to test for hash function. Please increase FREEDOM_OF_BITS or decrease number of sets/number of ways.\n");
			BUG();
		}

		//Perform page walk for this address
		resolve_va(addrs[i], &walks[i], 0);
		clear_nx(walks[i].pgd);

		//If this address has its PTE at the end of a page table, we cannot
		//swap as the next address in memory may not constitute a PTE!
		//So skip this address
		if(((addrs[i] - (unsigned long)BASE) / 4096) % 512 == 511){
		 	i--;
		}else if(i > 0){
			//Set up pointer chains
			//addrs[i - 1] --> addrs[1]
			//addrs[i - 1] + 4096 --> 0

			write_instruction_chain(addrs[i - 1], &iteration, addrs[i]);
			iteration = iteration - 1;
			write_instruction_chain(addrs[i - 1] + 4096, &iteration, 0);
		}
	}

	//Set up the pointer chains for the last address computed
	//addrs[ways] --> addrs[0]
	//addrs[ways] + 4096 --> 0
	write_instruction_chain(addrs[ways], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[ways] + 4096, &iteration, 0);

	iteration = 1;
	volatile unsigned long p = addrs[0];
	volatile unsigned int miss = 0;

	vfree(addrs);

	setcr3(cr3k);

	claim_cpu();

	//Prime ways + 1 PTEs in the TLB
	for(i = 0; i < ways + 1; i++){
		p = read_walk(p, &iteration);
		//Desync TLB
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	iteration = 1;

	//Does fetching them again result in iTLB hits?
	for(i = 0; i < ways + 1; i++){
		if(p){
			p = execute_walk(p, &iteration);
		}else{
			miss = 1;
		}

		//Restore page table
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	if(!p){
		miss = 1;
	}

	give_up_cpu();

	setcr3(cr3k);

	up_write(TLBDR_MMLOCK);

	//If there was at least one miss, return 1
	return miss;
}

/*
	This function tests whether accessing 'ways + 1' pages
	mapping to the same iTLB set (assuming an linear hash function and 2^set_bits sets)
	causes at least one iTLB eviction. It also assumes a linear sTLB hash function.
	It is explained in Section 4.2 of the paper.
*/
int test_lin_itlb_stlb_lin(int set_bits, int ways){
	disable_smep();

	volatile int i, iteration;
	volatile unsigned long p;

	volatile u64 cr3k = getcr3();

	down_write(TLBDR_MMLOCK);

	//Sample a random sTLB set
	unsigned int target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 1);

	volatile int addresses_needed = ways + 1 + (4 * tlb.shared_component->ways);
	addresses_needed = (addresses_needed % 2 == 1) ? addresses_needed + 1 : addresses_needed;

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);
	volatile unsigned long *addrs1 = vmalloc(sizeof(unsigned long) * addresses_needed / 2);
	volatile unsigned long *addrs2 = vmalloc(sizeof(unsigned long) * addresses_needed / 2);

	//Obtain ways + 1 + 4 * sTLB_ways addresses that map to the same same iTLB set,
	//making the assumption that the iTLB has a linear hash function and that it has 2^set_bits sets.
	//The addresses are equally spread over two sTLB sets.
	get_address_set_stlb_lin(addrs1, target_stlb_set, tlb.shared_component->set_bits, addresses_needed / 2);
	get_address_set_stlb_lin(addrs2, (target_stlb_set + set_bits_to_sets(set_bits)) % set_bits_to_sets(tlb.shared_component->set_bits), tlb.shared_component->set_bits, addresses_needed / 2);
	merge(addrs1, addrs2, addresses_needed / 2, addrs);

	iteration = 1;
	p = addrs[0];

	//Set up the pointer chains
	//addrs[0] --> addrs[1] --> ... --> addrs[ways + 1 * 4 * sTLB_ways - 1] --> addrs[0]
	//addrs[0] + 4906 --> 0
	//addrs[1] + 4096 --> 0
	//...
	//addrs[ways + 1 * 4 * sTLB_ways - 1] + 4096 --> 0

	for(i = 0; i < ways + 1 + 4 * tlb.shared_component->ways - 1; i++){
		write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
		iteration = iteration - 1;
		write_instruction_chain(addrs[i] + 4096, &iteration, 0);
	}

	write_instruction_chain(addrs[ways + 1 + 4 * tlb.shared_component->ways - 1], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[ways + 1 + 4 * tlb.shared_component->ways - 1] + 4096, &iteration, 0);

	//Perform page walks for the first ways + 1 addresses
	volatile struct ptwalk walks[ways + 1];
	volatile int values[ways + 1];

	for(i = 0; i < ways + 1; i++){
		resolve_va(addrs[i], &walks[i], 0);
		clear_nx(walks[i].pgd);
	}

	vfree(addrs);
	vfree(addrs1);
	vfree(addrs2);

	iteration = 1;

	setcr3(cr3k);

	claim_cpu();

	//Prime the iTLB (and sTLB) with ways + 1 PTEs
	for(i = 0; i < ways + 1; i++){
		p = execute_walk(p, &iteration);
		//Desync TLB
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	//Washing the sTLB (both sets)
	for(i = 0; i < 4 * tlb.shared_component->ways; i++){
		p = read_walk(p, &iteration);
	}

	volatile int miss = 0;
	iteration = 1;

	//Are the ways + 1 PTEs all cached?
	for(i = 0; i < ways + 1; i++){
		if(p){
			p = execute_walk(p, &iteration);
		}else{
			miss = 1;
		}

		//Restore page table
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	if(!p){
		miss = 1;
	}

	give_up_cpu();

	setcr3(cr3k);

	up_write(TLBDR_MMLOCK);

	//If there was at least one miss, return 1
	return !!(miss == 1);
}

/*
	This function tests whether accessing 'ways + 1' pages
	mapping to the same dTLB set (assuming an linear hash function and 2^set_bits sets)
	causes at least one dTLB eviction. It also assumes a linear sTLB hash function.
	It is explained in Section 4.2 of the paper.
*/
int test_lin_dtlb_stlb_lin(int set_bits, int ways){
	disable_smep();

	volatile int i, iteration;
	volatile unsigned long p;

	volatile u64 cr3k = getcr3();

	down_write(TLBDR_MMLOCK);

	//Sample a random sTLB set
	unsigned int target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 1);

	volatile int addresses_needed = ways + 1 + (4 * tlb.shared_component->ways);
	addresses_needed = (addresses_needed % 2 == 1) ? addresses_needed + 1 : addresses_needed;

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);
	volatile unsigned long *addrs1 = vmalloc(sizeof(unsigned long) * addresses_needed / 2);
	volatile unsigned long *addrs2 = vmalloc(sizeof(unsigned long) * addresses_needed / 2);

	//Obtain ways + 1 + 4 * sTLB_ways addresses that map to the same same dTLB set,
	//making the assumption that the dTLB has a linear hash function and that it has 2^set_bits sets.
	//The addresses are equally spread over two sTLB sets.
	get_address_set_stlb_lin(addrs1, target_stlb_set, tlb.shared_component->set_bits, addresses_needed / 2);
	get_address_set_stlb_lin(addrs2, (target_stlb_set + set_bits_to_sets(set_bits)) % set_bits_to_sets(tlb.shared_component->set_bits), tlb.shared_component->set_bits, addresses_needed / 2);
	merge(addrs1, addrs2, addresses_needed / 2, addrs);

	iteration = 1;
	p = addrs[0];

	//Set up the pointer chains
	//addrs[0] --> addrs[1] --> ... --> addrs[ways + 1 * 4 * sTLB_ways - 1] --> addrs[0]
	//addrs[0] + 4906 --> 0
	//addrs[1] + 4096 --> 0
	//...
	//addrs[ways + 1 * 4 * sTLB_ways - 1] + 4096 --> 0

	for(i = 0; i < ways + 1 + 4 * tlb.shared_component->ways - 1; i++){
		write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
		iteration = iteration - 1;
		write_instruction_chain(addrs[i] + 4096, &iteration, 0);
	}

	write_instruction_chain(addrs[ways + 1 + 4 * tlb.shared_component->ways - 1], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[ways + 1 + 4 * tlb.shared_component->ways - 1] + 4096, &iteration, 0);

	//Perform page walks for the first ways + 1 addresses
	volatile struct ptwalk walks[ways + 1];
	volatile int values[ways + 1];

	for(i = 0; i < ways + 1; i++){
		resolve_va(addrs[i], &walks[i], 0);
		clear_nx(walks[i].pgd);
	}

	vfree(addrs);
	vfree(addrs1);
	vfree(addrs2);

	iteration = 1;

	setcr3(cr3k);

	claim_cpu();

	//Prime the dTLB (and sTLB) with ways + 1 PTEs
	for(i = 0; i < ways + 1; i++){
		p = read_walk(p, &iteration);
		//Desync TLB
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	//Washing the sTLB (both sets)
	for(i = 0; i < 4 * tlb.shared_component->ways; i++){
		p = execute_walk(p, &iteration);
	}

	volatile int miss = 0;
	iteration = 1;

	//Are the ways + 1 PTEs all cached?
	for(i = 0; i < ways + 1; i++){
		if(p){
			p = read_walk(p, &iteration);
		}else{
			miss = 1;
		}

		//Restore page table
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	if(!p){
		miss = 1;
	}

	give_up_cpu();

	setcr3(cr3k);

	up_write(TLBDR_MMLOCK);

	//If there was at least one miss, return 1
	return !!(miss == 1);
}

/*
	This function tests whether accessing 'ways + 1' pages
	mapping to the same iTLB set (assuming an linear hash function and 2^set_bits sets)
	causes at least one iTLB eviction. It also assumes an XOR sTLB hash function.
	It is explained in Section 4.2 of the paper.
*/
int test_lin_itlb_stlb_xor(int set_bits, int ways){
	disable_smep();

	volatile int i, iteration;
	volatile unsigned long p;

	volatile u64 cr3k = getcr3();

	down_write(TLBDR_MMLOCK);

	//Sample a random iTLB set, assuming 2^set_ways sets and a linear hash function.
	//Also sample a random sTLB set.
	volatile unsigned int target_itlb_set = get_itlb_set(set_bits_to_sets(set_bits), 1);
	volatile unsigned long target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 1);

	volatile int addresses_needed = ways + 1 + (2 * tlb.shared_component->ways);

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);

	//Obtain ways + 1 * (2 * sTLB_ways) addresses that map to the same sTLB set and iTLB set
	get_address_set_stlb_xor(addrs, target_stlb_set, target_itlb_set, tlb.shared_component->set_bits, set_bits, addresses_needed);

	iteration = 1;
	p = addrs[0];

	//Set up the pointer chains
	//addrs[0] --> addrs[1] --> ... --> addrs[ways + 1 * 2 * sTLB_ways - 1] --> addrs[0]
	//addrs[0] + 4906 --> 0
	//addrs[1] + 4096 --> 0
	//...
	//addrs[ways + 1 * 2 * sTLB_ways - 1] + 4096 --> 0

	for(i = 0; i < ways + (2 * tlb.shared_component->ways); i++){
		write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
		iteration = iteration - 1;
		write_instruction_chain(addrs[i] + 4096, &iteration, 0);
	}

	write_instruction_chain(addrs[ways + (2 * tlb.shared_component->ways)], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[ways + (2 * tlb.shared_component->ways)] + 4096, &iteration, 0);

	//Perform page walks for the first ways + 1 addresses
	volatile struct ptwalk walks[ways + 1];
	volatile int values[ways + 1];

	for(i = 0; i < ways + 1; i++){
		resolve_va(addrs[i], &walks[i], 0);
		clear_nx(walks[i].pgd);
	}

	vfree(addrs);

	iteration = 1;

	setcr3(cr3k);

	claim_cpu();

	//Prime the iTLB (and sTLB) with ways + 1 PTEs
	for(i = 0; i < ways + 1; i++){
		p = execute_walk(p, &iteration);
		//Desync TLB
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	//Wash the sTLB
	for(i = 0; i < 2 * tlb.shared_component->ways; i++){
		p = read_walk(p, &iteration);
	}

	volatile int miss = 0;
	iteration = 1;

	//Are all ways + 1 PTEs cached?
	for(i = 0; i < ways + 1; i++){
		if(p){
			p = execute_walk(p, &iteration);
		}else{
			miss = 1;
		}

		//Restore page table
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	if(!p){
		miss = 1;
	}

	give_up_cpu();

	setcr3(cr3k);

	up_write(TLBDR_MMLOCK);

	//If there was at least one miss, return 1
	return !!(miss == 1);
}

/*
	This function tests whether accessing 'ways + 1' pages
	mapping to the same dTLB set (assuming an linear hash function and 2^set_bits sets)
	causes at least one dTLB eviction. It also assumes an XOR sTLB hash function.
	It is explained in Section 4.2 of the paper.
*/
int test_lin_dtlb_stlb_xor(int set_bits, int ways){
	disable_smep();

	volatile int i, iteration;
	volatile unsigned long p;

	volatile u64 cr3k = getcr3();

	down_write(TLBDR_MMLOCK);

	//Sample a random iTLB set, assuming 2^set_ways sets and a linear hash function.
	//Also sample a random sTLB set.
	volatile unsigned int target_dtlb_set = get_dtlb_set(set_bits_to_sets(set_bits), 1);
	volatile unsigned long target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 1);

	volatile int addresses_needed = ways + 1 + (2 * tlb.shared_component->ways);

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);

	//Obtain ways + 1 * (2 * sTLB_ways) addresses that map to the same sTLB set and iTLB set
	get_address_set_stlb_xor(addrs, target_stlb_set, target_dtlb_set, tlb.shared_component->set_bits, set_bits, addresses_needed);

	iteration = 1;
	p = addrs[0];

	//Set up the pointer chains
	//addrs[0] --> addrs[1] --> ... --> addrs[ways + 1 * 2 * sTLB_ways - 1] --> addrs[0]
	//addrs[0] + 4906 --> 0
	//addrs[1] + 4096 --> 0
	//...
	//addrs[ways + 1 * 2 * sTLB_ways - 1] + 4096 --> 0

	for(i = 0; i < ways + (2 * tlb.shared_component->ways); i++){
		write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
		iteration = iteration - 1;
		write_instruction_chain(addrs[i] + 4096, &iteration, 0);
	}

	write_instruction_chain(addrs[ways + (2 * tlb.shared_component->ways)], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[ways + (2 * tlb.shared_component->ways)] + 4096, &iteration, 0);

	//Perform page walks for the first ways + 1 addresses
	volatile struct ptwalk walks[ways + 1];
	volatile int values[ways + 1];

	for(i = 0; i < ways + 1; i++){
		resolve_va(addrs[i], &walks[i], 0);
		clear_nx(walks[i].pgd);
	}

	vfree(addrs);

	iteration = 1;

	setcr3(cr3k);

	claim_cpu();

	//Prime the iTLB (and sTLB) with ways + 1 PTEs
	for(i = 0; i < ways + 1; i++){
		p = read_walk(p, &iteration);
		//Desync TLB
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	//Wash the sTLB
	for(i = 0; i < 2 * tlb.shared_component->ways; i++){
		p = execute_walk(p, &iteration);
	}

	volatile int miss = 0;
	iteration = 1;

	//Are the ways + 1 PTEs cached?
	for(i = 0; i < ways + 1; i++){
		if(p){
			p = read_walk(p, &iteration);
		}else{
			miss = 1;
		}

		//Restore page table
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	if(!p){
		miss = 1;
	}

	give_up_cpu();

	setcr3(cr3k);

	up_write(TLBDR_MMLOCK);

	//If there was at least one miss, return 1
	return !!(miss == 1);
}
