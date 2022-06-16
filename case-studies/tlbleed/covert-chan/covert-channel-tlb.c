
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "profile.h"
#include "common.h"


// THRESHOLD TWEAKS

#define TIMETHRESH_NAIVE (500)
#define TIMETHRESH_NINJA (108)
#define NINJA_MASK (0x1ff)

// END OF TWEAKS


static unsigned char const crc8_table[] = {
    0xea, 0xd4, 0x96, 0xa8, 0x12, 0x2c, 0x6e, 0x50, 0x7f, 0x41, 0x03, 0x3d,
    0x87, 0xb9, 0xfb, 0xc5, 0xa5, 0x9b, 0xd9, 0xe7, 0x5d, 0x63, 0x21, 0x1f,
    0x30, 0x0e, 0x4c, 0x72, 0xc8, 0xf6, 0xb4, 0x8a, 0x74, 0x4a, 0x08, 0x36,
    0x8c, 0xb2, 0xf0, 0xce, 0xe1, 0xdf, 0x9d, 0xa3, 0x19, 0x27, 0x65, 0x5b,
    0x3b, 0x05, 0x47, 0x79, 0xc3, 0xfd, 0xbf, 0x81, 0xae, 0x90, 0xd2, 0xec,
    0x56, 0x68, 0x2a, 0x14, 0xb3, 0x8d, 0xcf, 0xf1, 0x4b, 0x75, 0x37, 0x09,
    0x26, 0x18, 0x5a, 0x64, 0xde, 0xe0, 0xa2, 0x9c, 0xfc, 0xc2, 0x80, 0xbe,
    0x04, 0x3a, 0x78, 0x46, 0x69, 0x57, 0x15, 0x2b, 0x91, 0xaf, 0xed, 0xd3,
    0x2d, 0x13, 0x51, 0x6f, 0xd5, 0xeb, 0xa9, 0x97, 0xb8, 0x86, 0xc4, 0xfa,
    0x40, 0x7e, 0x3c, 0x02, 0x62, 0x5c, 0x1e, 0x20, 0x9a, 0xa4, 0xe6, 0xd8,
    0xf7, 0xc9, 0x8b, 0xb5, 0x0f, 0x31, 0x73, 0x4d, 0x58, 0x66, 0x24, 0x1a,
    0xa0, 0x9e, 0xdc, 0xe2, 0xcd, 0xf3, 0xb1, 0x8f, 0x35, 0x0b, 0x49, 0x77,
    0x17, 0x29, 0x6b, 0x55, 0xef, 0xd1, 0x93, 0xad, 0x82, 0xbc, 0xfe, 0xc0,
    0x7a, 0x44, 0x06, 0x38, 0xc6, 0xf8, 0xba, 0x84, 0x3e, 0x00, 0x42, 0x7c,
    0x53, 0x6d, 0x2f, 0x11, 0xab, 0x95, 0xd7, 0xe9, 0x89, 0xb7, 0xf5, 0xcb,
    0x71, 0x4f, 0x0d, 0x33, 0x1c, 0x22, 0x60, 0x5e, 0xe4, 0xda, 0x98, 0xa6,
    0x01, 0x3f, 0x7d, 0x43, 0xf9, 0xc7, 0x85, 0xbb, 0x94, 0xaa, 0xe8, 0xd6,
    0x6c, 0x52, 0x10, 0x2e, 0x4e, 0x70, 0x32, 0x0c, 0xb6, 0x88, 0xca, 0xf4,
    0xdb, 0xe5, 0xa7, 0x99, 0x23, 0x1d, 0x5f, 0x61, 0x9f, 0xa1, 0xe3, 0xdd,
    0x67, 0x59, 0x1b, 0x25, 0x0a, 0x34, 0x76, 0x48, 0xf2, 0xcc, 0x8e, 0xb0,
    0xd0, 0xee, 0xac, 0x92, 0x28, 0x16, 0x54, 0x6a, 0x45, 0x7b, 0x39, 0x07,
    0xbd, 0x83, 0xc1, 0xff};

