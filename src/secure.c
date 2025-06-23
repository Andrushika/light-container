/* SPDX-License-Identifier: MIT
 *
 *  Portions of this file are adapted from
 *      lucavallin/barco  (commit be04c8c, 2024-05-07)
 *      https://github.com/lucavallin/barco
 *
 *  Original copyright (c) 2023 Luca Cavallin
 *  Modifications (c) 2025 Andrew Chang <andrew99154@gmail.com>
 *
 *  The original code is licensed under the MIT License; see
 *  licenses/LICENSE.barco or the project root LICENSE file for details.
 */

#define _GNU_SOURCE  
#include "secure.h"
#include "log.h"

#include <sys/capability.h>
#include <linux/prctl.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <seccomp.h>
#include <sys/stat.h>
#include <sched.h>
#include <sys/ioctl.h> 

int drop_bounding_set(int *drop_caps, int num_caps) {
    for(int i = 0; i < num_caps; i++) {
        if (prctl(PR_CAPBSET_DROP, drop_caps[i]) < 0) {
            log_error("Failed to drop capability %d: %m", drop_caps[i]);
            return -1;
        }
    }
    return 0;
}

int drop_inheritable_set(int *drop_caps, int num_caps) {
    cap_t curr_caps = cap_get_proc();
    if (curr_caps == NULL) {
        log_error("Failed to get current capabilities: %m");
        return -1;
    }
    if(cap_set_flag(curr_caps, CAP_INHERITABLE, num_caps, drop_caps, CAP_CLEAR) < 0 
        || cap_set_proc(curr_caps) < 0) {
        log_error("Failed to drop inheritable capabilities: %m");
        cap_free(curr_caps);
        return -1;
    }
    cap_free(curr_caps);
    return 0;
}

int prepare_capabilities() {
    int drop_caps[] = {
		CAP_AUDIT_CONTROL,
		CAP_AUDIT_READ,
		CAP_AUDIT_WRITE,
		CAP_BLOCK_SUSPEND,
		CAP_DAC_READ_SEARCH,
		CAP_FSETID,
		CAP_IPC_LOCK,
		CAP_MAC_ADMIN,
		CAP_MAC_OVERRIDE,
		CAP_MKNOD,
		CAP_SETFCAP,
		CAP_SYSLOG,
		CAP_SYS_ADMIN,
		CAP_SYS_BOOT,
		CAP_SYS_MODULE,
		CAP_SYS_NICE,
		CAP_SYS_RAWIO,
		CAP_SYS_RESOURCE,
		CAP_SYS_TIME,
		CAP_WAKE_ALARM,
        CAP_SYS_PACCT,
        CAP_NET_ADMIN,
        CAP_SYS_PTRACE,
        CAP_SYS_TTY_CONFIG,
        CAP_IPC_OWNER,
        CAP_LEASE,
        CAP_LINUX_IMMUTABLE
	};
    int num_caps = sizeof(drop_caps) / sizeof(*drop_caps);
    if(drop_bounding_set(drop_caps, num_caps) < 0
        || drop_inheritable_set(drop_caps, num_caps) < 0) {
        return -1;
    }
    log_info("Capabilities prepared, dropped %d capabilities", num_caps);
    return 0;
}

// Prepare syscall limitations using seccomp
// Inspired by the seccomp rules from barco
// Added some additional rules based on the docker seccomp profile
int prepare_syscall_limitations() {
    scmp_filter_ctx ctx = NULL;

    log_debug("setting syscalls...");
    if (!(ctx = seccomp_init(SCMP_ACT_ALLOW)) ||
        // Calls that allow creating new setuid / setgid executables.
        // The contained process could created a setuid binary that can be used
        // by an user to get root in absence of user namespaces.
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(chmod), 1,
                        SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID)) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(chmod), 1,
                        SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID)) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(fchmod), 1,
                        SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID)) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(fchmod), 1,
                        SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID)) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(fchmodat), 1,
                        SCMP_A2(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID)) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(fchmodat), 1,
                        SCMP_A2(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID)) ||

        // Calls that allow contained processes to start new user namespaces
        // and possibly allow processes to gain new capabilities.
        seccomp_rule_add(
            ctx, SEC_SCMP_FAIL, SCMP_SYS(unshare), 1,
            SCMP_A0(SCMP_CMP_MASKED_EQ, CLONE_NEWUSER, CLONE_NEWUSER)) ||
        seccomp_rule_add(
            ctx, SEC_SCMP_FAIL, SCMP_SYS(clone), 1,
            SCMP_A0(SCMP_CMP_MASKED_EQ, CLONE_NEWUSER, CLONE_NEWUSER)) ||

        // Allows contained processes to write to the controlling terminal
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(ioctl), 1,
                        SCMP_A1(SCMP_CMP_MASKED_EQ, TIOCSTI, TIOCSTI)) ||

        // The kernel keyring system is not namespaced
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(keyctl), 0) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(add_key), 0) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(request_key), 0) ||

        // Before Linux 4.8, ptrace breaks seccomp
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(ptrace), 0) ||

        // Calls that let processes assign NUMA nodes. These could be used to deny
        // service to other NUMA-aware application on the host.
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(mbind), 0) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(migrate_pages), 0) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(move_pages), 0) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(set_mempolicy), 0) ||

        // Alows userspace to handle page faults It can be used to pause execution
        // in the kernel by triggering page faults in system calls, a mechanism
        // often used in kernel exploits.
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(userfaultfd), 0) ||

        // This call could leak a lot of information on the host.
        // It can theoretically be used to discover kernel addresses and
        // uninitialized memory.
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(perf_event_open), 0) ||

        // Prevent container from enabling BSD emulation. Not inherently dangerous, 
        // but poorly tested, potential for a lot of kernel vulnerabilities.
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(personality), 0) ||

        // Obsolete syscalls
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(get_kernel_syms), 0) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(nfsservctl), 0) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(query_module), 0) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(sysfs), 0) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(_sysctl), 0) ||
        seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(ustat), 0) ||

        // Prevents setuid and setcap'd binaries from being executed
        // with additional privileges. This has some security benefits, but due to
        // weird side-effects, the ping command will not work in a process for
        // an unprivileged user.
        seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, 0) || seccomp_load(ctx)) {

        log_error("failed to set syscalls: %m");

        // Apply restrictions to the process and release the context.
        log_debug("releasing seccomp context...");
        if (ctx) {
            seccomp_release(ctx);
        }

        return -1;
    }

    seccomp_release(ctx);
    log_info("Syscall limitations prepared successfully");
    return 0;
}