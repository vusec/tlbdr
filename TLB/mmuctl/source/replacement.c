#include <replacement.h>
#include <linux/vmalloc.h>

int test_shared_replacement(int sequence[], int length, int failure_distribution[], int distribution[], int expect_eviction){	
	disable_smep();

	volatile int i, iteration, k, value, original, offset, j;
	volatile unsigned long p;
	offset = replacement_number_of_pages;

	volatile u64 cr3k = getcr3();
		
	down_write(&current->mm->mmap_lock);	
	
	volatile unsigned int target_dtlb_set = get_dtlb_set(set_bits_to_sets(tlb.split_component_data->set_bits), 0);
	volatile unsigned int target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 0);

	distribution[target_stlb_set]++;

	volatile int addresses_needed = replacement_number_of_pages + (2 * tlb.shared_component->ways);

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);	
	volatile unsigned long *wash = vmalloc(sizeof(unsigned long) * 2 * tlb.split_component_data->ways);	

	volatile struct ptwalk walk;

	if(tlb.shared_component && tlb.shared_component->hash_function == XOR){
		get_address_set_stlb_xor(addrs, target_stlb_set, target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, addresses_needed);
		get_address_set_stlb_xor(wash, (target_stlb_set + 1) % set_bits_to_sets(tlb.shared_component->set_bits), target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, addresses_needed);

		resolve_va(addrs[sequence[0]], &walk, 0);

		while(compute_xor_set(walk.pte, tlb.shared_component->set_bits) == target_stlb_set){
			shuffle(addrs, addresses_needed);
			resolve_va(addrs[sequence[0]], &walk, 0);
		}
	}else if(tlb.shared_component && tlb.shared_component->hash_function == LIN){
		target_stlb_set = target_stlb_set - (target_stlb_set % set_bits_to_sets(tlb.split_component_data->set_bits));
		target_stlb_set += target_dtlb_set;
		get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, addresses_needed);
		get_address_set_stlb_lin(wash, (target_stlb_set + set_bits_to_sets(tlb.split_component_data->set_bits)) % set_bits_to_sets(tlb.shared_component->set_bits), tlb.shared_component->set_bits, addresses_needed);

		resolve_va(addrs[sequence[0]], &walk, 0);

		while(compute_lin_set(walk.pte, tlb.shared_component->set_bits) == target_stlb_set){
			shuffle(addrs, addresses_needed);
			resolve_va(addrs[sequence[0]], &walk, 0);
		}
	}

	clear_nx(walk.pgd);

	original = read(addrs[sequence[0]]);

	iteration = 1;
	p = addrs[offset];

	for(i = 0; i < 2 * tlb.shared_component->ways - 1; i++){
		write_instruction_chain(addrs[i + offset], &iteration, addrs[i + 1 + offset]);
	}

	write_instruction_chain(addrs[2 * tlb.shared_component->ways - 1 + offset], &iteration, addrs[sequence[0]]);
	
	for(i = 0; i < length - 1; i++){
		write_instruction_chain(addrs[sequence[i]], &iteration, addrs[sequence[i + 1]]);
	}

	vfree(addrs);

	iteration = 1;
	claim_cpu();
		
	//Warming STLB & DTLB
	for(i = 0; i < 2 * tlb.shared_component->ways; i++){
		p = read_walk(p, &iteration);
	}

	//First in sequence
	p = read_walk(p, &iteration);

	switch_pages(walk.pte, walk.pte + 1);
	
	//Visit the rest of the sequence
	for(i = 0; i < length - 2; i++){
		for(j = 0; j < 2 * tlb.split_component_data->ways; j++){
			read(wash[j]);
		}
		p = read_walk(p, &iteration);
	}

	//Is it still in?
	value = read(p);

	spirt(p);

	switch_pages(walk.pte, walk.pte + 1);
	
	give_up_cpu();

	up_write(&current->mm->mmap_lock);	

	volatile int evicted = !!(value == ((original + 1) % set_bits_to_sets(UNIQUE_BITS)));

	if(!evicted && expect_eviction){
		failure_distribution[target_stlb_set]++;
	}else if(evicted && !expect_eviction){
		failure_distribution[target_stlb_set]++;
	}

	vfree(wash);

	return evicted;
}

