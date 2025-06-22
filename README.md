# light-container
light-container is a simplified rootless linux container runtime.

## Execute
```
make
./light-container -u 20 -m ~/rootfs-alpine -- /bin/ash -c "ls / && echo hello"
```
Please note that the UID is limited from 0 to 65535, and the command to execute in the container should be pass after `--`.

When execute, please make sure the directory open the +x permission for others (includes all the directory between to reach target directory), this is for the container mount under rootless.

Currently, the mount directory should open the read & write permissions to all the users. it is because the light-container run in rootless mode, and the container will have mkdir operations in the target directory (create the `old_root` dir for `pivot_root` operations). And also the operations that modified inode will cause the same effect on the host filesystem. (it's more like a "volume" in docker) 
However, this is not the best practice in container applications. The better solution is introduce overlayfs. Looking for your contribution!

## Prerequisite
* `newuidmap` / `newgidmap`
This project runs containers in rootless mode. However, we still need some special permission to write UID and GID mappings.
These tools (`newuidmap` and `newgidmap`) help us do that. They are small programs with setuid permission, so they can safely write the mapping files for us. You can install them via package manager:

    * Debian / Ubuntu
    ```
    $ sudo apt install uidmap
    ```

* `libcap`
This library is use for setting the capabilitiies.

    * Debian / Ubuntu
    ```
    sudo apt-get install libcap-dev
    ```

* `libseccomp`
Use for limiting the system call by seccomp.
    * Debian / Ubuntu
    ```
    sudo apt-get install libseccomp-dev
    ```