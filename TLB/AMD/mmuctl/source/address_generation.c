#include <address_generation.h>

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
		unsigned long base = ((unsigned long)BASE >> (12 + stlb_bits)) << (12 + stlb_bits);
		unsigned long addr = ((base >> (12 + stlb_bits)) + it) << (12 + stlb_bits);
		unsigned long final_addr = ((addr >> 12) + stlb_target) << 12;

        //Skip addresses that are at the end of the page table as they are not safe to swap
		int difference = (final_addr - (unsigned long)BASE) / 4096;
		if(difference % 512 == 511){
			continue;
		}

		addrs[index] = final_addr;

		if(unsafe_address(addrs[index])){
			printk("Need more addresses for address generation. Please increase FREEDOM_OF_BITS or decrease number of sets/number of ways.\n");
			for(i = 0; i < max; i++) addrs[i] = addrs[0];
			return;
			//BUG();	
		}

		index++;

		if(index == max){
			return;
		}
	}

	if(index < max){
		printk("Was not able to generate enough addresses (requested %d).\n", max);
		for(i = 0; i < max; i++) addrs[i] = addrs[0];
		return;
		//BUG();	
	}
}