// Return the CRC-8 of data[0..len-1] applied to the seed crc. This permits the
// calculation of a CRC a chunk at a time, using the previously returned value
// for the next seed. If data is NULL, then return the initial seed. See the
// test code for an example of the proper usage.
static inline unsigned crc8(unsigned crc, unsigned char const *data, size_t len)
{
    if (data == NULL)
        return 0;
    crc &= 0xff;
    unsigned char const *end = data + len;
    while (data < end)
        crc = crc8_table[crc ^ *data++];
    return crc;
}

/* !!!
 * uarch dependencies: how many TLB sets are there? and how big?
 * this determines HARDWARESETS and SETLIMIT
 */
#define HARDWARESETS 128
#define SETLIMIT 128
// uarch dependencies: TLB set size is a runtime setting but we want to know a maximum possible value
#define SETSIZE_MAX  64
static const int use_setsize = 12; // !! uarch dependency: tlb set size, set at runtime

/* These are TLB set number used for specific communication purposes. */
#define SET_SYNC_R_READY_DATA  0 /* receiver is ready to receive: set by receiver, probed by transmitter */
#define SET_SYNC_T_READY_DATA  1 /* data is available in tlb sets: set by transmitter, probed by receiver */
#define SET_DATA  2              /* tlb set number where data bits start */
#define WORDLEN 32               /* number of bits transmitted in parallel */
#define SET_SYNC_R_READY_REQ  (SET_DATA+WORDLEN)
#define SET_SYNC_T_READY_REQ  (SET_DATA+WORDLEN+1)
#define SET_REQ               (SET_DATA+WORDLEN+2)
#define SET_SAFE              (SET_DATA+WORDLEN+3)
#define REQLEN                (24)

#define REDUNDANCY     16


/* this is an area in virtual address space where everything happens. we allocate memory
 * here for our covert channel and also buffers that we want to write to (outside the sets
 * used for out covert channel.)
 */
#define VTARGET 0x200000000000ULL

#define CACHELINE 64





#define TLLINE(x) ((x) >> 12)

static inline uintptr_t TDL1(uintptr_t va) {return TLLINE(va) & 0xf;}
static inline uintptr_t TIL1(uintptr_t va) {return TLLINE(va) & 0x7;}
static inline uintptr_t TSL2(uintptr_t va)
{
	uintptr_t p = TLLINE(va);
	return (p ^ (p >> 7)) & 0x7f;
}



#define WAYBIT (16)

//static int set_offset = -1;
//#define calc_pageno(set, i) (i * (1ULL << 16) + (((set)+set_offset) % HARDWARESETS))
#define calc_pageno(set, i) (i * (1ULL << WAYBIT) + set)

void allocate_buffer(int *fdarray, volatile char *firstpage_d)
{
	volatile char *target_d, *target_i;

    int set, i;
    for(set = 0; set < SETLIMIT; set++) {
        for(i = 0; i < SETSIZE_MAX; i++) {
            unsigned long long p = calc_pageno(set, i);
            target_d = (void *) (firstpage_d+p*PAGE);
    		char *ret;

    		ret = mmap((void *) target_d, PAGE, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED|MAP_FILE|MAP_FIXED, fdarray[set], 0);
    		if(ret == MAP_FAILED) {
				fprintf(stderr, "failed at allocate_buffer\n");
			perror("mmap");
    			exit(1);
    		}
    		if(ret != (volatile char *) target_d) { fprintf(stderr, "Wrong mapping\n"); exit(1); }
	    }
    }
}


