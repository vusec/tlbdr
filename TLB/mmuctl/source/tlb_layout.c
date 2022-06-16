#include <tlb_layout.h>
#include <linux/vmalloc.h>

/*
	This function tests whether a PTE is cached the dTLB
	independently of the sTLB. It is explained in Section 4.1 of the paper.
*/
int non_inclusivity(void){
	if(FREEDOM_OF_BITS < 14){
		printk("Need more addresses to test for inclusivity. Please increase FREEDOM_OF_BITS to at least 14.\n");
		BUG();
	}

	disable_smep();
	
	u64 cr3k;
	cr3k = getcr3();
	setcr3(cr3k);

	volatile unsigned long addr;
    unsigned long random_offset;
	get_random_bytes(&random_offset, sizeof(random_offset));

	//Take a random page out of the first 1000 ones
	addr = (void *)BASE + (4096 * (random_offset % 1000));

	//We should not use an address whose PTE is the last entry of a page table,
	//as we would swap with arbritary memory (not necessarily a PTE)
	int difference = ((addr - (unsigned long)BASE) / 4096) % 512;
	while(difference % 512 == 511){
		get_random_bytes(&random_offset, sizeof(random_offset));
		addr = (void *)BASE + (4096 * (random_offset % 1000));
		difference = ((addr - (unsigned long)BASE) / 4096) % 512;
	}

	//Perform the page table walk and make it executable
	struct ptwalk walk;
	resolve_va(addr, &walk, 0);
	clear_nx(walk.pgd);
		
	down_write(&current->mm->mmap_lock);	

	claim_cpu();

	int original = read(addr);

	//Desync TLB
	switch_pages(walk.pte, walk.pte + 1);

	//Will executions evict the PTE?
	volatile int i;
	for(i = 0; i < 10000; i++){
		execute((void *)BASE + (4096 * i));
	}

	int curr = read(addr);

	give_up_cpu();
	
	//Restore page table
	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

	setcr3(cr3k);

	//Return 1 if PTE was still cached
	return !!(original == curr);
}

/*
	This function tests whether a PTE is cached in the sTLB
	in addition to the dTLB. It is explained in Section 4.1 of the paper.
*/
int non_exclusivity(void){
	disable_smep();
	
	volatile u64 cr3k = getcr3();
	volatile int i, iteration;
	volatile unsigned long p, random_offset;
	volatile unsigned long *addr = vmalloc(sizeof(unsigned long));

	get_random_bytes(&random_offset, sizeof(random_offset));

	//Take a random page out of the first 1000 ones
	*addr = (void *)BASE + (4096 * (random_offset % 1000));

	//We should not use an address whose PTE is the last entry of a page table,
	//as we would swap with arbritary memory (not necessarily a PTE)
	volatile unsigned int difference = ((*addr - (unsigned long)BASE) / 4096) % 512;
	while(difference % 512 == 511){
		get_random_bytes(&random_offset, sizeof(random_offset));
		*addr = (void *)BASE + (4096 * (random_offset % 1000));
		difference = ((*addr - (unsigned long)BASE) / 4096) % 512;
	}

	//Perform the page table walk and make it executable
	struct ptwalk walk;
	resolve_va(*addr, &walk, 0);
	clear_nx(walk.pgd);

	iteration = 1;
	p = *addr;
	volatile int original = read(p);

	//Set up pointer chain
	//addr[0] --> addr[0]
	write_instruction_chain(*addr, &iteration, *addr);

	vfree(addr);

	iteration = 1;

	down_write(&current->mm->mmap_lock);	
	setcr3(cr3k);

	claim_cpu();

	//Prime the target entry
	p = read_walk(p, &iteration);

	//Desync TLB
	switch_pages(walk.pte, walk.pte + 1);

	//Was it inserted into the sTLB?
	int curr = execute(p);

	give_up_cpu();
	
	//Restore page table
	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

	setcr3(cr3k);	

	//Return 1 if PTE was still cached
	return !!(original == curr);
}

