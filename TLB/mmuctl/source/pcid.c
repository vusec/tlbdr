#include <pcid.h>
#include <linux/vmalloc.h>

/*
	This function finds the maximum number of PCIDs supported by the sTLB.
	It corresponds to Section 4.5 of the paper.
*/
int stlb_pcid_limit(int pcid_writes, int no_flush){
    if(FREEDOM_OF_BITS < 10){
		printk("Need more addresses to test for STLB PCID limit. Please increase FREEDOM_OF_BITS to at least 10.\n");
		BUG();
	}
	
	disable_smep();

    volatile unsigned int i;
	
    unsigned long *pcids = vmalloc(4096 * sizeof(unsigned long));

	//Get the 4096 possible PCIDs in a random order
	get_random_pcids(pcids);

    u64 cr3 = (getcr3() >> 12) << 12;

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

	//Switch to the first PCID, either with the NOFLUSH bit set or not
	if(no_flush){
		setcr3(cr3 | pcids[0] | CR3_NOFLUSH);
	}else{
		setcr3(cr3 | pcids[0]);
	}
	
	//Read the original value to be able to distinguish TLB hits from misses
	int original = read(addr);

	switch_pages(walk.pte, walk.pte + 1);

	//Switch to 'pcid_writes' different PCIDs
	//Either with the NOFLUSH bit set or not
    for(i = 0; i < pcid_writes; i++){
        if(no_flush){
            setcr3(cr3 | pcids[i + 1] | CR3_NOFLUSH);
        }else{
            setcr3(cr3 | pcids[i + 1]);
        }
    }

	//Get back to the first PCID, in which we read the value of the address,
	//either with the NOFLUSH bit set or not
    setcr3(cr3 | pcids[0] | CR3_NOFLUSH);

	//Read the value again
	int curr = execute(addr);

	give_up_cpu();

	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

    vfree(pcids);

	spirt(addr);

	//Return 1 if we read another value after the PCID switches
    return !!(curr != original);
}


int stlb_pcid_vector_evicted(int vector_index, int element, int position, int no_flush){
    disable_smep();

	u64 cr3 = (getcr3() >> 12) << 12;

    volatile int original, curr, i;
	
    unsigned int target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 0);
	unsigned int target_dtlb_set = get_dtlb_set(set_bits_to_sets(tlb.split_component_data->set_bits), 0);

	unsigned long addrs[1];	

    if(tlb.shared_component->hash_function == XOR){
        get_address_set_stlb_xor(addrs, target_stlb_set, target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, 1);
    }else if(tlb.shared_component->hash_function == LIN){
        get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, 1);
    }

	unsigned long *pcids = vmalloc(4096 * sizeof(unsigned long));
	get_random_pcids(pcids);
    
	struct ptwalk walk;
	resolve_va(addrs[0], &walk, 0);
    clear_nx(walk.pgd);
 
    down_write(&current->mm->mmap_lock);	

	claim_cpu();

    //Getting the PCID to a known state
    for(i = tlb.shared_component->pcids_supported - 1; i >= 0; i--){
        if(no_flush){
			setcr3(cr3 | pcids[i] | CR3_NOFLUSH);
        }else{
			setcr3(cr3 | pcids[i]);
        }

		if(i == element){
			original = read(addrs[0]);
		}
    }

    setcr3(cr3 | pcids[vector_index] | CR3_NOFLUSH);

	switch_pages(walk.pte, walk.pte + 1);

    //To detect its position, we set 'position' fresh PCIDs
    for(i = 0; i < position; i++){
		if(no_flush){
	    	setcr3(cr3 | pcids[i + tlb.shared_component->pcids_supported] | CR3_NOFLUSH);
		}else{
			setcr3(cr3 | pcids[i + tlb.shared_component->pcids_supported] | CR3_NOFLUSH);
		}
    }
    
	setcr3(cr3 | pcids[element] | CR3_NOFLUSH);
    curr = execute(addrs[0]);
    give_up_cpu();

	switch_pages(walk.pte, walk.pte + 1);

    up_write(&current->mm->mmap_lock);

	vfree(pcids);

	spirt(addrs[0]);

    return !!(curr != original);
}

void detect_stlb_pcid_permutation(int vector_index, int vector[], int *agreement, int no_flush){
	int element, position, i;

    for(element = 0; element < tlb.shared_component->pcids_supported; element++){
        int votes[tlb.shared_component->pcids_supported + 1];

        for(i = 0; i < tlb.shared_component->pcids_supported + 1; i++){
            votes[i] = 0;
        }

        for(i = 0; i < iterations; i++){
            for(position = 0; position < tlb.shared_component->pcids_supported + 1; position++){
                if(stlb_pcid_vector_evicted(vector_index, element, position, no_flush)){
                    votes[position]++;
                    break;
                }
            }
        }

        int index = max_index(votes, tlb.shared_component->pcids_supported + 1);
        vector[tlb.shared_component->pcids_supported - index] = element;

        *agreement += votes[index];
    }
}