int test_split_data_replacement(int sequence[], int length, int failure_distribution[], int distribution[], int expect_eviction){	
	disable_smep();

	volatile int i, iteration, k, value, original, number_of_washings, offset;
	volatile unsigned long p;
	number_of_washings = 2 * tlb.shared_component->ways;
	offset = replacement_number_of_pages;
		
	down_write(&current->mm->mmap_lock);	
	
	volatile unsigned int target_dtlb_set = get_dtlb_set(set_bits_to_sets(tlb.split_component_data->set_bits), 0);
	volatile unsigned int target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 0);

	distribution[target_dtlb_set]++;

	volatile int addresses_needed = replacement_number_of_pages + (2 * tlb.split_component_data->ways) + (2 * tlb.shared_component->ways);

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);	

	if(tlb.shared_component && tlb.shared_component->hash_function == XOR){
		get_address_set_stlb_xor(addrs, target_stlb_set, target_dtlb_set, tlb.shared_component->set_bits, tlb.split_component_data->set_bits, addresses_needed);
	}else if(tlb.shared_component && tlb.shared_component->hash_function == LIN){
		target_stlb_set = target_stlb_set - (target_stlb_set % set_bits_to_sets(tlb.split_component_data->set_bits));
		target_stlb_set += target_dtlb_set;
		get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, addresses_needed);
	}

	volatile struct ptwalk walk;
	resolve_va(addrs[sequence[0]], &walk, 0);

	while(compute_lin_set(walk.pte, tlb.split_component_data->set_bits) == target_dtlb_set){
        shuffle(addrs, addresses_needed);
        resolve_va(addrs[sequence[0]], &walk, 0);
    }

	clear_nx(walk.pgd);

	original = read(addrs[sequence[0]]);

	iteration = 1;
	p = addrs[offset + number_of_washings];

	for(i = 0; i < 2 * tlb.split_component_data->ways - 1; i++){
		write_instruction_chain(addrs[i + offset + number_of_washings], &iteration, addrs[i + 1 + offset + number_of_washings]);
	}

	write_instruction_chain(addrs[2 * tlb.split_component_data->ways - 1 + offset + number_of_washings], &iteration, addrs[sequence[0]]);

	write_instruction_chain(addrs[sequence[0]], &iteration, addrs[offset]);

	for(i = 0; i < number_of_washings - 1; i++){
		write_instruction_chain(addrs[offset + i], &iteration, addrs[offset + i + 1]);
	}

	write_instruction_chain(addrs[offset + number_of_washings - 1], &iteration, addrs[sequence[1]]);

	for(i = 1; i < length - 1; i++){
		write_instruction_chain(addrs[sequence[i]], &iteration, addrs[sequence[i + 1]]);
	}

	vfree(addrs);

	iteration = 1;
	claim_cpu();

	//Warming STLB & DTLB
	for(i = 0; i < 2 * tlb.split_component_data->ways; i++){
		p = read_walk(p, &iteration);
	}

	//First in sequence
	p = read_walk(p, &iteration);

	switch_pages(walk.pte, walk.pte + 1);

	//Wash the sTLB
	for(i = 0; i < number_of_washings; i++){
		p = execute_walk(p, &iteration);
	}
	
	//Visit the rest of the sequence
	for(i = 0; i < length - 2; i++){
		p = read_walk(p, &iteration);
	}

	//Is it still in?
	value = read(p);

	give_up_cpu();

	spirt(p);

	switch_pages(walk.pte, walk.pte + 1);
	
	up_write(&current->mm->mmap_lock);	

	volatile int evicted = !!(value == ((original + 1) % set_bits_to_sets(UNIQUE_BITS)));

	if(!evicted && expect_eviction){
		failure_distribution[target_dtlb_set]++;
	}else if(evicted && !expect_eviction){
		failure_distribution[target_dtlb_set]++;
	}

	return evicted;
}

