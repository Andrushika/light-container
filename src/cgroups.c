#define _GNU_SOURCE
#include "cgroups.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CGROUP_ROOT "/sys/fs/cgroup"

static const struct {
    const char *enable; // written to the parent's cgroup.subtree_control
    const char *file;
    const char *value;
} limits[] = {
    {"+memory", "memory.max", "1073741824"},
    {"+pids", "pids.max", "64"},
    {"+cpu", "cpu.weight", "256"},
};

static char container_cgroup[PATH_MAX];

static int write_file(const char *dir, const char *file, const char *value) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, file);

    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    ssize_t len = strlen(value);
    ssize_t written = write(fd, value, len);
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return written == len ? 0 : -1;
}

static int own_cgroup(char *out, size_t size) {
    FILE *f = fopen("/proc/self/cgroup", "re");
    if (!f) {
        return -1;
    }
    char line[PATH_MAX];
    int found = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "0::", 3) == 0) {
            line[strcspn(line, "\n")] = '\0';
            snprintf(out, size, CGROUP_ROOT "%s", line + 3);
            found = 0;
            break;
        }
    }
    fclose(f);
    return found;
}

// An unprivileged user may only move processes between cgroups inside a
// subtree delegated to them, so the container's cgroup is created under the
// cgroup light-container itself runs in. On systemd hosts that cgroup comes
// from `systemd-run --user --scope -p Delegate=yes`.
int setup_cgroups(pid_t container_pid) {
    char base[PATH_MAX / 2]; // leaves room for the child cgroup names below
    if (own_cgroup(base, sizeof(base)) < 0) {
        log_warn("No cgroup v2 hierarchy found");
        return -1;
    }
    if (access(base, W_OK) < 0) {
        log_warn("Cgroup %s is not delegated to you. Run light-container under "
                 "`systemd-run --user --scope -p Delegate=yes` to apply limits",
                 base);
        return -1;
    }

    // "No internal processes": base can only enable controllers for its
    // children once it holds no processes, so first move ourselves and the
    // (still blocked) container out into leaves.
    char leaf[PATH_MAX], pid_str[16];
    snprintf(leaf, sizeof(leaf), "%s/runtime-%d", base, getpid());
    snprintf(container_cgroup, sizeof(container_cgroup), "%s/light-container-%d",
             base, container_pid);
    snprintf(pid_str, sizeof(pid_str), "%d", container_pid);
    if ((mkdir(leaf, 0755) < 0 && errno != EEXIST) ||
        write_file(leaf, "cgroup.procs", "0") < 0 ||
        mkdir(container_cgroup, 0755) < 0 ||
        write_file(container_cgroup, "cgroup.procs", pid_str) < 0) {
        log_warn("Failed to set up cgroups under %s: %m", base);
        return -1;
    }

    for (size_t i = 0; i < sizeof(limits) / sizeof(*limits); i++) {
        if (write_file(base, "cgroup.subtree_control", limits[i].enable) < 0) {
            log_warn("Controller %s is not available in %s, skipping %s: %m",
                     limits[i].enable + 1, base, limits[i].file);
        } else if (write_file(container_cgroup, limits[i].file, limits[i].value) < 0) {
            log_warn("Failed to set %s: %m", limits[i].file);
        }
    }
    log_debug("Container %d placed in cgroup %s", container_pid, container_cgroup);
    return 0;
}

void cleanup_cgroups(void) {
    if (!container_cgroup[0]) {
        return;
    }
    if (rmdir(container_cgroup) < 0) {
        log_warn("Failed to remove cgroup %s: %m", container_cgroup);
    }
}
