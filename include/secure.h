#ifndef SECURE_H
#define SECURE_H

#include <errno.h>

#define SEC_SCMP_FAIL SCMP_ACT_ERRNO(EPERM)

int prepare_capabilities();
int prepare_syscall_limitations();
#endif

