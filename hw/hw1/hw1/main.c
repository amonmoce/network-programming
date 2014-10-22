/*
 * Network Programming - HW1
 * Author:      Heron Yang
 * Director:    I-Chen Wu (Prof.)
 * Date:        Sat Oct 18 15:19:06 CST 2014
 *
 * Check NOTE.md for details.
 */

/*
 * FIXME:
 * 1. will '>>' be in the test data?
 * 2. sizes
 *
 * TODO:
 * 1. raise error if calling root command
 * 2. '>' piping
 * 3. cat pipe
 */


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#define DEBUG           1

#define SIZE_SEND_BUFF  10001
#define SIZE_READ_BUFF  10001
#define SIZE_PIPE_BUFF  10001
#define PORT            33916
#define BACKLOG         10
#define MAX_PIPE_NUM    10000

#define FALSE           0
#define TRUE            1

#define IN              0
#define OUT             1

char send_buff[SIZE_SEND_BUFF];
char read_buff[SIZE_READ_BUFF];
char pipe_buff[SIZE_PIPE_BUFF];

char **argv;    // decoded command
int argc = 0;
int *pipe_map[MAX_PIPE_NUM];
int *old_pipe;

/*
 * Pipe Map
 */
int *pipe_create(int p_n) {

    int *fd = malloc(sizeof(int) * 2);
    if(pipe(fd) < 0)    fprintf(stderr, "Fork failed\n");

    if(pipe_map[p_n]) {     // TODO: can we barely do '='?
        old_pipe = malloc(sizeof(int) * 2);
        old_pipe[IN] = pipe_map[p_n][IN];
        old_pipe[OUT] = pipe_map[p_n][OUT];
    }

    pipe_map[p_n] = fd;
    return fd;

}

int pipe_get() {
    if(!pipe_map[0])    return 0;
    return pipe_map[0][IN];
}

void pipe_shift() {
    int i;
    for( i=0 ; i<(MAX_PIPE_NUM-1) ; i++ ){
        pipe_map[i] = pipe_map[i+1];
    }
}

/*
 * Shell
 */
char **command_decode(char *command) {
    char *token = " \t\n\r";                // characters for splitting
    char *p = strtok(command, token);

    argc = 0;
    while(p) {
        argc ++;
        argv = realloc(argv, sizeof(char *) * argc);

        // if memory allocation failed
        if(argv == NULL)    exit(-1);

        argv[argc-1] = p;
        p = strtok(NULL, token);

    }

    // for the last extra one
    argv = realloc(argv, sizeof(char *) * (argc+1));
    argv[argc] = 0;     // set last one as NULL

    // output for debug
    int i;
    printf("argv: ");
    for( i=0 ; i<argc ; i++ )   printf(" %s", argv[i]);
    printf("\n");

    return argv;
}

int prompt(int connfd) {

    int r = 0;

    memset(send_buff, 0, sizeof(send_buff)); 
    memset(read_buff, 0, sizeof(read_buff)); 

    snprintf(send_buff, sizeof(send_buff), "$ ");
    write(connfd, send_buff, strlen(send_buff)); 
    r = read(connfd, read_buff, sizeof(read_buff));

    argv = command_decode(read_buff);
    if(strcmp(argv[0], "exit")==0) {
        close(connfd);
        return 0;   // same as end
    }

    printf("[prompt] r = %d\n", r);
    return r;

}

void welcome_msg(int connfd) {
    char *msg = "****************************************\n\
** Welcome to the information server. **\n\
****************************************\n";
    snprintf(send_buff, sizeof(send_buff), msg);
    write(connfd, send_buff, strlen(send_buff)); 
}

void debug_print_pipe_cat_content(int count) {
    int k;
    fprintf(stderr, "---\n");
    for( k=0 ; k<count ; k++ ) {
        fprintf(stderr, "%c", pipe_buff[k], pipe_buff[k]);
    }
    fprintf(stderr, "---\n");
}

// simple exec (no pipe included)
void fork_and_exec(char **cmd, int p_n) {

    // create pipe
    int *fd = pipe_create(p_n);

    pid_t pid;
    pid = fork();

    if(pid<0) {                     // if error
        fprintf(stderr, "Fork failed\n");
        exit(-1);
    } else if (pid ==0) {           // if child

        // output old_pipe content if exist
        if(old_pipe!=NULL) {

            memset(pipe_buff, 0, sizeof(pipe_buff)); 
            close(old_pipe[OUT]);
            if(DEBUG)   fprintf(stderr, "cating [%d] -> [%d]\n", old_pipe[IN], fd[OUT]);

            int count;
            if( (count = read(old_pipe[IN], pipe_buff, SIZE_PIPE_BUFF)) < 0 ) {
                fprintf(stderr, "read pipe connent error: %s\n", strerror(errno));
            }

            if(DEBUG)   debug_print_pipe_cat_content(count);

            if( write(fd[OUT], pipe_buff, count) < 0 ) {
                fprintf(stderr, "write pipe connent error: %s\n", strerror(errno));
            }
        }

        // redirect STDOUT to pipe
        close(fd[IN]);
        dup2(fd[OUT], STDOUT_FILENO);

        // DEBUG
        if(DEBUG) {
            int i;
            for( i=0 ; i<10 ; i++ )
                if(pipe_map[i]) fprintf(stderr, "pipe_map[%d] = %d, [%d][%d]\n", i, pipe_map[i], pipe_map[i][IN], pipe_map[i][OUT]);
        }

        // redirect STDIN to pipe_map[0][IN]
        int fd_in = pipe_get();
        if(fd_in)   dup2(fd_in, STDIN_FILENO);

        // handle rest
        if(DEBUG) {
            fprintf(stderr, "IN: %d\n", fd_in);
            fprintf(stderr, "OUT: %d\n", fd[OUT]);
        }
        if( execvp(cmd[0], cmd)<0 ) {
            fprintf(stderr, "Unknown command: [%s]\n", argv[0]);
        }

        exit(0);

    } else {                        // if parent
        // for future pipe in
        wait(NULL);
        close(fd[OUT]);
    }
}

