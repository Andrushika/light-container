#ifndef CGROUPS_H
#define CGROUPS_H

#include <sys/types.h>

int setup_cgroups(pid_t container_pid);
void cleanup_cgroups(void);

#endif
