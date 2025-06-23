#ifndef CGROUPS_H
#define CGROUPS_H

int setup_cgroups(const char *container_name, int pid);
void cleanup_cgroups();

#endif