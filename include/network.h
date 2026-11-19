#ifndef NETWORK_H
#define NETWORK_H

#include <sys/types.h>

pid_t network_start(pid_t container_pid, int quiet);
void network_stop(pid_t pid);

#endif
