#include <permutation.h>

#include <linux/vmalloc.h>
/*
    This function tests whether accessing sTLB_ways pages in the same sTLB set
    results in all of them being cached in the sTLB.
*/
int __attribute__((optimize("O0"))) stlb_vector_evicted(volatile struct experiment_info info){
    disable_smep();

    volatile unsigned int addresses_needed = 3 * info.ways + (info.ways - info.position);

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);	
	volatile unsigned long *wash_addr = vmalloc(sizeof(unsigned long) * info.number_of_washings * 2);	

    volatile struct ptwalk walk;

    //Obtain 4 * sTLB_ways - position addresses mapping to the same sTLB set
    //In addition, do the page walk for the entry of interest (3 * dTLB_ways - element - 1)
    //If its PTE maps to the same dTLB map, shuffle to avoid this
    //Also obtain an eviction set for the same dTLB set, but mapping do a different sTLB set,
    //of size 2 * number_of_washings = 4 * dTLB_ways
    if(tlb.shared_component->hash_function == XOR){
        get_address_set_stlb_xor(addrs, info.target_stlb_set, info.target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, addresses_needed);
        get_address_set_stlb_xor(wash_addr, (info.target_stlb_set + 1) % set_bits_to_sets(tlb.shared_component->set_bits), info.target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, info.number_of_washings * 2);
   
        resolve_va(addrs[3 * info.ways - info.element - 1], &walk, 0);

        while(compute_lin_set(walk.pte, tlb.shared_component->set_bits) == info.target_stlb_set){
            shuffle(addrs, addresses_needed);
            resolve_va(addrs[3 * info.ways - info.element - 1], &walk, 0);
        }
    }else if(tlb.shared_component->hash_function == LIN){
        get_address_set_stlb_lin(addrs, info.target_stlb_set, tlb.shared_component->set_bits, addresses_needed);
        get_address_set_stlb_lin(wash_addr, (info.target_stlb_set + set_bits_to_sets(tlb.split_component_data->set_bits)) % set_bits_to_sets(tlb.shared_component->set_bits), tlb.shared_component->set_bits, info.number_of_washings * 2);
    
        resolve_va(addrs[3 * info.ways - info.element - 1], &walk, 0);

        while(compute_xor_set(walk.pte, tlb.shared_component->set_bits) == info.target_stlb_set){
            shuffle(addrs, addresses_needed);
            resolve_va(addrs[3 * info.ways - info.element - 1], &walk, 0);
        }
    }

    clear_nx(walk.pgd);

    info.p = addrs[0];
    info.iteration = 1;

    //Obtain the original value of the page to distinguish between a TLB hit/miss
    info.original = read(addrs[3 * info.ways - info.element - 1]);

    //Set up the pointer chain
    //1. We first warm the sTLB set (2 * sTLB_ways accesses) and access sTLB_ways addresses
    //2. Then, we wash the dTLB set, so that the entries are only in 
    //the sTLB (info.number_of_washings == 2 * dTLB_ways accesses).
    //3. Then, we touch the entry corresponding to the desired permutation vector
    //4. We have to wash the dTLB set again (2 * dTLB_ways accesses).
    //5. Lastly, we do sTLB_ways - position extra accesses to the same sTLB set
    //6. Then, we get back to the entry of interest, and see if it still cached

    //So the pointer chain:
    //1. addrs[0] --> addrs[1] --> ... --> addrs[3 * sTLB_ways - 1] -->
    //2. wash_addr[0] --> wash_addr[1] --> .... --> wash_addr[2 * dTLB_ways - 1] -->
    //3. addrs[3 * sTLB_ways - vector_index] -->
    //4. wash_addr[2 * dTLB_ways] -->wash_addr[2 * dTLB_ways + 1] --> ... --> wash_addr[4 * dTLB_ways - 1] -->
    //5. addrs[3 * sTLB_ways] --> ... --> addrs[4 * sTLB_ways - position] -->
    //6. addrs[3 * sTLB_ways - element - 1]

    //Warming + getting known state (1)
    for(info.i = 0; info.i < 3 * info.ways - 1; info.i++){
        write_instruction_chain(addrs[info.i], &info.iteration, addrs[info.i + 1]);
    }
    
    //Washing (2)
    write_instruction_chain(addrs[3 * info.ways - 1], &info.iteration, wash_addr[0]);

    for(info.i = 0; info.i < info.number_of_washings - 1; info.i++){
        write_instruction_chain(wash_addr[info.i], &info.iteration, wash_addr[info.i + 1]);
    }

    //Touching the vector index, triggering the right permutation vector (3)
    write_instruction_chain(wash_addr[info.number_of_washings - 1], &info.iteration, addrs[3 * info.ways - info.vector_index - 1]);

    //Washing again (4)
    write_instruction_chain(addrs[3 * info.ways - info.vector_index - 1], &info.iteration, wash_addr[info.number_of_washings]);

    for(info.i = 0; info.i < info.number_of_washings - 1; info.i++){
        write_instruction_chain(wash_addr[info.i + info.number_of_washings], &info.iteration, wash_addr[info.i + info.number_of_washings + 1]);
    }

    //Starting to visit fresh addresses (5)
    write_instruction_chain(wash_addr[2 * info.number_of_washings - 1], &info.iteration, addrs[3 * info.ways]);

    for(info.i = 0; info.i < (info.ways - info.position) - 1; info.i++){
        write_instruction_chain(addrs[3 * info.ways + info.i], &info.iteration, addrs[3 * info.ways + info.i + 1]);
    }

    //Get back to element we are interested in (6)
    write_instruction_chain(addrs[3 * info.ways + (info.ways - info.position) - 1], &info.iteration, addrs[3 * info.ways - info.element - 1]);

    //Reset iteration
    info.iteration = 1;

    vfree(addrs);
    vfree(wash_addr);

    down_write(&current->mm->mmap_lock);	

    //Walk the pointer chain (it will desync)
    walk_stlb_chain(&info, walk.pte);

    spirt(info.p);

    //Restore page table
    switch_pages(walk.pte, walk.pte + 1);

    up_write(&current->mm->mmap_lock);

    //If not cached anymore, return 1
    return !!(info.curr != info.original);
}

