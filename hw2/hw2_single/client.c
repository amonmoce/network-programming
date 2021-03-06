#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <fcntl.h>

#include "constant.h"
#include "pipe.h"
#include "fork_exec.h"
#include "client.h"
#include "clients.h"
#include "mytype.h"
#include "variable.h"
#include "env.h"
#include "broadcast.h"
#include "fifo_lock.h"
#include "fifo.h"

/*
 * Globals
 */
char send_buff[SIZE_SEND_BUFF];
char read_buff[SIZE_READ_BUFF];
char original_read_buff[SIZE_READ_BUFF];

char **argv;
int argc = 0;

/*
 * Shell
 */
char **command_decode(char *command) {

    char *token = " \t\n\r";
    char *p = strtok(command, token);

    argc = 0;
    while(p) {
        argc ++;
        argv = realloc(argv, sizeof(char *) * argc);

        if(argv == NULL)    exit(EXIT_FAILURE);

        argv[argc-1] = p;
        p = strtok(NULL, token);
    }

    // for the last extra one
    argv = realloc(argv, sizeof(char *) * (argc+1));
    argv[argc] = NULL;

    return argv;

}

void socket_error_message(int connfd) {
    memset(send_buff, 0, sizeof(send_buff)); 
    snprintf(send_buff, sizeof(send_buff), "Invalid Inputs.\n");
    write(connfd, send_buff, strlen(send_buff)); 
}

void setenv_helper(int connfd) {
    if(argc != 3) {
        socket_error_message(connfd);
        return;
    }
    setenv(argv[1], argv[2], TRUE);
    env_save(clients_get_id_from_socket(connfd));
}

void printenv_helper(int connfd) {

    if(argc != 2) {
        socket_error_message(connfd);
        return;
    }

    char *r;
    r = getenv(argv[1]);

    memset(send_buff, 0, sizeof(send_buff)); 
    if(r)   snprintf(send_buff, sizeof(send_buff), "%s=%s\n", argv[1], r);
    else    snprintf(send_buff, sizeof(send_buff), "Variable Not Found.\n");
    write(connfd, send_buff, strlen(send_buff)); 

}

int read_helper(int connfd, char *buf) {
    int count = 0;
    char c;
    while(1) {
        if(!read(connfd, &c, 1))  break;
        buf[count++] = c;
        if(c == '\n')   break;
    }
    buf[count] = '\0';
    return count;
}

void cmd_who(int connfd) {

    char content[SIZE_SEND_BUFF] = "<ID>\t<nickname>\t<IP/port>\t<indicate me>\n";
    char t_s[TMP_STRING_SIZE];

    int i;
    Client *c;

    for( i=0 ; i<CLIENT_MAX_NUM ; i++ ) {

        c = clients_get(i);
        if(c->valid == FALSE)   continue;

        sprintf(t_s, "%d\t%s\t%s/%d", i+1, c->name, c->ip, c->port);

        if(c->socket == connfd) {
            strcat(t_s, "\t<-me\n");
        } else {
            strcat(t_s, "\n");
        }

        strcat(content, t_s);

    }

    snprintf(send_buff, sizeof(send_buff), content);
    write(connfd, send_buff, strlen(send_buff)); 

}

void cmd_name(int connfd, char *name) {

    // if already exisits
    int i;
    Client *c;

    for( i=0 ; i<CLIENT_MAX_NUM ; i++ ) {
        c = clients_get(i);
        if(strcmp(c->name, name) == 0) {

            sprintf(send_buff, "*** User '%s' already exists. ***\n", name);
            write(connfd, send_buff, strlen(send_buff)); 

            return;
        }
    }

    c = clients_get_from_socket(connfd);
    strcpy(c->name, name);

    broadcast_cmd_name(connfd);
}

void cmd_yell(int connfd, char *buff) {
    broadcast_cmd_yell(connfd, buff);
}

void cmd_tell(int connfd, int target_id, char *buff) {

    Client *c = clients_get(target_id);

    char valid = c->valid;

    if(!valid) {

        sprintf(send_buff, "*** Error: user #%d does not exist yet. ***\n", target_id+1);
        write(connfd, send_buff, strlen(send_buff)); 

    } else {

        broadcast_cmd_tell(connfd, target_id, buff);

    }

}

void print_prompt_sign(int connfd) {

    memset(send_buff, 0, SIZE_SEND_BUFF); 
    snprintf(send_buff, SIZE_SEND_BUFF, "%% ");
    write(connfd, send_buff, strlen(send_buff)); 

}

