#include <stdlib.h>
#include <errno.h>
#include "worker.h"
#include "fileio.h"

void* thread_main(void *data){
    thread_data_t *thread_data = (thread_data_t*)data;
    worker_t *worker=thread_data->worker;
    pthread_mutex_t *mutex=&worker->stat_mutex;
    while (worker->running) {
        thread_data->game_data.report_s[guess(&thread_data->rand,false)-1]++;
        thread_data->game_data.report_m[guess(&thread_data->rand,true)-1]++;
        if(thread_data->do_report || !worker->running){
            pthread_mutex_lock(mutex);
            int i;
            for(i=0; i<GUESS_CHANCES; i++){
                worker->game_data_main.report_s[i] += thread_data->game_data.report_s[i];
                worker->game_data_main.report_m[i] += thread_data->game_data.report_m[i];
            }
            worker->thread_report--;
            pthread_mutex_unlock(mutex);
            thread_data->do_report=false;
            memset(&thread_data->game_data,0,sizeof(thread_data->game_data));
        }
    }
    return NULL;
}
worker_t *worker_start(worker_param_t *param)
{
    worker_t *worker = (worker_t*)calloc(1,sizeof(worker_t)+sizeof(thread_data_t)*param->thread_count);
    if(NULL==worker){
        fprintf(stderr,"malloc failed\n");
        return NULL;
    }
    worker->fileinfo.stat_path=param->stat_path;
    worker->fileinfo.pipe_in=param->pipe_in;
    worker->fileinfo.pipe_out=param->pipe_out;
    int rc=init_files(&worker->fileinfo);
    if(rc!=E_OK){
        free(worker);
        return NULL;
    }
    init();
    srand(time(NULL));
    worker->thread_count=param->thread_count;
    worker->running=true;
    read_stat(worker);
    pthread_mutex_init(&worker->stat_mutex, NULL);
    int i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        thread_data->worker=worker;
        thread_data->do_report=false;
        initRandom(&thread_data->rand, rand());
        pthread_create(&(thread_data->tid), NULL, thread_main, thread_data);
    }
    return worker;
}
int worker_report(worker_t *worker,game_data_t *out)
{
    pthread_mutex_lock(&worker->stat_mutex);
    worker->thread_report=worker->thread_count;
    pthread_mutex_unlock(&worker->stat_mutex);
    int i;
    for(i=0; i<worker->thread_count; i++){
        thread_data_t *thread_data=&(worker->thread_data[i]);
        thread_data->do_report=true;
    }
    
    bool cont=true;
    time_t t0=time(NULL);
    while(cont && (time(NULL)-t0)<1){
        pthread_mutex_lock(&worker->stat_mutex);
        if(worker->thread_report <= 0){
            memcpy(out,&worker->game_data_main,sizeof(worker->game_data_main));
            cont=false;
        }
        pthread_mutex_unlock(&worker->stat_mutex);
        if(cont){
            usleep(10);
        }
    }
    
    return cont ? E_TIMEOUT : E_OK;
}
void worker_stop(worker_t *worker)
{
    worker->running=false;
    int i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_join(thread_data->tid, NULL);
    }
    write_stat(worker);
    close_files(&worker->fileinfo);
    pthread_mutex_destroy(&worker->stat_mutex);
    free(worker);
}
