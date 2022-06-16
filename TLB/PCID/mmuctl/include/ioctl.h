#pragma once

#define MMUCTL_MAGIC_NUM 0x1337

#define MMUCTL_GET_PAGE_SIZE \
	_IOR(MMUCTL_MAGIC_NUM, 0, unsigned long)
#define MMUCTL_LOCK \
	_IOR(MMUCTL_MAGIC_NUM, 1, unsigned long)
#define MMUCTL_UNLOCK \
	_IOR(MMUCTL_MAGIC_NUM, 2, unsigned long)
#define MMUCTL_GET_ROOT \
	_IOR(MMUCTL_MAGIC_NUM, 3, unsigned long)
#define MMUCTL_SET_ROOT \
	_IOR(MMUCTL_MAGIC_NUM, 4, unsigned long)
#define MMUCTL_RESOLVE \
	_IOR(MMUCTL_MAGIC_NUM, 5, unsigned long)
#define MMUCTL_UPDATE \
	_IOR(MMUCTL_MAGIC_NUM, 6, unsigned long)
#define MMUCTL_INVLPG \
	_IOR(MMUCTL_MAGIC_NUM, 7, unsigned long)
#define MMUCTL_LOCAL_INVLPG \
	_IOR(MMUCTL_MAGIC_NUM, 8, unsigned long)
#define MMUCTL_GET_PAT \
	_IOR(MMUCTL_MAGIC_NUM, 9, unsigned long)
#define MMUCTL_SET_PAT \
	_IOR(MMUCTL_MAGIC_NUM, 10, unsigned long)
#define MMUCTL_TOUCH \
	_IOR(MMUCTL_MAGIC_NUM, 11, unsigned long)