/*
	This function tests whether a PTE is inserted in the iTLB
	upon an sTLB hit using an instruction fetch. It is explained in Section 4.3 of the paper.
*/
int reinsert_itlb(void){
	disable_smep();
	
	volatile u64 cr3k = getcr3();
	volatile int i, iteration;
	volatile unsigned long p;

	//Sample random sTLB and iTLB sets
	unsigned int target_stlb_set = get_stlb_set(tlb.shared_component->set_bits, 0);
	unsigned int target_itlb_set = get_itlb_set(tlb.split_component_instruction->set_bits, 0);
	
	//Obtain sTLB_ways * 2 + 1 addresses that map to the same iTLB and sTLB set
	unsigned long *addrs = vmalloc(sizeof(unsigned long) * (tlb.shared_component->ways * 2 + 1));
	if(tlb.shared_component->hash_function == XOR){
		get_address_set_stlb_xor(addrs, target_stlb_set, target_itlb_set, tlb.shared_component->set_bits, tlb.split_component_instruction->set_bits, tlb.shared_component->ways * 2 + 1);
	}else if(tlb.shared_component->hash_function == LIN){
		get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, tlb.shared_component->ways * 2 + 1);
	}

	//Perform the page walk for the first address
	struct ptwalk walk;
	resolve_va(addrs[0], &walk, 0);
	clear_nx(walk.pgd);

	iteration = 1;
	p = addrs[0];
	
	//Read the original value of the addr[0]
	volatile int original = read(p);

	//Set up the pointer chain
	//addr[0] --> addrs[0] --> addr[1] -> ... --> addr[2 * sTLB_ways] --> addr[0]

	write_instruction_chain(addrs[0], &iteration, addrs[0]);

    for(i = 0; i < tlb.shared_component->ways * 2; i++){
        write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
    }

	write_instruction_chain(addrs[tlb.shared_component->ways * 2], &iteration, addrs[0]);

	vfree(addrs);

	iteration = 1;

	down_write(&current->mm->mmap_lock);	
	setcr3(cr3k);

	claim_cpu();

	//Prime the target entry
	p = read_walk(p, &iteration);

	//Will this sTLB hit cause iTLB insertion?
	p = execute_walk(p, &iteration);

	//Desync TLB
	switch_pages(walk.pte, walk.pte + 1);
	
	//We wash the sTLB
	for(i = 0; i < tlb.shared_component->ways * 2; i++){
		p = read_walk(p, &iteration);
	}

	//Is it now in the iTLB?
	int curr = execute(p);

	give_up_cpu();

	//Restore page table
	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

	setcr3(cr3k);	

	//Return 1 if PTE was still cached
	return !!(original == curr);
}

/*
	This function tests whether a PTE is inserted in the dTLB
	upon an sTLB hit using an data load. It is explained in Section 4.3 of the paper.
*/
int reinsert_dtlb(void){
	disable_smep();
	
	volatile u64 cr3k = getcr3();
	volatile int i, iteration;
	volatile unsigned long p;

	//Sample random sTLB and dTLB sets
	unsigned int target_stlb_set = get_stlb_set(tlb.shared_component->set_bits, 0);
	unsigned int target_dtlb_set = get_dtlb_set(tlb.split_component_data->set_bits, 0);
	
	//Obtain sTLB_ways * 2 + 1 addresses that map to the same dTLB and sTLB set
	unsigned long *addrs = vmalloc(sizeof(unsigned long) * (tlb.shared_component->ways * 2 + 1));
	if(tlb.shared_component->hash_function == XOR){
		get_address_set_stlb_xor(addrs, target_stlb_set, target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, tlb.shared_component->ways * 2 + 1);
	}else if(tlb.shared_component->hash_function == LIN){
		get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, tlb.shared_component->ways * 2 + 1);
	}

	//Perform the page walk for the first address
	struct ptwalk walk;
	resolve_va(addrs[0], &walk, 0);
	clear_nx(walk.pgd);

	iteration = 1;
	p = addrs[0];

	//Read the original value of the addr[0]
	volatile int original = execute(p);

	//Set up pointer chain
	//addr[0] --> addrs[0] --> addr[1] -> ... --> addr[2 * sTLB_ways] --> addr[0]

	write_instruction_chain(addrs[0], &iteration, addrs[0]);

    for(i = 0; i < tlb.shared_component->ways * 2; i++){
        write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
    }

	write_instruction_chain(addrs[tlb.shared_component->ways * 2], &iteration, addrs[0]);

	vfree(addrs);

	iteration = 1;

	down_write(&current->mm->mmap_lock);	
	setcr3(cr3k);

	claim_cpu();

	//Prime the target entry
	p = execute_walk(p, &iteration);

	//Will this sTLB hit cause dTLB insertion?
	p = read_walk(p, &iteration);

	//Desync TLB
	switch_pages(walk.pte, walk.pte + 1);
	
	//We wash the sTLB
	for(i = 0; i < tlb.shared_component->ways * 2; i++){
		p = execute_walk(p, &iteration);
	}

	//Is it now in the dTLB?
	int curr = read(p);

	give_up_cpu();

	//Restore page table
	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

	setcr3(cr3k);	

	//Return 1 if PTE was still cached
	return !!(original == curr);
}