/*
    This function tests whether accessing sTLB_ways pages in the same sTLB set
    results in all of them being cached.
*/
int __attribute__((optimize("O0"))) stlb_miss_vector(volatile struct experiment_info info){
    disable_smep();

    volatile u64 cr3k = getcr3();

    //+25 to allow room for shuffling
    volatile unsigned int addresses_needed = 3 * info.ways + 25;

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);	

    //Obtain 3 * sTLB_ways + 25 addresses mapping to the same sTLB set
    if(tlb.shared_component->hash_function == XOR){
        get_address_set_stlb_xor(addrs, info.target_stlb_set, info.target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, addresses_needed);
    }else if(tlb.shared_component->hash_function == LIN){
        get_address_set_stlb_lin(addrs, info.target_stlb_set, tlb.shared_component->set_bits, addresses_needed);
    }

    //Do the page walk for the sTLB_ways addresses (index 2 * sTLB_ways till 3 * sTLB_ways - 1)
    //Whenever the PTE has an address that maps to the same sTLB map,
    //we shuffle as we would need to access it during the experiment, causing interference.
    volatile struct ptwalk walks[info.ways];

    for(info.i = 0; info.i < info.ways; info.i++){
	    resolve_va(addrs[2 * info.ways + info.i], &walks[info.i], 0);
        clear_nx(walks[info.i].pgd);

        while((tlb.shared_component->hash_function == XOR && compute_xor_set(walks[info.i].pte, tlb.shared_component->set_bits) == info.target_stlb_set) || (tlb.shared_component->hash_function == LIN && compute_lin_set(walks[info.i].pte, tlb.shared_component->set_bits) == info.target_stlb_set)){
            shuffle(&addrs[2 * info.ways + info.i], addresses_needed - (2 * info.ways + info.i));
            resolve_va(addrs[2 * info.ways + info.i], &walks[info.i], 0);
            clear_nx(walks[info.i].pgd);
        }
    }

    info.p = addrs[0];
    info.iteration = 1;
    info.original = 1;

    //Setting up the pointer chain
    //addrs[0] --> addrs[1] --> ... --> addrs[3 * sTLB_ways - 1] --> addrs[2 * sTLB_ways]
    //addrs[0] + 4096 --> 0
    //addrs[1] + 4096 --> 0
    //...
    //addrs[3 * sTLB_ways - 1] + 4096 --> 0

    for(info.i = 0; info.i < 3 * info.ways - 1; info.i++){
        write_instruction_chain(addrs[info.i], &info.iteration, addrs[info.i + 1]);
        info.iteration = info.iteration - 1;
        write_instruction_chain(addrs[info.i] + 4096, &info.iteration, 0);
    }

    write_instruction_chain(addrs[3 * info.ways - 1], &info.iteration, addrs[2 * info.ways]);
    info.iteration = info.iteration - 1;
    write_instruction_chain(addrs[3 * info.ways - 1] + 4096, &info.iteration, 0);

    info.iteration = 1;

    vfree(addrs);

    down_write(&current->mm->mmap_lock);	
    claim_cpu();

    //Warming the sTLB set
	for(info.i = 0; info.i < 2 * info.ways; info.i++){
	    info.p = read_walk(info.p, &info.iteration);
	} 

    //Prime sTLB_ways entries
    for(info.i = 0; info.i < info.ways; info.i++){
		info.p = read_walk(info.p, &info.iteration);
        //Desync the TLB
        switch_pages(walks[info.i].pte, walks[info.i].pte + 1);
	} 

    info.iteration = 2 * info.ways + 1;

    //Visit the sTLB_ways addresses again, and see if any of them is a TLB miss
    for(info.i = 0; info.i < info.ways; info.i++){
        if(info.p){
            info.p = execute_walk(info.p, &info.iteration);
        }else{
            info.original = 0;
        }

        //Restore page table
        switch_pages(walks[info.i].pte, walks[info.i].pte + 1);
	}

    if(!info.p){
        info.original = 0;
    }
 
    give_up_cpu();

    up_write(&current->mm->mmap_lock);

    setcr3(cr3k);

    //If at least one was a miss, return 0
    return info.original;
}