#define EVBSZ (calc_pageno(0, 32)*PAGE)
static void setup_tbuf(void)
{
	if (mmap((void *)VTARGET, EVBSZ, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0) == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
}


// calculate the virtual address of a single page
// buffer occuping tlb set number 'set', position number 'i'.
static inline uint64_t calc_probe(int set, int i)
{
	uint64_t pageno = calc_pageno(set, i);
	//assert(pageno >= 0);
    uint64_t *probe = (uint64_t *) (VTARGET+pageno*PAGE);
    //if((uint64_t) probe < VTARGET) printf("%lx; pageno %lx\n", (uint64_t) probe, pageno);
	//assert((uint64_t) probe >= VTARGET);
    uint64_t index = i*16;
    assert(index >= 0);
    assert(index < 512);
    uint64_t rv = (uint64_t) &probe[index];
	return rv;
}


/* mmap_safe returns a single allocated page that we can write into (e.g. timing values) during the experiment
 * *without* perturbing the covert channel.
 * We do this by picking pages in the VTARGET area that occupy the 'safeset' tlb set, and therefore
 * are not used by our covert channel.
 *
 * this is used for higher level buffer allocation of large safe buffers.
 */
static unsigned char *mmap_safe(uint64_t safeset)
{
    static uint64_t safe_i = SETSIZE_MAX+1;
    safe_i++;
    uint64_t safe_pageno = calc_pageno(safeset, safe_i); // get pages from the 'safeset' tlb set.
    unsigned char *safe_probe = (unsigned char *) (VTARGET+safe_pageno*PAGE);
    assert((uint64_t) safe_probe >= VTARGET); // check for wrap

    // actually allocate the page
    unsigned char *ret = mmap(safe_probe, PAGE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);

    if(ret == MAP_FAILED) {
				fprintf(stderr, "failed at mmap_safe\n");
				perror("mmap"); exit(1);
	}

    if(ret != safe_probe) {
		fprintf(stderr, "mmap wrong; asked for %lx, got %lx\n",
			(uint64_t) safe_probe,
			(uint64_t) ret);
			exit(1);
	}

   memset(ret, 0, PAGE);

   return ret;
}

// use mmap_safe() to allocate a large buffer that we can safely write to without perturbing the covert channel.
// this is virtually noncontiguous so some indirection is needed to access the buffer.
static unsigned char **allocpages(uint64_t bytes, uint64_t safeset)
{
    uint64_t pages = 1 + (bytes/PAGE);
    uint64_t p;
    unsigned char **pageptr;

    pageptr = (unsigned char **) mmap_safe(safeset);

    for(p = 0; p < pages; p++) {
        assert(p * sizeof(pageptr[0]) < PAGE);
        pageptr[p] = mmap_safe(safeset);
    }

    return pageptr;
}


#define ipchase(head, niter, tim) asm volatile (\
	"mov %%ecx, %%esi\n"\
	"lfence\n"\
	"rdtsc\n"\
	"mov %%eax, %%edi\n"\
	"1:"\
	"mov (%[probe]),%[probe]\n"\
	"loop 1b\n"\
	"lfence\n"\
	"rdtscp\n"\
	"sub %%edi, %%eax\n"\
	"mov %%esi, %%ecx\n"\
	: [probe]"+r" (head), "=a" (tim) : "c" (niter) : "rdx", "rdi", "rsi", "cc"\
)

//static volatile uint64_t njcur[SETLIMIT];
static volatile uint64_t *njcur;
#define NJ_SETSZ (12)
#define NJ_STEP (4)
#define NJ_INIT (18+NJ_SETSZ)

#ifndef USE_NINJA
#define DT_THRESH (TIMETHRESH_NAIVE)
#else
#define DT_THRESH (TIMETHRESH_NINJA)
#endif

#if 1
static inline uint32_t _tevset(uint64_t set)
{
    uint64_t prb;
    prb = njcur[set];
#ifndef USE_NINJA
    prb = calc_probe(set, 1);
#endif
    uint32_t tim;
#ifndef USE_NINJA
    ipchase(prb, use_setsize, tim);
#else
    ipchase(prb, NJ_STEP, tim);
#endif
    njcur[set] = prb;
    return tim;
}
#else
// return logical 0 or 1, indicating whether a TLB set is 'full' or not.
// this is done by probing it using simple pointer chasing and rdtsc.
static inline uint32_t _tevset(uint64_t set)
{
            uint64_t probe = calc_probe(set, 1);
            uint32_t time1,time2;
asm volatile (
"lfence\n"
"rdtsc\n"
"mov %%eax, %%edi\n"
"1:\n"
"mov (%2), %2\n"
"test %2, %2\n"
"jnz 1b\n"
"lfence\n"
"rdtscp\n"
"mov %%edi, %0\n"
"mov %%eax, %1\n"
        : "=r" (time1), "=r" (time2)
        : "r" (probe)
        : "rax", "rbx", "rcx", "rdx", "rdi" );

            return time2-time1;
}
#endif

static int _evset(uint64_t set)
{
    uint32_t dt = _tevset(set);
    //printf("%u\n", dt);
    if(dt <= DT_THRESH) return 0;
    return 1; // tlb miss
}