/*
	This function tests whether a PTE is inserted in the sTLB
	upon an dTLB hit. It is explained in Section 4.3 of the paper.
*/
int reinsert_stlb_data(void){
	disable_smep();
	
	volatile u64 cr3k = getcr3();
	volatile int i, iteration, curr;
	volatile unsigned long p;

	//Sample random sTLB and dTLB sets
	unsigned int target_stlb_set = get_stlb_set(tlb.shared_component->set_bits, 0);
	unsigned int target_dtlb_set = get_dtlb_set(tlb.split_component_data->set_bits, 0);
	
	//Obtain sTLB_ways * 2 + 1 addresses that map to the same dTLB and sTLB set
	unsigned long *addrs = vmalloc(sizeof(unsigned long) * (tlb.shared_component->ways * 2 + 1));
	if(tlb.shared_component->hash_function == XOR){
		get_address_set_stlb_xor(addrs, target_stlb_set, target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, tlb.shared_component->ways * 2 + 1);
	}else if(tlb.shared_component->hash_function == LIN){
		get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, tlb.shared_component->ways * 2 + 1);
	}

	//Perform the page walk for the first address
	struct ptwalk walk;
	resolve_va(addrs[0], &walk, 0);
	clear_nx(walk.pgd);

	iteration = 1;
	p = addrs[0];
	
	//Read the original value of the addr[0]
	volatile int original = execute(p);

	//Set up pointer chains
	//addr[0] --> addr[1] -> ... --> addr[2 * sTLB_ways] --> addr[0] --> addr[0]
	//addr[0] --> addr[1] -> ... --> addr[2 * sTLB_ways] --> addr[0] + 4096 --> 0

    for(i = 0; i < tlb.shared_component->ways * 2; i++){
        write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
    }

	write_instruction_chain(addrs[tlb.shared_component->ways * 2], &iteration, addrs[0]);

	write_instruction_chain(addrs[0], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[0] + 4096, &iteration, 0);

	vfree(addrs);

	iteration = 1;

	down_write(&current->mm->mmap_lock);	

	setcr3(cr3k);

	claim_cpu();

	//Prime the target entry
	p = read_walk(p, &iteration);

	//Desync TLB
	switch_pages(walk.pte, walk.pte + 1);
	
	//We wash the sTLB
	for(i = 0; i < tlb.shared_component->ways * 2; i++){
		p = execute_walk(p, &iteration);
	}
	
	//Hit on the dTLB
	p = read_walk(p, &iteration);

	//Did that cause sTLB reinsertion?
	if(p){
		curr = execute(p);
	}else{
		printk("No dTLB hit!\n");
		curr = original;
	}	

	give_up_cpu();

	//Restore page table
	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

	setcr3(cr3k);	

	//Return 1 if PTE was still cached
	return !!(original == curr);
}

/*
	This function tests whether a PTE is inserted in the sTLB
	upon an dTLB hit. It is explained in Section 4.3 of the paper.
*/
int reinsert_stlb_instruction(void){
	disable_smep();
	
	volatile u64 cr3k = getcr3();
	volatile int i, iteration, curr;
	volatile unsigned long p;

	//Sample random sTLB and iTLB sets
	unsigned int target_stlb_set = get_stlb_set(tlb.shared_component->set_bits, 0);
	unsigned int target_itlb_set = get_itlb_set(tlb.split_component_instruction->set_bits, 0);
	
	//Obtain sTLB_ways * 2 + 1 addresses that map to the same iTLB and sTLB set
	unsigned long *addrs = vmalloc(sizeof(unsigned long) * (tlb.shared_component->ways * 2 + 1));
	if(tlb.shared_component->hash_function == XOR){
		get_address_set_stlb_xor(addrs, target_stlb_set, target_itlb_set, tlb.shared_component->set_bits, tlb.split_component_instruction->set_bits, tlb.shared_component->ways * 2 + 1);
	}else if(tlb.shared_component->hash_function == LIN){
		get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, tlb.shared_component->ways * 2 + 1);
	}

	//Perform the page walk for the first address
	struct ptwalk walk;
	resolve_va(addrs[0], &walk, 0);
	clear_nx(walk.pgd);

	iteration = 1;
	p = addrs[0];

	//Read the original value of the addr[0]
	volatile int original = execute(p);

	//Set up pointer chains
	//addr[0] --> addr[1] -> ... --> addr[2 * sTLB_ways] --> addr[0] --> addr[0]
	//addr[0] --> addr[1] -> ... --> addr[2 * sTLB_ways] --> addr[0] + 4096 --> 0

    for(i = 0; i < tlb.shared_component->ways * 2; i++){
        write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
    }

	write_instruction_chain(addrs[tlb.shared_component->ways * 2], &iteration, addrs[0]);

	write_instruction_chain(addrs[0], &iteration, addrs[0]);
	iteration = iteration - 1;
	write_instruction_chain(addrs[0] + 4096, &iteration, 0);

	vfree(addrs);

	iteration = 1;

	down_write(&current->mm->mmap_lock);	

	setcr3(cr3k);

	claim_cpu();

	//Prime the target entry
	p = execute_walk(p, &iteration);

	//Desync TLB
	switch_pages(walk.pte, walk.pte + 1);
	
	//We wash the sTLB
	for(i = 0; i < tlb.shared_component->ways * 2; i++){
		p = read_walk(p, &iteration);
	}
	
	//Hit on the iTLB
	p = execute_walk(p, &iteration);

	//Did that cause sTLB reinsertion?
	if(p){
		curr = read(p);
	}else{
		printk("No iTLB hit!\n");
		curr = original;
	}

	give_up_cpu();

	//Restore page table
	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

	setcr3(cr3k);	

	//Return 1 if PTE was still cached
	return !!(original == curr);
}

