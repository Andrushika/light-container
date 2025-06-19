#define _GNU_SOURCE
#include "container.h"
#include "log.h"
#include "userns.h"

int container_start(void *arg) {
    container_config *config = (container_config *)arg;
    log_debug("Setting UID to %d", config->uid);
    if (userns_init(config->uid, config->socket_fd) < 0) {
        log_error("Failed to set UID: %m");
        return 1;
    }

    if (config->cmd) {
        char *exec_args[] = {"/bin/sh", "-c", (char*)config->cmd, NULL};
        execve("/bin/sh", exec_args, NULL);
        log_error("failed to execvp: %m");
        return 1;
    }
    log_error("No command provided to execute in the container");
    return 1;
}

int container_init(container_config *config, char *stack) {
    int container_pid = 0;

    int flags = CLONE_NEWNS | CLONE_NEWCGROUP | CLONE_NEWPID | CLONE_NEWIPC |
                CLONE_NEWNET | CLONE_NEWUTS | CLONE_NEWUSER | SIGCHLD;

    log_debug("cloning process...");
    if ((container_pid =
            clone(container_start, stack, flags, config)) == -1) {
        log_error("failed to clone: %m");
        return -1;
    }

    return container_pid;
}

int container_wait(int container_pid) {
    int status;
    if (waitpid(container_pid, &status, 0) == -1) {
        log_error("waitpid failed: %m");
        return -1;
    }
    if (WIFEXITED(status)) {
        log_info("Container exited with status %d", WEXITSTATUS(status));
    } else {
        log_error("Container did not exit cleanly");
    }
    return 0;
}