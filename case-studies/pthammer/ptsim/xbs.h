#ifndef XBS_H
#define XBS_H 1

#include <stdint.h>

#if __x86_64__ || __i386__
#include <x86intrin.h>
#endif

static inline unsigned char xbs8(uint8_t x)
{
#if (__x86_64__ || __i386__) && !defined(NOASM)
	unsigned char r;
	__asm__ ("test %1, %1\n\t"
	         "setnp %0\n\t"
	         : "=b" (r) : "r" ((unsigned short)x) : "cc");
	return r;
#else
	uint8_t xx = x ^ (x >> 4);
	xx ^= xx >> 2;
	xx ^= xx >> 1;
	return xx & 1;
#endif
}

static inline unsigned char xbs32(uint32_t x)
{
#if (__x86_64__ || __i386__) && __POPCNT__
	return _popcnt32(x) & 1;
#else
	uint16_t xx = x ^ (x >> 16);
	return xbs8(xx ^ (xx >> 8));
#endif
}

static inline unsigned char xbs64(uint64_t x)
{
#if __x86_64__ && __POPCNT__
	return _popcnt64(x) & 1;
#else
	return xbs32((uint32_t)x) ^ xbs32((uint32_t)(x >> 32));
#endif
}

#endif /* xbs.h */
