#ifndef USERNS_H
#define USERNS_H

#include <sys/types.h>

int write_id_maps(pid_t container_pid);
int userns_init(uid_t uid);

#endif
