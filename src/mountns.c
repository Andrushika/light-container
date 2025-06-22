#include "log.h"
#include <sys/mount.h>
#include <unistd.h>
#include <sys/stat.h>

int mount_dir_into_container(const char *mount_dir)
{
    // isolate mount namespace, make whole mount tree private
    // MS_REC: recursively apply the mount flags to all submounts
    // MS_PRIVATE: make the mount private, so it won't propagate to other mount namespaces
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)){
        log_error("Failed to make mount namespace private: %m");
        return -1;
    }

    // make sure the /mnt is empty
    if (mount("tmpfs", "/mnt", "tmpfs", 0, "size=16m")){
        log_error("Failed to mount tmpfs at /mnt: %m");
        return -1;
    }

    // bind new root
    if (mount(mount_dir, "/mnt", NULL, MS_BIND, NULL)){
        log_error("Failed to bind mount %s to /mnt: %m", mount_dir);
        return -1;
    }


    // prepare tje dir for old root
    if (mkdir("/mnt/oldroot", 0755)){
        log_error("Failed to create oldroot directory: %m");
        return -1;
    }

    // pivot root to /mnt
    if (pivot_root("/mnt", "/mnt/oldroot")){
        log_error("Failed to pivot root to /mnt: %m");
        return -1;
    }

    // drop the old root, preventing it from being used
    chdir("/");
    umount2("/oldroot", MNT_DETACH);
    rmdir("/oldroot");
    return 0;
}
