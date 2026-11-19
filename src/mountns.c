#define _GNU_SOURCE
#include "mountns.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <unistd.h>

// A rootless container cannot mknod, so these are bind-mounted from the host.
static const char *const host_devices[] = {"null", "zero", "full", "random", "urandom", "tty"};

static const struct {
    const char *target;
    const char *path;
} dev_links[] = {
    {"/proc/self/fd", "fd"},       {"/proc/self/fd/0", "stdin"},
    {"/proc/self/fd/1", "stdout"}, {"/proc/self/fd/2", "stderr"},
    {"pts/ptmx", "ptmx"},
};

static const struct {
    const char *path;
    const char *fstype;
    unsigned long flags;
    const char *data;
} fs_mounts[] = {
    {"/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL},
    {"/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME, "mode=755"},
    {"/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "newinstance,ptmxmode=0666,mode=0620"},
    {"/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=1777"},
};

static int mount_fs(const char *src, const char *dst, const char *fstype,
                    unsigned long flags, const char *data) {
    if (mount(src, dst, fstype, flags, data) < 0) {
        log_error("Failed to mount %s on %s: %m", fstype ? fstype : src, dst);
        return -1;
    }
    return 0;
}

static int setup_dev(const char *root) {
    char path[PATH_MAX], host[PATH_MAX];
    for (size_t i = 0; i < sizeof(host_devices) / sizeof(*host_devices); i++) {
        snprintf(path, sizeof(path), "%s/dev/%s", root, host_devices[i]);
        snprintf(host, sizeof(host), "/dev/%s", host_devices[i]);
        if (access(host, F_OK) < 0) {
            continue;
        }
        int fd = open(path, O_CREAT | O_WRONLY | O_CLOEXEC, 0666);
        if (fd < 0) {
            log_error("Failed to create %s: %m", path);
            return -1;
        }
        close(fd);
        if (mount_fs(host, path, NULL, MS_BIND, NULL) < 0) {
            return -1;
        }
    }
    for (size_t i = 0; i < sizeof(dev_links) / sizeof(*dev_links); i++) {
        snprintf(path, sizeof(path), "%s/dev/%s", root, dev_links[i].path);
        if (symlink(dev_links[i].target, path) < 0) {
            log_error("Failed to create %s: %m", path);
            return -1;
        }
    }
    return 0;
}

// Point DNS at slirp4netns's resolver. Runs after pivot_root, so symlinks in
// the rootfs resolve inside it. The file lands in the overlay's upper layer,
// so the rootfs keeps its own copy.
static void write_resolv_conf(void) {
    if (access("/etc", F_OK) < 0) {
        return;
    }
    unlink("/etc/resolv.conf"); // may be a symlink, e.g. to systemd-resolved's stub
    FILE *f = fopen("/etc/resolv.conf", "we");
    if (!f || fputs("nameserver 10.0.2.3\n", f) < 0 || fclose(f) != 0) {
        log_warn("Failed to write /etc/resolv.conf, DNS may not work: %m");
    }
}

// overlayfs splits lowerdir on ':' and options on ','; escape them.
static void escape_overlay_path(char *out, size_t size, const char *path) {
    size_t n = 0;
    for (; *path && n + 2 < size; path++) {
        if (*path == ':' || *path == ',' || *path == '\\') {
            out[n++] = '\\';
        }
        out[n++] = *path;
    }
    out[n] = '\0';
}

// Make an overlay with the rootfs as its read-only lower layer and a tmpfs
// upper layer, so the host rootfs is never modified and needs no write
// permission. Then pivot into it.
int mount_dir_into_container(const char *rootfs, const char *tmp_dir) {
    // MS_PRIVATE: keep every mount below from propagating to the host
    if (mount_fs(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0 ||
        mount_fs("tmpfs", tmp_dir, "tmpfs", 0, "mode=755") < 0) {
        return -1;
    }

    char upper[PATH_MAX], work[PATH_MAX], root[PATH_MAX], lower[2 * PATH_MAX];
    char opts[5 * PATH_MAX];
    snprintf(upper, sizeof(upper), "%s/upper", tmp_dir);
    snprintf(work, sizeof(work), "%s/work", tmp_dir);
    snprintf(root, sizeof(root), "%s/root", tmp_dir);
    escape_overlay_path(lower, sizeof(lower), rootfs);
    snprintf(opts, sizeof(opts), "lowerdir=%s,upperdir=%s,workdir=%s,userxattr",
             lower, upper, work);
    if (mkdir(upper, 0755) < 0 || mkdir(work, 0755) < 0 || mkdir(root, 0755) < 0) {
        log_error("Failed to create overlay directories in %s: %m", tmp_dir);
        return -1;
    }
    if (setxattr(upper, "user.light-container", "", 0, 0) < 0) {
        log_warn("tmpfs has no user xattrs (needs Linux 6.6): deleting or renaming a "
                 "rootfs directory inside the container will fail with EIO");
    }
    if (mount_fs("overlay", root, "overlay", 0, opts) < 0) {
        return -1;
    }

    char path[PATH_MAX];
    for (size_t i = 0; i < sizeof(fs_mounts) / sizeof(*fs_mounts); i++) {
        snprintf(path, sizeof(path), "%s%s", root, fs_mounts[i].path);
        if ((mkdir(path, 0755) < 0 && errno != EEXIST) ||
            mount_fs(fs_mounts[i].fstype, path, fs_mounts[i].fstype,
                     fs_mounts[i].flags, fs_mounts[i].data) < 0) {
            return -1;
        }
    }
    if (setup_dev(root) < 0) {
        return -1;
    }

    // pivot_root(".", ".") stacks the old root on top of the new one; detaching
    // it leaves only the container's root visible.
    if (chdir(root) < 0 || syscall(SYS_pivot_root, ".", ".") < 0 ||
        umount2(".", MNT_DETACH) < 0 || chdir("/") < 0) {
        log_error("Failed to pivot into %s: %m", root);
        return -1;
    }
    write_resolv_conf();
    return 0;
}
