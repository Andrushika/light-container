#ifndef USERNS_H
#define USERNS_H

#define HOST_MAP_ID_START "100000"
#define USERNS_MAP_ID_START "0"
#define USERNS_MAP_LEN "65536"

int write_id_map_with_helper(int container_pid, int socket_fd);
int userns_init(int uid, int socket_fd);

#endif