/*
    This function finds a permutation vector for the sTLB.
    'vector_index' indicates which permutation vector we want.
    The resulting vector will be written to 'vector' and the success rate is written to 'agreement'.
    Other arguments are used for further analysis that is not part of the paper.
*/
void __attribute__((optimize("O0"))) detect_stlb_vector(volatile unsigned int vector_index, volatile int vector[], volatile unsigned int *agreement, volatile unsigned int set_mistakes_early[][tlb.shared_component->ways], volatile unsigned int set_mistakes_late[][tlb.shared_component->ways], int set_attempts[]){
    volatile unsigned int element, position, i, j;

    volatile struct experiment_info info;
    info.number_of_washings = 2 * tlb.split_component_data->ways;
    info.ways = tlb.shared_component->ways;
    info.vector_index = vector_index;

    //For each element (entry) in the permutation vector, find its position
    for(element = 0; element < tlb.shared_component->ways; element++){
        info.element = element;
        volatile unsigned int votes[set_bits_to_sets(tlb.shared_component->set_bits)][tlb.shared_component->ways];
        volatile unsigned int all_votes[tlb.shared_component->ways];

        //Reset votes
        for(j = 0; j < set_bits_to_sets(tlb.shared_component->set_bits); j++){
            for(i = 0; i < tlb.shared_component->ways; i++){
                votes[j][i] = 0;
            }
        }

        for(i = 0; i < tlb.shared_component->ways; i++){
            all_votes[i] = 0;
        }
        
        for(i = 0; i < iterations; i++){
            volatile unsigned int target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 0);
            volatile unsigned int target_dtlb_set = get_dtlb_set(set_bits_to_sets(tlb.split_component_data->set_bits), 0);

            info.target_stlb_set = target_stlb_set;
            info.target_dtlb_set = target_dtlb_set;

            //If vector_index == -1, we want to find whether accessing
            //sTLB_ways entries results in all of them being cached
            if(vector_index == -1){
                *agreement += stlb_miss_vector(info);

                if(i == iterations - 1){
                    return;
                }

                continue;
            }

            volatile unsigned int voted = 0;
            
            //Test each possible position in the permutation vector for this entry
            for(position = tlb.shared_component->ways - 1; (position + 1) >= 1; position--){
                info.position = position;

                //If we evicted the entry (element) from the sTLB set
                //we add a vote for this position.
                //We also break as assuming any lower position should also yield eviction
                if(stlb_vector_evicted(info)){
                    votes[target_stlb_set][position]++;
                    all_votes[position]++;
                    voted = 1;
                    break;
                }
            }

            if(!voted){
                set_mistakes_late[target_stlb_set][0] += 1;
                set_attempts[target_stlb_set] += 1;
            }
        }

        //Find which position has most votes for this entry
        volatile unsigned int index = max_index(all_votes, tlb.shared_component->ways);
        vector[index] = element;

        *agreement += all_votes[index];

        //For further analysis, not part of the paper
        for(j = 0; j < set_bits_to_sets(tlb.shared_component->set_bits); j++){
            for(i = 0; i < tlb.shared_component->ways; i++){
		        set_attempts[j] += votes[j][i];

                //If i < index, then it was evicted later than expected, as it voted for a position more to the left in the vector
                if(i < index){
                    set_mistakes_late[j][index - i] += votes[j][i];
                }else if(i > index){
                    set_mistakes_early[j][i - index] += votes[j][i];
                }
            }
        }
    }
}

