#include <address_generation.h>

/* 
	This function computes 'max' addresses that map to a sTLB and a (linear-indexed) iTLB/dTLB set.
	'stlb_target' is the target sTLB set, and 'split_target' is the target iTLB/dTLB set.
	'stlb_bits' is the number of bits that are used for sTLB set selection, whereas
	'split_tlb_bits' is the number of bits that are used for iTLB/dTLB set selection. 
*/
void get_address_set_stlb_xor(unsigned long addrs[], int stlb_target, int split_target, int stlb_bits, int split_tlb_bits, int max){
    int i = 0;
	int it, it2;

	unsigned long right_mask = 0;
	for(i = 0; i < stlb_bits; i++){ 
		right_mask |= (0x1 << (i + 12));
	}

	int max_outer = 1;
	int max_inner = 1;

	for(i = 0; i < FREEDOM_OF_BITS - (2 * stlb_bits); i++){
		max_outer *= 2;
	}

	for(i = 0; i < stlb_bits - split_tlb_bits; i++){
		max_inner *= 2;
	}

	int index = 0;
	for(it = 0; it < max_outer; it++){
		for(it2 = 0; it2 < max_inner; it2++){
			unsigned long base = (((unsigned long)BASE >> (12 + 2 * stlb_bits)) + it) << (12 + 2 * stlb_bits);
			unsigned long right_side = ((((base >> (12 + split_tlb_bits)) + it2) << split_tlb_bits) + split_target) << 12;
			unsigned long left_side = ((right_side & right_mask) ^ (stlb_target << 12)) << stlb_bits;
			unsigned long final_addr = left_side | right_side;

            //Skip addresses that are at the end of the page table as they are not safe to swap
			int difference = (final_addr - (unsigned long)BASE) / 4096;
			if(difference % 512 == 511){
				continue;
			}

			addrs[index] = final_addr;

			if(unsafe_address(addrs[index])){
				printk("Need more addresses for address generation. Please increase FREEDOM_OF_BITS or decrease number of sets/number of ways.\n");
				BUG();	
			}

			index++;

			if(index == max){
				return;
			}
		}
	}

	if(index < max){
		printk("Was not able to generate enough addresses (requested %d).\n", max);
		BUG();
	}
}

/* 
	This function computes 'max' addresses that map to a dTLB/iTLB/dTLB set.
	'stlb_target' is the target sTLB/dTLB/iTLB set.
	'stlb_bits' is the number of bits that are used for sTLB set selection.
*/
void get_address_set_stlb_lin(unsigned long addrs[], int stlb_target, int stlb_bits, int max){
	int i = 0;
	int it;

	int max_outer = 1;

	for(i = 0; i < FREEDOM_OF_BITS - stlb_bits; i++){
		max_outer *= 2;
	}

	int index = 0;
	for(it = 0; it < max_outer; it++){
		unsigned long base = ((unsigned long)BASE >> (12 + 2 * stlb_bits)) << (12 + 2 * stlb_bits);
		unsigned long right_side = ((base >> 12) + stlb_target) << 12;
		unsigned long left_side = ((base >> (12 + stlb_bits)) + it) << (12 + stlb_bits);
		unsigned long final_addr = left_side | right_side;

        //Skip addresses that are at the end of the page table as they are not safe to swap
		int difference = (final_addr - (unsigned long)BASE) / 4096;
		if(difference % 512 == 511){
			continue;
		}

		addrs[index] = final_addr;

		if(unsafe_address(addrs[index])){
			printk("Need more addresses for address generation. Please increase FREEDOM_OF_BITS or decrease number of sets/number of ways.\n");
			BUG();	
		}

		index++;

		if(index == max){
			return;
		}
	}

	if(index < max){
		printk("Was not able to generate enough addresses (requested %d).\n", max);
		BUG();
	}
}