/*
	This function tests whether a PTE is inserted in the sTLB
	upon an eviction from the dTLB. It is explained in Section 4.3 of the paper.
*/
int reinsert_stlb_dtlb_eviction(void){
	disable_smep();
	
	volatile u64 cr3k = getcr3();
	volatile int i, iteration;
	volatile unsigned long p;

	//Sample random sTLB and dTLB sets
	unsigned int target_stlb_set = get_stlb_set(tlb.shared_component->set_bits, 0);
	unsigned int target_dtlb_set = get_dtlb_set(tlb.split_component_data->set_bits, 0);

	//Obtain sTLB_ways * 2 + 1 addresses that map to the same dTLB and sTLB set	
	//Obtain another dTLB_ways * 2 addresses that map to the same dTLB set, but different sTLB set
	unsigned long *addrs = vmalloc(sizeof(unsigned long) * (tlb.shared_component->ways * 2 + 1));
	unsigned long *wash_addr = vmalloc(sizeof(unsigned long) * tlb.split_component_data->ways * 2);

	if(tlb.shared_component->hash_function == XOR){
		get_address_set_stlb_xor(addrs, target_stlb_set, target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, tlb.shared_component->ways * 2 + 1);
		get_address_set_stlb_xor(wash_addr, (target_stlb_set + 1) % set_bits_to_sets(tlb.shared_component->set_bits), target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, tlb.split_component_data->ways * 2);
	}else if(tlb.shared_component->hash_function == LIN){
		get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, tlb.shared_component->ways * 2 + 1);
		get_address_set_stlb_lin(wash_addr, (target_stlb_set + set_bits_to_sets(tlb.split_component_data->set_bits)) % set_bits_to_sets(tlb.shared_component->set_bits), tlb.shared_component->set_bits, tlb.split_component_data->ways * 2);
	}

	//Perform the page walk for the first address
	struct ptwalk walk;
	resolve_va(addrs[0], &walk, 0);
	clear_nx(walk.pgd);

	iteration = 1;
	p = addrs[0];

	//Read the original value of the addr[0]
	volatile int original = read(p);

	//Set up pointer chain
	//addr[0] --> addr[1] -> ... --> addr[2 * sTLB_ways] -->
	//wash_addr[0] --> ... --> wash_addr[2 * dTLB_ways - 1] --> addrs[0]

    for(i = 0; i < tlb.shared_component->ways * 2; i++){
        write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
    }

	write_instruction_chain(addrs[tlb.shared_component->ways * 2], &iteration, wash_addr[0]);

	for(i = 0; i < tlb.split_component_data->ways * 2 - 1; i++){
        write_instruction_chain(wash_addr[i], &iteration, wash_addr[i + 1]);
    }

	write_instruction_chain(wash_addr[tlb.split_component_data->ways * 2 - 1], &iteration, addrs[0]);

	vfree(addrs);
	vfree(wash_addr);

	iteration = 1;

	down_write(&current->mm->mmap_lock);	
	setcr3(cr3k);

	claim_cpu();

	//Prime the target entry
	p = read_walk(p, &iteration);

	//Desync TLB
	switch_pages(walk.pte, walk.pte + 1);
	
	//We wash the sTLB
	for(i = 0; i < tlb.shared_component->ways * 2; i++){
		p = execute_walk(p, &iteration);
	}

	//At this point, the entry is only in the dTLB, and not in the sTLB

	//We wash the dTLB, evicting the target address
	for(i = 0; i < tlb.split_component_data->ways * 2; i++){
		p = read_walk(p, &iteration);
	}
	
	//Did this eviction result in sTLB re-insertion?
	int curr = execute(p);

	give_up_cpu();

	//Restore page table
	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

	setcr3(cr3k);	

	//Return 1 if PTE was still cached
	return !!(original == curr);
}

