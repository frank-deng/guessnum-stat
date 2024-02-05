#ifndef __worker_h__
#define __worker_h__

#include <stdio.h>
#include <pthread.h>
#include "guessnum.h"
#include "common.h"

typedef struct{
    unsigned long long report_s[GUESS_CHANCES];
    unsigned long long report_m[GUESS_CHANCES];
}game_data_t;

struct worker_s;
typedef struct {
    pthread_t tid;
    struct worker_s *worker;
    volatile bool do_report;
    rand_t rand;
    game_data_t game_data;
} thread_data_t;

typedef struct{
    const char *stat_path;
    const char *pipe_in;
    const char *pipe_out;
    bool pipe_in_created;
    bool pipe_out_created;
    int fd_in;
    int fd_out;
    FILE *fp_stat;
}fileinfo_t;

struct worker_s {
    pthread_mutex_t stat_mutex;
    uint16_t thread_report;
    game_data_t game_data_main;
    volatile bool running;
    fileinfo_t fileinfo;
    uint16_t thread_count;
    thread_data_t thread_data[0];
};
typedef struct worker_s worker_t;

typedef struct{
    uint16_t thread_count;
    const char *stat_path;
    const char *pipe_in;
    const char *pipe_out;
}worker_param_t;

#ifdef __cplusplus
extern "C" {
#endif

worker_t *worker_start(worker_param_t *param);
void worker_stop(worker_t *worker);
int worker_report(worker_t *worker);
int worker_pipe_handler(worker_t *worker);

#ifdef __cplusplus
}
#endif

#endif