static void _touchset(uint64_t set)
{
asm volatile (
"lfence\n"
"mov (%0), %%rax\n"
"lfence\n"
    : : "r" (calc_probe(set, 0)) : "rax"
);
}

#define TM_THRESH (50)

static int _timeset(uint64_t set)
{
    uint32_t tim;
asm volatile (
"lfence\n"
"rdtsc\n"
"mov %%eax, %%edi\n"
"clflush (%%rcx)\n"
//"mov (%%rcx), %%rax\n"
"lfence\n"
"rdtscp\n"
"sub %%edi, %%eax\n"
    : "=a" (tim) : "c" (calc_probe(set, 0)) : "rdi", "rdx", "cc"
);
    return tim > TM_THRESH;
}


#define MEV (2)
#define MTH (1)
#define MEV_THRESH (180)

static int _mevset(uint64_t set)
{
    uint64_t t = 0;
    for (int i = 0; i < MEV; i++) {
        t += _tevset(set);
    }
    return t > MEV_THRESH;
}

static inline uint16_t nxr(uint16_t x)
{
	x ^= x >> 7;
	x ^= x << 9;
	x ^= x >> 13;
	return x;
}
#define DO_DELAY {rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);rn = nxr(rn);}

static void _mtouchset(uint64_t set)
{
    uint16_t rn = 0xace1;
    for (int i = 0; i < MTH; i++) {
        _touchset(set);
        //DO_DELAY;
    }
}


#if 0
#define getset(x) _timeset(x)
#define putset(x) _evset(x)
#else
#define getset(x) _evset(x)
//#define getset(x) _mevset(x)
//#define putset(x) _evset(x)
#define putset(x) _touchset(x)
//#define putset(x) _mtouchset(x)
#define mputset(x) _touchset(x)
//#define mputset(x) _mtouchset(x)
#endif


// set up TLB eviction sets for all tlb sets. SETLIMIT is the number of hardware tlb sets.
void setup_buffer(void)
{
	uint64_t set;
    for(set = 0; set < SETLIMIT; set++) {
        uint64_t i;
        for(i = 0; i < use_setsize; i++) {
            uint64_t *probe = (uint64_t *) calc_probe(set, 1+i);
            uint64_t next = calc_probe(set, 1+((i+1) % use_setsize));
		    //if(i == use_setsize-1) { next = 0; }
            // here we set up the pointer chasing: write the pointer of the next set into this buffer,
            // or 0 if this is the last one (determined by use_setsize).
            *probe = (uint64_t) next;
    	}
    }

}


#define WAYX(p) ((int)((uintptr_t)p >> (12+WAYBIT)) & 0xfff)

// set up TLB eviction sets for all tlb sets. SETLIMIT is the number of hardware tlb sets.
void setup_buffer_ninja(void)
{
	uint64_t set;
    for(set = 0; set < SETLIMIT; set++) {
        //printf("\nOn to set %3ld\n", set);
        for(int i = 0; i < NJ_SETSZ; i++) {
            uint64_t *probe = (uint64_t *) calc_probe(set, 1+i);
            uint64_t next = calc_probe(set, 1+i+1);
            *probe = (uint64_t) next;
            //printf("%p -> %p (%2d -> %2d)\n", probe, (void *)next, WAYX(probe), WAYX(next));
    	}
        void **ev[9];
        for (int i = 0; i < 9; i++) {
            ev[i] = (void **)calc_probe(set, 1 + NJ_SETSZ + i);
            //printf("%p %2d\n", ev[i], WAYX(ev[i]));
        }
        ev[0][0] = &ev[1][0];
        ev[0][1] = &ev[2][1];
        ev[0][2] = &ev[2][2];
        ev[0][3] = &ev[8][0];
        ev[0][4] = &ev[2][4];
        ev[0][5] = &ev[4][2];
        ev[0][6] = &ev[2][6];

        ev[1][0] = &ev[2][0];
        ev[1][1] = &ev[6][0];
        ev[1][2] = &ev[0][3];
        ev[1][3] = &ev[5][1];
        ev[1][4] = &ev[0][5];
        ev[1][5] = &ev[6][2];

        ev[2][0] = &ev[3][0];
        ev[2][1] = &ev[1][1];
        ev[2][2] = &ev[7][0];
        ev[2][3] = &ev[1][3];
        ev[2][4] = &ev[3][2];
        ev[2][5] = &ev[1][5];
        ev[2][6] = &ev[7][2];

        ev[3][0] = &ev[4][0];
        ev[3][1] = &ev[0][2];
        ev[3][2] = &ev[8][1];
        ev[3][3] = &ev[0][6];

        ev[4][0] = &ev[5][0];
        ev[4][1] = &ev[1][2];
        ev[4][2] = &ev[5][2];

        ev[5][0] = &ev[0][1];
        ev[5][1] = &ev[7][1];
        ev[5][2] = &ev[2][5];

        ev[6][0] = &ev[3][1];
        ev[6][1] = &ev[2][3];
        ev[6][2] = &ev[3][3];

        ev[7][0] = &ev[4][1];
        ev[7][1] = &ev[0][4];
        ev[7][2] = &ev[4][1];

        ev[8][0] = &ev[6][1];
        ev[8][1] = &ev[1][4];

        //printf("Buffer setup for set %3ld\n", set);
        //void **p = (void **)calc_probe(set, 1);
        //for (int i = 0; i < NJ_SETSZ + NJ_INIT + 4*6; i++) {
            //printf("%p %2d -> %2d\n", p, WAYX(p)-13, WAYX(*p)-13);
            //p = *p;
        //}
    }
}