// a whole line as input (pipe may be included)
void command_handler() {

    int i, j;
    int is_pipe;

    while(1) {

        is_pipe = FALSE;

        for( i=0 ; i<argc ; i++ ) {
            if(argv[i] && argv[i][0] == '|') {

                char **argv_s = malloc(sizeof(char *) * (i));
                int p_n = 1;

                if(strlen(argv[i]) == 1)  p_n = 1;
                else    sscanf(argv[i], "|%d", &p_n);
                
                is_pipe = TRUE;

                for( j=0 ; j<argc ; j++ ) {
                    // move to sub argv, which will be exec
                    if( j<i ) {
                        argv_s[j] = malloc(sizeof(char) * sizeof(argv[j]));
                        strcpy(argv_s[j], argv[j]);
                    }

                    // shift argv
                    if( j<(argc-i-1) ) {
                        argv[j] = malloc(sizeof(char) * sizeof(argv[j+i+1]));
                        strcpy(argv[j], argv[j+i+1]);
                    }
                }

                argc -= (i+1);
                argv[argc] = NULL;
                argv_s[i] = NULL;

                if(DEBUG) {
                    int k;
                    printf("\n==========\n(pipe)exec: ");
                    fprintf(stderr, "p_n = %d\n", p_n);
                    for( k=0 ; argv_s[k]!=NULL ; k++ ){
                        printf(".%s", argv_s[k]);
                    }
                    printf("\n");
                }

                fork_and_exec(argv_s, p_n);
                pipe_shift();

                break;

            }
        }
        if(!is_pipe)    break;
    }

    // DEBUG
    if(DEBUG) {
        printf("\n==========\n(rest)exec: ");
        for( i=0 ; argv[i]!=NULL ; i++ ){
            printf(".%s", argv[i]);
        }
        printf("\n");
    }

    // DEBUG
    if(DEBUG) {
        for( i=0 ; i<10 ; i++ )
            if(pipe_map[i]) fprintf(stderr, "pipe_map[%d] = %d, [%d][%d]\n", i, pipe_map[i], pipe_map[i][IN], pipe_map[i][OUT]);
    }

    // bind pipe in
    int fd_in = pipe_get();
    if(fd_in)   dup2(fd_in, STDIN_FILENO);

    // handle rest
    if(DEBUG) {
        fprintf(stderr, "IN: %d\n", fd_in);
        fprintf(stderr, "OUT: -\n");
    }
    if( execvp(argv[0], argv)<0 ) {
        fprintf(stderr, "Unknown command: [%s]\n", argv[0]);
    }
    pipe_shift();
}


void client_handler(int connfd) {

    // handle (first)
    welcome_msg(connfd);
    if(!prompt(connfd)) return;

    // handle (rest)
    while(1) {
        pid_t pid;
        pid = fork();
        
        if(pid<0) {                     // if error
            fprintf(stderr, "Fork failed\n");
            exit(-1);
        } else if (pid ==0) {           // if child

            dup2(connfd, STDOUT_FILENO);    // duplicate socket on stdout
            dup2(connfd, STDERR_FILENO);    // duplicate socket on stderr
            close(connfd);                  // close connection

            command_handler();
            exit(0);

        } else {                        // if parent
            wait(NULL);
            if(!prompt(connfd)) {
                close(connfd);
                return;
            }
        }
    }
}


/*
 * Main
 */
int main(int argc, char *argv[]) {

    /* variables */
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr; 

    time_t ticks; 

    /* init */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof(serv_addr));

    /* setup */
    serv_addr.sin_family = AF_INET;                 // internet protocols (IPv4)
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // address
    serv_addr.sin_port = htons(PORT);               // port

    /* socket - bind */
    int optval = 1;
    if ( setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1 ) {
        perror("setsockopt");
    }
    bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 

    /* socket - listen */
    listen(listenfd, BACKLOG); 

    printf("start listen on port %d...\n", PORT);
    while(1) {

        /* socket - accept */
        connfd = accept(listenfd, (struct sockaddr*)NULL, NULL); 
        printf("accepted connection: %d\n", connfd);

        client_handler(connfd);

        printf("closed connection: %d\n", connfd);
        sleep(1);

    }

    return 0;
}
