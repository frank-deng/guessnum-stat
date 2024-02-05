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
#include "viewer.h"

typedef uint64_t board_t;
typedef struct{
    int fd_in;
    int fd_out;
    volatile bool running;
    volatile bool refresh;
    uint16_t cols;
    bool term_init;
    struct termios flags_orig;
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
static inline int print_stat(viewer_t *viewer)
{
    char cmd='b',buf[1024];
    int i;
    while(read(viewer->fd_out,buf,sizeof(buf))>0);
    int rc=write(viewer->fd_in,&cmd,sizeof(cmd));
    if(rc<=0){
        return E_FILEIO;
    }
    uint32_t retry=200000;
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
    
    // Get thread count
    char *p_start=buf,*p_end=strchr(p_start,'\n');
    if(p_end==NULL){
        return E_INVAL;
    }
    *p_end='\0';
    uint16_t records_count=strtoul(p_start,NULL,0);
    p_start=p_end+1;
    
    // Display boards
    unsigned long long total_s=0,total_m=0;
    goto_rowcol(1,0);
    for(i=0; i<records_count; i++){
        p_end=strchr(p_start,'\n');
        if(p_end!=NULL){
            *p_end='\0';
        }
        unsigned int guesses=0;
        unsigned long long result_s=0,result_m=0;
        sscanf(p_start,"%llu,%llu",&result_s,&result_m);
        printf("%-2u      %21llu %21llu\n",i+1,result_s,result_m);
        total_s+=result_s;
        total_m+=result_m;
        if(p_end!=NULL){
            p_start=p_end+1;
        }else{
            break;
        }
    }
    printf("Total:  %21llu %21llu\n",total_s,total_m);
    fflush(stdout);
    return E_OK;
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
    int rc_print=E_OK;
    while(viewer.running) {
        if(viewer.refresh){
            viewer.refresh=false;
            draw_screen(&viewer);
            rc_print=print_stat(&viewer);
            t0=time(NULL);
        }else{
            time_t t=time(NULL);
            if((t-t0)>=1){
                t0=t;
                rc_print=print_stat(&viewer);
            }
        }
        if(rc_print!=E_OK){
            viewer.running=false;
            break;
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
