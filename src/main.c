#include <stdio.h>
#include "log.h"

int main() {
    log_set_level(LOG_INFO);
    log_set_quiet(false);

    log_info("This is an info message.");
    log_debug("This debug message will not be shown because the level is set to INFO.");
    log_error("An error occurred: %s", "File not found");

    return 0;
}
