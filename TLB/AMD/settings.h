#ifndef _SETTINGS_H_
#define _SETTINGS_H_

struct TLB_level {
	volatile unsigned int hash_function;
	volatile unsigned int set_bits;
	volatile unsigned int ways;
	volatile unsigned int pcids_supported;
	volatile unsigned int pcids_supported_no_flush;
};

struct TLB {
	struct TLB_level *split_component_data;
	struct TLB_level *split_component_instruction;
	struct TLB_level *shared_component;
}; 

//Settings that can be used by arguments 
extern int show_set_distribution;
extern int show_sequence;
extern int iterations;
extern int preferred_stlb_set;
extern int preferred_itlb_set;
extern int preferred_dtlb_set;
extern int replacement_number_of_pages;
extern int oke;

//For storing information acquired during testing
extern struct TLB_level shared_level;
extern struct TLB_level split_level_data;
extern struct TLB_level split_level_instruction;
extern struct TLB tlb;

//For increasing readability
#define PAGE_SIZE (0x1000)
#define LIN (0)
#define XOR (1)

//The address at which we allocate
#define BASE ((void *)0x133800000000ULL) 
//Determines how many virtual pages will be allocated. In total, we use 2^FREEDOM_OF_BITS virtual pages.
#define FREEDOM_OF_BITS (30)
//Determines how many physical pages will be allocated. In total, we use 2^UNIQUE_BITS physical pages.
#define UNIQUE_BITS (20) 
//Determines how many characters the return message buffer can hold
#define MESSAGE_BUFFER_SIZE (10000)

//Some nice colors
#define RESET "\033[0m"
#define BOLD_BLACK "\033[1m\033[30m" 

//For increasing readability, the experiments
#define SHARED (0)
#define DTLB_BITS (1)
#define ITLB_BITS (2)
#define INCLUSIVITY (3)

//For increasing readability, the settings
#define RESET_SETTINGS (49)
#define ENABLE_SET_DISTRIBUTION (99)
#define ENABLE_SEQUENCE (98)
#define START_PREFERRED_STLB_SET (1000)
#define END_PREFERRED_STLB_SET (2000)
#define START_PREFERRED_DTLB_SET (3000)
#define END_PREFERRED_DTLB_SET (4000)
#define START_PREFERRED_ITLB_SET (2000) 
#define END_PREFERRED_ITLB_SET (3000)
#define START_ITERATIONS (4000)

#endif