/*
    This function tests whether accessing dTLB_ways pages in the same dTLB set
    results in all of them being cached in the dTLB.
*/
int __attribute__((optimize("O0"))) dtlb_vector_evicted(volatile struct experiment_info info){
    disable_smep();
      
    volatile unsigned int addresses_needed = 3 * info.ways + info.number_of_washings + (info.ways - info.position);

    volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);	

    //Obtain 4 * sTLB_ways - position + 2 * sTLB_ways (number_of_washings) addresses mapping to the same sTLB and dTLB set
    if(tlb.shared_component->hash_function == XOR){
        get_address_set_stlb_xor(addrs, info.target_stlb_set, info.target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, addresses_needed);
    }else if(tlb.shared_component->hash_function == LIN){
        info.target_stlb_set = info.target_stlb_set - (info.target_stlb_set % set_bits_to_sets(tlb.split_component_data->set_bits));
        info.target_stlb_set += info.target_dtlb_set;
        get_address_set_stlb_lin(addrs, info.target_stlb_set, tlb.shared_component->set_bits, addresses_needed);
    }

    //Do the page walk for the entry of interest (3 * sTLB_ways - element - 1)
    //If its PTE maps to the same dTLB map, shuffle to avoid this
    volatile struct ptwalk walk;
	resolve_va(addrs[3 * info.ways - info.element - 1], &walk, 0);

    while(compute_lin_set(walk.pte, tlb.split_component_data->set_bits) == info.target_dtlb_set){
        shuffle(addrs, addresses_needed);
        resolve_va(addrs[3 * info.ways - info.element - 1], &walk, 0);
    }

    clear_nx(walk.pgd);

    info.p = addrs[0];
    info.iteration = 1;

    //Obtain the original value of the page to distinguish between a TLB hit/miss
    info.original = read(addrs[3 * info.ways - info.element - 1]);

    //Set up the pointer chain
    //1. We first warm the dTLB set (2 * dTLB_ways accesses) and access dTLB_ways addresses
    //2. Then, we wash the sTLB set, so that the entries are only in in the dTLB
    //3. Then, we touch the entry corresponding to the desired permutation vector
    //4. Lastly, we do dTLB_ways - position extra accesses to the same dTLB set
    //5. Then, we get back to the entry of interest, and see if it still cached

    //So the pointer chain:
    //1. addrs[0] --> addrs[1] --> addrs[3 * dTLB_ways - 1] -->
    //2. addrs[4 * dTLB_ways - position] --> addrs[4 * dTLB_ways - position + 1] --> ... --> addrs[4 * dTLB_ways - position + 2 * sTLB_ways - 1] -->
    //3. addrs[3 * dTLB_ways - vector_index] -->
    //4. addrs[3 * dTLB_ways] --> addrs[3 * dTLB_ways + 1] --> ... --> addrs[3 * dTLB_ways + position - 1] --> 
    //5. addrs[3 * dTLB_ways - element - 1]

    //Warming + getting known state (1)
    for(info.i = 0; info.i < 3 * info.ways - 1; info.i++){
        write_instruction_chain(addrs[info.i], &info.iteration, addrs[info.i + 1]);
    }

    //Start washing (2)
    write_instruction_chain(addrs[3 * info.ways - 1], &info.iteration, addrs[3 * info.ways + (info.ways - info.position)]);

    for(info.i = 0; info.i < info.number_of_washings - 1; info.i++){
        write_instruction_chain(addrs[3 * info.ways + (info.ways - info.position) + info.i], &info.iteration, addrs[3 * info.ways + (info.ways - info.position) + info.i + 1]);
    }

    //Retouching vector index, triggering the right permutation vector (3)
    write_instruction_chain(addrs[3 * info.ways + (info.ways - info.position) + info.number_of_washings - 1], &info.iteration, addrs[3 * info.ways - info.vector_index - 1]);

    //Starting to visit fresh addresses (4)
    write_instruction_chain(addrs[3 * info.ways - info.vector_index - 1], &info.iteration, addrs[3 * info.ways]);

    for(info.i = 0; info.i < (info.ways - info.position) - 1; info.i++){
        write_instruction_chain(addrs[3 * info.ways + info.i], &info.iteration, addrs[3 * info.ways + info.i + 1]);
    }

    //Get back to element we are interested in (5)
    write_instruction_chain(addrs[3 * info.ways + (info.ways - info.position) - 1], &info.iteration, addrs[3 * info.ways - info.element - 1]);

    //Reset iteration
    info.iteration = 1;

    vfree(addrs);

    down_write(&current->mm->mmap_lock);	
	
    //Walk the pointer chain (it will desync)
    walk_dtlb_chain(&info, walk.pte);

    spirt(info.p);

    //Restore page table
    switch_pages(walk.pte, walk.pte + 1);

    up_write(&current->mm->mmap_lock);

    //If not cached anymore, return 1
    return !!(info.curr != info.original);
}

