#ifndef MOUNTNS_H
#define MOUNTNS_H

#include <sys/syscall.h> 

#define pivot_root(newroot, oldroot) syscall(SYS_pivot_root, newroot, oldroot)
int mount_dir_into_container(const char *mount_dir);

#endif