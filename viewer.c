#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <termios.h>
#include "common.h"
#include "guessnum.h"
#include "viewer.h"

typedef uint64_t board_t;
typedef struct{
    int fd_in;
    int fd_out;
    volatile bool running;
    volatile bool refresh;
    bool term_init;
    struct termios flags_orig;
    uint16_t game_data_len;
    game_data_t game_data;
}viewer_t;

static inline void _clrscr()
{
    printf("\033[2J\033[1;1H");
}
static inline void goto_rowcol(uint8_t row, uint8_t col)
{
    printf("\033[%u;%uH",row+1,col+1);
}
static inline int _kbhit() {
    int bytesWaiting;
    ioctl(STDIN_FILENO, FIONREAD, &bytesWaiting);
    return bytesWaiting;
}

void close_viewer(viewer_t *viewer);
int init_viewer(viewer_t *viewer, const char *pipe_in, const char *pipe_out)
{
    int rc=E_OK;
    viewer->fd_in=viewer->fd_out=-1;
    viewer->term_init=false;
    
    viewer->fd_in=open(pipe_in,O_RDWR|O_NONBLOCK);
    if(viewer->fd_in<0){
        fprintf(stderr,"Failed to open pipe %s, maybe daemon is not running.\n",pipe_in);
        rc=E_FILEIO;
        goto error_exit;
    }
    
    viewer->fd_out=open(pipe_out,O_RDWR|O_NONBLOCK);
    if(viewer->fd_out<0){
        fprintf(stderr,"Failed to open pipe %s\n",pipe_in);
        rc=E_FILEIO;
        goto error_exit;
    }
    
    viewer->running=true;
    viewer->refresh=true;
    
    // Disable echo, set stdin non-blocking for kbhit works
    struct termios flags;
    tcgetattr(STDIN_FILENO, &flags);
    viewer->flags_orig=flags;
    flags.c_lflag &= ~ICANON; //Make stdin non-blocking
    flags.c_lflag &= ~ECHO;   //Turn off echo
    flags.c_lflag |= ECHONL;  //Turn off echo
    tcsetattr(STDIN_FILENO, TCSANOW, &flags);
    setbuf(stdin, NULL);
    viewer->term_init=true;
    return E_OK;
error_exit:
    close_viewer(viewer);
    return rc;
}
void close_viewer(viewer_t *viewer)
{
    if(viewer->term_init){
        tcsetattr(STDIN_FILENO, TCSANOW, &viewer->flags_orig);
    }
    if(viewer->fd_in>=0){
        flock(viewer->fd_in,LOCK_UN|LOCK_NB);
        close(viewer->fd_in);
    }
    if(viewer->fd_out>=0){
        flock(viewer->fd_out,LOCK_UN|LOCK_NB);
        close(viewer->fd_out);
    }
}

static void draw_screen(viewer_t *viewer)
{
    _clrscr();
    goto_rowcol(19,0);
    printf("Press q to quit.");
    goto_rowcol(0,0);
    printf("Guesses %21s %21s\n", "Normal", "Mastermind");
}
static inline int get_stat(viewer_t *viewer)
{
    char cmd='b',buf[1024];
    int i;
    while(read(viewer->fd_out,buf,sizeof(buf))>0);
    int rc=write(viewer->fd_in,&cmd,sizeof(cmd));
    if(rc<=0){
        return E_FILEIO;
    }
    usleep(1000);
    uint32_t retry=20000;
    do{
        usleep(1000);
        rc=read(viewer->fd_out,buf,sizeof(buf)-1);
    }while(viewer->running && rc<0 && errno==EAGAIN && retry--);
    if(rc<=0 && errno==EAGAIN){
        return E_TIMEOUT;
    }else if(rc<=0){
        return E_FILEIO;
    }
    buf[rc]='\0';
    
    // Get record count
    char *p_start=buf,*p_end=strchr(p_start,'\n');
    if(p_end==NULL){
        return E_INVAL;
    }
    *p_end='\0';
    uint16_t game_data_len=strtoul(p_start,NULL,0);
    game_data_t game_data={0};
    p_start=p_end+1;
    
    // Get game data
    i=0;
    while(p_end!=NULL && i<game_data_len){
        p_end=strchr(p_start,'\n');
        if(p_end!=NULL){
            *p_end='\0';
        }
        sscanf(p_start,"%llu,%llu",&game_data.report_s[i],
            &game_data.report_m[i]);
        if(p_end!=NULL){
            p_start=p_end+1;
        }
        i++;
    }
    if(game_data_len != i){
        return E_INVAL;
    }
    viewer->game_data_len=game_data_len;
    viewer->game_data=game_data;
    return E_OK;
}
void print_stat(viewer_t *viewer)
{
    int i;
    goto_rowcol(1,0);
    unsigned long long total_s=0,total_m=0;
    for(i=0; i<viewer->game_data_len; i++){
        unsigned long long report_s=viewer->game_data.report_s[i];
        unsigned long long report_m=viewer->game_data.report_m[i];
        total_s+=report_s;
        total_m+=report_m;
        printf("%-2u      %21llu %21llu\n",i+1,report_s,report_m);
    }
    printf("Total:  %21llu %21llu\n",total_s,total_m);
    fflush(stdout);
}

viewer_t viewer;
void do_stop_viewer(int signal)
{
    viewer.running=false;
}
void do_refresh_viewer(int signal)
{
    viewer.refresh=true;
}
int viewer_guessnum(const char *pipe_in,const char *pipe_out)
{
    if(init_viewer(&viewer,pipe_in,pipe_out)!=E_OK){
        return 1;
    }
    signal(SIGINT,do_stop_viewer);
    signal(SIGQUIT,do_stop_viewer);
    signal(SIGTERM,do_stop_viewer);
    signal(SIGWINCH,do_refresh_viewer);
    time_t t0=time(NULL);
    get_stat(&viewer);
    while(viewer.running) {
        if(viewer.refresh){
            viewer.refresh=false;
            draw_screen(&viewer);
            print_stat(&viewer);
        }
        time_t t=time(NULL);
        if((t-t0)>=1){
            t0=t;
            get_stat(&viewer);
            print_stat(&viewer);
        }
        
        char ch='\0';
        if(_kbhit()){
            ch=getchar();
        }
        switch(ch){
            case 'q':
            case 'Q':
                viewer.running=false;
            break;
        }
	    usleep(1000);
    }
    _clrscr();
    close_viewer(&viewer);
    return 0;
}