/*
	This function tests whether a PTE is inserted in the sTLB
	upon an eviction from the iTLB. It is explained in Section 4.3 of the paper.
*/
int reinsert_stlb_itlb_eviction(void){
	disable_smep();
	
	volatile u64 cr3k = getcr3();
	volatile int i, iteration;
	volatile unsigned long p;

	//Sample random sTLB and dTLB sets
	unsigned int target_stlb_set = get_stlb_set(tlb.shared_component->set_bits, 0);
	unsigned int target_itlb_set = get_itlb_set(tlb.split_component_instruction->set_bits, 0);

	//Obtain sTLB_ways * 2 + 1 addresses that map to the same iTLB and sTLB set	
	//Obtain another iTLB_ways * 2 addresses that map to the same iTLB set, but different sTLB set
	unsigned long *addrs = vmalloc(sizeof(unsigned long) * (tlb.shared_component->ways * 2 + 1));
	unsigned long *wash_addr = vmalloc(sizeof(unsigned long) * tlb.split_component_instruction->ways * 2);

	if(tlb.shared_component->hash_function == XOR){
		get_address_set_stlb_xor(addrs, target_stlb_set, target_itlb_set, tlb.shared_component->set_bits, tlb.split_component_instruction->set_bits, tlb.shared_component->ways * 2 + 1);
		get_address_set_stlb_xor(wash_addr, (target_stlb_set + 1) % set_bits_to_sets(tlb.shared_component->set_bits), target_itlb_set, tlb.shared_component->set_bits, tlb.split_component_instruction->set_bits, tlb.split_component_instruction->ways * 2);
	}else if(tlb.shared_component->hash_function == LIN){
		get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, tlb.shared_component->ways * 2 + 1);
		get_address_set_stlb_lin(wash_addr, (target_stlb_set + set_bits_to_sets(tlb.split_component_instruction->set_bits)) % set_bits_to_sets(tlb.shared_component->set_bits), tlb.shared_component->set_bits, tlb.split_component_instruction->ways * 2);
	}

	//Perform the page walk for the first address
	struct ptwalk walk;
	resolve_va(addrs[0], &walk, 0);
	clear_nx(walk.pgd);

	iteration = 1;
	p = addrs[0];

	//Read the original value of the addr[0]
	volatile int original = execute(p);

	//Set up pointer chains
	//addr[0] --> addr[1] -> ... --> addr[2 * sTLB_ways] -->
	//wash_addr[0] --> ... --> wash_addr[2 * dTLB_ways - 1] --> addrs[0]

    for(i = 0; i < tlb.shared_component->ways * 2; i++){
        write_instruction_chain(addrs[i], &iteration, addrs[i + 1]);
    }

	write_instruction_chain(addrs[tlb.shared_component->ways * 2], &iteration, wash_addr[0]);

	for(i = 0; i < tlb.split_component_instruction->ways * 2 - 1; i++){
        write_instruction_chain(wash_addr[i], &iteration, wash_addr[i + 1]);
    }

	write_instruction_chain(wash_addr[tlb.split_component_instruction->ways * 2 - 1], &iteration, addrs[0]);

	vfree(addrs);
	vfree(wash_addr);

	iteration = 1;

	down_write(&current->mm->mmap_lock);	
	setcr3(cr3k);

	claim_cpu();

	//Prime the target entry
	p = execute_walk(p, &iteration);

	//Desync TLB
	switch_pages(walk.pte, walk.pte + 1);
	
	//We wash the sTLB
	for(i = 0; i < tlb.shared_component->ways * 2; i++){
		p = read_walk(p, &iteration);
	}

	//At this point, the entry is only in the iTLB, and not in the sTLB

	//We wash the iTLB, evicting the target address
	for(i = 0; i < tlb.split_component_instruction->ways * 2; i++){
		p = execute_walk(p, &iteration);
	}
	
	//Did this eviction result in sTLB re-insertion?
	int curr = read(p);

	give_up_cpu();
	
	//Restore page table
	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

	setcr3(cr3k);	

	//Return 1 if PTE was still cached
	return !!(original == curr);
}