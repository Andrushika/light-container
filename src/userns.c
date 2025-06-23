#define _GNU_SOURCE
#include "userns.h"
#include "log.h"
#include "container.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <grp.h>


int write_id_map_with_helper(int container_pid, int socket_fd) {
    const char *helper[] = {
        "newuidmap",
        "newgidmap",
        NULL
    };

    for(char **helper_bin = (char **)helper; *helper_bin; helper_bin++) {
        
        int helper_pid = fork();
        if (helper_pid < 0) {
            perror("fork");
            return -1;
        }
        if (helper_pid == 0) { 
            char container_pid_s[16];

            snprintf(container_pid_s, sizeof(container_pid_s), "%d", container_pid);

            execlp(*helper_bin, *helper_bin,
                container_pid_s, USERNS_MAP_ID_START, HOST_MAP_ID_START, USERNS_MAP_LEN, NULL);
            log_error("Failed to exec %s: %m", *helper_bin);
            _exit(127);
        }

        int status;
        if (waitpid(helper_pid, &status, 0) < 0) {
            log_error("waitpid failed for %s: %m", *helper_bin);
            return -1;
        }
        if (!WIFEXITED(status)) {
            log_error("%s did not exit cleanly", *helper_bin);
            return -1;
        }
    }

    if(write(socket_fd, &(int){0}, sizeof(int)) != sizeof(int)) {
        log_error("Failed to write to socket: %m");
        return -1;
    }

    log_debug("User namespace ID mapping written with helper for PID %d", container_pid);
    return 0;
}

int userns_init(int uid, int socket_fd) {
    int result = 0;
    if(read(socket_fd, &result, sizeof(result)) != sizeof(result)) {
        log_error("Failed to read from socket: %m");
        return -1;
    }

    if(result){
        return -1;
    }

    if (setgroups(1, &(gid_t){uid}) || setresgid(uid, uid, uid) ||
        setresuid(uid, uid, uid)) {
        log_error("failed to set uid %d / gid %d mappings: %m", uid, uid);
        return -1;
    }

    return 0;
}
