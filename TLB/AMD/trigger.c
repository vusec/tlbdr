#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>

#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <stdlib.h>

#include <sched.h>
#include "settings.h"
#include <getopt.h>

#define BUF_LENGTH (100)
#define NUMBER_OF_TESTS (19)

int tests[NUMBER_OF_TESTS] = {SHARED, DTLB_BITS, ITLB_BITS, INCLUSIVITY};

int pinned_core = 0;
int number_of_cores = 0;
int disable_hyper = 1;
int stress = 0;
int test = 0;

void disable_core(unsigned int core){
	FILE *fp;
	char buf[BUF_LENGTH];

	snprintf(buf, BUF_LENGTH, "/sys/devices/system/cpu/cpu%d/online", core);
	fp = fopen(buf, "w");
	fprintf(fp, "0");
	fclose(fp);
}

void enable_core(unsigned int core){
	FILE *fp;
	char buf[BUF_LENGTH];

	snprintf(buf, BUF_LENGTH, "/sys/devices/system/cpu/cpu%d/online", core);
	fp = fopen(buf, "w");
	fprintf(fp, "1");
	fclose(fp);
}

int get_phys_core(unsigned int core){
	FILE *fp;
	unsigned int phys_core;
	char buf[BUF_LENGTH];

	snprintf(buf, BUF_LENGTH, "/sys/devices/system/cpu/cpu%d/topology/core_id", core);
	fp = fopen(buf, "r");
	fscanf(fp, "%d,", &phys_core);
	fclose(fp);

	return phys_core;
}

void enable_hyperthreading(){ 
	int core;
	
	for(core = 1; core < number_of_cores; core++){
		enable_core(core);
	}
}

void set_number_of_cores(){
	FILE *fp;
	char buf[BUF_LENGTH];
	int core = 1;

	while(1){ 
		snprintf(buf, BUF_LENGTH, "/sys/devices/system/cpu/cpu%d/online", core);
		if(access(buf, F_OK ) == -1) {
			number_of_cores = core;
			break;
		}

		core++;
	}
}

int get_co_resident(unsigned long co_core){
	FILE *fp;
	char buf[BUF_LENGTH];
	int needed_phys_core;
	int core;

	snprintf(buf, BUF_LENGTH, "/sys/devices/system/cpu/cpu%d/topology/core_id", co_core);
	fp = fopen(buf, "r");
	fscanf(fp, "%d,", &needed_phys_core);
	fclose(fp);

	for(core = 0; core < number_of_cores; core++){
		if(core == pinned_core){
			continue;
		}

		int phys_core = get_phys_core(core);

		if(phys_core == needed_phys_core){
			return core;
		}
	}


	return -1;
}

void disable_hyperthreading(){
	int core;

	for(core = 1; core < number_of_cores; core++){
		int phys_core = get_phys_core(core);

		if(core == phys_core){
			enable_core(core);
		}else{
			disable_core(core);
		}
	} 
}

void disable_all(){
	FILE *fp;
	char buf[BUF_LENGTH];
	int core;

	for(core = 1; core < number_of_cores; core++){
		disable_core(core);
	} 
}

void remove_line_above(){
	printf("\033[A\33[2K\r");
}

char read_response(){
	char res = getchar();

	while (res == '\n' || res == EOF){
		res = getchar();
	}

	return res;
}

int read_args(int argc, char *argv[], int fd){	
    int opt, option_index;
    static struct option long_options[] = {
		{"set-distribution",  no_argument, 0, 'f'},
		{"sequence",  no_argument, 0, 'c'},
		{"iterations",  required_argument, 0, 'n'},
		{"stlb-set",  required_argument, 0, 's'},
		{"itlb-set",  required_argument, 0, 'i'},
		{"dtlb-set",  required_argument, 0, 'd'},
		{"stress",  no_argument, 0, 't'},
		{"hyperthreading",  no_argument, 0, 'h'},
		{"core",  required_argument, 0, 'p'},
		{"test",  required_argument, 0, 'l'},
		{0, 0, 0, 0}       
	};

	while((opt = getopt_long(argc, argv, "di:", long_options, &option_index)) != -1){
		switch(opt){
			case 'n':
				if(atoi(optarg) < 0){
					printf("Invalid number of iterations\n");
					return 1;
				}

				read(fd, NULL, START_ITERATIONS + atoi(optarg));
				break;
			case 'f':
				read(fd, NULL, ENABLE_SET_DISTRIBUTION);
				break;
			case 's':
				if(atoi(optarg) < 0 || atoi(optarg) > (END_PREFERRED_STLB_SET - START_PREFERRED_STLB_SET - 1)){
					printf("Invalid chosen set\n");
					return 1;
				}

				read(fd, NULL, START_PREFERRED_STLB_SET + atoi(optarg));
				break;
			case 'i':
				if(atoi(optarg) < 0 || atoi(optarg) > (END_PREFERRED_ITLB_SET - START_PREFERRED_ITLB_SET - 1)){
					printf("Invalid chosen set\n");
					return 1;
				}
				
				read(fd, NULL, START_PREFERRED_ITLB_SET + atoi(optarg));
				break;
			case 'd':
				if(atoi(optarg) < 0 || atoi(optarg) > (END_PREFERRED_DTLB_SET - START_PREFERRED_DTLB_SET - 1)){
					printf("Invalid chosen set\n");
					return 1;
				}

				read(fd, NULL, START_PREFERRED_DTLB_SET + atoi(optarg));
				break;
			case 'c':
				read(fd, NULL, ENABLE_SEQUENCE);
				break;
			case 't':
				disable_hyper = 0;
				stress = 1;
				break;
			case 'h':
				disable_hyper = 0;
				break;
			case 'p':
				if(atoi(optarg) < 0 || atoi(optarg) > number_of_cores){
					printf("Invalid number of cores\n");
					return 1;
				}

				pinned_core = atoi(optarg);
				break;
			case 'l':
				if(atoi(optarg) < 1 || atoi(optarg) > NUMBER_OF_TESTS){
					printf("Invalid test\n");
					return 1;
				}

				test = atoi(optarg);
				break;
			case '?':
				printf("Unknown option\n"); 
				return 1;
		}
	}
}
	
