#!/bin/bash
# End-to-end tests. Runs as root inside the test container (see run.sh), builds
# light-container, then drives it as the unprivileged user "dev".
set -u
CG=/sys/fs/cgroup
ROOTFS=/home/dev/rootfs-alpine
BIN=/home/dev/light-container/light-container
fails=0
pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $1"; fails=$((fails + 1)); }

cp -r /src /home/dev/light-container && rm -rf /home/dev/light-container/build /home/dev/light-container/light-container
chown -R dev:dev /home/dev/light-container
su dev -c 'make -s -C ~/light-container' >/dev/null || { echo "build failed"; exit 1; }

# Act as the cgroup v2 manager (what systemd does for a Delegate=yes scope):
# park our own processes in a leaf, enable controllers, and hand each run a
# fresh cgroup owned by "dev".
mkdir -p $CG/init
for p in $(cat $CG/cgroup.procs); do echo "$p" > $CG/init/cgroup.procs 2>/dev/null; done
echo "+memory +pids +cpu" > $CG/cgroup.subtree_control
delegated_cgroup() {
    local d; d=$(mktemp -d $CG/scope-XXXXXX) && chown dev:dev "$d" "$d/cgroup.procs" "$d/cgroup.subtree_control" "$d/cgroup.threads"
    echo "$d"
}
# as_dev <cgroup dir> <cmd...> : run cmd as dev with its processes in <cgroup dir>
as_dev() {
    local d=$1; shift
    sh -c 'echo $$ > "$0/cgroup.procs" && exec setpriv --reuid=dev --regid=dev --init-groups env -i HOME=/home/dev PATH=/usr/bin:/bin "$@"' "$d" "$@"
}
lc() { as_dev "$(delegated_cgroup)" "$BIN" "$@" 2>/tmp/lc.err; }

out=$(lc -u 0 -m "$ROOTFS" -- /bin/echo hello)
[ "$out" = hello ] && pass "runs a command in a rootfs under a 0700 home" || { fail "runs a command (got '$out')"; cat /tmp/lc.err; }

lc -u 0 -m "$ROOTFS" -- /bin/sh -c 'exit 3'; rc=$?
[ $rc = 3 ] && pass "propagates the exit status" || fail "propagates the exit status (got $rc)"

out=$(lc -u 1000 -m "$ROOTFS" -- /usr/bin/id -u)
[ "$out" = 1000 ] && pass "runs as a non-root uid" || { fail "runs as uid 1000 (got '$out')"; cat /tmp/lc.err; }

out=$(lc -u 1000 -m "$ROOTFS" -- /bin/sh -c 'grep -E "^(CapEff|Seccomp):" /proc/self/status | tr -s "\t" " "')
[ "$out" = "$(printf 'CapEff: 0000000000000000\nSeccomp: 2')" ] && pass "non-root uid has no capabilities, seccomp filter on" || fail "caps/seccomp (got '$out')"

out=$(as_dev $CG/init "$BIN" -u 0 -m "$ROOTFS" -- /bin/echo hello 2>/tmp/lc.err)
[ "$out" = hello ] && grep -q "not delegated" /tmp/lc.err && pass "without cgroup delegation: warns and still runs" || fail "no delegation (got '$out')"

out=$(lc -u 0 -m "$ROOTFS" -- /bin/sh -c 'touch /etc/written && echo ok')
if [ "$out" = ok ] && [ ! -e $ROOTFS/etc/written ]; then pass "writes go to an overlay, rootfs untouched"
else fail "overlay writes (out='$out', host file exists: $([ -e $ROOTFS/etc/written ] && echo yes || echo no))"; cat /tmp/lc.err; fi

out=$(lc -u 0 -m "$ROOTFS" -- /bin/sh -c 'echo x > /dev/null && head -c1 /dev/zero | wc -c')
[ "$out" = 1 ] && pass "/dev/null and /dev/zero work" || { fail "/dev nodes (got '$out')"; cat /tmp/lc.err; }

out=$(lc -u 0 -m "$ROOTFS" -- /bin/sh -c 'hostname; echo $$; cat /proc/1/comm; true')
[ "$out" = "$(printf 'light-container\n1\nsh')" ] && pass "own hostname, pid 1, private /proc" || fail "namespaces (got '$out')"

out=$(lc -u 0 -m "$ROOTFS" -- /bin/sh -c 'cat /proc/self/cgroup')
[ "$out" = "0::/" ] && pass "cgroup namespace rooted at the container cgroup" || fail "cgroup ns (got '$out')"

# Limits as the kernel sees them, read from outside while the container runs.
d=$(delegated_cgroup)
as_dev "$d" "$BIN" -u 0 -m "$ROOTFS" -- /bin/sleep 2 2>/tmp/lc.err &
sleep 1
c=$(echo "$d"/light-container-*)
if grep -q . "$c/cgroup.procs" && [ "$(cat "$c/memory.max")" = 1073741824 ] && [ "$(cat "$c/pids.max")" = 64 ] && [ "$(cat "$c/cpu.weight")" = 256 ]; then
    pass "cgroup limits applied (memory.max=1G pids.max=64 cpu.weight=256)"
else
    fail "cgroup limits (cgroup='$c')"; ls -R "$d" | head; cat /tmp/lc.err
fi
wait
rmdir "$d"/* 2>/dev/null
[ -z "$(ls -d "$d"/light-container-* 2>/dev/null)" ] && pass "container cgroup removed on exit" || fail "container cgroup left behind"

out=$(lc -u 0 -m "$ROOTFS" -- /bin/sh -c 'for i in $(seq 80); do sleep 1 & done; wait' 2>&1)
grep -q "can't fork" /tmp/lc.err && pass "pids.max stops a fork flood" || fail "pids.max not enforced"

out=$(lc -u 0 -m "$ROOTFS" -- /bin/sh -c 'wget -q -T 10 -O /dev/null http://dl-cdn.alpinelinux.org/alpine/ && echo online')
[ "$out" = online ] && pass "outbound network via slirp4netns" || { fail "network (got '$out')"; cat /tmp/lc.err; }

out=$(lc -u 0 -m "$ROOTFS" -- /bin/sh -c '/sbin/ip -o link show lo | grep -q UP && echo up')
[ "$out" = up ] && pass "loopback is up" || fail "loopback (got '$out')"

echo "---- $fails failure(s)"
exit $fails
