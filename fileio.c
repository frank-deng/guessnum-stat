#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
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
int wait_daemon(bool start_action, const char *socket_path, time_t timeout)
{
    int rc=E_TIMEOUT;
    time_t t0=time(NULL),t;
    do{
        t=time(NULL);
        struct stat stat_in;
        if(start_action){
            if(stat(socket_path,&stat_in)==0){
                rc=E_OK;
                break;
            }
        }else{
            if(stat(socket_path,&stat_in)!=0){
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
    size_t i;
    if(NULL==info || NULL==info->stat_path || NULL==info->socket_path){
        fprintf(stderr,"Invalid param.\n");
        return E_INVAL;
    }
    info->fp_stat=NULL;
    info->fd_socket=-1;
    info->socket_created=false;
    for(i=0; i<MAX_CONNECTIONS; i++){
        info->clients[i]=-1;
    }
    
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
    
    unlink(info->socket_path);
    int fd=socket(AF_UNIX,SOCK_STREAM,0);
    if(fd<0){
        goto error_exit;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    info->fd_socket=fd;
    struct sockaddr_un addr;
    addr.sun_family=AF_UNIX;
    strncpy(addr.sun_path, info->socket_path, sizeof(addr.sun_path)-1);
    if(bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0){
        fprintf(stderr,"Failed to create socket %s: %s.\n",info->socket_path,strerror(errno));
        goto error_exit;
    }
    info->socket_created=true;
    if(listen(fd,1)<0){  
        fprintf(stderr, "Listen failed: %s\n", strerror(errno));  
        goto error_exit;
    }
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
    size_t i;
    for(i=0; i<MAX_CONNECTIONS; i++){
        int fd=info->clients[i];
        if(fd>=0){
            close(fd);
        }
    }
    if(info->fd_socket >= 0){
        close(info->fd_socket);
    }
    if(info->socket_created){
        unlink(info->socket_path);
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

static int add_fd(size_t count, int *fd_arr, int fd)
{
    size_t i;
    int res=-1;
    for(i=0; i<count; i++){
        int val=fd_arr[i];
        if(val==fd){
            return i;
        }else if(val<0 && res<0){
            res=i;
        }
    }
    if(res>=0){
        fd_arr[res]=fd;
    }
    return res;
}
static void del_fd(size_t count, int *fd_arr, int fd)
{
    size_t i;
    for(i=0; i<count; i++){
        if(fd_arr[i]==fd){
            fd_arr[i]=-1;
        }
    }
}
static void output_stat(int fd,worker_t *worker)
{
    worker_report(worker);
    game_data_t game_data;
    pthread_mutex_lock(&worker->stat_mutex);
    game_data = worker->game_data_main;
    pthread_mutex_unlock(&worker->stat_mutex);
    
    uint8_t i,last_record=GUESS_CHANCES;
    char buf[80];
    while(last_record>8) {
        if(game_data.report_s[last_record-1]!=0 ||
            game_data.report_m[last_record-1]!=0) {
            break;
        }
        last_record--;
    }
    snprintf(buf,sizeof(buf),"%u\n",last_record);
    int rc=write(fd,buf,strlen(buf));
    if(rc<=0){
        fprintf(stderr,"Failed to write pipe %d %d\n",rc,errno);
    }
    for(i=0; i<last_record; i++) {
        snprintf(buf,sizeof(buf),"%llu,%llu\n",game_data.report_s[i],game_data.report_m[i]);
        int rc=write(fd,buf,strlen(buf));
        if(rc<=0){
            fprintf(stderr,"Failed to write pipe %d %d\n",rc,errno);
        }
    }
}
static int session_handler(worker_t *worker,int fd)
{
    char cmd='\0';
    int rc=read(fd,&cmd,sizeof(cmd));
    if(rc<0 && errno==EAGAIN){
        return E_AGAIN;
    }else if(rc<=0){
        return E_FILEIO;
    }
    switch(cmd){
        case 'Q':
        case 'q':
            worker->running=false;
        break;
        case 'B':
        case 'b':
            output_stat(fd,worker);
        break;
    }
    return E_OK;
}
void socket_handler(worker_t *worker)
{
    size_t i;
    fd_set readset;
    int listenfd=worker->fileinfo.fd_socket;
    int *fd_arr=worker->fileinfo.clients;
    FD_ZERO(&readset);
    FD_SET(listenfd,&readset);
    int maxfd=listenfd;
    for(i=0; i<MAX_CONNECTIONS; i++){
        int fd=fd_arr[i];
        if(fd<0){
            continue;
        }
        FD_SET(fd,&readset);
        if(fd>maxfd){
            maxfd=fd;
        }
    }
    struct timeval tm;
    tm.tv_sec=0;
    tm.tv_usec=10000;
    if(select(maxfd+1,&readset,NULL,NULL,&tm)<=0){
        return;
    }
    if(FD_ISSET(listenfd,&readset)){
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen=sizeof(clientaddr);
        int clientfd = accept(listenfd,(struct sockaddr *)&clientaddr,&clientaddrlen);
        if(clientfd >= 0){
            int idx=add_fd(MAX_CONNECTIONS,fd_arr,clientfd);
            if(idx<0){
                close(clientfd);
            }
        }
        return;
    }
    for(i=0; i<MAX_CONNECTIONS; i++){
        int fd=fd_arr[i];
        if(fd<0 || !FD_ISSET(fd,&readset)){
            continue;
        }
        if(E_FILEIO==session_handler(worker,fd)){
            close(fd);
            del_fd(MAX_CONNECTIONS,fd_arr,fd);
        }
    }
}