int prompt(int connfd) {

    int r = 0;

    memset(read_buff, 0, SIZE_READ_BUFF); 

    // r = read(connfd, read_buff, SIZE_READ_BUFF);
    r = read_helper(connfd, read_buff);
    if(r == 1)  return COMMAND_HANDLED;

    strcpy(original_read_buff, read_buff);

    argv = command_decode(read_buff);
    if(strcmp(argv[0], "exit") == 0)  return 0;   // same as end
    if(strcmp(argv[0], "setenv") == 0) {
        setenv_helper(connfd);
        return COMMAND_HANDLED;
    }
    if(strcmp(argv[0], "printenv") == 0) {
        printenv_helper(connfd);
        return COMMAND_HANDLED;
    }
    if(strcmp(argv[0], "who") == 0) {
        cmd_who(connfd);
        return COMMAND_HANDLED;
    }
    if(strcmp(argv[0], "name") == 0) {
        cmd_name(connfd, argv[1]);
        return COMMAND_HANDLED;
    }
    if(strcmp(argv[0], "yell") == 0) {
        cmd_yell(connfd, original_read_buff);
        return COMMAND_HANDLED;
    }
    if(strcmp(argv[0], "tell") == 0) {
        cmd_tell(connfd, atoi(argv[1])-1, original_read_buff);
        return COMMAND_HANDLED;
    }

    return r;

}

void welcome_msg(int connfd) {

    char *msg = "****************************************\n\
** Welcome to the information server. **\n\
****************************************\n";
    snprintf(send_buff, sizeof(send_buff), msg);
    write(connfd, send_buff, strlen(send_buff)); 

}

void debug_print_command(char** argv_s, int p_n) {
    int k;
    fprintf(stderr, "\n==========\n(pipe)exec: ");
    fprintf(stderr, "p_n = %d\n", p_n);
    for( k=0 ; argv_s[k]!=NULL ; k++ ){
        fprintf(stderr, ".%s", argv_s[k]);
    }
    fprintf(stderr, "\n");
}

/*
 * Handler (command, client)
 */
// helper tool
char **extract_command(int len) {

    int j;
    char **argv_s = malloc(sizeof(char *) * (len));

    for( j=0 ; j<argc ; j++ ) {
        // move to sub argv, which will be exec
        if( j<len ) {
            argv_s[j] = malloc(sizeof(char) * sizeof(argv[j]));
            strcpy(argv_s[j], argv[j]);
        }

        // shift argv
        if( j<(argc-len-1) ) {
            argv[j] = malloc(sizeof(char) * sizeof(argv[j+len+1]));
            strcpy(argv[j], argv[j+len+1]);
        }
    }

    argc -= (len+1);
    argv[argc] = NULL;
    argv_s[len] = NULL;

    return argv_s;

}