int test_split_instruction_replacement(int sequence[], int length, int failure_distribution[], int distribution[], int expect_eviction){	
	disable_smep();

	volatile int i, iteration, k, value, original, number_of_washings, offset;
	volatile unsigned long p;
	number_of_washings = 2 * tlb.shared_component->ways;
	offset = replacement_number_of_pages;
		
	down_write(&current->mm->mmap_lock);	
	
	volatile unsigned int target_itlb_set = get_itlb_set(set_bits_to_sets(tlb.split_component_instruction->set_bits), 0);
	volatile unsigned int target_stlb_set = get_stlb_set(set_bits_to_sets(tlb.shared_component->set_bits), 0);

	distribution[target_itlb_set]++;

	volatile int addresses_needed = replacement_number_of_pages + (2 * tlb.split_component_instruction->ways) + (2 * tlb.shared_component->ways);

	volatile unsigned long *addrs = vmalloc(sizeof(unsigned long) * addresses_needed);	

	if(tlb.shared_component && tlb.shared_component->hash_function == XOR){
		get_address_set_stlb_xor(addrs, target_stlb_set, target_itlb_set, tlb.shared_component->set_bits, tlb.split_component_instruction->set_bits, addresses_needed);
	}else if(tlb.shared_component && tlb.shared_component->hash_function == LIN){
		target_stlb_set = target_stlb_set - (target_stlb_set % set_bits_to_sets(tlb.split_component_instruction->set_bits));
		target_stlb_set += target_itlb_set;
		get_address_set_stlb_lin(addrs, target_stlb_set, tlb.shared_component->set_bits, addresses_needed);
	}

	volatile struct ptwalk walk;
	resolve_va(addrs[sequence[0]], &walk, 0);
	clear_nx(walk.pgd);

	original = execute(addrs[sequence[0]]);

	iteration = 1;
	p = addrs[offset + number_of_washings];

	for(i = 0; i < 2 * tlb.split_component_instruction->ways - 1; i++){
		write_instruction_chain(addrs[i + offset + number_of_washings], &iteration, addrs[i + 1 + offset + number_of_washings]);
	}

	write_instruction_chain(addrs[2 * tlb.split_component_instruction->ways - 1 + offset + number_of_washings], &iteration, addrs[sequence[0]]);

	write_instruction_chain(addrs[sequence[0]], &iteration, addrs[offset]);

	for(i = 0; i < number_of_washings - 1; i++){
		write_instruction_chain(addrs[offset + i], &iteration, addrs[offset + i + 1]);
	}

	write_instruction_chain(addrs[offset + number_of_washings - 1], &iteration, addrs[sequence[1]]);

	for(i = 1; i < length - 1; i++){
		write_instruction_chain(addrs[sequence[i]], &iteration, addrs[sequence[i + 1]]);
	}

	vfree(addrs);

	iteration = 1;
	claim_cpu();

	//Warming STLB & ITLB
	for(i = 0; i < 2 * tlb.split_component_instruction->ways; i++){
		p = execute_walk(p, &iteration);
	}

	//First in sequence
	p = execute_walk(p, &iteration);

	switch_pages(walk.pte, walk.pte + 1);

	//Wash the sTLB
	for(i = 0; i < number_of_washings; i++){
		p = read_walk(p, &iteration);
	}
	
	//Visit the rest of the sequence
	for(i = 0; i < length - 2; i++){
		p = execute_walk(p, &iteration);
	}

	//Is it still in?
	value = execute(p);

	give_up_cpu();

	spirt(p);	

	switch_pages(walk.pte, walk.pte + 1);

	up_write(&current->mm->mmap_lock);	

	volatile int evicted = !!(value == ((original + 1) % set_bits_to_sets(UNIQUE_BITS)));

	if(!evicted && expect_eviction){
		failure_distribution[target_itlb_set]++;
	}else if(evicted && !expect_eviction){
		failure_distribution[target_itlb_set]++;
	}

	return evicted;
}

