#define _GNU_SOURCE 1
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <ioctl.h>
#include <mmuctl.h>

int
mmuctl_init(struct mmuctl *ctx)
{
	ctx->fd = open("/dev/mmuctl", O_RDONLY);

	if (ctx->fd < 0)
		return -1;

	return 0;
}

void
mmuctl_cleanup(struct mmuctl *ctx)
{
	if (!ctx)
		return;

	if (ctx->fd >= 0) {
		close(ctx->fd);
		ctx->fd = -1;
	}
}

size_t
mmuctl_get_page_size(struct mmuctl *ctx)
{
	return (size_t)ioctl(ctx->fd, MMUCTL_GET_PAGE_SIZE, 0);
}

int
mmuctl_lock(struct mmuctl *ctx)
{
	return ioctl(ctx->fd, MMUCTL_LOCK, 0);
}

int
mmuctl_unlock(struct mmuctl *ctx)
{
	return ioctl(ctx->fd, MMUCTL_UNLOCK, 0);
}

ssize_t
mmuctl_read(struct mmuctl *ctx, void *buf, size_t len, uint64_t addr)
{
	if (!ctx)
		return -EINVAL;

	return pread(ctx->fd, buf, len, addr);
}

ssize_t
mmuctl_write(struct mmuctl *ctx, const void *buf, size_t len, uint64_t addr)
{
	if (!ctx)
		return -EINVAL;

	return pwrite(ctx->fd, buf, len, addr);
}

uint64_t
mmuctl_get_root(struct mmuctl *ctx, pid_t pid)
{
	struct mmuctl_root root;

	root.pid = pid;

	ioctl(ctx->fd, MMUCTL_GET_ROOT, (unsigned long)&root);

	return root.root;
}

int
mmuctl_set_root(struct mmuctl *ctx, pid_t pid, uint64_t pa)
{
	struct mmuctl_root root;

	root.pid = pid;
	root.root = pa;

	return ioctl(ctx->fd, MMUCTL_GET_ROOT, (unsigned long)&root);
}

int
mmuctl_resolve(struct mmuctl *ctx, struct mmuctl_ptwalk *ptwalk, void *va,
	pid_t pid)
{
	if (!ctx || !ptwalk)
		return -EINVAL;

	ptwalk->va = (unsigned long)va;
	ptwalk->pid = pid;

	return ioctl(ctx->fd, MMUCTL_RESOLVE, (unsigned long)ptwalk);
}

int
mmuctl_update(struct mmuctl *ctx, struct mmuctl_ptwalk *ptwalk, void *va,
	pid_t pid)
{
	if (!ctx || !ptwalk)
		return -EINVAL;

	ptwalk->va = (unsigned long)va;
	ptwalk->pid = pid;

	return ioctl(ctx->fd, MMUCTL_UPDATE, (unsigned long)ptwalk);
}

int
mmuctl_invlpg(struct mmuctl *ctx, void *addr)
{
	return ioctl(ctx->fd, MMUCTL_INVLPG, (unsigned long)addr);
}

int
mmuctl_local_invlpg(struct mmuctl *ctx, void *addr)
{
	return ioctl(ctx->fd, MMUCTL_LOCAL_INVLPG, (unsigned long)addr);
}

unsigned long
mmuctl_get_pat(struct mmuctl *ctx)
{
	return ioctl(ctx->fd, MMUCTL_GET_PAT, 0);
}

int mmuctl_set_pat(struct mmuctl *ctx, unsigned long pat)
{
	return ioctl(ctx->fd, MMUCTL_SET_PAT, pat);
}

unsigned char
mmuctl_find_mem_type(struct mmuctl *ctx, unsigned char mem_type)
{
	unsigned long pat = mmuctl_get_pat(ctx);
	unsigned char found = 0;
	size_t i;

	for (i = 0; i < 8; ++i) {
#if defined(__i386__) || defined(__x86_64__)
		if (((pat >> (i * 8)) & 7) == mem_type)
			found |= (1 << i);
#elif defined(__aarch64__)
		if (((pat >> (i * 8)) & 0xff) == mem_type) {
			found |= (1 << i);
		} else {
			unsigned char lo, hi;

			lo = (mts >> (i * 8)) & 0xf;
			hi = (mts >> (i * 8 + 4)) & 0xf;

			if (lo == hi && lo == mem_type) {
				found |= (1 << i);
			}
		}
#endif
	}

	return found;
}

unsigned char
mmuctl_get_mem_type(unsigned long entry)
{
	unsigned char mem_type = 0;
#if defined(__i386__) || defined(__x86_64__)

	if (entry & PAGE_WRITE_THROUGH)
		mem_type |= 1;

	if (entry & PAGE_NO_CACHE)
		mem_type |= 2;

	if (entry & PAGE_PAT)
		mem_type |= 4;
#elif defined(__aarch64__)
	mem_type = PAGE_MAIR(entry);
#endif

	return mem_type;
}

unsigned long
mmuctl_set_mem_type(unsigned long entry, unsigned char mem_type)
{
#if defined(__i386__) || defined(__x86_64__)
	entry &= ~(PAGE_WRITE_THROUGH | PAGE_NO_CACHE | PAGE_PAT);

	if (mem_type & 1)
		entry |= PAGE_WRITE_THROUGH;

	if (mem_type & 2)
		entry |= PAGE_NO_CACHE;

	if (mem_type & 4)
		entry |= PAGE_PAT;
#elif defined(__aarch64__)
	entry &= ~0x1c;
	entry |= PAGE_SET_MAIR(entry, mem_type);
#endif

	return entry;
}

int
mmuctl_touch(struct mmuctl *ctx, void *addr, size_t len)
{
	struct mmuctl_buf buf;

	buf.addr = addr;
	buf.len = len;

	return ioctl(ctx->fd, MMUCTL_TOUCH, (unsigned long)&buf);
}

void
mmuctl_print_walk(struct mmuctl_ptwalk *walk)
{
	printf("va=%p -> (pgd=%016llx p4d=%016llx pud=%016llx pmd=%016llx pte=%016llx)\n",
		walk->va, walk->pgd, walk->p4d, walk->pud, walk->pmd, walk->pte);
}
