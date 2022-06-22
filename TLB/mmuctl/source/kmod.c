#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <linux/highmem.h>
#include <linux/memory.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <helpers.h>
#include <pgtable.h>
#include <hash_functions.h>
#include <mem_access.h>
#include <address_generation.h>
#include <tlb_layout.h>
#include <replacement.h>
#include <pcid.h>
#include <permutation.h>
#include <linux/types.h>
#include <linux/time.h>

#include "../../settings.h"

struct TLB_level shared_level;
struct TLB_level split_level_data;
struct TLB_level split_level_instruction;
struct TLB tlb;

int show_set_distribution = 0;
int show_sequence = 0;
int iterations = 1000;
int preferred_stlb_set = -1;
int preferred_itlb_set = -1;
int preferred_dtlb_set = -1;
int replacement_number_of_pages = 40;

MODULE_AUTHOR("Daniel Trujillo, with contributions from Stephan van Schaik and Andrei Tatar");
MODULE_DESCRIPTION("A kernel module for testing TLB properties");
MODULE_LICENSE("GPL");

static int device_open(struct inode *inode, struct file *file){
	return 0;
}

/*
	This is the entry point for all experiments.
	The argument 'count' indicates the experiment to be carried out.
	The other arguments are ignored.
*/
static ssize_t device_read(struct file *file, char __user *buf, size_t count, loff_t *ppos){
	printk("PID: %d, Core: %d\n", current->pid, smp_processor_id());
	printk("COUNT: %d\n", count);

	char *return_message = vmalloc(sizeof(char) * MESSAGE_BUFFER_SIZE);
	int i;

	if(count == INCLUSIVITY){
		/*
			Tests whether a PTE can be cached in the dTLB independently of sTLB.
			The experiment is described in Section 4.1 of the paper.
		*/

		int non_inclusive = 0;

		for(i = 0; i < iterations; i++){
			non_inclusive += non_inclusivity();
		}

		if(non_inclusive > iterations / 2){
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB and dTLB are non-inclusive of sTLB: Yes (success rate " BOLD_BLACK "%d / %d" RESET ").\n", non_inclusive, iterations);
			tlb.split_component_instruction = &split_level_instruction;
			tlb.split_component_data = &split_level_data;
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB and dTLB are non-inclusive of sTLB: No, or no split component present. (success rate " BOLD_BLACK "%d / %d" RESET ") Will not test for iTLB and dTLB properties and ignore its possible presence.\n", non_inclusive, iterations);
			tlb.split_component_instruction = NULL;
			tlb.split_component_data = NULL;
		}
	}else if(count == EXCLUSIVITY){
		/*
			Tests whether a PTE can be cached in the sTLB in addition to dTLB.
			The experiment is described in Section 4.1 of the paper.
		*/

		int non_exclusive = 0;

		for(i = 0; i < iterations; i++){
			non_exclusive += non_exclusivity();
		}

		if(non_exclusive > iterations / 2){
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB is non-exclusive of iTLB and dTLB: Yes (success rate " BOLD_BLACK "%d / %d" RESET ").\n", non_exclusive, iterations);
			tlb.shared_component = &shared_level;
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB is non-exclusive of iTLB and dTLB: No, or no shared component present, or hardware page walker does not populate sTLB on first access (success rate " BOLD_BLACK "%d / %d" RESET "). Will not test for sTLB properties and ignore its possible presence.\n", non_exclusive, iterations);
			tlb.shared_component = NULL;
		}
	}else if(count == STLB_HASH){
		/*
			Finds the lowest limit on the number of PTEs cached in the sTLB,
			assuming the hash function (linear or XOR) and the number of sets.
			This will give us the set mapping and associativity.
			The experiment is described in Section 4.2 of the paper.
		*/

		int ways, set_bits;
		int smallest_ways = 99;
		int smallest_set_bits = 99;
		int success = 0;

		if(tlb.shared_component){
			for(set_bits = 3; set_bits < 9; set_bits++){
				for(ways = 1; ways < 30; ways++){
					int res = 0;
					for(i = 0; i < iterations; i++){
						res += test_lin_stlb(set_bits, ways);
					}

					if(res == iterations && ways < smallest_ways){
						smallest_ways = ways;
						smallest_set_bits = set_bits;
					}
				}
			}

			if(smallest_set_bits != 99){
				success = 1;
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB hash function: LIN-%d (%d sets), %d ways/set.\n", set_bits_to_sets(smallest_set_bits), set_bits_to_sets(smallest_set_bits), smallest_ways);
				tlb.shared_component->hash_function = LIN;
				tlb.shared_component->set_bits = smallest_set_bits;
				tlb.shared_component->ways = smallest_ways;
			}

			if(!success){
				smallest_ways = 99;
				smallest_set_bits = 99;

				for(set_bits = 6; set_bits < 9; set_bits++){
					for(ways = 1; ways < 30; ways++){
						int res = 0;
						for(i = 0; i < iterations; i++){
							res += test_xor_stlb(set_bits, ways);
						}

						if(res == iterations && ways < smallest_ways){
							smallest_ways = ways;
							smallest_set_bits = set_bits;
						}
					}
				}

				if(smallest_set_bits != 99){
					success = 1;
					snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB hash function: XOR-%d hash function (%d sets), %d ways/set.\n", smallest_set_bits, set_bits_to_sets(smallest_set_bits), smallest_ways);
					tlb.shared_component->hash_function = XOR;
					tlb.shared_component->set_bits = smallest_set_bits;
					tlb.shared_component->ways = smallest_ways;
				}
			}

			if(!success){
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB hash function: Unable to identify. LIN-8 through LIN-256 and XOR-3 through XOR-8 had no success (tested w = 1 up to w = 30).\n");
				tlb.shared_component = NULL;
			}
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "STLB hash function: Unable to test.\n");
		}
	}else if(count == ITLB_HASH){
		/*
			Finds the lowest limit on the number of PTEs cached in the iTLB,
			assuming the hash function (linear) and the number of sets.
			This will give us the set mapping and associativity.
			The experiment is described in Section 4.2 of the paper.
		*/

		if(tlb.split_component_instruction && tlb.shared_component){
			int ways, set_bits;
			int smallest_ways = 99;
			int smallest_set_bits = 99;
			int success = 0;

			for(set_bits = 2; set_bits < 7; set_bits++){
				for(ways = 1; ways < 20; ways++){
					int res = 0;
					for(i = 0; i < iterations; i++){
						if(tlb.shared_component->hash_function == XOR){
							res += test_lin_itlb_stlb_xor(set_bits, ways);
						}else if(tlb.shared_component->hash_function == LIN){
							res += test_lin_itlb_stlb_lin(set_bits, ways);
						}
					}

					if(res == iterations && ways < smallest_ways){
						smallest_ways = ways;
						smallest_set_bits = set_bits;
					}
				}
			}

			if(smallest_set_bits != 99){
				success = 1;
				tlb.split_component_instruction->hash_function = LIN;
				tlb.split_component_instruction->set_bits = smallest_set_bits;
				tlb.split_component_instruction->ways = smallest_ways;
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB hash function: LIN-%d hash function (%d sets), %d ways/set.\n", set_bits_to_sets(smallest_set_bits), set_bits_to_sets(smallest_set_bits), smallest_ways);
			}

			if(!success){
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB hash function: Unable to identify. LIN-4 through LIN-64 had no success (tested w = 1 up to w = 20).\n");
				tlb.split_component_instruction = NULL;
			}
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB hash function: Unable to test.\n");
		}
	}else if(count == DTLB_HASH){
		/*
			Finds the lowest limit on the number of PTEs cached in the dTLB,
			assuming the hash function (linear) and the number of sets.
			This will give us the set mapping and associativity.
			The experiment is described in Section 4.2 of the paper.
		*/

		if(tlb.split_component_data && tlb.shared_component){
			int ways, set_bits;
			int smallest_ways = 99;
			int smallest_set_bits = 99;
			int success = 0;

			for(set_bits = 2; set_bits < 7; set_bits++){
				for(ways = 1; ways < 20; ways++){
					int res = 0;
					for(i = 0; i < iterations; i++){
						if(tlb.shared_component->hash_function == XOR){
							res += test_lin_dtlb_stlb_xor(set_bits, ways);
						}else if(tlb.shared_component->hash_function == LIN){
							res += test_lin_dtlb_stlb_lin(set_bits, ways);
						}
					}

					if(res == iterations && ways < smallest_ways){
						smallest_ways = ways;
						smallest_set_bits = set_bits;
					}
				}
			}

			if(smallest_set_bits != 99){
				success = 1;
				tlb.split_component_data->hash_function = LIN;
				tlb.split_component_data->set_bits = smallest_set_bits;
				tlb.split_component_data->ways = smallest_ways;
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB hash function: LIN-%d hash function (%d sets), %d ways/set.\n", set_bits_to_sets(smallest_set_bits), set_bits_to_sets(smallest_set_bits), smallest_ways);
			}

			if(!success){
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB hash function: Unable to identify. LIN-4 through LIN-64 had no success (tested w = 1 up to w = 20).\n");
				tlb.split_component_data = NULL;
			}
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB hash function: Unable to test.\n");
		}
	}else if(count == ITLB_REINSERTION){
		/*
			Tests whether a PTE is inserted in the iTLB after an sTLB hit.
			The experiment is described in Section 4.3 of the paper.
		*/

		if(tlb.split_component_instruction && tlb.shared_component){
			int itlb_reinsert_success = 0;

			for(i = 0; i < iterations; i++){
				itlb_reinsert_success += reinsert_itlb();
			}

			if(itlb_reinsert_success > iterations / 2){
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB re-insertion upon sTLB hit: Yes (success rate " BOLD_BLACK "%d / %d" RESET ").\n", itlb_reinsert_success, iterations);
			}else{
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB re-insertion upon sTLB hit: No (success rate " BOLD_BLACK "%d / %d" RESET ").\n", itlb_reinsert_success, iterations);
			}
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB re-insertion upon sTLB hit: Unable to test.\n");
		}
	}else if(count == DTLB_REINSERTION){
		/* Tests whether a PTE is inserted in the dTLB after an sTLB hit.
		   The experiment is described in Section 4.3 of the paper.
		*/

		if(tlb.split_component_data && tlb.shared_component){
			int dtlb_reinsert_success = 0;

			for(i = 0; i < iterations; i++){
				dtlb_reinsert_success += reinsert_dtlb();
			}

			if(dtlb_reinsert_success > iterations / 2){
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB re-insertion upon sTLB hit: Yes (success rate " BOLD_BLACK "%d / %d" RESET ").\n", dtlb_reinsert_success, iterations);
			}else{
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB re-insertion upon sTLB hit: No (success rate " BOLD_BLACK "%d / %d" RESET ").\n", dtlb_reinsert_success, iterations);
			}
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB re-insertion upon sTLB hit: Unable to test.\n");
		}
	}else if(count == STLB_REINSERTION){
		/*
			Tests whether a PTE is inserted in the sTLB after an iTLB- or dTLB hit.
			The experiment is described in Section 4.3 of the paper.
		*/

		if(tlb.shared_component && tlb.split_component_data && tlb.split_component_instruction){
			int stlb_data_success = 0;
			int stlb_instruction_success = 0;

			for(i = 0; i < iterations; i++){
				stlb_data_success += reinsert_stlb_data();
				stlb_instruction_success += reinsert_stlb_instruction();
			}

			if((stlb_data_success + stlb_instruction_success) > iterations / 2){
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB re-insertion upon L1 hit: Yes (success rate data " BOLD_BLACK "%d / %d" RESET ", success rate instruction " BOLD_BLACK "%d / %d" RESET ").\n", stlb_data_success, iterations, stlb_instruction_success, iterations);
			}else{
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB re-insertion upon L1 hit: No (success rate data " BOLD_BLACK "%d / %d" RESET ", success rate instruction " BOLD_BLACK "%d / %d" RESET ").\n", stlb_data_success, iterations, stlb_instruction_success, iterations);
			}
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB re-insertion upon L1 hit: Unable to test.\n");
		}
	}else if(count == STLB_REINSERTION_L1_EVICTION){
		/*
			Tests whether a PTE is inserted in the sTLB after being
			evicted from the dTLB or iTLB.
			The experiment is described in Section 4.3 of the paper.
		*/

		if(tlb.shared_component && tlb.split_component_data && tlb.split_component_instruction){
			int stlb_data_success = 0;
			int stlb_instruction_success = 0;

			for(i = 0; i < iterations; i++){
				stlb_data_success += reinsert_stlb_dtlb_eviction();
				stlb_instruction_success += reinsert_stlb_itlb_eviction();
			}

			if((stlb_data_success + stlb_instruction_success) > iterations / 2){
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB re-insertion upon L1 eviction: Yes (success rate data " BOLD_BLACK "%d / %d" RESET ", success rate instruction " BOLD_BLACK "%d / %d" RESET ").\n", stlb_data_success, iterations, stlb_instruction_success, iterations);
			}else{
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB re-insertion upon L1 eviction: No (success rate data " BOLD_BLACK "%d / %d" RESET ", success rate instruction " BOLD_BLACK "%d / %d" RESET ").\n", stlb_data_success, iterations, stlb_instruction_success, iterations);
			}
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB re-insertion upon L1 eviction: Unable to test.\n");
		}
	}else if(count == STLB_REPLACEMENT){
		/*
			NOTE: this experiment is NOT described in the paper and is only used for extra validation
			of the replacement policies found.

			Tests whether we can evict an entry fom the sTLB (short_succ), creating an eviction set according to the
			tree-PLRU replacement policy (ways == 4 or ways == 8) or the (MRU+1)%3PLRU4 policy (ways == 12).

			Also tests whether we can avoid eviction of an entry from the sTLB (long_succ), creating the reverse of an
			eviction set according to the same policy.
		*/

		if(tlb.shared_component && tlb.split_component_data){
			int short_succ = 0;
			int long_succ = 0;
			int policy, i;
			int failure_dis[set_bits_to_sets(tlb.shared_component->set_bits)];
			int dis[set_bits_to_sets(tlb.shared_component->set_bits)];
			for(i = 0; i < set_bits_to_sets(tlb.shared_component->set_bits); i++){
				failure_dis[i] = 0;
				dis[i] = 0;
			}

			if(tlb.shared_component->ways == 4){
				test_plru4(test_shared_replacement, &short_succ, &long_succ, failure_dis, dis);
				build_plru4_message(return_message, "sTLB", short_succ, long_succ, failure_dis, dis, set_bits_to_sets(tlb.shared_component->set_bits));
			}else if(tlb.shared_component->ways == 8){
				test_plru8(test_shared_replacement, &short_succ, &long_succ, failure_dis, dis);
				build_plru8_message(return_message, "sTLB", short_succ, long_succ, failure_dis, dis, set_bits_to_sets(tlb.shared_component->set_bits));
			}else if(tlb.shared_component->ways == 12){
				test_nmru3plru(test_shared_replacement, &short_succ, &long_succ, failure_dis, dis);
				build_nmru3plru_message(return_message, "sTLB", short_succ, long_succ, failure_dis, dis, set_bits_to_sets(tlb.shared_component->set_bits));
			}else{
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB replacement policy: No candidate based on number of ways.\n");
			}
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB replacement policy: Not able to test.\n");
		}
	}else if(count == ITLB_REPLACEMENT){
		/*
			NOTE: this experiment is NOT described in the paper and is only used for extra validation
			of the replacement policies found.

			Tests whether we can evict an entry fom the iTLB (short_succ), creating an eviction set according to the
			tree-PLRU replacement policy (ways == 4 or ways == 8) or the (MRU+1)%3PLRU4 policy (ways == 12).

			Also tests whether we can avoid eviction of an entry from the iTLB (long_succ), creating the reverse of an
			eviction set according to the same policy.
		*/

		if(tlb.split_component_instruction && tlb.shared_component){
			int short_succ = 0;
			int long_succ = 0;
			int policy;
			int REPEAT = 10000;
			int failure_dis[set_bits_to_sets(tlb.split_component_instruction->set_bits)];
			int dis[set_bits_to_sets(tlb.split_component_instruction->set_bits)];
			for(i = 0; i < set_bits_to_sets(tlb.split_component_instruction->set_bits); i++){
				failure_dis[i] = 0;
				dis[i] = 0;
			}

			if(tlb.split_component_instruction->ways == 4){
				test_plru4(test_split_instruction_replacement, &short_succ, &long_succ, failure_dis, dis);

				if((short_succ + long_succ) > iterations){
					build_plru4_message(return_message, "iTLB", short_succ, long_succ, failure_dis, dis, set_bits_to_sets(tlb.split_component_instruction->set_bits));
				}else{

					short_succ = 0;
					long_succ = 0;

					for(i = 0; i < set_bits_to_sets(tlb.split_component_instruction->set_bits); i++){
						failure_dis[i] = 0;
						dis[i] = 0;
					}

					test_lru4(test_split_instruction_replacement, &short_succ, &long_succ, failure_dis, dis);

					build_lru4_message(return_message, "iTLB", short_succ, long_succ, failure_dis, dis, set_bits_to_sets(tlb.split_component_instruction->set_bits));
				}

			}else if(tlb.split_component_instruction->ways == 8){
				test_plru8(test_split_instruction_replacement, &short_succ, &long_succ, failure_dis, dis);
				build_plru8_message(return_message, "iTLB", short_succ, long_succ, failure_dis, dis, set_bits_to_sets(tlb.split_component_instruction->set_bits));
			}else if(tlb.split_component_instruction->ways == 12){
				test_nmru3plru(test_split_instruction_replacement, &short_succ, &long_succ, failure_dis, dis);
				build_nmru3plru_message(return_message, "iTLB", short_succ, long_succ, failure_dis, dis, set_bits_to_sets(tlb.split_component_instruction->set_bits));
			}else{
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB replacement policy: No candidate based on number of ways.\n");
			}
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB replacement policy: Not able to test.\n");
		}
	}else if(count == DTLB_REPLACEMENT){
		/*
			NOTE: this experiment is NOT described in the paper and is only used for extra validation
			of the replacement policies found.

			Tests whether we can evict an entry fom the dTLB (short_succ), creating an eviction set according to the
			tree-PLRU replacement policy (ways == 4 or ways == 8) or the (MRU+1)%3PLRU4 policy (ways == 12).

			Also tests whether we can avoid eviction of an entry from the dTLB (long_succ), creating the reverse of an
			eviction set according to the same policy.
		*/

		if(tlb.split_component_data && tlb.shared_component){
			int short_succ = 0;
			int long_succ = 0;
			int policy;
			int REPEAT = 10000;
			int failure_dis[set_bits_to_sets(tlb.split_component_data->set_bits)];
			int dis[set_bits_to_sets(tlb.split_component_data->set_bits)];
			for(i = 0; i < set_bits_to_sets(tlb.split_component_data->set_bits); i++){
				failure_dis[i] = 0;
				dis[i] = 0;
			}

			if(tlb.split_component_data->ways == 4){
				test_plru4(test_split_data_replacement, &short_succ, &long_succ, failure_dis, dis);
				build_plru4_message(return_message, "dTLB", short_succ, long_succ, failure_dis, dis, set_bits_to_sets(tlb.split_component_data->set_bits));
			}else if(tlb.split_component_data->ways == 8){
				test_plru8(test_split_data_replacement, &short_succ, &long_succ, failure_dis, dis);
				build_plru8_message(return_message, "dTLB", short_succ, long_succ, failure_dis, dis, set_bits_to_sets(tlb.split_component_data->set_bits));
			}else if(tlb.split_component_data->ways == 12){
				test_nmru3plru(test_split_data_replacement, &short_succ, &long_succ, failure_dis, dis);
				build_nmru3plru_message(return_message, "dTLB", short_succ, long_succ, failure_dis, dis, set_bits_to_sets(tlb.split_component_data->set_bits));
			}else{
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB replacement policy: No candidate based on number of ways.\n");
			}
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB replacement policy: Not able to test.\n");
		}
	}else if(count == STLB_PERMUTATION){
		/*
			Find the permutation vectors of the sTLB.
			Corresponds to Section 4.4.1 of the paper.
		*/

		if(tlb.shared_component && tlb.split_component_data){
			volatile unsigned int vector_index, i, j;
			volatile unsigned int set_attempts[set_bits_to_sets(tlb.shared_component->set_bits)];
			volatile unsigned int (*set_mistakes_early)[tlb.shared_component->ways] = vmalloc(sizeof(int[set_bits_to_sets(tlb.shared_component->set_bits)][tlb.shared_component->ways]));
			volatile unsigned int (*set_mistakes_late)[tlb.shared_component->ways] = vmalloc(sizeof(int[set_bits_to_sets(tlb.shared_component->set_bits)][tlb.shared_component->ways]));

			//Reset counters for extra analysis (not part of paper)
			for(i = 0; i < set_bits_to_sets(tlb.shared_component->set_bits); i++){
				for(j = 0; j < tlb.shared_component->ways; j++){
					set_mistakes_early[i][j] = 0;
					set_mistakes_late[i][j] = 0;
				}

				set_attempts[i] = 0;
			}

			volatile unsigned int broken = 0;
			volatile unsigned int agreement = 0;

			snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB permutation vectors: \n");

			//Finds whether accessing sTLB_ways addresses mapping to the same set
			//causes all of them to be cached in the sTLB
			detect_stlb_vector(-1, NULL, &agreement, NULL, NULL, NULL);

			snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Testing for miss vector: accessing %d addresses resulted in all of them being in the set %d / %d\n", tlb.shared_component->ways, agreement, iterations);

			//Find each permutation vector
			for(vector_index = 0; vector_index < tlb.shared_component->ways; vector_index++){
				agreement = 0;
				volatile int vector[tlb.shared_component->ways];

				for(i = 0; i < tlb.shared_component->ways; i++){
					vector[i] = -1;
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "\u03C0%d: ", vector_index);

				//Find the permutation vector
				detect_stlb_vector(vector_index, vector, &agreement, set_mistakes_early, set_mistakes_late, set_attempts);

				//Write the resulting permutation vector to the output buffer
				for(i = 0; i < tlb.shared_component->ways - 1; i++){
					snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d, ", vector[i]);
					if(vector[i] == -1){
						broken = 1;
					}
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d (agreement %d / %d)\n", vector[tlb.shared_component->ways - 1], agreement, iterations * tlb.shared_component->ways);
			}

			//If we have an incomplete permutation vector, add a warning
			if(broken){
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "WARNING: not all positions filled.\n");
			}

			//More analysis of the results that do not fit the permutation vector (errors)
			//Not described in the paper
			if(show_set_distribution){
				volatile unsigned int total_attempts = 0;
				volatile unsigned int total_mistakes = 0;

				for(i = 0; i < set_bits_to_sets(tlb.shared_component->set_bits); i++){
					total_attempts += set_attempts[i];
					volatile unsigned int current_mistakes = 0;

					for(j = 0; j < tlb.shared_component->ways; j++){
						current_mistakes += set_mistakes_early[i][j];
						current_mistakes += set_mistakes_late[i][j];
					}

					if(current_mistakes != 0){
						total_mistakes += current_mistakes;
						snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Set %d:", i);

						//Early evictions
						for(j = 1; j < tlb.shared_component->ways; j++){
							if(set_mistakes_early[i][j] != 0){
								snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, " %d (-%d)",  set_mistakes_early[i][j], j);
							}
						}

						//Late evictions
						for(j = 1; j < tlb.shared_component->ways; j++){
							if(set_mistakes_late[i][j] != 0){
								snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, " %d (+%d)",  set_mistakes_late[i][j], j);
							}
						}

						//If never evicted mistakes
						if(set_mistakes_late[i][0] != 0){
							snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, " %d (NE)",  set_mistakes_late[i][0]);
						}

						snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "\n\tTotal %d mistakes out of %d attempts\n", current_mistakes, set_attempts[i]);
					}
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Total mistakes: %d\n", total_mistakes);
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Total attempts: %d\n", total_attempts);
			}

			vfree(set_mistakes_early);
			vfree(set_mistakes_late);
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB permutation vectors: Unable to test.\n");
		}
	}else if(count == DTLB_PERMUTATION){
		/*
			Find the permutation vectors of the dTLB.
			Corresponds to Section 4.4.1 of the paper.
		*/

		if(tlb.split_component_data && tlb.shared_component){
			volatile unsigned int vector_index, i, j;
			volatile unsigned int set_attempts[set_bits_to_sets(tlb.split_component_data->set_bits)];
			volatile unsigned int (*set_mistakes_early)[tlb.split_component_data->ways] = vmalloc(sizeof(int[set_bits_to_sets(tlb.split_component_data->set_bits)][tlb.split_component_data->ways]));
			volatile unsigned int (*set_mistakes_late)[tlb.split_component_data->ways] = vmalloc(sizeof(int[set_bits_to_sets(tlb.split_component_data->set_bits)][tlb.split_component_data->ways]));

			//Reset counters for extra analysis (not part of paper)
			for(i = 0; i < set_bits_to_sets(tlb.split_component_data->set_bits); i++){
				for(j = 0; j < tlb.split_component_data->ways; j++){
					set_mistakes_early[i][j] = 0;
					set_mistakes_late[i][j] = 0;
				}

				set_attempts[i] = 0;
			}

			volatile unsigned int broken = 0;
			volatile unsigned int agreement = 0;

			snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB permutation vectors: \n");

            //Finds whether accessing dTLB_ways addresses mapping to the same set
			//causes all of them to be cached in the dTLB
            detect_dtlb_vector(-1, NULL, &agreement, NULL, NULL, NULL);

            snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Testing for miss vector: accessing %d addresses resulted in all of them being in the set %d / %d\n", tlb.split_component_data->ways, agreement, iterations);

			//Find each permutation vector
			for(vector_index = 0; vector_index < tlb.split_component_data->ways; vector_index++){
				agreement = 0;
				volatile int vector[tlb.split_component_data->ways];

				for(i = 0; i < tlb.split_component_data->ways; i++){
					vector[i] = -1;
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "\u03C0%d: ", vector_index);

				//Find the permutation vector
				detect_dtlb_vector(vector_index, vector, &agreement, set_mistakes_early, set_mistakes_late, set_attempts);

				//Write the resulting permutation vector to the output buffer
				for(i = 0; i < tlb.split_component_data->ways - 1; i++){
					snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d, ", vector[i]);
					if(vector[i] == -1){
						broken = 1;
					}
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d (agreement %d / %d)\n", vector[tlb.split_component_data->ways - 1], agreement, iterations * tlb.split_component_data->ways);
			}

			//If we have an incomplete permutation vector, add a warning
			if(broken){
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "WARNING: not all positions filled.\n");
			}

			//More analysis of the results that do not fit the permutation vector (errors)
			//Not described in the paper
			if(show_set_distribution){
				volatile unsigned int total_attempts = 0;
				volatile unsigned int total_mistakes = 0;

				for(i = 0; i < set_bits_to_sets(tlb.split_component_data->set_bits); i++){
					total_attempts += set_attempts[i];
					volatile unsigned int current_mistakes = 0;

					for(j = 0; j < tlb.split_component_data->ways; j++){
						current_mistakes += set_mistakes_early[i][j];
						current_mistakes += set_mistakes_late[i][j];
					}

					if(current_mistakes != 0){
						total_mistakes += current_mistakes;
						snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Set %d:", i);

						//Early evictions
						for(j = 1; j < tlb.split_component_data->ways; j++){
							if(set_mistakes_early[i][j] != 0){
								snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, " %d (-%d)",  set_mistakes_early[i][j], j);
							}
						}

						//Late evictions
						for(j = 1; j < tlb.split_component_data->ways; j++){
							if(set_mistakes_late[i][j] != 0){
								snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, " %d (+%d)",  set_mistakes_late[i][j], j);
							}
						}

						//If never evicted mistakes
						if(set_mistakes_late[i][0] != 0){
							snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, " %d (NE)",  set_mistakes_late[i][0]);
						}

						snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "\n\tTotal %d mistakes out of %d attempts\n", current_mistakes, set_attempts[i]);
					}
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Total mistakes: %d\n", total_mistakes);
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Total attempts: %d\n", total_attempts);
			}

			vfree(set_mistakes_early);
			vfree(set_mistakes_late);
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB permutation vectors: Unable to test.\n");
		}
	}else if(count == ITLB_PERMUTATION){
        /*
			Find the permutation vectors of the iTLB.
			Corresponds to Section 4.4.1 of the paper.
		*/

		if(tlb.split_component_instruction && tlb.shared_component){
			volatile unsigned int vector_index, i, j;
			volatile unsigned int set_attempts[set_bits_to_sets(tlb.split_component_instruction->set_bits)];
			volatile unsigned int (*set_mistakes_early)[tlb.split_component_instruction->ways] = vmalloc(sizeof(int[set_bits_to_sets(tlb.split_component_instruction->set_bits)][tlb.split_component_instruction->ways]));
			volatile unsigned int (*set_mistakes_late)[tlb.split_component_instruction->ways] = vmalloc(sizeof(int[set_bits_to_sets(tlb.split_component_instruction->set_bits)][tlb.split_component_instruction->ways]));

			//Reset counters for extra analysis (not part of paper)
			for(i = 0; i < set_bits_to_sets(tlb.split_component_instruction->set_bits); i++){
				for(j = 0; j < tlb.split_component_instruction->ways; j++){
					set_mistakes_early[i][j] = 0;
					set_mistakes_late[i][j] = 0;
				}

				set_attempts[i] = 0;
			}

			volatile unsigned int broken = 0;
			volatile unsigned int agreement = 0;

			snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB permutation vectors: \n");

            //Finds whether accessing iTLB_ways addresses mapping to the same set
			//causes all of them to be cached in the iTLB
            detect_itlb_vector(-1, NULL, &agreement, NULL, NULL, NULL);

            snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Testing for miss vector: accessing %d addresses resulted in all of them being in the set %d / %d\n", tlb.split_component_instruction->ways, agreement, iterations);

			//Find each permutation vector
			for(vector_index = 0; vector_index < tlb.split_component_instruction->ways; vector_index++){
				agreement = 0;
				volatile int vector[tlb.split_component_instruction->ways];

				for(i = 0; i < tlb.split_component_instruction->ways; i++){
					vector[i] = -1;
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "\u03C0%d: ", vector_index);

				//Find the permutation vector
				detect_itlb_vector(vector_index, vector, &agreement, set_mistakes_early, set_mistakes_late, set_attempts);

				//Write the resulting permutation vector to the output buffer
				for(i = 0; i < tlb.split_component_instruction->ways - 1; i++){
					snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d, ", vector[i]);
					if(vector[i] == -1){
						broken = 1;
					}
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d (agreement %d / %d)\n", vector[tlb.split_component_instruction->ways - 1], agreement, iterations * tlb.split_component_instruction->ways);
			}

			//If we have an incomplete permutation vector, add a warning
			if(broken){
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "WARNING: not all positions filled.\n");
			}

			//More analysis of the results that do not fit the permutation vector (errors)
			//Not described in the paper
			if(show_set_distribution){
				volatile unsigned int total_attempts = 0;
				volatile unsigned int total_mistakes = 0;

				for(i = 0; i < set_bits_to_sets(tlb.split_component_instruction->set_bits); i++){
					total_attempts += set_attempts[i];
					volatile unsigned int current_mistakes = 0;

					for(j = 0; j < tlb.split_component_instruction->ways; j++){
						current_mistakes += set_mistakes_early[i][j];
						current_mistakes += set_mistakes_late[i][j];
					}

					if(current_mistakes != 0){
						total_mistakes += current_mistakes;
						snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Set %d:", i);

						//Early evictions
						for(j = 1; j < tlb.split_component_instruction->ways; j++){
							if(set_mistakes_early[i][j] != 0){
								snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, " %d (-%d)",  set_mistakes_early[i][j], j);
							}
						}

						//Late evictions
						for(j = 1; j < tlb.split_component_instruction->ways; j++){
							if(set_mistakes_late[i][j] != 0){
								snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, " %d (+%d)",  set_mistakes_late[i][j], j);
							}
						}

						//If never evicted mistakes
						if(set_mistakes_late[i][0] != 0){
							snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, " %d (NE)",  set_mistakes_late[i][0]);
						}

						snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "\n\tTotal %d mistakes out of %d attempts\n", current_mistakes, set_attempts[i]);
					}
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Total mistakes: %d\n", total_mistakes);
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Total attempts: %d\n", total_attempts);
			}

			vfree(set_mistakes_early);
			vfree(set_mistakes_late);
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB permutation vectors: Unable to test.\n");
		}
	}else if(count == STLB_PCID){
		/*
			Find the maximum number of PCIDs that the sTLB can keep track of.
			Corresponds to Section 4.5 of the paper.
		*/

		if(tlb.shared_component && tlb.split_component_data){
			u64 cr3k = getcr3();

			int pcid_writes;
			int smallest_pcid_noflush = 4096;
			int smallest_pcid = 4096;
			int evictions[100] = {[0 ... 99] = 0};
			int evictions_no_flush[100] = {[0 ... 99] = 0};

			//Find the limit without the NOFLUSH bit set
			for(pcid_writes = 0; pcid_writes < 4095; pcid_writes++){
				int res = 0;
				for(i = 0; i < iterations; i++){
					res += stlb_pcid_limit(pcid_writes, 0);
				}

				evictions[pcid_writes] = res;

				//If we have a miss every iteration, we found the limit
				if(res == iterations){
					smallest_pcid = pcid_writes;
					break;
				}
			}

			//Find the limit with the NOFLUSH bit set
			for(pcid_writes = 0; pcid_writes < 4095; pcid_writes++){
				int res = 0;
				for(i = 0; i < iterations; i++){
					res += stlb_pcid_limit(pcid_writes, 1);
				}

				evictions_no_flush[pcid_writes] = res;

				//If we have a miss every iteration, we found the limit
				if(res == iterations){
					smallest_pcid_noflush = pcid_writes;
					break;
				}
			}

			tlb.shared_component->pcids_supported = smallest_pcid;
			tlb.shared_component->pcids_supported_no_flush = smallest_pcid_noflush;

			//Write result to output buffer
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "STLB PCID limit: %d (with the NOFLUSH bit: %d)\nDistribution:\n", smallest_pcid, smallest_pcid_noflush);

			for(i = 0; i < smallest_pcid + 1; i++){
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d PCIDs: %d / %d.\n", i, evictions[i], iterations);
			}

			snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Distribution NOFLUSH:\n");

			for(i = 0; i < smallest_pcid_noflush + 1; i++){
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d PCIDs: %d / %d.\n", i, evictions_no_flush[i], iterations);
			}

			setcr3(cr3k);
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "STLB PCID limit: Not present.\n");
		}
	}else if(count == DTLB_PCID){
		/*
			Find the maximum number of PCIDs that the dTLB can keep track of.
			Corresponds to Section 4.5 of the paper.
		*/

		if(tlb.split_component_data && tlb.shared_component){
			u64 cr3k = getcr3();
			int pcid_writes, i;
			int smallest_pcid_noflush = 4096;
			int smallest_pcid = 4096;
			int evictions[100] = {[0 ... 99] = 0};
			int evictions_no_flush[100] = {[0 ... 99] = 0};

			//Find the limit without the NOFLUSH bit set
			for(pcid_writes = 0; pcid_writes < 4095; pcid_writes++){
				int res = 0;
				for(i = 0; i < iterations; i++){
					if(tlb.shared_component){
						res += dtlb_pcid_limit(pcid_writes, 0);
					}
				}

				evictions[pcid_writes] = res;

				//If we have a miss every iteration, we found the limit
				if(res == iterations){
					smallest_pcid = pcid_writes;
					break;
				}
			}

			//Find the limit with the NOFLUSH bit set
			for(pcid_writes = 0; pcid_writes < 4095; pcid_writes++){
				int res = 0;
				for(i = 0; i < iterations; i++){
					if(tlb.shared_component){
						res += dtlb_pcid_limit(pcid_writes, 1);
					}
				}

				evictions_no_flush[pcid_writes] = res;

				//If we have a miss every iteration, we found the limit
				if(res == iterations){
					smallest_pcid_noflush = pcid_writes;
					break;
				}
			}

			//Write result to output buffer
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB PCID limit: %d (with the NOFLUSH bit: %d).\nDistribution:\n", smallest_pcid, smallest_pcid_noflush);

			for(i = 0; i < smallest_pcid + 1; i++){
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d PCIDs: %d / %d.\n", i, evictions[i], iterations);
			}

			snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Distribution NOFLUSH:\n");

			for(i = 0; i < smallest_pcid_noflush + 1; i++){
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d PCIDs: %d / %d.\n", i, evictions_no_flush[i], iterations);
			}

			setcr3(cr3k);
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB PCID limit: Unable to test.\n");
		}
	}else if(count == ITLB_PCID){
		/*
			Find the maximum number of PCIDs that the iTLB can keep track of.
			Corresponds to Section 4.5 of the paper.
		*/

		if(tlb.split_component_instruction && tlb.shared_component){
			u64 cr3k = getcr3();
			int pcid_writes, i;
			int smallest_pcid_noflush = 4096;
			int smallest_pcid = 4096;
			int evictions[100] = {[0 ... 99] = 0};
			int evictions_no_flush[100] = {[0 ... 99] = 0};

			//Find the limit without the NOFLUSH bit set
			for(pcid_writes = 0; pcid_writes < 4095; pcid_writes++){
				int res = 0;
				for(i = 0; i < iterations; i++){
					if(tlb.shared_component){
						res += itlb_pcid_limit(pcid_writes, 0);
					}
				}

				evictions[pcid_writes] = res;

				//If we have a miss every iteration, we found the limit
				if(res == iterations){
					smallest_pcid = pcid_writes;
					break;
				}
			}

			//Find the limit with the NOFLUSH bit set
			for(pcid_writes = 0; pcid_writes < 4095; pcid_writes++){
				int res = 0;
				for(i = 0; i < iterations; i++){
					if(tlb.shared_component){
						res += itlb_pcid_limit(pcid_writes, 1);
					}
				}

				evictions_no_flush[pcid_writes] = res;

				//If we have a miss every iteration, we found the limit
				if(res == iterations){
					smallest_pcid_noflush = pcid_writes;
					break;
				}
			}

			//Write result to output buffer
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB PCID limit: %d (with the NOFLUSH bit: %d).\nDistribution:\n", smallest_pcid, smallest_pcid_noflush);

			for(i = 0; i < smallest_pcid + 1; i++){
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d PCIDs: %d / %d.\n", i, evictions[i], iterations);
			}

			snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "Distribution NOFLUSH:\n");

			for(i = 0; i < smallest_pcid_noflush + 1; i++){
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d PCIDs: %d / %d.\n", i, evictions_no_flush[i], iterations);
			}

			setcr3(cr3k);
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB PCID limit: Unable to test.\n");
		}
	}else if(count == STLB_PCID_PERMUTATION){
		/*
			Find the (sTLB) PCID cache replacement policy,
			Corresponds to Section 4.6 of the paper.
		*/

		//We set iterations low, this test takes way too long otherwise
		iterations = 5;

		if(tlb.shared_component && tlb.split_component_data){
			u64 cr3k = getcr3();
			int vector_index, i;

			int broken;

			snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB PCID permutation vectors: \n");

			//Find each permutation vector (without NOFLUSH)
			for(vector_index = 0; vector_index < tlb.shared_component->pcids_supported; vector_index++){
				int agreement = 0;

				int vector[tlb.shared_component->pcids_supported];

				for(i = 0; i < tlb.shared_component->pcids_supported; i++){
					vector[i] = -1;
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "\u03C0%d: ", vector_index);

				//Find the permutation vector (without NOFLUSH)
				detect_stlb_pcid_permutation(vector_index, vector, &agreement, 0);

				broken = 0;

				//Write the result to the output buffer
				for(i = 0; i < tlb.shared_component->pcids_supported - 1; i++){
					snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d, ", vector[i]);

					if(vector[i] == -1){
						broken = 1;
					}
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d (agreement %d / %d)\n", vector[tlb.shared_component->pcids_supported - 1], agreement, iterations * tlb.shared_component->pcids_supported);
			}

			if(broken){
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "WARNING: not all positions filled.\n");
			}

			snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "sTLB PCID permutation vectors NOFLUSH: \n");

			//Find each permutation vector (with NOFLUSH)
			for(vector_index = 0; vector_index < tlb.shared_component->pcids_supported_no_flush; vector_index++){
				int agreement = 0;

				int vector[tlb.shared_component->pcids_supported_no_flush];

				for(i = 0; i < tlb.shared_component->pcids_supported_no_flush; i++){
					vector[i] = -1;
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "\u03C0%d: ", vector_index);

				//Find the permutation vector (with NOFLUSH)
				detect_stlb_pcid_permutation(vector_index, vector, &agreement, 1);

				broken = 0;

				//Write the result to the output buffer
				for(i = 0; i < tlb.shared_component->pcids_supported_no_flush - 1; i++){
					snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d, ", vector[i]);

					if(vector[i] == -1){
						broken = 1;
					}
				}

				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "%d (agreement %d / %d)\n", vector[tlb.shared_component->pcids_supported_no_flush - 1], agreement, iterations * tlb.shared_component->pcids_supported_no_flush);
			}

			if(broken){
				snprintf(return_message + strlen(return_message), MESSAGE_BUFFER_SIZE, "WARNING: not all positions filled.\n");
			}

			setcr3(cr3k);
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "sTLB PCID permutation vectors: Unable to test.\n");
		}
	}else if(count == RESET_SETTINGS){
		//Resets the setting to default
		iterations = 1000;
		show_set_distribution = 0;
		show_sequence = 0;
		preferred_stlb_set = -1;
		preferred_itlb_set = -1;
		preferred_dtlb_set = -1;
	}else if(count == ENABLE_SET_DISTRIBUTION){
		//Enables the extra analysis, not described in the paper
		show_set_distribution = 1;
	}else if(count == ENABLE_SEQUENCE){
		//Shows the sequence used in the additional replacement policy tests, which
		//are not described in the paper
		show_sequence = 1;
	}else if(count >= START_PREFERRED_STLB_SET && count < END_PREFERRED_STLB_SET){
		//Changes the preferred sTLB set, to allow testing in a single set
		preferred_stlb_set = count - START_PREFERRED_STLB_SET;
	}else if(count >= START_PREFERRED_ITLB_SET && count < END_PREFERRED_ITLB_SET){
		//Changes the preferred iTLB set, to allow testing in a single set
		preferred_itlb_set = count - START_PREFERRED_ITLB_SET;
	}else if(count >= START_PREFERRED_DTLB_SET && count < END_PREFERRED_DTLB_SET){
		//Changes the preferred dTLB set, to allow testing in a single set
		preferred_dtlb_set = count - START_PREFERRED_DTLB_SET;
	}else if(count >= START_ITERATIONS){
		//Changes the number of iterations each test is ran
		iterations = count - START_ITERATIONS;
	}

	//Return the message to userspace
	copy_to_user(buf, return_message, MESSAGE_BUFFER_SIZE);

	return 0;
}

static ssize_t device_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos){
	return 0;
}

static struct file_operations fops = {
	.open = device_open,
	.read = device_read,
	.write = device_write,
};

static struct miscdevice misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mmuctl",
	.fops = &fops,
	.mode = S_IRWXUGO,
};

int init_module(void){
	int ret;

	ret = misc_register(&misc_dev);
	if (ret != 0) {
		printk(KERN_ALERT "mmuctl: failed to register device with %d\n", ret);
		return -1;
	}

	printk(KERN_INFO "mmuctl: initialized.\n");

	return 0;
}

void cleanup_module(void){
	misc_deregister(&misc_dev);
	printk(KERN_INFO "mmuctl: cleaned up.\n");
}
