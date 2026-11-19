# light-container
light-container is a simplified rootless Linux container runtime.

## Execute
```
make
./light-container -u 0 -m ~/rootfs-alpine -- /bin/ash -c "echo hello light-container!"
```
Pass the command to run after `--`. `-u` sets the UID inside the container. UID 0 is you, and UIDs 1 to 65535 come from your range in `/etc/subuid`. Add `-v` for debug logs.

The rootfs only needs to be readable by you. It becomes the read-only lower layer of an overlayfs whose upper layer is a tmpfs, so writes inside the container never reach the rootfs and are discarded on exit. This needs Linux 5.11 or later. Before Linux 6.6 tmpfs has no user xattrs, so deleting or renaming a directory that comes from the rootfs fails with EIO inside the container, and light-container warns about it.

The container gets its own hostname, PID, IPC, mount, network, cgroup and user namespaces, a private `/proc`, and a minimal `/dev` (`null`, `zero`, `full`, `random`, `urandom`, `tty`, `pts`, `shm`). The exit status of the command becomes the exit status of light-container. Ctrl-C goes to the container. SIGTERM and SIGHUP sent to light-container are forwarded to it, and if light-container is killed the container dies with it.

## Prerequisite
* `newuidmap` / `newgidmap`
This project runs containers in rootless mode. However, we still need some special permission to write UID and GID mappings.
These tools (`newuidmap` and `newgidmap`) help us do that. They are small programs with setuid permission, so they can safely write the mapping files for us. They map only the ranges listed for you in `/etc/subuid` and `/etc/subgid`, which most distributions create when you add a user. You can install them via package manager:

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

### Using cgroups (optional)
An unprivileged user can only move processes inside a cgroup subtree delegated to them. light-container creates the container's cgroup under the cgroup it runs in, so start it in a delegated one. On systemd hosts:

```
systemd-run --user --scope -p Delegate=yes ./light-container -u 0 -m ~/rootfs-alpine -- /bin/ash
```

The container then gets `memory.max` 1 GiB, `pids.max` 64 and `cpu.weight` 256. systemd delegates only the `memory` and `pids` controllers to users by default. To delegate `cpu` too, run:

```
sudo mkdir -p /etc/systemd/system/user@.service.d
printf '[Service]\nDelegate=cpu cpuset io memory pids\n' | sudo tee /etc/systemd/system/user@.service.d/delegate.conf
sudo systemctl daemon-reload
```

Without delegation light-container prints a warning and runs the container without limits.

### AppArmor (Ubuntu 23.10 and later)
Ubuntu restricts unprivileged user namespaces with AppArmor by default (`kernel.apparmor_restrict_unprivileged_userns=1`), which blocks the mounts light-container needs. Either give the binary an AppArmor profile that allows `userns`, or turn the restriction off with `sudo sysctl kernel.apparmor_restrict_unprivileged_userns=0`.

## Networking
If [slirp4netns](https://github.com/rootless-containers/slirp4netns) is installed, light-container starts it for each container. That gives the container a `tap0` interface with outbound access and DNS at `10.0.2.3`. Without slirp4netns the container has only a loopback interface.

* Debian / Ubuntu
```
sudo apt install slirp4netns
```

## Tests
`./tests/run.sh` builds a Debian image with Docker and runs end-to-end tests as an unprivileged user inside a privileged container. It checks namespaces, the overlay rootfs, `/dev`, capabilities and seccomp, cgroup limits, networking and exit status.

## License

This project is licensed under the MIT License – see the [LICENSE](./LICENSE) file for details.