#include <sys/mount.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"

static char saved_cgroup_path[256] = {0};

static int write_to_file(const char *path, const char *value) {
    FILE *fd = fopen(path, "w");
    if (fd == NULL) {
        log_error("Failed to open file %s: %m", path);
        return -1;
    }
    fwrite(value, sizeof(value), 1, fd);
    fclose(fd);
    return 0;
}

int setup_cgroups(const char *container_name, int pid) {
    char cgroup_path[256];
    char container_cgroup[256];
    
    snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup/light-container");

    snprintf(container_cgroup, sizeof(container_cgroup), 
             "%s/%s-%d", cgroup_path, container_name, pid);
    
    if (mkdir(container_cgroup, 0755) < 0) {
        log_error("Failed to create container cgroup: %m");
        return -1;
    }
    
    char path[512];
    
    snprintf(path, sizeof(path), "%s/memory.max", container_cgroup);
    if (write_to_file(path, "1073741824") < 0) {
        log_warn("Failed to set memory limit: %m");
    }
    
    snprintf(path, sizeof(path), "%s/cpu.weight", container_cgroup);
    if (write_to_file(path, "256") < 0) {
        log_warn("Failed to set CPU weight: %m");
    }
    
    snprintf(path, sizeof(path), "%s/pids.max", container_cgroup);
    if (write_to_file(path, "64") < 0) {
        log_warn("Failed to set process limit: %m");
    }
    
    snprintf(path, sizeof(path), "%s/cgroup.procs", container_cgroup);
    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);
    if (write_to_file(path, pid_str) < 0) {
        log_error("Failed to add process to cgroup: %m");
        return -1;
    }
    
    // Save the cgroup path for cleanup later
    strncpy(saved_cgroup_path, container_cgroup, sizeof(saved_cgroup_path) - 1);
    saved_cgroup_path[sizeof(saved_cgroup_path) - 1] = '\0';

    return 0;
}

const char* get_saved_cgroup_path() {
    return saved_cgroup_path[0] ? saved_cgroup_path : NULL;
}

void cleanup_cgroups() {
    char *cgroup_path = get_saved_cgroup_path();
    if (!cgroup_path || !cgroup_path[0]) {
        log_warn("No cgroup path saved, nothing to clean up");
        return;
    }
    
    if(rmdir(cgroup_path) < 0) {
        log_error("Failed to remove cgroup %s: %m", cgroup_path);
    } else {
        log_info("Removed cgroup %s", cgroup_path);
    }
}