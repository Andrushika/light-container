#include <stdio.h>
#include "log.h"
#include "argtable3.h"
#include "container.h"

int main(int argc, char *argv[]) {
    int exit_code = 0;
    int container_pid = 0;
    container_config config;

    char *stack = NULL;

    struct arg_lit *help = arg_lit0(NULL, "help", "display this help and exit");
    struct arg_int *uid_opt = arg_int1("u", "uid", "<uid>", "set the user ID in the container");
    struct arg_str *mount_opt = arg_str1("m", "mount", "<dir>", "set the mount directory");
    struct arg_str *cmd_opt = arg_str1("c", "cmd", "<command>", "set the command to run in the container");
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
    
    config.uid = uid_opt->ival[0];
    config.mount_dir = mount_opt->sval[0];
    config.cmd = cmd_opt->sval[0];

    stack = malloc(CONTAINER_STACK_SIZE);

    if(!stack) {
        log_error("Failed to allocate stack memory for container");
        exit_code = -1;
        goto exit;
    }

    if((container_pid = container_init(&config, stack + CONTAINER_STACK_SIZE)) <= 0){
        log_error("Failed to initialize container");
        exit_code = -1;
        goto exit;
    }

exit:
    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    if (stack) {
        free(stack);
    }
    log_debug("Exiting with code %d", exit_code);
    return exit_code;
}