void test_nmru3plru(int (*test_function)(int[], int, int[], int[], int), int *short_succ, int *long_succ, int failure_distribution[], int distribution[]){
	*short_succ = 0;
	*long_succ = 0;
	int i;
	
	for(i = 0; i < iterations; i++){
		*short_succ += test_function(nmru3plru4_evict, nmru3plru4_evict_length, failure_distribution, distribution, 1);
		*long_succ += !!(test_function(nmru3plru4_noevict, nmru3plru4_noevict_length, failure_distribution, distribution, 0) == 0);
	}
}

void test_plru4(int (*test_function)(int[], int, int[], int[], int), int *short_succ, int *long_succ, int failure_distribution[], int distribution[]){
	*short_succ = 0;
	*long_succ = 0;
	int i;
	
	for(i = 0; i < iterations; i++){
		*short_succ += test_function(plru4_evict, plru4_evict_length, failure_distribution, distribution, 1);
		*long_succ += !!(test_function(plru4_noevict, plru4_noevict_length, failure_distribution, distribution, 0) == 0);
	}
}

void test_lru4(int (*test_function)(int[], int, int[], int[], int), int *short_succ, int *long_succ, int failure_distribution[], int distribution[]){
	*short_succ = 0;
	*long_succ = 0;
	int i;
	
	for(i = 0; i < iterations; i++){
		*short_succ += test_function(lru4_evict, lru4_evict_length, failure_distribution, distribution, 1);
		*long_succ += !!(test_function(lru4_noevict, lru4_noevict_length, failure_distribution, distribution, 0) == 0);
	}
}

void test_plru8(int (*test_function)(int[], int, int[], int[], int), int *short_succ, int *long_succ, int failure_distribution[], int distribution[]){
	*short_succ = 0;
	*long_succ = 0;
	int i;
	
	for(i = 0; i < iterations; i++){
		*short_succ += test_function(plru8_evict, plru8_evict_length, failure_distribution, distribution, 1);
		*long_succ += !!(test_function(plru8_noevict, plru8_noevict_length, failure_distribution, distribution, 0) == 0);
	}
}

void build_sequence_string(char buffer[], int buff_length, int sequence[], int sequence_length){
	int i;
	for(i = 0; i < sequence_length - 1; i++){
		snprintf(buffer + strlen(buffer), buff_length, "%d, ", sequence[i]);
	}

	snprintf(buffer + strlen(buffer), buff_length, "%d ", sequence[sequence_length - 1]);
}

void build_failures_string(char buffer[], int buff_length, int failures[], int total[], int number_of_sets){
	int i;
	for(i = 0; i < number_of_sets; i++){
		if(failures[i] == 0){
			continue;
		}
		snprintf(buffer + strlen(buffer), buff_length, "Set %d: %d failures out of %d tries\n", i, failures[i], total[i]);
	}
}

void build_plru4_message(char message[], char *component, int short_succ, int long_succ, int failure_distribution[], int distribution[], int number_of_sets){
	snprintf(message, MESSAGE_BUFFER_SIZE, "%s replacement policy: PLRU short sequence ", component);
	if(show_sequence){
		build_sequence_string(message, MESSAGE_BUFFER_SIZE, plru4_evict, plru4_evict_length);
	}
	snprintf(message + strlen(message), MESSAGE_BUFFER_SIZE, "evicted 0 with success " BOLD_BLACK "%d / %d" RESET ", long sequence ", short_succ, iterations);
	if(show_sequence){
		build_sequence_string(message, MESSAGE_BUFFER_SIZE, plru4_noevict, plru4_noevict_length);
	}
	snprintf(message + strlen(message), MESSAGE_BUFFER_SIZE, "did not evict 0 with success " BOLD_BLACK "%d / %d" RESET".\n", long_succ, iterations);
	if(show_set_distribution){
		build_failures_string(message, MESSAGE_BUFFER_SIZE, failure_distribution, distribution, number_of_sets);
	}
}

