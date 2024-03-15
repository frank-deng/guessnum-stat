#ifndef __fileio_h__
#define __fileio_h__

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include "worker.h"

#ifdef __cplusplus
extern "C" {
#endif

bool test_running(const char *stat_path);
int wait_daemon(bool start_action, const char *socket_path, time_t timeout);
int init_files(fileinfo_t *info);
int init_socket(fileinfo_t *info);
void close_files(fileinfo_t *info);
int read_stat(worker_t *worker);
int write_stat(worker_t *worker);
void socket_handler(worker_t *worker);

#ifdef __cplusplus
}
#endif

#endif