void ninja_sync(int lo, int hi)
{
    for (int set = lo; set < hi; set++) {
        uint64_t probe = calc_probe(set, 1);
        uint32_t dt;
        ipchase(probe, NJ_INIT, dt);
        njcur[set] = probe;
    }
}




// receiver function: read a 'wordlen'-size word from tlb sets
uint64_t readword(int r_ready_set, int t_ready_set, int data_set, int wordlen)
{
        int set;
        long maxloops = 10000000;
        int ix = 0;
		assert((wordlen % 8) == 0);
        uint16_t rn = 0xace1;

        // tell transmitter that receiver is ready: assert r_ready_set
        //getset(r_ready_set);
        //getset(r_ready_set);
        //putset(r_ready_set);
        //mputset(r_ready_set);

        // wait for transmitter to have sent valid data: poll t_ready_set rising edge
        // with maxloops timeout
        //while(getset(t_ready_set) == 0 && maxloops-- > 0) ;   /* check sync set */
        //while(getset(t_ready_set) == 1 && maxloops-- > 0) ;   /* empty sync set */
        //while(getset(t_ready_set) == 0) ;   /* check sync set */
        //while(getset(t_ready_set) == 1) ;   /* empty sync set */

        // timeout, return invalid word
        //if(maxloops <= 0) return 0; /* zero is never a valid word in our protocol */

        /* collect actual bits: transmit 'wordlen' bits in parallel (one bit per tlb set) */
        uint64_t word = 0;
        for(set = data_set; set < data_set+wordlen; set++) {
            word <<= 1;
            word |= getset(set);
    	}

        uint64_t b0, b1, b2, b3, crcval = 0;
        // seperate message into bytes, which also seperates the crc from the message.
        // so we can easily compute the crc of the bytes and discard the crc.
        b0 = (word >>  0) & 0xff;
        b1 = (word >>  8) & 0xff;
        b2 = (word >> 16) & 0xff;
        b3 = (word >> 24) & 0xff;

        // compute and check crc, return 0 if invalid crc, otherwise return data bits without crc
        assert(wordlen == 24 || wordlen == 32);
        if(wordlen == 24) {
              crcval = crc8_table[crcval ^ b0];
              crcval = crc8_table[crcval ^ b1];
              if(crcval != b2) { return 0; } // invalid crc; return no word
              return word & 0xffff;
        }
        if(wordlen == 32) {
              crcval = crc8_table[crcval ^ b0];
              crcval = crc8_table[crcval ^ b1];
              crcval = crc8_table[crcval ^ b2];
              if(crcval != b3) return 0; // invalid crc; return no word
              return word & 0xffffff;
        }
        return 0;
}