void build_lru4_message(char message[], char *component, int short_succ, int long_succ, int failure_distribution[], int distribution[], int number_of_sets){
	snprintf(message, MESSAGE_BUFFER_SIZE, "%s replacement policy: LRU short sequence ", component);
	if(show_sequence){
		build_sequence_string(message, MESSAGE_BUFFER_SIZE, lru4_evict, lru4_evict_length);
	}
	snprintf(message + strlen(message), MESSAGE_BUFFER_SIZE, "evicted 0 with success " BOLD_BLACK "%d / %d" RESET ", long sequence ", short_succ, iterations);
	if(show_sequence){
		build_sequence_string(message, MESSAGE_BUFFER_SIZE, lru4_noevict, lru4_noevict_length);
	}
	snprintf(message + strlen(message), MESSAGE_BUFFER_SIZE, "did not evict 0 with success " BOLD_BLACK "%d / %d" RESET".\n", long_succ, iterations);
	if(show_set_distribution){
		build_failures_string(message, MESSAGE_BUFFER_SIZE, failure_distribution, distribution, number_of_sets);
	}
}

void build_plru8_message(char message[], char *component, int short_succ, int long_succ, int failure_distribution[], int distribution[], int number_of_sets){
	snprintf(message, MESSAGE_BUFFER_SIZE, "%s replacement policy: PLRU policy short sequence ", component);
	if(show_sequence){
		build_sequence_string(message, MESSAGE_BUFFER_SIZE, plru8_evict, plru8_evict_length);
	}
	snprintf(message + strlen(message), MESSAGE_BUFFER_SIZE, "evicted 0 with success " BOLD_BLACK "%d / %d" RESET ", long sequence ", short_succ, iterations);
	if(show_sequence){
		build_sequence_string(message, MESSAGE_BUFFER_SIZE, plru8_noevict, plru8_noevict_length);
	}
	snprintf(message + strlen(message), MESSAGE_BUFFER_SIZE, "did not evict 0 with success " BOLD_BLACK "%d / %d" RESET ".\n", long_succ, iterations);
	if(show_set_distribution){
		build_failures_string(message, MESSAGE_BUFFER_SIZE, failure_distribution, distribution, number_of_sets);
	}
}

void build_nmru3plru_message(char message[], char *component, int short_succ, int long_succ, int failure_distribution[], int distribution[], int number_of_sets){
	snprintf(message, MESSAGE_BUFFER_SIZE, "%s replacement policy: (MRU+1)%%3PLRU4 short sequence ", component);
	if(show_sequence){
		build_sequence_string(message, MESSAGE_BUFFER_SIZE, nmru3plru4_evict, nmru3plru4_evict_length);
	}
	snprintf(message + strlen(message), MESSAGE_BUFFER_SIZE, "evicted 0 with success " BOLD_BLACK "%d / %d" RESET ", long sequence ", short_succ, iterations);
	if(show_sequence){
		build_sequence_string(message, MESSAGE_BUFFER_SIZE, nmru3plru4_noevict, nmru3plru4_noevict_length);
	}
	snprintf(message + strlen(message), MESSAGE_BUFFER_SIZE, "did not evict 0 with success " BOLD_BLACK "%d / %d" RESET ".\n", long_succ, iterations);
	if(show_set_distribution){
		build_failures_string(message, MESSAGE_BUFFER_SIZE, failure_distribution, distribution, number_of_sets);
	}
}
