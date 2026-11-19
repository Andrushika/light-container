#define _GNU_SOURCE
#include "userns.h"
#include "log.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Container IDs 1..USERNS_MAX_SUBIDS come from the user's /etc/subuid range.
#define USERNS_MAX_SUBIDS 65535UL

// Find the subordinate ID range ("name_or_id:start:count") for the caller.
static int read_subid_range(const char *file, unsigned long id,
                            unsigned long *start, unsigned long *count) {
    FILE *f = fopen(file, "re");
    if (!f) {
        return -1;
    }
    struct passwd *pw = getpwuid(getuid());
    char owner[256];
    int found = -1;
    while (fscanf(f, " %255[^:\n]:%lu:%lu", owner, start, count) == 3) {
        char *end;
        unsigned long owner_id = strtoul(owner, &end, 10);
        if ((pw && strcmp(owner, pw->pw_name) == 0) ||
            (*end == '\0' && owner_id == id)) {
            found = 0;
            break;
        }
    }
    fclose(f);
    return found;
}

// Map container root to the calling user, so the container can use files the
// user owns (the rootfs), and IDs 1.. to the user's subordinate range.
// /etc/subuid and /etc/subgid are both keyed by user name or UID.
static int write_id_map(const char *helper, const char *subid_file, uid_t owner,
                        unsigned long own_id, pid_t container_pid) {
    char pid_s[16], own_s[16], start_s[24], count_s[24];
    snprintf(pid_s, sizeof(pid_s), "%d", container_pid);
    snprintf(own_s, sizeof(own_s), "%lu", own_id);
    const char *argv[] = {helper, pid_s, "0", own_s, "1", NULL, NULL, NULL, NULL};

    unsigned long start, count;
    if (read_subid_range(subid_file, owner, &start, &count) == 0) {
        snprintf(start_s, sizeof(start_s), "%lu", start);
        snprintf(count_s, sizeof(count_s), "%lu",
                 count < USERNS_MAX_SUBIDS ? count : USERNS_MAX_SUBIDS);
        argv[5] = "1";
        argv[6] = start_s;
        argv[7] = count_s;
    } else {
        log_warn("No entry for you in %s; only ID 0 exists in the container", subid_file);
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_error("fork failed: %m");
        return -1;
    }
    if (pid == 0) {
        execvp(helper, (char **)argv);
        log_error("Failed to exec %s: %m", helper);
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        log_error("waitpid failed for %s: %m", helper);
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        log_error("%s failed for PID %d", helper, container_pid);
        return -1;
    }
    return 0;
}

int write_id_maps(pid_t container_pid) {
    if (write_id_map("newuidmap", "/etc/subuid", getuid(), getuid(), container_pid) < 0 ||
        write_id_map("newgidmap", "/etc/subgid", getuid(), getgid(), container_pid) < 0) {
        return -1;
    }
    log_debug("User namespace ID mapping written for PID %d", container_pid);
    return 0;
}

int userns_init(uid_t uid) {
    // newgidmap denies setgroups when it maps only the caller's own GID (no
    // /etc/subgid entry); the container then keeps its single group.
    if ((setgroups(1, &(gid_t){uid}) && errno != EPERM) || setresgid(uid, uid, uid) ||
        setresuid(uid, uid, uid)) {
        log_error("failed to set uid %d / gid %d: %m", uid, uid);
        return -1;
    }
    return 0;
}
