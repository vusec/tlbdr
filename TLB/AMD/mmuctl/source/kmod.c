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
#include <linux/delay.h>
#include <linux/random.h>
#include <helpers.h>
#include <pgtable.h>
#include <experiments.h>
#include <mem_access.h>
#include <address_generation.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include "../../settings.h"

struct TLB_level shared_level;
struct TLB_level split_level_data;
struct TLB_level split_level_instruction;
struct TLB tlb;

int max_dtlb_ways = -1;
int max_itlb_ways = -1;

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

static int device_open(struct inode *inode, struct file *file)
{
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

	if(count == SHARED){
		/*
			Tests whether a PTE cached in response to a data load can be used
			for an consequent instruction fetch and vice versa.
			The experiment is described in Section A.1 of the paper.
		*/

		int number_to_evict = 5000;
		
		int score_itlb_dtlb = 0; int score_dtlb_itlb = 0;
		int wash; int first_hit = -1;

		for(wash = 0; wash < number_to_evict; wash++){
			for(i = 0; i < 10; i++){
				score_itlb_dtlb += seperate_itlb_and_dtlb(wash);
				score_dtlb_itlb += seperate_dtlb_and_itlb(wash);

				if((score_itlb_dtlb != 0 || score_dtlb_itlb != 0) && first_hit == -1) first_hit = wash;
			}
		}

		if(score_itlb_dtlb < iterations * number_to_evict && score_dtlb_itlb < iterations * number_to_evict){
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB and dTLB are completely separated (success rate " BOLD_BLACK "%d / %d +  %d / %d = %d / %d" RESET ", first hit after %d evictions)\n", score_itlb_dtlb, iterations * number_to_evict, score_dtlb_itlb, iterations * number_to_evict, score_itlb_dtlb + score_dtlb_itlb, 2 * iterations * number_to_evict, first_hit);
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB and dTLB have a shared component (success rate " BOLD_BLACK "%d / %d +  %d / %d = %d / %d" RESET ", first hit after %d evictions)\n", score_itlb_dtlb, iterations * number_to_evict, score_dtlb_itlb, iterations * number_to_evict, score_itlb_dtlb + score_dtlb_itlb, 2 * iterations * number_to_evict, first_hit);
		}
	}else if(count == DTLB_BITS){
		/*
			Tests which bits of the virtual address are used to index a dTLB set.
			The experiment is described in Section A.2 of the paper.
		*/

		printk("\n\nDTLB BITS:\n");
		int bit, ways, set_bits = 20;

		int max_ways = 0;
		int min_bit = 99; int max_bit = 0;

		for(ways = 1; ways < 10; ways++){
			int bits_cnt = 0;
			int bit_helps = 0;

			printk("\n\nWays: %d\n", ways);

			for(bit = 0; bit < set_bits; bit++){
				int score = 0;

				for(i = 0; i < iterations; i++){
					score += detect_bits_dtlb(set_bits, bit, ways);
				}

				printk("Bit %d: %d / %d\n", bit, score, iterations);

				if(score != iterations){
					bit_helps = 1;
					bits_cnt++;
				}
			}

			if(!bit_helps && !max_ways) max_ways = ways - 1;  
			if(bit_helps){
				min_bit = min(min_bit, bits_cnt);
				max_bit = max(max_bit, bits_cnt);
			}
		}

		if(max_ways && min_bit != 99 && max_bit != 0){
			max_dtlb_ways = max_ways;
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "The dTLB has %d ways. Between %d and %d bits are used for the hash function (see dmesg for result).\n", max_ways, min_bit, max_bit);
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "Unable to determine number of dTLB ways and/or indexing bits (see dmesg for result)\n");
		}
	}else if(count == ITLB_BITS){
		/*
			Tests which bits of the virtual address are used to index a iTLB set.
			The experiment is described in Section A.2 of the paper.
		*/

		printk("\n\nITLB BITS:\n");
		int bit, ways, set_bits = 20;

		int max_ways = 0;
		int min_bit = 99; int max_bit = 0;

		for(ways = 1; ways < 10; ways++){
			int bits_cnt = 0;
			int bit_helps = 0;

			printk("\n\nWays: %d\n", ways);

			for(bit = 0; bit < set_bits; bit++){
				int score = 0;

				for(i = 0; i < iterations; i++){
					score += detect_bits_itlb(set_bits, bit, ways);
				}

				printk("Bit %d: %d / %d\n", bit, score, iterations);

				if(score != iterations){
					bit_helps = 1;
					bits_cnt++;
				}
			}

			if(!bit_helps && !max_ways) max_ways = ways - 1;  
			if(bit_helps){
				min_bit = min(min_bit, bits_cnt);
				max_bit = max(max_bit, bits_cnt);
			}
		}

		if(max_ways && min_bit != 99 && max_bit != 0){
			max_itlb_ways = max_ways;
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "The iTLB has %d ways. Between %d and %d bits are used for the hash function (see dmesg for result).\n", max_ways, min_bit, max_bit);
		}else{
			snprintf(return_message, MESSAGE_BUFFER_SIZE, "Unable to determine number of dTLB ways and/or indexing bits (see dmesg for result)\n");
		}
	}else if(count == INCLUSIVITY){
		/*
			Tests whether a PTE can be cached in L1 independently of L2.
			The experiment is described in Section A.3 of the paper.
		*/

		int data_score = 0; int instruction_score = 0;
		int set_bits = 15;
		int itlb_ways = max_itlb_ways != -1 ? max_itlb_ways : 12;
		int dtlb_ways = max_dtlb_ways != -1 ? max_dtlb_ways : 12;

		for(i = 0; i < iterations; i++){
			data_score += determine_inclusivity_data(set_bits, dtlb_ways + 1);
			instruction_score += determine_inclusivity_instructions(set_bits, itlb_ways + 1);
		}

		if(data_score > iterations / 2){
			if(instruction_score > iterations / 2){
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB and dTLB are both inclusive (success rate " BOLD_BLACK "data: %d / %d, instruction: %d / %d" RESET ")\n", data_score, iterations, instruction_score, iterations);
			}else{
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB is not inclusive, but dTLB is inclusive (success rate " BOLD_BLACK "data: %d / %d, instruction: %d / %d" RESET ")\n", data_score, iterations, instruction_score, iterations);
			}
		}else{
			if(instruction_score > iterations / 2){
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "dTLB is not inclusive, but iTLB is inclusive (success rate " BOLD_BLACK "data: %d / %d, instruction: %d / %d" RESET ")\n", data_score, iterations, instruction_score, iterations);
			}else{
				snprintf(return_message, MESSAGE_BUFFER_SIZE, "iTLB and dTLB are both NOT inclusive (success rate " BOLD_BLACK "data: %d / %d, instruction: %d / %d" RESET ")\n", data_score, iterations, instruction_score, iterations);
			}
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
		//Enables the extra analysis, not described in the paper & not used for AMD
		show_set_distribution = 1;
	}else if(count == ENABLE_SEQUENCE){
		//Shows the sequence used in the additional replacement policy tests, which
		//are not described in the paper and not used for AMD.
		show_sequence = 1;
	}else if(count >= START_PREFERRED_STLB_SET && count < END_PREFERRED_STLB_SET){
		//Changes the preferred sTLB set, to allow testing in a single set. Not used for AMD.
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
