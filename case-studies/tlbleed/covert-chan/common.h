
#ifndef _TLBCOVERTCOMMON_H
#define _TLBCOVERTCOMMON_H 1

#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <stdlib.h>

#include <pthread.h>
#include <sched.h>

#define PAGE 4096
#define SHAREFILE  "/tmp/.tlb-covert-channel-shared-file"
#define SYNCFILE   "/tmp/.tlb-covert-channel-sync-file"
#define SECRETBITS 256

#define MAGIC 0x31337

// shared memory struct used between sender and receiver to synchronize
struct sharestruct {
	volatile int receivemode_started;
	volatile int sendmode_started;
#define SANITYSET_NONE -1
	volatile int sendmode_sanitycheck_set;
};

// create file that backs sharestruct (called if it doesn't exist)
static int createfile(const char *fnprefix, int setno)
{
        int fd;
        struct stat sb;
        static char fn[3000];
        sprintf(fn, "%s-%d", fnprefix, setno);
        char sharebuf[PAGE];
        if(stat(fn, &sb) != 0 || sb.st_size != PAGE) {
                fd = open(fn, O_RDWR | O_CREAT | O_TRUNC, 0644);
                if(fd < 0) {
			perror("open");
                        fprintf(stderr, "createfile: couldn't create shared file %s\n", fn);
                        exit(1);
                }
                if(write(fd, sharebuf, PAGE) != PAGE) {
                        fprintf(stderr, "createfile: couldn't write shared file\n");
                        exit(1);
                }
                return fd;
        }

        assert(sb.st_size == PAGE);

        fd = open(fn, O_RDWR, 0644);
        if(fd < 0) {
            perror(fn);
                fprintf(stderr, "createfile: couldn't open shared file\n");
                exit(1);
        }
        return fd;

}

// obtain shared memory for sharestruct, shared between receiver and sender
static volatile struct sharestruct *get_sharestruct()
{
    int fd = createfile(SYNCFILE, 0);
	volatile struct sharestruct *ret = (volatile struct sharestruct *) mmap(NULL, PAGE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FILE, fd, 0);
	if(ret == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	return ret;
}

// use cpu affinity to fix us to a single logical processor
static int am_pinned = 0;
static void pin_cpu(size_t i)
{
        cpu_set_t cpu_set;
        pthread_t thread;

        thread = pthread_self();

        assert (i >= 0);
        assert (i < CPU_SETSIZE);

        CPU_ZERO(&cpu_set);
        CPU_SET(i, &cpu_set);

        int v = pthread_setaffinity_np(thread, sizeof cpu_set, &cpu_set);
        if(v != 0) { perror("pthread_setaffinity_np"); exit(1); }
        fprintf(stderr, "# cpu %d\n", (int) i);
        am_pinned=1;
}

#endif
