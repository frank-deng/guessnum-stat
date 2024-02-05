#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include "fileio.h"

bool test_running(const char *stat_path)
{
    bool running=false;
    int fd_log=open(stat_path,O_RDWR|O_NONBLOCK);
    if(fd_log>=0){
        if(flock(fd_log,LOCK_EX|LOCK_NB)!=0){
            running=true;
        }else{
            flock(fd_log,LOCK_UN|LOCK_NB);
        }
    }
    return running;
}
int wait_daemon(bool start_action,const char *pipe_in,const char *pipe_out,
    time_t timeout)
{
    int rc=E_TIMEOUT;
    time_t t0=time(NULL),t;
    do{
        t=time(NULL);
        struct stat stat_in,stat_out;
        if(start_action){
            if(stat(pipe_in,&stat_in)==0 && stat(pipe_out,&stat_out)==0){
                rc=E_OK;
                break;
            }
        }else{
            if(stat(pipe_in,&stat_in)!=0 && stat(pipe_out,&stat_out)!=0){
                rc=E_OK;
                break;
            }
        }
        usleep(1000);
    }while(t-t0 < timeout);
    return rc;
}
int init_files(fileinfo_t *info)
{
    if(NULL==info || NULL==info->stat_path || NULL==info->pipe_in || NULL==info->pipe_out){
        fprintf(stderr,"Invalid param.\n");
        return E_INVAL;
    }
    info->fp_stat=NULL;
    info->fd_in=info->fd_out=-1;
    info->pipe_in_created=info->pipe_out_created=false;
    
    FILE *fp_stat=fopen(info->stat_path, "rb+");
    if(NULL==fp_stat){
        fp_stat=fopen(info->stat_path, "wb+");
    }
    if(NULL==fp_stat){
        fprintf(stderr,"Failed to open stat file %s\n",info->stat_path);
        goto error_exit;
    }
    info->fp_stat=fp_stat;
    if(flock(fileno(fp_stat),LOCK_EX|LOCK_NB)!=0){
        fprintf(stderr,"Failed to lock stat file %s, possibly other instance is running.\n",info->stat_path);
        goto error_exit;
    }
    
    struct stat stat_pipe;
    if(stat(info->pipe_in,&stat_pipe)!=0){
        int res=mkfifo(info->pipe_in, 0666);
        if(res < 0) {
            fprintf(stderr,"Failed to create fifo %s\n",info->pipe_in);
            goto error_exit;
        }
    }
    info->pipe_in_created=true;
    
    if(stat(info->pipe_out,&stat_pipe)!=0){
        int res=mkfifo(info->pipe_out, 0666);
        if(res < 0) {
            fprintf(stderr,"Failed to create fifo %s\n",info->pipe_out);
            goto error_exit;
        }
    }
    info->pipe_out_created=true;
    
    int fd_in=open(info->pipe_in,O_RDWR|O_NONBLOCK);
    if(fd_in<0){
        fprintf(stderr,"Failed to open fifo %s\n",info->pipe_in);
        goto error_exit;
    }
    info->fd_in=fd_in;
    
    int fd_out=open(info->pipe_out,O_RDWR|O_NONBLOCK);
    if(fd_out<0){
        fprintf(stderr,"Failed to open fifo %s\n",info->pipe_out);
        goto error_exit;
    }
    info->fd_out=fd_out;
    return E_OK;
error_exit:
    close_files(info);
    return E_FILEIO;
}
void close_files(fileinfo_t *info)
{
    if (NULL != info->fp_stat) {
        flock(fileno(info->fp_stat),LOCK_UN|LOCK_NB);
        fclose(info->fp_stat);
    }
    if(info->fd_in >= 0){
        close(info->fd_in);
    }
    if(info->fd_out >= 0){
        close(info->fd_out);
    }
    if(info->pipe_in_created){
        unlink(info->pipe_in);
    }
    if(info->pipe_out_created){
        unlink(info->pipe_out);
    }
}
int write_stat(worker_t *worker)
{
    game_data_t game_data;
    pthread_mutex_lock(&worker->stat_mutex);
    game_data = worker->game_data_main;
    pthread_mutex_unlock(&worker->stat_mutex);
    uint8_t i,last_record=GUESS_CHANCES;
    while(last_record>8) {
        if(worker->game_data_main.report_s[last_record-1]!=0 ||
            worker->game_data_main.report_m[last_record-1]!=0) {
            break;
        }
        last_record--;
    }
    FILE *fp=worker->fileinfo.fp_stat;
    fseek(fp,0,SEEK_SET);
    ftruncate(fileno(fp), 0);
    fseek(fp,0,SEEK_SET);
    fprintf(fp,"Guesses,Normal,Mastermind\n");
    for(i=0; i<last_record; i++) {
        fprintf(fp,"%u,%llu,%llu\n",i+1,
            worker->game_data_main.report_s[i],
            worker->game_data_main.report_m[i]
        );
    }
    fflush(fp);
    return E_OK;
}
int read_stat(worker_t *worker)
{
    FILE *fp=worker->fileinfo.fp_stat;
    char buf[80];
    uint8_t i;
    unsigned int guesses=0;
    unsigned long long count_s=0,count_m=0;
    fgets(buf,sizeof(buf),fp);
    game_data_t game_data={0};
    for(i=0; i<GUESS_CHANCES && !feof(fp); i++) {
        fscanf(fp,"%u,%llu,%llu\n",&guesses,&count_s,&count_m);
        game_data.report_s[i]=count_s;
        game_data.report_m[i]=count_m;
    }
    pthread_mutex_lock(&worker->stat_mutex);
    worker->game_data_main=game_data;
    pthread_mutex_unlock(&worker->stat_mutex);
    return E_OK;
}
