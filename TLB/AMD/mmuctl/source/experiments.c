#include <experiments.h>
#include "mm_locking.h"

/*
	This function tests whether accessing 'ways' pages mapping to the same iTLB set (A)
	causes at least one iTLB eviction when trying to evict it with an eviction set for set B.
	This happens if 'ways' is greater than the number of ways in an iTLB set, or when 'bit'
	is not changing the iTLB set, i.e. when A == B. 
	It is explained in Section A.2 of the paper.
*/
int detect_bits_itlb(int set_bits, int bit, int ways){
	disable_smep();

	volatile int i, j, iteration;
	volatile unsigned long p;

	volatile u64 cr3k = getcr3();

	down_write(TLBDR_MMLOCK);

	volatile unsigned long *base = vmalloc(sizeof(unsigned long) * (600 + ways + 1));
	volatile unsigned long *addrs = base + ways + 1;

	//Sample a random iTLB set
	long int set = get_itlb_set(set_bits_to_sets(set_bits), 0);

	//If either of the two sets (A and B) result in 'end of page table' entries, resample
	while(set % 512 == 511 || (set ^ set_bits_to_sets(bit)) % 512 == 511) set = get_itlb_set(set_bits_to_sets(set_bits), 1);

	//Get 300 addresses mapping to set A
	get_address_set_stlb_lin(base, set ^ set_bits_to_sets(bit), set_bits, 300 + ways + 1);

	//Get ways addresses mapping to set B
	get_address_set_stlb_lin(addrs + 300, set, set_bits, ways);

	shuffle(addrs, 300);
	shuffle(addrs + 300, ways);

	iteration = 1;
	p = addrs[0];

	//Set up the pointer chain
	//addrs[0] --> addrs[1] --> addrs[2] --> ... --> addrs[300 + ways - 1] --> addrs[0]
	//addrs[0] + 4096 --> 0
	//addrs[1] + 4096 --> 0
	//addrs[2] + 4096 --> 0
	//...
	//addrs[300 + ways - 1] + 4096 --> 0

	for(i = 0; i < (300 + ways - 1); i++){
			write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
			iteration = iteration - 1;
			write_instruction_chain(addrs[i] + 4096, &iteration, 0);
	}

	write_instruction_chain(addrs[(300 + ways - 1)], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[(300 + ways - 1)] + 4096, &iteration, 0);

	//Perform the page walks
	volatile struct ptwalk walks[ways];
	volatile int values[ways];

	for(i = 0; i < 300; i++){
			resolve_va(addrs[i], &walks[0], 0);
			clear_nx(walks[0].pgd);
	}

	for(i = 0; i < ways; i++){
			resolve_va(addrs[i + 300], &walks[i], 0);
			clear_nx(walks[i].pgd);
	}

	vfree(base);

	iteration = 1;

	setcr3(cr3k);

	claim_cpu();

	//Wash the iTLB
	for(i = 0; i < 300; i++){
			p = execute_walk(p, &iteration);
	}

	//Prime the iTLB with 'ways' entries & desync the TLB
	for(i = 0; i < ways; i++){
			p = execute_walk(p, &iteration);
			switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	//Save the current pointer
	volatile unsigned long p_ = p;

	//Wash the iTLB, 10x with the same 300 entries
	for(j = 0; j < 10; j++){
		p = p_;
		iteration = 1;

		for(i = 0; i < 300; i++){
				p = execute_walk(p, &iteration);
		}
	}

	volatile int miss = 0;

	//Check if any of the 'ways' accesses miss the iTLB
	for(i = 0; i < ways; i++){
			if(p){
					p = execute_walk(p, &iteration);
			}else{
					miss = 1;
			}

			//Restore page table
			switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	if(!p) miss = 1;

	give_up_cpu();

	setcr3(cr3k);

	up_write(TLBDR_MMLOCK);

	return !!(miss == 1);
}

/*
	This function tests whether accessing 'ways' pages mapping to the same dTLB set (A)
	causes at least one dTLB eviction when trying to evict it with an eviction set for set B.
	This happens if 'ways' is greater than the number of ways in a dTLB set, or when 'bit'
	is not changing the dTLB set, i.e. when A == B. 
	It is explained in Section A.2 of the paper.
*/
int detect_bits_dtlb(int set_bits, int bit, int ways){
	disable_smep();

	volatile int i, j, iteration;
	volatile unsigned long p;

	volatile u64 cr3k = getcr3();

	down_write(TLBDR_MMLOCK);

	volatile unsigned long *base = vmalloc(sizeof(unsigned long) * (600 + ways + 1));
	volatile unsigned long *addrs = base + ways + 1;

	//Sample a random dTLB set
	long int set = get_dtlb_set(set_bits_to_sets(set_bits), 0);

	//If either of the two sets (A and B) result in 'end of page table' entries, resample
	while(set % 512 == 511 || (set ^ set_bits_to_sets(bit)) % 512 == 511) set = get_dtlb_set(set_bits_to_sets(set_bits), 1);

	//Get 300 addresses mapping to set A
	get_address_set_stlb_lin(base, set ^ set_bits_to_sets(bit), set_bits, 300 + ways + 1);

	//Get ways addresses mapping to set B
	get_address_set_stlb_lin(addrs + 300, set, set_bits, ways);

	shuffle(addrs, 300);
	shuffle(addrs + 300, ways);

	iteration = 1;
	p = addrs[0];

	//Set up the pointer chain
	//addrs[0] --> addrs[1] --> addrs[2] --> ... --> addrs[300 + ways - 1] --> addrs[0]
	//addrs[0] + 4096 --> 0
	//addrs[1] + 4096 --> 0
	//addrs[2] + 4096 --> 0
	//...
	//addrs[300 + ways - 1] + 4096 --> 0

	for(i = 0; i < (300 + ways - 1); i++){
			write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
			iteration = iteration - 1;
			write_instruction_chain(addrs[i] + 4096, &iteration, 0);
	}

	write_instruction_chain(addrs[(300 + ways - 1)], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[(300 + ways - 1)] + 4096, &iteration, 0);

	//Perform the page walks
	volatile struct ptwalk walks[ways];
	volatile int values[ways];

	for(i = 0; i < ways; i++){
			resolve_va(addrs[i + 300], &walks[i], 0);
			clear_nx(walks[i].pgd);
	}

	vfree(base);

	iteration = 1;

	setcr3(cr3k);

	claim_cpu();

	//Wash the dTLB
	for(i = 0; i < 300; i++){
			p = read_walk(p, &iteration);
	}

	//Prime the dTLB with 'ways' entries & desync the TLB
	for(i = 0; i < ways; i++){
			p = read_walk(p, &iteration);
			switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	//Save the current pointer
	volatile unsigned long p_ = p;

	//Wash the dTLB, 10x with the same 300 entries
	for(j = 0; j < 10; j++){
		p = p_;
		iteration = 1;

		for(i = 0; i < 300; i++){
				p = read_walk(p, &iteration);
		}
	}

	volatile int miss = 0;

	//Check if any of the 'ways' accesses miss the dTLB
	for(i = 0; i < ways; i++){
			if(p){
					p = read_walk(p, &iteration);
			}else{
					miss = 1;
			}

			switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	if(!p) miss = 1;

	give_up_cpu();

	setcr3(cr3k);

	up_write(TLBDR_MMLOCK);

	return !!(miss == 1);
}

/*
	This function tests whether accessing 'ways + 1' pages mapping to the same iTLB set
	causes at least one iTLB eviction.
	This happens if the L1 iTLB is inclusive of the L2 iTLB.
	It is explained in Section A.3 of the paper.
*/
int determine_inclusivity_instructions(int set_bits, int ways){
	disable_smep();
	
	volatile int i, iteration;
	volatile unsigned long p;

	volatile u64 cr3k = getcr3();
		
	down_write(TLBDR_MMLOCK);	
	
	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * (ways + 1));

	//Sample a random iTLB set
	//If we sample a set whose entries are at the end of the page table, resample
	long int set = get_itlb_set(set_bits_to_sets(set_bits), 0);
	while(set % 512 == 511) set = get_itlb_set(set_bits_to_sets(set_bits), 1);

	//Obtain ways + 1 addresses mapping to the same set
	get_address_set_stlb_lin(addrs, set, set_bits, ways + 1);
	
	iteration = 1;
	p = addrs[0];

	//Set up the pointer chain
	//addrs[0] --> addrs[1] --> addrs[2] --> ... --> addrs[ways] --> addrs[0]

	for(i = 0; i < ways; i++){
		write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
		iteration = iteration - 1;
		write_instruction_chain(addrs[i] + 4096, &iteration, 0);
	}

	write_instruction_chain(addrs[ways], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[ways] + 4096, &iteration, 0);

	//Perform the page walks
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

	//Prime the iTLB & desync the TLB
	for(i = 0; i < ways + 1; i++){
		p = execute_walk(p, &iteration);
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	volatile int miss = 0;
	iteration = 1;

	//Determine if all 'ways + 1' entries are still cached in the iTLB
	for(i = 0; i < ways + 1; i++){
		if(p){
			p = execute_walk(p, &iteration);		
		}else{
			miss = 1;
		}

		//Restore page table
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	if(!p) miss = 1;

	give_up_cpu();

	setcr3(cr3k);
	
	up_write(TLBDR_MMLOCK);	

	return !!(miss == 1);
}

/*
	This function tests whether accessing 'ways + 1' pages mapping to the same dTLB set
	causes at least one dTLB eviction.
	This happens if the L1 dTLB is inclusive of the L2 dTLB.
	It is explained in Section A.3 of the paper.
*/
int determine_inclusivity_data(int set_bits, int ways){
	disable_smep();
	
	volatile int i, iteration;
	volatile unsigned long p;

	volatile u64 cr3k = getcr3();
		
	down_write(TLBDR_MMLOCK);	
	
	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * (ways + 1));

	//Sample a random dTLB set
	//If we sample a set whose entries are at the end of the page table, resample
	long int set = get_dtlb_set(set_bits_to_sets(set_bits), 0);
	while(set % 512 == 511) set = get_dtlb_set(set_bits_to_sets(set_bits), 1);

	//Obtain ways + 1 addresses mapping to the same set
	get_address_set_stlb_lin(addrs, set, set_bits, ways + 1);
	
	iteration = 1;
	p = addrs[0];

	//Set up the pointer chain
	//addrs[0] --> addrs[1] --> addrs[2] --> ... --> addrs[ways] --> addrs[0]

	for(i = 0; i < ways; i++){
		write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
		iteration = iteration - 1;
		write_instruction_chain(addrs[i] + 4096, &iteration, 0);
	}

	write_instruction_chain(addrs[ways], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[ways] + 4096, &iteration, 0);

	//Perform the page walks
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

	//Prime the dTLB & desync the TLB
	for(i = 0; i < ways + 1; i++){
		p = read_walk(p, &iteration);
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	volatile int miss = 0;
	iteration = 1;

	//Determine if all 'ways + 1' entries are still cached in the dTLB
	for(i = 0; i < ways + 1; i++){
		if(p){
			p = read_walk(p, &iteration);		
		}else{
			miss = 1;
		}

		//Restore page table
		switch_pages(walks[i].pte, walks[i].pte + 1);
	}

	if(!p) miss = 1;

	give_up_cpu();

	setcr3(cr3k);
	
	up_write(TLBDR_MMLOCK);	

	return !!(miss == 1);
}

/*
	This function tests whether an PTE cached in response to a data load
	can be used for a consequent instruction fetch.
	It is explained in Section A.1 of the paper.
*/
int seperate_itlb_and_dtlb(int ways){
	disable_smep();
	
	u64 cr3k;
	cr3k = getcr3();
	setcr3(cr3k);

	volatile unsigned long addr;
    unsigned long random_offset;
	get_random_bytes(&random_offset, sizeof(random_offset));

	//Take a random page out of the first 1000 ones
	addr = (void *)BASE + (4096 * (random_offset % 1000));

	//If the PTE of our address is at the end of a page table, resample
	int difference = ((addr - (unsigned long)BASE) / 4096) % 512;
	while(difference % 512 == 511){
		get_random_bytes(&random_offset, sizeof(random_offset));
		addr = (void *)BASE + (4096 * (random_offset % 1000));
		difference = ((addr - (unsigned long)BASE) / 4096) % 512;
	}

	//Perform the page walk
	struct ptwalk walk;
	resolve_va(addr, &walk, 0);
	clear_nx(walk.pgd);
		
	down_write(TLBDR_MMLOCK);	

	claim_cpu();

	int original = read(addr);

	//Desync the TLB
	switch_pages(walk.pte, walk.pte + 1);

	//Issue 'ways' additional data loads
	volatile int i;
	for(i = 0; i < ways; i++){
		read((void *)BASE + (4096 * ((random_offset % 1000) + 1 + (i * 2))));
	}

	//Do we have an iTLB hit?
	int curr = execute(addr);

	give_up_cpu();

	//Restore the page table
	switch_pages(walk.pte, walk.pte + 1);

	up_write(TLBDR_MMLOCK);	

	setcr3(cr3k);

	return !!(original == curr);
}

/*
	This function tests whether an PTE cached in response to an instruction fetch
	can be used for a consequent data load.
	It is explained in Section A.1 of the paper.
*/
int seperate_dtlb_and_itlb(int ways){
	disable_smep();
	
	u64 cr3k;
	cr3k = getcr3();
	setcr3(cr3k);

	volatile unsigned long addr;
    unsigned long random_offset;
	get_random_bytes(&random_offset, sizeof(random_offset));

	//Take a random page out of the first 1000 ones
	addr = (void *)BASE + (4096 * (random_offset % 1000));

	//If the PTE of our address is at the end of a page table, resample
	int difference = ((addr - (unsigned long)BASE) / 4096) % 512;
	while(difference % 512 == 511){
		get_random_bytes(&random_offset, sizeof(random_offset));
		addr = (void *)BASE + (4096 * (random_offset % 1000));
		difference = ((addr - (unsigned long)BASE) / 4096) % 512;
	}

	//Perform the page walk
	struct ptwalk walk;
	resolve_va(addr, &walk, 0);
	clear_nx(walk.pgd);
		
	down_write(TLBDR_MMLOCK);	

	claim_cpu();

	int original = execute(addr);

	//Desync the TLB
	switch_pages(walk.pte, walk.pte + 1);

	//Issue 'ways' additional instruction fetches
	volatile int i;
	for(i = 0; i < ways; i++){
		execute((void *)BASE + (4096 * ((random_offset % 1000) + 1 + (i * 2))));
	}

	//Do we have a dTLB hit?
	int curr = read(addr);

	give_up_cpu();

	//Restore the page table
	switch_pages(walk.pte, walk.pte + 1);

	up_write(TLBDR_MMLOCK);	

	setcr3(cr3k);

	return !!(original == curr);
}