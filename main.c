#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include "worker.h"
#include "fileio.h"
#include "viewer.h"

#define ENV_STAT_FILE ("RUN_GUESSNUM_STAT_FILE")
#define ENV_PIPE_IN ("RUN_GUESSNUM_PIPE_IN")
#define ENV_PIPE_OUT ("RUN_GUESSNUM_PIPE_OUT")

#define DEFAULT_STAT_FILE ("guessnum.csv")
#define DEFAULT_PIPE_IN (".guessnum-run.in")
#define DEFAULT_PIPE_OUT (".guessnum-run.out")

uint16_t get_cpu_count()
{
    FILE *fp=fopen("/proc/cpuinfo","r");
    if(NULL==fp){
    	fprintf(stderr,"Failed to get cpu count.\n");
	return 1;
    }
    uint16_t res=0;
    char buf[80];
    while(fgets(buf,sizeof(buf),fp)!=NULL){
	if(strncmp(buf,"processor",sizeof("processor")-1)==0){
	    res++;
	}
    }
    return max(res,1);
}
const char *getfromenv(const char *key,const char *defval)
{
    char *res=getenv(key);
    if(NULL==res){
        return defval;
    }
    return res;
}
int do_stop_daemon(bool daemon_running,const char *pipe_in,const char *pipe_out){
    if(!daemon_running){
        fprintf(stderr,"Bulls and Cows daemon is not running.\n");
        return 1;
    }
    int fd_in=open(pipe_in,O_RDWR|O_NONBLOCK);
    if(fd_in<0){
        fprintf(stderr,"Failed to communicate with Bulls and Cows daemon.\n");
        return 1;
    }
    int rc=0;
    char cmd='q';
    if(write(fd_in,&cmd,sizeof(cmd))<sizeof(cmd)){
        fprintf(stderr,"Failed to stop 2048 daemon.\n");
    }
    close(fd_in);
    if(wait_daemon(false,pipe_in,pipe_out,20)!=E_OK){
        fprintf(stderr,"Timeout.\n");
        return 1;
    }
    fprintf(stderr,"Bulls and Cows daemon stopped.\n");
    return rc;
}

worker_t *worker=NULL;
static void do_stop_worker(int signal)
{
    if(NULL==worker){
        return;
    }
    worker->running=false;
}
void print_help(const char *app_name){
    char *app_name_last_posix=strrchr(app_name,'/');
    char *app_name_last_win=strrchr(app_name,'\\');
    if(NULL!=app_name_last_posix){
        app_name=app_name_last_posix+1;
    }else if(NULL!=app_name_last_win){
        app_name=app_name_last_win+1;
    }
    fprintf(stderr,"Usage: %s [-h] [-d] [-s] [-n threads]\n",app_name);
    fprintf(stderr,"       -h          Print help.\n");
    fprintf(stderr,"       -d          Start Bulls and Cows daemon.\n");
    fprintf(stderr,"       -s          Stop Bulls and Cows daemon.\n");
    fprintf(stderr,"       -n threads  Specify threads for running.\n");
}
int main(int argc, char *argv[])
{
    const char *filename_stat=getfromenv(ENV_STAT_FILE,DEFAULT_STAT_FILE);
    const char *pipe_in=getfromenv(ENV_PIPE_IN,DEFAULT_PIPE_IN);
    const char *pipe_out=getfromenv(ENV_PIPE_OUT,DEFAULT_PIPE_OUT);
    uint16_t proc_cnt = 0;
    bool viewer=true;
    bool stop_daemon=false;
    unsigned char opt;
    while((opt=getopt(argc,argv,"hdsn:")) != 0xff){
        switch(opt){
            case 'd':
            	viewer=false;
            break;
            case 's':
            	stop_daemon=true;
            break;
            case 'n':
                proc_cnt=strtoul(optarg,NULL,10);
                if(proc_cnt<1){
                    print_help(argv[0]);
                	return 1;
                }
            break;
            default:
                print_help(argv[0]);
                return 1;
            break;
        }
    }
    bool daemon_running=test_running(filename_stat);
    if(stop_daemon){
        return do_stop_daemon(daemon_running,pipe_in,pipe_out);
    }
    if(daemon_running){
        if(viewer){
            return viewer_guessnum(pipe_in,pipe_out);
        }else{
            fprintf(stderr,"Bulls and Cows daemon is already running.\n");
            return 1;
        }
    }
    pid_t pid=fork();
    if(0!=pid){
        if(E_OK!=wait_daemon(true,pipe_in,pipe_out,20)){
            fprintf(stderr,"Failed to start Bulls and Cows daemon.\n");
            return 1;
        }
        if(viewer){
            return viewer_guessnum(pipe_in,pipe_out);
        }
        fprintf(stderr,"Bulls and Cows daemon started.\n");
        return 0;
    }
    
    if(proc_cnt==0){
	    proc_cnt=get_cpu_count();
    }
    worker_param_t param={
        .thread_count=proc_cnt,
        .stat_path=filename_stat,
        .pipe_in=pipe_in,
        .pipe_out=pipe_out,
    };
    signal(SIGINT,SIG_IGN);
    signal(SIGQUIT,SIG_IGN);
    signal(SIGTERM,do_stop_worker);
    signal(SIGWINCH,SIG_IGN);
    worker=worker_start(&param);
    if(NULL==worker){
        return 1;
    }
    game_data_t report={0};
    time_t t_write=time(NULL),t;
    while(worker->running) {
        worker_pipe_handler(worker);
        t=time(NULL);
        if(t-t_write >= 2) {
            worker_report(worker);
            write_stat(worker);
            t_write=t;
        }
        usleep(1000);
    }
    worker_stop(worker);
    return 0;
}
