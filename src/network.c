#define _GNU_SOURCE
#include "network.h"
#include "log.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

// Give the container's network namespace a tap0 device backed by slirp4netns,
// a user-mode network stack that needs no privileges. Returns its PID once the
// interface is configured.
pid_t network_start(pid_t container_pid, int quiet) {
    int ready[2];
    if (pipe2(ready, O_CLOEXEC) < 0) {
        log_warn("pipe failed: %m");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_warn("fork failed: %m");
        close(ready[0]);
        close(ready[1]);
        return -1;
    }
    if (pid == 0) {
        char pid_s[16], fd_s[16];
        snprintf(pid_s, sizeof(pid_s), "%d", container_pid);
        snprintf(fd_s, sizeof(fd_s), "%d", ready[1]);
        fcntl(ready[1], F_SETFD, 0);
        if (quiet) {
            int devnull = open("/dev/null", O_WRONLY);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }
        execlp("slirp4netns", "slirp4netns", "--configure", "--mtu=65520",
               "--disable-host-loopback", "--ready-fd", fd_s, pid_s, "tap0", NULL);
        _exit(127);
    }

    close(ready[1]);
    char byte;
    ssize_t n = read(ready[0], &byte, 1);
    close(ready[0]);
    if (n != 1) {
        waitpid(pid, NULL, 0);
        log_warn("slirp4netns failed or is not installed; the container has no network");
        return -1;
    }
    return pid;
}

void network_stop(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
    }
}
