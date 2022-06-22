#ifndef _ADDRESS_GEN_H_
#define _ADDRESS_GEN_H_

#include <linux/syscalls.h>
#include <linux/version.h>
#include "../../settings.h"
#include <helpers.h>

void get_address_set_stlb_lin(unsigned long addrs[], int stlb_target, int stlb_bits, int max);

#endif