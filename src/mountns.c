#define _GNU_SOURCE
#include "log.h"
#include <sys/mount.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h> 

int mount_dir_into_container(const char *mount_dir, char *tmp_dir){

    // MS_REC: recursively apply the mount flags to all submounts
    // MS_PRIVATE: make the mount private, so it won't propagate to other mount namespaces
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
        log_error("failed to mount /: %m");
        return -1;
    }
    
    // bind new root
    if (mount(mount_dir, tmp_dir, NULL, MS_BIND | MS_PRIVATE, NULL)){
        log_error("Failed to bind mount %s to %s: %m", mount_dir, tmp_dir);
        return -1;
    }

    char oldroot[128];
    snprintf(oldroot, sizeof(oldroot), "%s/oldroot.XXXXXX", tmp_dir);
    if (!mkdtemp(oldroot)){
        log_error("Failed to create temporary directory for old root: %m");
        return -1;
    }

    // // pivot root to the new root
    if (pivot_root(tmp_dir, oldroot)){
        log_error("Failed to pivot root: %m");
        return -1;
    }

    // drop the old root, preventing it from being used
    char oldroot_base[128];
    snprintf(oldroot_base, sizeof(oldroot_base), "/%s", basename(strdup(oldroot)));

    if (chdir("/") < 0) {
        log_error("Failed to change directory to new root: %m");
        return -1;
    }

    if (umount2(oldroot_base, MNT_DETACH) < 0) {
        log_error("Failed to unmount old root: %m");
        return -1;
    }
    if (rmdir(oldroot_base) < 0) {
        log_error("Failed to remove old root directory: %m");
        return -1;
    }
    
    return 0;
}