/*
	This function finds the maximum number of PCIDs supported by the dTLB.
	It corresponds to Section 4.5 of the paper.
*/
int dtlb_pcid_limit(int pcid_writes, int no_flush){
    if(FREEDOM_OF_BITS < 10){
		printk("Need more addresses to test for DTLB PCID limit. Please increase FREEDOM_OF_BITS to at least 10.\n");
		BUG();
	}
	
	disable_smep();

    volatile unsigned int i;
	
    unsigned long *pcids = vmalloc(4096 * sizeof(unsigned long));

	//Get the 4096 possible PCIDs in a random order
	get_random_pcids(pcids);

	u64 cr3k = getcr3();
    u64 cr3 = (cr3k >> 12) << 12;

	//Sample a random sTLB and dTLB set
	unsigned int target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 1);
	unsigned int target_dtlb_set = get_dtlb_set(set_bits_to_sets(tlb.split_component_data->set_bits), 1);

	int addresses_needed = 2 * tlb.shared_component->ways + 1;

	unsigned long addrs[addresses_needed];

	//Obtain 2 * sTLB_ways + 1 addresses mapping to the same sTLB- and dTLB set
	if(tlb.shared_component->hash_function == XOR){
		get_address_set_stlb_xor(addrs, target_stlb_set, target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, addresses_needed);
	}else if(tlb.shared_component->hash_function == LIN){
		get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, addresses_needed);
	}

	//Perform the page walk for the first address
	struct ptwalk walk;
	resolve_va(addrs[0], &walk, 0);
	clear_nx(walk.pgd);
		
	down_write(&current->mm->mmap_lock);	

	claim_cpu();

	//Switch to the first PCID, either with the NOFLUSH bit set or not
	if(no_flush){
		setcr3(cr3 | pcids[0] | CR3_NOFLUSH);
	}else{
		setcr3(cr3 | pcids[0]);
	}
	
	//Read the original value to be able to distinguish TLB hits from misses
	int original = read(addrs[0]);

	//Wash the sTLB
	for(i = 0; i < 2 * tlb.shared_component->ways; i++){
		execute(addrs[i + 1]);
	}
	
	//Desync the TLB
	switch_pages(walk.pte, walk.pte + 1);

	//Switch to 'pcid_writes' different PCIDs
	//Either with the NOFLUSH bit set or not
    for(i = 0; i < pcid_writes; i++){
        if(no_flush){
            setcr3(cr3 | pcids[i + 1] | CR3_NOFLUSH);
        }else{
            setcr3(cr3 | pcids[i + 1]);
        }
    }

	//Get back to the first PCID, in which we read the value of the address,
	//either with the NOFLUSH bit set or not
    setcr3(cr3 | pcids[0] | CR3_NOFLUSH);

	//Read the value again
	int curr = read(addrs[0]);

	give_up_cpu();

	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

	setcr3(cr3k);

    vfree(pcids);

	spirt(addrs[0]);

	//Return 1 if we read another value after the PCID switches
    return !!(curr != original);
}

/*
	This function finds the maximum number of PCIDs supported by the iTLB.
	It corresponds to Section 4.5 of the paper.
*/
int itlb_pcid_limit(int pcid_writes, int no_flush){
    if(FREEDOM_OF_BITS < 10){
		printk("Need more addresses to test for DTLB PCID limit. Please increase FREEDOM_OF_BITS to at least 10.\n");
		BUG();
	}
	
	disable_smep();

    volatile unsigned int i;
	
    unsigned long *pcids = vmalloc(4096 * sizeof(unsigned long));

	//Get the 4096 possible PCIDs in a random order
	get_random_pcids(pcids);

    u64 cr3 = (getcr3() >> 12) << 12;

	//Sample a random sTLB and iTLB set
	unsigned int target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 1);
	unsigned int target_itlb_set = get_dtlb_set(set_bits_to_sets(tlb.split_component_instruction->set_bits), 1);

	int addresses_needed = 2 * tlb.shared_component->ways + 1;

	unsigned long addrs[addresses_needed];

	//Obtain 2 * sTLB_ways + 1 addresses mapping to the same sTLB- and iTLB set
	if(tlb.shared_component->hash_function == XOR){
		get_address_set_stlb_xor(addrs, target_stlb_set, target_itlb_set, tlb.shared_component->set_bits, tlb.split_component_instruction->set_bits, addresses_needed);
	}else if(tlb.shared_component->hash_function == LIN){
		get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, addresses_needed);
	}

	//Perform the page walk for the first address
	struct ptwalk walk;
	resolve_va(addrs[0], &walk, 0);
	clear_nx(walk.pgd);
		
	down_write(&current->mm->mmap_lock);	

	claim_cpu();

	//Switch to the first PCID, either with the NOFLUSH bit set or not
	if(no_flush){
		setcr3(cr3 | pcids[0] | CR3_NOFLUSH);
	}else{
		setcr3(cr3 | pcids[0]);
	}

	//Read the original value to be able to distinguish TLB hits from misses
	int original = execute(addrs[0]);

	//Wash the sTLB
	for(i = 0; i < 2 * tlb.shared_component->ways; i++){
		read(addrs[i + 1]);
	}

	//Desync the TLB
	switch_pages(walk.pte, walk.pte + 1);

	//Switch to 'pcid_writes' different PCIDs
	//Either with the NOFLUSH bit set or not
    for(i = 0; i < pcid_writes; i++){
        if(no_flush){
            setcr3(cr3 | pcids[i + 1] | CR3_NOFLUSH);
        }else{
            setcr3(cr3 | pcids[i + 1]);
        }
    }

	//Get back to the first PCID, in which we read the value of the address,
	//either with the NOFLUSH bit set or not
    setcr3(cr3 | pcids[0] | CR3_NOFLUSH);

	//Read the value again
	int curr = execute(addrs[0]);

	give_up_cpu();

	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

    vfree(pcids);

	spirt(addrs[0]);

	//Return 1 if we read another value after the PCID switches
    return !!(curr != original);
}