/*
    This function tests whether accessing dTLB_ways pages in the same dTLB set
    results in all of them being cached.
*/
int __attribute__((optimize("O0"))) dtlb_miss_vector(volatile struct experiment_info info){
    disable_smep();

    volatile u64 cr3k = getcr3();

    volatile int addresses_needed = 3 * info.ways + info.number_of_washings;

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);	

    //Obtain 3 * dTLB_ways + 2 * sTLB_ways (== number_of_washings) addresses mapping to the same dTLB and sTLB set
    if(tlb.shared_component->hash_function == XOR){
        get_address_set_stlb_xor(addrs, info.target_stlb_set, info.target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, addresses_needed);
    }else if(tlb.shared_component->hash_function == LIN){
        get_address_set_stlb_lin(addrs, info.target_stlb_set, tlb.shared_component->set_bits, addresses_needed);
    }

    volatile struct ptwalk walks[info.ways];

    //Do the page walk for the dTLB_ways addresses (index 2 * dTLB_ways till 3 * dTLB_ways - 1)
    //Whenever the PTE has an address that maps to the same dTLB map,
    //we shuffle as we would need to access it during the experiment, causing interference.
    for(info.i = 0; info.i < info.ways; info.i++){
	    resolve_va(addrs[2 * info.ways + info.i], &walks[info.i], 0);
        clear_nx(walks[info.i].pgd);

        while(compute_lin_set(walks[info.i].pte, tlb.split_component_data->set_bits) == info.target_dtlb_set){
            shuffle(&addrs[2 * info.ways + info.i], addresses_needed - (2 * info.ways + info.i));
            resolve_va(addrs[2 * info.ways + info.i], &walks[info.i], 0);
            clear_nx(walks[info.i].pgd);
        }
    }


    info.p = addrs[0];
    info.iteration = 1;
    info.original = 1;

    //Setting up the pointer chain
    //addrs[0] --> addrs[1] --> ... --> addrs[3 * dTLB_ways + 2 * sTLB_ways - 1] -->
    //addrs[2 * dTLB_ways]

    //addrs[2 * dTLB_ways] + 4096 --> 0
    //addrs[2 * dTLB_ways + 1] + 4096 --> 0
    //...
    //addrs[3 * dTLB_ways - 1] + 4096 --> 0

    for(info.i = 0; info.i < 2 * info.ways; info.i++){
        write_instruction_chain(addrs[info.i], &info.iteration, addrs[info.i + 1]);
    }
    
    for(info.i = 0; info.i < info.ways; info.i++){
        write_instruction_chain(addrs[2 * info.ways + info.i], &info.iteration, addrs[2 * info.ways + info.i + 1]);
        info.iteration = info.iteration - 1;
        write_instruction_chain(addrs[2 * info.ways + info.i] + 4096, &info.iteration, 0);
    }

    for(info.i = 0; info.i < info.number_of_washings - 1; info.i++){
        write_instruction_chain(addrs[3 * info.ways + info.i], &info.iteration, addrs[3 * info.ways + info.i + 1]);
    }

    write_instruction_chain(addrs[3 * info.ways + info.number_of_washings - 1], &info.iteration, addrs[2 * info.ways]);

    info.iteration = 1;

    vfree(addrs);

    down_write(&current->mm->mmap_lock);	
    claim_cpu();

    //Warming the dTLB set
	for(info.i = 0; info.i < 2 * info.ways; info.i++){
	    info.p = read_walk(info.p, &info.iteration);
	} 

    //Prime dTLB_ways entries
    for(info.i = 0; info.i < info.ways; info.i++){
		info.p = read_walk(info.p, &info.iteration);
        //Desync the TLB
        switch_pages(walks[info.i].pte, walks[info.i].pte + 1);
	} 

    //Wash the sTLB set
    for(info.i = 0; info.i < info.number_of_washings; info.i++){
		info.p = execute_walk(info.p, &info.iteration);
	} 

    info.iteration = 2 * info.ways + 1;

    //Visit the dTLB_ways addresses again, and see if any of them is a TLB miss
    for(info.i = 0; info.i < info.ways; info.i++){
        if(info.p){
            info.p = read_walk(info.p, &info.iteration);
        }else{
            info.original = 0;
        }

        //Restore page table
        switch_pages(walks[info.i].pte, walks[info.i].pte + 1);
	} 

    if(!info.p){
        info.original = 0;
    }
 
    give_up_cpu();

    up_write(&current->mm->mmap_lock);

    setcr3(cr3k);

    //If at least one was a miss, return 0
    return info.original;
}