// transmitter function: write a 'wordlen'-size word to tlb sets
void writeword(int r_ready_set, int t_ready_set, int data_set, int wordlen, uint64_t sendword)
{
            int setix = 0;
            int set;
            assert((sendword >> (wordlen-8)) == 0);

            // seperate data word into bytes, compute crc over bytes
            uint64_t b0, b1, b2, b3;
            uint64_t crcval = 0;
            b0 = (sendword >>  0) & 0xff;
            b1 = (sendword >>  8) & 0xff;
            b2 = (sendword >> 16) & 0xff;

            // compute crc
            assert(wordlen == 24 || wordlen == 32);
            if(wordlen == 24) {
                  crcval = crc8_table[crcval ^ b0];
                  crcval = crc8_table[crcval ^ b1];
                  sendword |= (crcval << 16);
            } else if(wordlen == 32) {
                  crcval = crc8_table[crcval ^ b0];
                  crcval = crc8_table[crcval ^ b1];
                  crcval = crc8_table[crcval ^ b2];
                  sendword |= (crcval << 24);
            } else {
                   assert(0);
            }
            assert((sendword >> (wordlen)) == 0);
            assert(sendword != 0);

            // transmit data bits + crc

            //while(getset(r_ready_set) == 0) ; // wait for receiver to be ready: poll r_ready_set rising edge
            //while(getset(r_ready_set) == 1) ;
            for (int i = 0; i < MTH; i++) {
                setix = 0;
                for(set = data_set; set < data_set+wordlen; set++) {
                       //if((sendword >> (wordlen-1-setix)) & 1) { getset(set); }
                       if((sendword >> (wordlen-1-setix)) & 1) { putset(set); }
                       setix++;
                }
            }
            //getset(t_ready_set); // tell receiver that data is valid: assert t_ready_set
            //getset(t_ready_set);
            //mputset(t_ready_set);
}

