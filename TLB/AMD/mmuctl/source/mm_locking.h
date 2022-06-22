#ifndef MMLOCKING_H_INCLUDED
#define MMLOCKING_H_INCLUDED

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
#define TLBDR_MMLOCK (&current->mm->mmap_lock)
#else
#define TLBDR_MMLOCK (&current->mm->mmap_sem)
#endif

#endif