/*
    This function finds a permutation vector for the dTLB.
    'vector_index' indicates which permutation vector we want.
    The resulting vector will be written to 'vector' and the success rate is written to 'agreement'.
    Other arguments are used for further analysis that is not part of the paper.
*/
void __attribute__((optimize("O0"))) detect_dtlb_vector(volatile unsigned int vector_index, volatile int vector[], volatile unsigned int *agreement, volatile unsigned int set_mistakes_early[][tlb.split_component_data->ways], volatile unsigned int set_mistakes_late[][tlb.split_component_data->ways], int set_attempts[]){
    volatile unsigned int element, position, i, j;

    volatile struct experiment_info info;
    info.number_of_washings = 2 * tlb.shared_component->ways;
    info.ways = tlb.split_component_data->ways;
    info.vector_index = vector_index;

    //For each element (entry) in the permutation vector, find its position
    for(element = 0; element < tlb.split_component_data->ways; element++){
        info.element = element;
        volatile unsigned int votes[set_bits_to_sets(tlb.split_component_data->set_bits)][tlb.split_component_data->ways];
        volatile unsigned int all_votes[tlb.split_component_data->ways];

        //Reset votes
        for(j = 0; j < set_bits_to_sets(tlb.split_component_data->set_bits); j++){
            for(i = 0; i < tlb.split_component_data->ways; i++){
                votes[j][i] = 0;
            }
        }

        for(i = 0; i < tlb.split_component_data->ways; i++){
            all_votes[i] = 0;
        }
        
        for(i = 0; i < iterations; i++){
            volatile unsigned int target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 0);
            volatile unsigned int target_dtlb_set = get_dtlb_set(set_bits_to_sets(tlb.split_component_data->set_bits), 0);

            info.target_stlb_set = target_stlb_set;
            info.target_dtlb_set = target_dtlb_set;

            //If vector_index == -1, we want to find whether accessing
            //dTLB_ways entries results in all of them being cached
            if(vector_index == -1){
                *agreement += dtlb_miss_vector(info);

                if(i == iterations - 1){
                    return;
                }

                continue;
            }

            volatile unsigned int voted = 0;
            
            //Test each possible position in the permutation vector for this entry
            for(position = tlb.split_component_data->ways - 1; (position + 1) >= 1; position--){
                info.position = position;

                //If we evicted the entry (element) from the dTLB set
                //we add a vote for this position.
                //We also break as assuming any lower position should also yield eviction
                if(dtlb_vector_evicted(info)){
                    votes[target_dtlb_set][position]++;
                    all_votes[position]++;
                    voted = 1;
                    break;
                }
            }

            if(!voted){
                set_mistakes_late[target_dtlb_set][0] += 1;
                set_attempts[target_dtlb_set] += 1;
            }
        }

        //Find which position has most votes for this entry
        volatile unsigned int index = max_index(all_votes, tlb.split_component_data->ways);
        vector[index] = element;

        *agreement += all_votes[index];

        //For further analysis, not part of the paper
        for(j = 0; j < set_bits_to_sets(tlb.split_component_data->set_bits); j++){
            for(i = 0; i < tlb.split_component_data->ways; i++){
		        set_attempts[j] += votes[j][i];

                //If i < index, then it was evicted later than expected, as it voted for a position more to the left in the vector
                if(i < index){
                    set_mistakes_late[j][index - i] += votes[j][i];
                }else if(i > index){
                    set_mistakes_early[j][i - index] += votes[j][i];
                }
            }
        }
    }
}

/*
    This function tests whether accessing iTLB_ways pages in the same iTLB set
    results in all of them being cached in the dTLB.
*/
int __attribute__((optimize("O0"))) itlb_vector_evicted(volatile struct experiment_info info){
    disable_smep();

    volatile unsigned int addresses_needed = 3 * info.ways + info.number_of_washings + (info.ways - info.position);

    volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);	

    //Obtain 4 * sTLB_ways - position + 2 * sTLB_ways (number_of_washings) addresses mapping to the same sTLB and iTLB set
    if(tlb.shared_component->hash_function == XOR){
        get_address_set_stlb_xor(addrs, info.target_stlb_set, info.target_itlb_set, tlb.shared_component->set_bits, tlb.split_component_instruction->set_bits, addresses_needed);
    }else if(tlb.shared_component->hash_function == LIN){
        info.target_stlb_set = info.target_stlb_set - (info.target_stlb_set % set_bits_to_sets(tlb.split_component_instruction->set_bits));
        info.target_stlb_set += info.target_itlb_set;
        get_address_set_stlb_lin(addrs, info.target_stlb_set, tlb.shared_component->set_bits, addresses_needed);
    }

    //Do the page walk for the entry of interest (3 * iTLB_ways - element - 1)
    volatile struct ptwalk walk;
	resolve_va(addrs[3 * info.ways - info.element - 1], &walk, 0);

    clear_nx(walk.pgd);

    info.p = addrs[0];
    info.iteration = 1;

    //Obtain the original value of the page to distinguish between a TLB hit/miss
    info.original = read(addrs[3 * info.ways - info.element - 1]);

    //Set up the pointer chain
    //1. We first warm the iTLB set (2 * iTLB_ways accesses) and access iTLB_ways addresses
    //2. Then, we wash the sTLB set, so that the entries are only in in the iTLB
    //3. Then, we touch the entry corresponding to the desired permutation vector
    //4. Lastly, we do iTLB_ways - position extra accesses to the same iTLB set
    //5. Then, we get back to the entry of interest, and see if it still cached

    //So the pointer chain:
    //1. addrs[0] --> addrs[1] --> addrs[3 * iTLB_ways - 1] -->
    //2. addrs[4 * dTLB_ways - position] --> addrs[4 * dTLB_ways - position + 1] --> ... --> addrs[4 * dTLB_ways - position + 2 * sTLB_ways - 1] -->
    //3. addrs[3 * dTLB_ways - vector_index] -->
    //4. addrs[3 * dTLB_ways] --> addrs[3 * dTLB_ways + 1] --> ... --> addrs[3 * dTLB_ways + position - 1] --> 
    //5. addrs[3 * dTLB_ways - element - 1]

    //Warming + getting known state (1)
    for(info.i = 0; info.i < 3 * info.ways - 1; info.i++){
        write_instruction_chain(addrs[info.i], &info.iteration, addrs[info.i + 1]);
    }

    //Start washing (2)
    write_instruction_chain(addrs[3 * info.ways - 1], &info.iteration, addrs[3 * info.ways + (info.ways - info.position)]);

    for(info.i = 0; info.i < info.number_of_washings - 1; info.i++){
        write_instruction_chain(addrs[3 * info.ways + (info.ways - info.position) + info.i], &info.iteration, addrs[3 * info.ways + (info.ways - info.position) + info.i + 1]);
    }

    //Retouching vector index, triggering the right permutation vector (3)
    write_instruction_chain(addrs[3 * info.ways + (info.ways - info.position) + info.number_of_washings - 1], &info.iteration, addrs[3 * info.ways - info.vector_index - 1]);

    //Starting to visit fresh addresses (4)
    write_instruction_chain(addrs[3 * info.ways - info.vector_index - 1], &info.iteration, addrs[3 * info.ways]);

    for(info.i = 0; info.i < (info.ways - info.position) - 1; info.i++){
        write_instruction_chain(addrs[3 * info.ways + info.i], &info.iteration, addrs[3 * info.ways + info.i + 1]);
    }

    //Get back to element we are interested in (5)
    write_instruction_chain(addrs[3 * info.ways + (info.ways - info.position) - 1], &info.iteration, addrs[3 * info.ways - info.element - 1]);

    //Reset iteration
    info.iteration = 1;

    vfree(addrs);

    down_write(&current->mm->mmap_lock);	

    //Walk the pointer chain (it will desync)
    walk_itlb_chain(&info, walk.pte);

    spirt(info.p);

    //Restore page table
    switch_pages(walk.pte, walk.pte + 1);

    up_write(&current->mm->mmap_lock);

    //If not cached anymore, return 1
    return !!(info.curr != info.original);
}