int main(int argc, char *argv[])
{
    uint64_t safeset = SET_SAFE;

    int clamps = 0, time;

    if(argc != 2) {
        fprintf(stderr, "usage: %s <core>\n", argv[0]);
        exit(1);
    }

    int mycpu = atoi(argv[1]);
    pin_cpu(mycpu);

	unsigned long long set;
    int fd[SETLIMIT];
    for(set = 0; set < SETLIMIT; set++) {
        fd[set] = createfile(SHAREFILE, set);
		assert(set < 512);
    }

    allocate_buffer(fd, (volatile char *) VTARGET);
    //setup_tbuf();
    //setup_buffer();
    setup_buffer_ninja();
    njcur = (uint64_t *)mmap_safe(100);
    ninja_sync(0, SETLIMIT);

#define RESETMESSAGE "start receiver first.\nif processes are out of sync, and you want to be sure to resync them,\nkill them all and delete /tmp/.tlb*, then start receiver first, then sender.\nbut just restarting first receiver, then sender, should also work.\n"

     time=1;

     printf("receiver: sanity check mode\nyou should see a 'power level indicator' (between 0 and 100).\n"
             "that goes up when the sender prints 'set ON' and down when the sender prints 'set OFF'.\n"
             "this tests basic bit-level communicating using tlb set latencies and no framing, crc, etc.\n"
             "if that doesn't work, that should be debugged first.\n");

     for(int saneset = 0; saneset < 1; saneset++) {
         //ninja_sync(SET_DATA, SET_DATA+WORDLEN);
         //assert(saneset >= 0);
         //assert(saneset < HARDWARESETS);
         printf("set ON\n");
         for (int i = 0; i < 5000; i++) {
             ninja_sync(SET_DATA, SET_DATA+WORDLEN);
             int count = 0;
             int n = 0;
             for(n = 0; n < 100; n++) {
                 for (int sset = SET_DATA; sset < SET_DATA+WORDLEN; sset += 2) {
                     mputset(sset);
                 }
                 for (int sset = SET_DATA; sset < SET_DATA+WORDLEN; sset++) {
                     count += getset(sset);
                 }
            }
            count /= WORDLEN;
             printf("\r%*s*%*s%4d\r",count, "", 100-count, "", count);
             fflush(stdout);
         }
         printf("set OFF\n");
         for (int i = 0; i < 5000; i++) {
             ninja_sync(SET_DATA, SET_DATA+WORDLEN);
             int count = 0;
             int n = 0;
             for(n = 0; n < 100; n++) {
                 for (int sset = SET_DATA; sset < SET_DATA+WORDLEN; sset++) {
                     count += getset(sset);
                 }
            }
            count /= WORDLEN;
             printf("\r%*s*%*s%4d\r",count, "", 100-count, "", count);
             fflush(stdout);
         }
     }
     //exit(0);
     printf("\n");
     printf("receiver: sanity check done; doing real covert channel test.\n");

     int receive_errors = 0;

     struct timeval tv1, tv2;
     gettimeofday(&tv1, NULL);
     int goodwords=0;
     //for(;;) {
     ninja_sync(SET_DATA, SET_DATA+WORDLEN);
     for (long att = 0; time <= 100000; att++) {

    #ifdef USE_NINJA
         if (!(att & NINJA_MASK)) {
             ninja_sync(SET_DATA, SET_DATA+WORDLEN);
         }
    #endif

        uint64_t word;
        uint64_t requestword = time;
        // receiver: write a request, wait for reply.
        //
        // request: ask for word number 'time'
        //writeword(SET_SYNC_R_READY_REQ, SET_SYNC_T_READY_REQ, SET_REQ, REQLEN, time);

        //if((requestword = readword(SET_SYNC_R_READY_REQ, SET_SYNC_T_READY_REQ, SET_REQ, REQLEN)) != 0) {
            uint64_t responseword = 1;
            responseword |= (requestword & 0x7f) << 1;
            responseword |= ((requestword) & 0xffff) << 8;
            //printf("%6x: %8lx\n", time, requestword);
            // send response: repsonseword
            //readword(SET_SYNC_R_READY_DATA, SET_SYNC_T_READY_DATA, SET_DATA, WORDLEN);
            //readword(SET_SYNC_R_READY_DATA, SET_SYNC_T_READY_DATA, SET_DATA, WORDLEN);
            //readword(SET_SYNC_R_READY_DATA, SET_SYNC_T_READY_DATA, SET_DATA, WORDLEN);

            writeword(SET_SYNC_R_READY_DATA, SET_SYNC_T_READY_DATA, SET_DATA, WORDLEN, responseword);
            //time++;
        //}

        // read reply using readword() into word. return 0 if invalid data received or timeout.
        // if 0 is received, time does not tick and we simply ask for the same word again next time.
        if((word = readword(SET_SYNC_R_READY_DATA, SET_SYNC_T_READY_DATA, SET_DATA, WORDLEN)) != 0) {
            uint64_t writetime = time;
            uint64_t frameno = (word >> 1) & 0x7f;
            uint64_t dataword = (word >> 8) & 0xffff;
            uint64_t expect_dataword = time & 0xffff;
            writetime = (writetime & 0xffff80) | frameno;
            // if a valid frame was received, we check we are in sync. if we are not in sync, we ask for the
            // same frame again.
            if((time & 0x7f) == frameno) {
                    // if we are in sync, everything went OK and we ask for the next frame.
                    time++;
                    // we check if the data word is correct - unlikely to be wrong (because the crc passed)
                    // but of course the crc can accidentally be right if there's enough noise often enough.
                    // We have out-of-band knowledge of the right data so in this experiment we also know what
                    // the undetected error rate would be. receive_errors are undetected errors.
                    // timeouts or crc errors are not called receive errors because a framing protocol will detect them
                    // and not cause any problems.
                    if(dataword != expect_dataword) { fprintf(stderr, "for time 0x%x, expecting dataword 0x%lx, saw 0x%lx, \n", time, expect_dataword, dataword); receive_errors++; }
                    else goodwords++;
            //} else {
                //printf("w%5x: %8lx\n", time, word);
            }
        }

    }

    gettimeofday(&tv2, NULL);

    uint64_t usecs1 = tv1.tv_sec*1000000ULL + tv1.tv_usec;
    uint64_t usecs2 = tv2.tv_sec*1000000ULL + tv2.tv_usec;

    double dtf = (double)usecs2/1000000.0-(double)usecs1/1000000.0;

    printf("receiver detects that sender has exited.\n");
    printf("elapsed: %lf bytespersecond: %lf kbit: %lf ", dtf, goodwords*3/dtf, ((goodwords*3/dtf*8))/1000);
    printf("undetected errors %d correctly received frames %d\n", receive_errors, goodwords);

    return 0;
}

