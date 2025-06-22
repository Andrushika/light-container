#ifndef CONTAINER_H
#define CONTAINER_H
#include <sys/types.h>

#define CONTAINER_STACK_SIZE (1024 * 1024) // 1 MB stack size

typedef struct {
  uid_t uid;
  char *hostname;
  char *cmd;
  char *mount_dir;
  int socket_fd;
} container_config;

int container_init(container_config *config, char *stack);

int container_wait(int container_pid);
#endif