/*
    This function tests whether accessing iTLB_ways pages in the same iTLB set
    results in all of them being cached.
*/
int __attribute__((optimize("O0"))) itlb_miss_vector(volatile struct experiment_info info){
    disable_smep();

    u64 cr3k = getcr3();

    volatile unsigned int addresses_needed = 3 * info.ways + info.number_of_washings;

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);	

    //Obtain 3 * iTLB_ways + 2 * sTLB_ways (== number_of_washings) addresses mapping to the same iTLB and sTLB set
    if(tlb.shared_component->hash_function == XOR){
        get_address_set_stlb_xor(addrs, info.target_stlb_set, info.target_itlb_set, tlb.shared_component->set_bits, tlb.split_component_instruction->set_bits, addresses_needed);
    }else if(tlb.shared_component->hash_function == LIN){
        get_address_set_stlb_lin(addrs, info.target_stlb_set, tlb.shared_component->set_bits, addresses_needed);
    }

    volatile struct ptwalk walks[info.ways];

    //Do the page walk for the iTLB_ways addresses (index 2 * iTLB_ways till 3 * iTLB_ways - 1)
    for(info.i = 0; info.i < info.ways; info.i++){
	    resolve_va(addrs[2 * info.ways + info.i], &walks[info.i], 0);
        clear_nx(walks[info.i].pgd);
    }

    info.p = addrs[0];
    info.iteration = 1;
    info.original = 1;

    //Setting up the pointer chain
    //addrs[0] --> addrs[1] --> ... --> addrs[3 * iTLB_ways + 2 * sTLB_ways - 1] -->
    //addrs[2 * iTLB_ways]

    //addrs[2 * iTLB_ways] + 4096 --> 0
    //addrs[2 * iTLB_ways + 1] + 4096 --> 0
    //...
    //addrs[3 * iTLB_ways - 1] + 4096 --> 0

    for(info.i = 0; info.i < 2 * info.ways; info.i++){
        write_instruction_chain(addrs[info.i], &info.iteration, addrs[info.i + 1]);
    }
    
    for(info.i = 0; info.i < info.ways; info.i++){
        write_instruction_chain(addrs[2 * info.ways + info.i], &info.iteration, addrs[2 * info.ways + info.i + 1]);
        info.iteration = info.iteration - 1;
        write_instruction_chain(addrs[2 * info.ways + info.i] + 4096, &info.iteration, 0);
    }

    for(info.i = 0; info.i < info.number_of_washings - 1; info.i++){
        write_instruction_chain(addrs[3 * info.ways + info.i], &info.iteration, addrs[3 * info.ways + info.i + 1]);
    }

    write_instruction_chain(addrs[3 * info.ways + info.number_of_washings - 1], &info.iteration, addrs[2 * info.ways]);

    info.iteration = 1;

    vfree(addrs);

    down_write(&current->mm->mmap_lock);	
    claim_cpu();

    //Warming the iTLB set
	for(info.i = 0; info.i < 2 * info.ways; info.i++){
	    info.p = execute_walk(info.p, &info.iteration);
	} 

    //Prime iTLB_ways entries
    for(info.i = 0; info.i < info.ways; info.i++){
		info.p = execute_walk(info.p, &info.iteration);
        //Desync the TLB
        switch_pages(walks[info.i].pte, walks[info.i].pte + 1);
	} 

    //Wash the sTLB set
    for(info.i = 0; info.i < info.number_of_washings; info.i++){
		info.p = read_walk(info.p, &info.iteration);
	} 

    info.iteration = 2 * info.ways + 1;

    //Visit the iTLB_ways addresses again, and see if any of them is a TLB miss
    for(info.i = 0; info.i < info.ways; info.i++){
        if(info.p){
            info.p = execute_walk(info.p, &info.iteration);
        }else{
            info.original = 0;
        }

        //Restore page table
        switch_pages(walks[info.i].pte, walks[info.i].pte + 1);
	} 

    if(!info.p){
        info.original = 0;
    }
 
    give_up_cpu();

    up_write(&current->mm->mmap_lock);

    setcr3(cr3k);

    //If at least one was a miss, return 0
    return info.original;
}

