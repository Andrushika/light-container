#define _GNU_SOURCE
#include "container.h"
#include "log.h"
#include "userns.h"
#include "secure.h"
#include "mountns.h"
#include "cgroups.h"

#include <signal.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <net/if.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

// A new network namespace starts with lo down; slirp4netns only sets up tap0.
static int bring_up_loopback(void) {
    struct ifreq ifr = {.ifr_name = "lo"};
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || ioctl(fd, SIOCGIFFLAGS, &ifr) < 0 ||
        (ifr.ifr_flags |= IFF_UP, ioctl(fd, SIOCSIFFLAGS, &ifr) < 0)) {
        log_error("Failed to bring up lo: %m");
        if (fd >= 0) {
            close(fd);
        }
        return -1;
    }
    close(fd);
    return 0;
}

int container_start(void *arg) {
    container_config *config = (container_config *)arg;

    // Die with light-container, and see EOF if it dies before releasing us.
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    close(config->parent_socket_fd);

    int ready = 0;
    if (read(config->socket_fd, &ready, sizeof(ready)) != sizeof(ready) || !ready) {
        log_error("Host side setup failed, exiting");
        return -1;
    }

    // Unshare only now that the parent has moved us into the container's
    // cgroup, so the namespace is rooted there.
    if (unshare(CLONE_NEWCGROUP) < 0) {
        log_error("Failed to create cgroup namespace: %m");
        return -1;
    }

    log_debug("Setting hostname to %s", config->hostname);
    if (sethostname(config->hostname, strlen(config->hostname)) < 0) {
        log_error("Failed to set hostname: %m");
        return -1;
    }

    if (bring_up_loopback() < 0) {
        return -1;
    }

    log_debug("Mounting directory '%s' into container...", config->mount_dir);
    if (mount_dir_into_container(config->mount_dir, config->mount_tmp_dir) < 0) {
        log_error("Failed to mount directory into container");
        return -1;
    }

    log_debug("preparing capabilities...");
    if(prepare_capabilities() < 0) {
        log_error("Failed to prepare capabilities: %m");
        return -1;
    }

    log_debug("preparing syscall limitations...");
    if(prepare_syscall_limitations() < 0) {
        log_error("Failed to prepare syscall limitations: %m");
        return -1;
    }

    // Last, because dropping root clears the capabilities the two steps above
    // need (CAP_SETPCAP for the bounding set, CAP_SYS_ADMIN for seccomp).
    log_debug("Setting UID to %d", config->uid);
    if (userns_init(config->uid) < 0) {
        return -1;
    }

    if(config->argv && config->argv[0]) {
        execvp(config->argv[0], config->argv);
        log_error("Failed to execvp command '%s': %m", config->argv[0]);
        return errno == ENOENT ? 127 : 126; // the shell's convention
    }
    log_error("No command provided to execute in the container");
    return -1;
}

int container_init(container_config *config, char *stack) {
    int container_pid = 0;

    int flags = CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWIPC |
                CLONE_NEWNET | CLONE_NEWUTS | CLONE_NEWUSER | SIGCHLD;

    log_debug("cloning process...");
    if ((container_pid =
            clone(container_start, stack, flags, config)) == -1) {
        log_error("failed to clone: %m");
        return -1;
    }
    log_info("Container process started with PID %d", container_pid);
    return container_pid;
}

int container_release(int socket_fd, int ready) {
    if (send(socket_fd, &ready, sizeof(ready), MSG_NOSIGNAL) != sizeof(ready)) {
        log_error("Failed to write to socket: %m");
        return -1;
    }
    return 0;
}

int container_wait(pid_t container_pid) {
    int status;
    pid_t ret;
    while ((ret = waitpid(container_pid, &status, 0)) == -1 && errno == EINTR) {
    }
    if (ret == -1) {
        log_error("waitpid failed: %m");
        return -1;
    }
    if (WIFSIGNALED(status)) {
        log_info("Container killed by signal %d", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    log_info("Container exited with status %d", WEXITSTATUS(status));
    return WEXITSTATUS(status);
}
