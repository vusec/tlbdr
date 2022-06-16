#include <helpers.h>

/*
	Computes 2^set_bits,
	i.e. how many sets we can index into with 'set_bits' bits.
*/
long int set_bits_to_sets(int set_bits){
	int i;
    int res = 1;

    for(i = 0; i < set_bits; i++){
		res *= 2;
	}
        
    return res;
}

/*
	Disables kernel preemption to reduce interference.
*/
void claim_cpu(void){
	preempt_disable();
	raw_local_irq_save(flags);
}

/*
	Enables kernel preempion.
*/
void give_up_cpu(void){
	raw_local_irq_restore(flags);
	preempt_enable();
}

/*
	Writes to the CR3 register.
*/
u64 setcr3(u64 val){
	u64 r;
	asm volatile ("mov %0, %%cr3" :: "r" (val));
	asm volatile ("mov %%cr3, %0" : "=r" (r));
	return r;
}

/*
	Retrieves the value in the CR3 register.
*/
u64 getcr3(void){
	u64 cr3k;
	asm volatile ("mov %%cr3, %0" : "=r" (cr3k));
	return cr3k;
}

/*
	Disables SMEP.
*/
void disable_smep(void){
	u64 cr4v;
	asm volatile ("mov %%cr4, %0" : "=r" (cr4v));
	asm volatile ("mov %0, %%cr4" :: "r" (cr4v & (~(1ULL << 20))));
}

/*
	Interleaves to arrays into one, such that one input array is spread out 
	over the even indices of the output array, while the other input array 
	is spread out over the uneven indices of the output array.
*/
void merge(unsigned long list1[], unsigned long list2[], int length, unsigned long result[]){
	volatile int i;
	for(i = 0; i < length; i++){
		result[i * 2] = list1[i];
		result[i * 2 + 1] = list2[i];
	}
}

/*
	Returns a random sTLB set, or returns the preferred sTLB set
	If force_random == 1, we always return a random set.
*/
int get_stlb_set(int max, int force_random){
	if(preferred_stlb_set == -1 || force_random == 1){
		unsigned int target_set;
		get_random_bytes(&target_set, sizeof(target_set));
		return target_set % max;
	}else{
		return preferred_stlb_set % max;
	}
}

/*
	Returns a random iTLB set, or returns the preferred iTLB set
	If force_random == 1, we always return a random set.
*/
int get_itlb_set(int max, int force_random){
	if(preferred_itlb_set == -1 || force_random == 1){
		unsigned int target_set;
		get_random_bytes(&target_set, sizeof(target_set));
		return target_set % max;
	}else{
		return preferred_itlb_set % max;
	}
}

/*
	Returns a random dTLB set, or returns the preferred dTLB set
	If force_random == 1, we always return a random set.
*/
int get_dtlb_set(int max, int force_random){
	if(preferred_dtlb_set == -1 || force_random == 1){
		unsigned int target_set;
		get_random_bytes(&target_set, sizeof(target_set));
		return target_set % max;
	}else{
		return preferred_dtlb_set % max;
	}
}

/*
	Invalides cache line and TLB entry of given address.
*/
void spirt(u64 *p){
	__uaccess_begin_nospec();
	asm volatile (
		"clflush (%0)\n\t"
		"invlpg (%0)\n\t"
		:: "r" (p)
	);
	__uaccess_end();
}

/*
	Returns one if the address is outsided of the allocated area.
	Should not happen.
*/
int unsafe_address(unsigned long addr){
	unsigned long max = (unsigned long)BASE + (4096 * set_bits_to_sets(FREEDOM_OF_BITS));
	if(addr >= max || addr < (unsigned long)BASE){
		return 1;
	}

	return 0;
}

/*
	Returns 0 - 4096 in a randomized order.
*/
void get_random_pcids(unsigned long pcids[]){
	int i;
	for(i = 0; i < 4096; i++){
		pcids[i] = i;
	}

	for(i = 0; i < 4096; i++){
		unsigned long choice;
		get_random_bytes(&choice, sizeof(choice));
		unsigned long tmp = pcids[i];
		pcids[i] = pcids[choice % 4096];
		pcids[choice % 4096] = tmp;
	}
}

/*
	Computes the set of an address, assuming an XOR hash function
	that maps to 2^set_bits sets.
*/
int compute_xor_set(unsigned long addr, int set_bits){
	unsigned int i;
	unsigned long mask = 0;
	for(i = 0; i < set_bits; i++){
		mask |= (0x1 << (i + 12));
	}

	return ((addr & mask) ^ ((addr & (mask << set_bits)) >> set_bits)) >> 12;
}

/*
	Computes the set of an address, assuming a linear hash function
	that maps to 2^set_bits sets.
*/
int compute_lin_set(unsigned long addr, int set_bits){
	unsigned int i;
	unsigned long mask = 0;
	for(i = 0; i < set_bits; i++){
		mask |= (0x1 << (i + 12));
	}
	
	return (addr & mask) >> 12;
}

/*
	Returns the maximum value in the given array.
*/
int max_index(int list[], int length){
    int i;
    int max = 0;

    for(i = 1; i < length; i++){
        if(list[i] > list[max]){
            max = i;
        }
    }

    return max;
}

/*
	Returns the input array, but then shuffled in a random order.
*/
void shuffle(unsigned long list[], int length){
	int i;
	for(i = 0; i < length; i++){
		unsigned int rand;
		get_random_bytes(&rand, sizeof(rand));

		unsigned long tmp = list[i];
		list[i] = list[rand % length];
		list[rand % length] = tmp;
	}
}
