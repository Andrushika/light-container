#ifndef CONTAINER_H
#define CONTAINER_H

#include <sys/types.h>
#include <signal.h>
#include <sched.h>
#include <stdlib.h>

#define CONTAINER_STACK_SIZE (1024 * 1024) // 1 MB stack size

typedef struct {
  uid_t uid;
  char *hostname;
  char *cmd;
  char *mount_dir;
} container_config;

int container_init(container_config *config, char *stack);
#endif