#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include "worker.h"

#define ENV_STAT_FILE ("RUN_GUESSNUM_STAT_FILE")
#define ENV_PIPE_IN ("RUN_GUESSNUM_PIPE_IN")
#define ENV_PIPE_OUT ("RUN_GUESSNUM_PIPE_OUT")

#define DEFAULT_STAT_FILE ("guessnum.csv")
#define DEFAULT_PIPE_IN (".guessnum-run.in")
#define DEFAULT_PIPE_OUT (".guessnum-run.out")

static inline void goto_rowcol(uint8_t row, uint8_t col)
{
    printf("\033[%u;%uH",row+1,col+1);
}
static inline void _clrscr()
{
    printf("\033[2J");
    goto_rowcol(0,0);
}
static void draw_screen()
{
    _clrscr();
    goto_rowcol(19,0);
    printf("Press Ctrl-c to quit.");
    goto_rowcol(0,0);
    printf("Guesses %21s %21s\n", "Normal", "Mastermind");
}
void print_report(unsigned long long *report_s, unsigned long long *report_m)
{
    uint8_t i,last_record=GUESS_CHANCES;
    unsigned long long total_s=0, total_m=0;
    goto_rowcol(1,0);
    while(last_record>8) {
        if(report_s[last_record-1]!=0 || report_m[last_record-1]!=0) {
            break;
        }
        last_record--;
    }
    for(i=0; i<last_record; i++) {
        total_s+=report_s[i];
        total_m+=report_m[i];
        printf("%-2u      %21llu %21llu\n", i+1, report_s[i], report_m[i]);
    }
    printf("Total: %llu\n", total_s);
}

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

worker_t *worker=NULL;
static void do_stop_worker(int signal)
{
    if(NULL==worker){
        return;
    }
    worker->running=false;
}
bool refresh=true;
static void do_refresh_signal(int signo)
{
    refresh=true;
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
    
    if(proc_cnt==0){
	    proc_cnt=get_cpu_count();
    }
    worker_param_t param={
        .thread_count=proc_cnt,
        .stat_path=filename_stat,
        .pipe_in=pipe_in,
        .pipe_out=pipe_out,
    };
    signal(SIGINT,do_stop_worker);
    signal(SIGQUIT,do_stop_worker);
    signal(SIGTERM,do_stop_worker);
    signal(SIGWINCH,do_refresh_signal);
    puts("Initializing...");
    worker=worker_start(&param);
    if(NULL==worker){
        return 1;
    }
    game_data_t report={0};
    time_t t_print=time(NULL),t;
    worker_report(worker,&report);
    while(worker->running) {
        if(refresh) {
            refresh=false;
            draw_screen();
            print_report(report.report_s,report.report_m);
        }
        t=time(NULL);
        if(t-t_print >= 1) {
            worker_report(worker,&report);
            print_report(report.report_s,report.report_m);
            t_print=t;
        }
        usleep(1000);
    }
    _clrscr();
    worker_stop(worker);
    return 0;
}
