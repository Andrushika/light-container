#ifndef CONTAINER_H
#define CONTAINER_H
#include <sys/types.h>

#define CONTAINER_STACK_SIZE (1024 * 1024) // 1 MB stack size

typedef struct {
  uid_t uid;
  char *hostname;
  char **argv;
  const char *mount_dir;
  int socket_fd;
  int parent_socket_fd; // the parent's end, closed in the child
  char *mount_tmp_dir;
} container_config;

int container_init(container_config *config, char *stack);

// Tell the cloned process whether the host side (ID maps, cgroup, network) is
// ready, so it may continue or must exit.
int container_release(int socket_fd, int ready);

// Returns the container's exit status (128 + signal if it was killed), or -1.
int container_wait(pid_t container_pid);
#endif
