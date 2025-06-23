#include <stdio.h>
#include <sys/socket.h>
#include "log.h"
#include "argtable3.h"
#include "container.h"
#include "userns.h"
#include "cgroups.h"
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int exit_code = 0;
    int container_pid = 0;
    container_config config;
    int sockets[2] = {0};

    char *stack = NULL;

    struct arg_lit *help = arg_lit0(NULL, "help", "display this help and exit");
    struct arg_int *uid_opt = arg_int1("u", "uid", "<uid>", "set the user ID in the container");
    struct arg_str *mount_opt = arg_str1("m", "mount", "<dir>", "set the mount directory");
    struct arg_str *cmd_opt = arg_strn(NULL, NULL, "<cmd>...", 1, 10000, "command and its arguments");
    struct arg_end *end = arg_end(20);

    void *argtable[] = {
        help,
        uid_opt,
        cmd_opt,
        mount_opt,
        end
    };

    int nerrors = arg_parse(argc, argv, argtable);

    if (help->count > 0) {
        arg_print_syntax(stdout, argtable, "\n");
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        goto exit;
    }

    if(nerrors != 0) {
        arg_print_errors(stdout, end, argv[0]);
        log_error("Invalid arguments provided.");
        exit_code = -1;
        goto exit;
    }

    if (cmd_opt->count < 1) {
        log_error("No command provided to run in the container.");
        arg_print_syntax(stderr, argtable, "\n");
        exit_code = -1;
        goto exit;
    }
    cmd_opt->sval[cmd_opt->count] = NULL;


    if (socketpair(AF_LOCAL, SOCK_SEQPACKET, 0, sockets)) {
        log_error("Failed to create socket pair: %m");
        exit_code = -1;
        goto exit;
    }

    if(fcntl(sockets[0], F_SETFD, FD_CLOEXEC) < 0) {
        log_error("Failed to set close-on-exec flag on socket: %m");
        exit_code = -1;
        goto exit;
    }

    char tmpdir[] = "/tmp/light-container-mnt.XXXXXX";
    if (!mkdtemp(tmpdir)){
        log_error("Failed to create temporary directory for mount: %m");
        return -1;
    }
    
    config.hostname = "light-container";
    config.socket_fd = sockets[1];
    config.uid = uid_opt->ival[0];
    config.mount_dir = mount_opt->sval[0];
    config.argv = cmd_opt->sval;
    config.mount_tmp_dir = tmpdir;

    stack = malloc(CONTAINER_STACK_SIZE);

    if(!stack) {
        log_error("Failed to allocate stack memory for container");
        exit_code = -1;
        goto exit;
    }

    if((container_pid = container_init(&config, stack + CONTAINER_STACK_SIZE)) <= 0){
        exit_code = -1;
        goto exit;
    }
    
    if(write_id_map_with_helper(container_pid, sockets[0]) < 0) {
        exit_code = -1;
        goto exit;
    }

    log_debug("Setting up cgroups for container...");
    if (setup_cgroups(config.hostname, container_pid) < 0) {
        log_warn("Cgroups is not enabled; resource limits will be skipped");
    }

    log_debug("Container initialized with PID %d", container_pid);

    if(container_wait(container_pid) < 0) {
        exit_code = -1;
        goto exit;
    }
    

exit:
    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    if (stack) {
        free(stack);
    }
    rmdir(tmpdir);
    close(sockets[0]);
    close(sockets[1]);
    cleanup_cgroups();
    log_debug("Exiting with code %d", exit_code);
    return exit_code;
}