int main(int argc, char *argv[]) 
{ 
	int fd = open("/dev/mmuctl", O_RDONLY);
	read(fd, NULL, 49);

	set_number_of_cores(); 
	enable_hyperthreading();

	if(read_args(argc, argv, fd) == 1){
		return 1;
	}

	const int BUF_PROT = PROT_READ|PROT_WRITE|PROT_EXEC;

	unsigned long number_of_pages = pow(2, FREEDOM_OF_BITS);
	unsigned long unique_pages = pow(2, UNIQUE_BITS);
 
	int fd_shm = shm_open("/example_shm", O_RDWR | O_CREAT, 0777);
	ftruncate(fd_shm, PAGE_SIZE * unique_pages);
	int i;

	for(i = 0; i < number_of_pages / unique_pages; i++){
		if(mmap(BASE + (PAGE_SIZE * unique_pages * i), PAGE_SIZE * unique_pages, BUF_PROT, MAP_SHARED|MAP_POPULATE, fd_shm, 0) == MAP_FAILED){
			printf("Unable to allocate memory at %p (i = %d)\n", BASE + (PAGE_SIZE * unique_pages * i), i);
			return 1;
		}
	}
 
	volatile unsigned char *p1;
	for(i = 0; i < unique_pages; i++){
		p1 = BASE + (4096 * i);
		*(uint16_t *)p1 = 0x9090;
		p1[2] = 0x48; p1[3] = 0xb8;
		*(uint64_t *)(&p1[4]) = i;
		p1[12] = 0xc3;		
	}  
	
	int res;
	printf("This tool tests TLB properties. It will disable all but one core per physical core. In addition, kernel preemption and interrupts will be disabled while testing. YOU MAY LOSE CONTROL OVER YOUR MACHINE DURING TESTING. Please save important work before proceeding. Do you want to continue [y|n]?\n");
	if(read_response() != 'y'){
		return 0;
	}

	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(pinned_core, &mask);
	if(sched_setaffinity(0, sizeof(mask), &mask) == -1){
		printf("Unable to pin at core %d\n", pinned_core);
		return 1;
	}

	int coresident = get_co_resident(pinned_core);

	if(disable_hyper){
		if(coresident != -1){
			disable_core(coresident);
			printf("Pinned on core %d, disabled core %d.\n\n", pinned_core, coresident);
		}else{
			printf("Pinned on core %d, no co-resident found\n\n", pinned_core);
		}
		
	}

	if(stress){
		if(fork() == 0){			
			if(coresident == -1){
				printf("Not able to stress; could not find two logical cores on physical core %d\n", pinned_core);
				return 1;
			}

			char buf[100];
			snprintf(buf, 100, "taskset -c %d-%d stress -m 1", coresident, coresident);
			system(buf);
			return 0;
		}

		//Bit of delay; we want stress to have started before testing
		volatile unsigned long p;
		for(p = 0; p < 10000; p++){}
	}

	char *msg = malloc(sizeof(char) * MESSAGE_BUFFER_SIZE); 

	if(test != 0){
		printf("Testing, please wait a moment...\n");
		read(fd, msg, test - 1);
		remove_line_above();
		printf("%d. %s\n", test, msg);
	}else{
		for(i = 0; i < 4; i++){
			printf("Testing, please wait a moment...\n");
			read(fd, msg, tests[i]);
			remove_line_above();
			printf("%d. %s\n", i + 1, msg);
		}
	}

	enable_hyperthreading();
	printf("Enabled all cores.\n");

	return 0;
}