/*
    This function finds a permutation vector for the iTLB.
    'vector_index' indicates which permutation vector we want.
    The resulting vector will be written to 'vector' and the success rate is written to 'agreement'.
    Other arguments are used for further analysis that is not part of the paper.
*/
void __attribute__((optimize("O0"))) detect_itlb_vector(volatile unsigned int vector_index, volatile int vector[], volatile unsigned int *agreement, volatile unsigned int set_mistakes_early[][tlb.split_component_instruction->ways], volatile unsigned int set_mistakes_late[][tlb.split_component_instruction->ways], int set_attempts[]){
    volatile unsigned int element, position, i, j;

    volatile struct experiment_info info;
    info.number_of_washings = 2 * tlb.shared_component->ways;
    info.ways = tlb.split_component_instruction->ways;
    info.vector_index = vector_index;

    //For each element (entry) in the permutation vector, find its position
    for(element = 0; element < tlb.split_component_instruction->ways; element++){
        info.element = element;
        volatile unsigned int votes[set_bits_to_sets(tlb.split_component_instruction->set_bits)][tlb.split_component_instruction->ways];
        volatile unsigned int all_votes[tlb.split_component_instruction->ways];

        //Reset votes
        for(j = 0; j < set_bits_to_sets(tlb.split_component_instruction->set_bits); j++){
            for(i = 0; i < tlb.split_component_instruction->ways; i++){
                votes[j][i] = 0;
            }
        }

        for(i = 0; i < tlb.split_component_instruction->ways; i++){
            all_votes[i] = 0;
        }
        
        for(i = 0; i < iterations; i++){
            volatile unsigned int target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 0);
            volatile unsigned int target_itlb_set = get_itlb_set(set_bits_to_sets(tlb.split_component_instruction->set_bits), 0);

            info.target_stlb_set = target_stlb_set;
            info.target_itlb_set = target_itlb_set;

            //If vector_index == -1, we want to find whether accessing
            //iTLB_ways entries results in all of them being cached
            if(vector_index == -1){
                *agreement += itlb_miss_vector(info);

                if(i == iterations - 1){
                    return;
                }

                continue;
            }

            volatile unsigned int voted = 0;
            
            //Test each possible position in the permutation vector for this entry
            for(position = tlb.split_component_instruction->ways - 1; (position + 1) >= 1; position--){
                info.position = position;

                //If we evicted the entry (element) from the iTLB set
                //we add a vote for this position.
                //We also break as assuming any lower position should also yield eviction
                if(itlb_vector_evicted(info)){
                    votes[target_itlb_set][position]++;
                    all_votes[position]++;
                    voted = 1;
                    break;
                }
            }

            if(!voted){
                set_mistakes_late[target_itlb_set][0] += 1;
                set_attempts[target_itlb_set] += 1;
            }
        }

        //Find which position has most votes for this entry
        volatile unsigned int index = max_index(all_votes, tlb.split_component_instruction->ways);
        vector[index] = element;

        *agreement += all_votes[index];

        //For further analysis, not part of the paper
        for(j = 0; j < set_bits_to_sets(tlb.split_component_instruction->set_bits); j++){
            for(i = 0; i < tlb.split_component_instruction->ways; i++){
		        set_attempts[j] += votes[j][i];

                //If i < index, then it was evicted later than expected, as it voted for a position more to the left in the vector
                if(i < index){
                    set_mistakes_late[j][index - i] += votes[j][i];
                }else if(i > index){
                    set_mistakes_early[j][i - index] += votes[j][i];
                }
            }
        }
    }
}
