#include <helpers.h>

/*
	Computes 2^set_bits,
	i.e. how many sets we can index into with 'set_bits' bits.
*/
long int set_bits_to_sets(int set_bits)
{
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
