# light-container
light-container is a simplified rootless linux container runtime.

## Execute
```
make
./light-container -u 20 -c 'sleep infinity' -m /your/mount/path
```
Please note that the UID is limited from 0 to 65535, and remember to use the quotation marks when passing the `-c` (which refer to command to execute in container) arg.

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