void command_handler(int connfd) {

    int i;
    int is_pipe;

    int client_id = clients_get_id_from_socket(connfd);

    while(1) {

        is_pipe = FALSE;

        for( i=0 ; i<argc ; i++ ) {

            if(argv[i] && argv[i][0] == '|') {

                int p_n = 1;
                if(strlen(argv[i]) == 1)  p_n = 1;
                else    sscanf(argv[i], "|%d", &p_n);

                is_pipe = TRUE;

                //
                int r;
                char **argv_s = extract_command(i);

                if(DEBUG)   debug_print_command(argv_s, p_n);

                if( (r=fork_and_exec_pipe(connfd, argv_s, p_n)) == EXIT_FAILURE) {
                    return;
                } else if (r != SKIP_SHIFT) {
                    pipe_shift(client_id);
                }

                break;

            }

            if(argv[i] && strlen(argv[i])==1 && argv[i][0] == '>') {

                char *filepath = argv[i+1];
                char **argv_s = extract_command(i);
                if(!filepath)   fprintf(stderr, "filepath error\n");
                if( fork_and_exec_file(connfd, argv_s, filepath) == EXIT_FAILURE ) {
                    return;
                }

                pipe_shift(client_id);
                return;

            }

            if(argv[i] && strlen(argv[i])!=1 && argv[i][0] == '<' && argc == i+1) {

                char **argv_s = extract_command(i);
                int source_id;
                sscanf(argv[i], "<%d", &source_id);

                source_id--;

                if(!fifo_lock_get(source_id, clients_get_id_from_socket(connfd))) {
                    sprintf(send_buff, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", source_id+1, clients_get_id_from_socket(connfd)+1);
                    write(connfd, send_buff, strlen(send_buff)); 
                    return;
                }

                if(!check_client_exist(source_id)) {
                    sprintf(send_buff, "*** Error: user #%d does not exist yet. ***\n", source_id+1);
                    write(connfd, send_buff, strlen(send_buff)); 
                    return;
                }

                if( fork_and_exec_fifo_in(connfd, argv_s, source_id) == EXIT_FAILURE ) {
                    return;
                } else {
                    fifo_lock_set(source_id, clients_get_id_from_socket(connfd), FALSE);
                    broadcast_cmd_fifo_in(connfd, source_id, original_read_buff);
                }

                return;

            }

            if(argv[i] && strlen(argv[i])!=1 && argv[i][0] == '>' && argc == i+1) {

                char **argv_s = extract_command(i);
                int target_id;
                sscanf(argv[i], ">%d", &target_id);

                target_id --;

                if(fifo_lock_get(clients_get_id_from_socket(connfd), target_id)) {
                    sprintf(send_buff, "*** Error: the pipe #%d->#%d already exists. ***\n", clients_get_id_from_socket(connfd)+1, target_id+1);
                    write(connfd, send_buff, strlen(send_buff)); 
                    return;
                }

                if(!check_client_exist(target_id)) {
                    sprintf(send_buff, "*** Error: user #%d does not exist yet. ***\n", target_id+1);
                    write(connfd, send_buff, strlen(send_buff)); 
                    return;
                }

                if( fork_and_exec_fifo_out(connfd, argv_s, target_id) == EXIT_FAILURE ) {
                    return;
                } else {
                    fifo_lock_set(clients_get_id_from_socket(connfd), target_id, TRUE);
                    broadcast_cmd_fifo_out(connfd, target_id, original_read_buff);
                }

                return;

            }

            if(argv[i] && strlen(argv[i])!=1 && (argv[i][0] == '<' || argv[i][0] == '>') && argc != i+1) {

                int target_id, source_id;

                // get target_id, source_id
                if(argv[i][0] == '<') {
                    if(sscanf(argv[i], "<%d", &source_id) != 1) {
                        perror("sscanf");
                        return;
                    }
                    if(sscanf(argv[i+1], ">%d", &target_id) != 1) {
                        perror("sscanf");
                        return;
                    }
                } else {
                    if(sscanf(argv[i], ">%d", &target_id) != 1) {
                        perror("sscanf");
                        return;
                    }
                    if(sscanf(argv[i+1], "<%d", &source_id) != 1) {
                        perror("sscanf");
                        return;
                    }
                }

                if(DEBUG)   fprintf(stderr, "source_id=%d\ttarget_id=%d\n", source_id, target_id);

                target_id--, source_id--;

                char **argv_s = extract_command(i);

                if(!fifo_lock_get(source_id, clients_get_id_from_socket(connfd))) {
                    sprintf(send_buff, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", source_id+1, clients_get_id_from_socket(connfd)+1);
                    write(connfd, send_buff, strlen(send_buff)); 
                    return;
                }

                if(fifo_lock_get(clients_get_id_from_socket(connfd), target_id)) {
                    sprintf(send_buff, "*** Error: the pipe #%d->#%d already exists. ***\n", clients_get_id_from_socket(connfd)+1, target_id+1);
                    write(connfd, send_buff, strlen(send_buff)); 
                    return;
                }

                if(!check_client_exist(source_id)) {
                    sprintf(send_buff, "*** Error: user #%d does not exist yet. ***\n", source_id+1);
                    write(connfd, send_buff, strlen(send_buff)); 
                    return;
                }

                if(!check_client_exist(target_id)) {
                    sprintf(send_buff, "*** Error: user #%d does not exist yet. ***\n", target_id+1);
                    write(connfd, send_buff, strlen(send_buff)); 
                    return;
                }

                if( fork_and_exec_fifo_in_out(connfd, argv_s, source_id, target_id) == EXIT_FAILURE ) {
                    return;
                } else {

                    fifo_lock_set(source_id, clients_get_id_from_socket(connfd), FALSE);
                    fifo_lock_set(clients_get_id_from_socket(connfd), target_id, TRUE);

                    broadcast_cmd_fifo_in(connfd, source_id, original_read_buff);
                    broadcast_cmd_fifo_out(connfd, target_id, original_read_buff);

                }

                return;
            }

        }

        if(!is_pipe)    break;

    }

    if(argc == 0)   return;
    if(fork_and_exec_last(connfd, argv) == EXIT_SUCCESS) {
        pipe_shift(client_id);
    }

}

// handle one socket connection
int client_handler(int connfd) {

    int r = prompt(connfd);

    if(!r)  return CLIENT_END;

    if(r != COMMAND_HANDLED) {
        command_handler(connfd);
    }
    print_prompt_sign(connfd);
    return CLIENT_CONT;

}
