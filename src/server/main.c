#include "../shared/shared.h"
#include "lib/header/logbuffer.h"
#include "lib/header/counter.h"
#include "lib/header/thread_in.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <argp.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>

extern void* connection_handler(void*);
extern void* flush_logbuffer(void*);
extern bool  flush_consumer(struct log);
extern FILE* logfile;

volatile sig_atomic_t stop = 0;

static void handle_sigint(int sig)
{ stop = 1; }

static int parse_opt(int key, char* arg, struct argp_state* state)
{
    struct server_conf* conf = state->input;

    switch(key)
    {
        case 's':
            conf->service = arg;
            break;

        case 'p':
            conf->period = strtoul(arg, NULL, 10);
            break; 

        case 'l':
            conf->listen_queue_size = strtoul(arg, NULL, 10);
            break;

        case 'v':
            conf->verbose_selected = true;
            break;

        case 'm':
            conf->logfile_max_size = strtoul(arg, NULL, 10);
            break;

        case 'd':
            conf->logdir_pathname = arg;
            break;

        default: return ARGP_ERR_UNKNOWN;     
    }

    return 0;
}

int main(int argc, char** argv)
{
    int return_code;

    struct server_conf server_conf = {
        NULL, 
        STD_PERIOD, 
        STD_LISTEN_QUEUE_SIZE, 
        false,
        STD_MAX_LOFGILE_SIZE,
        STD_LOGDIR_PATHNAME
    };

    struct argp_option options[] = {
        {"service",   's', "PORT NUMBER", 0, "Select the server port number (mandatory)"},
        {"period",    'p', "TIME(ns)",    0, "Select the log writing period (std period 500ms)"},
        {"lenght",    'l', "LENGHT",      0, "Select the listen queue size (std lenght 10)"},
        {"verbose",   'v',  NULL,         0, "Force the log to stdout"},
        {"maxsize",   'm', "SIZE",        0, "Select the maximum logs file size (std maxsize 200)"},
        {"directory", 'd', "PATHNAME",    0, "Select the log directory pathname (std pathname \".\")"},
        {0}
    };

    struct argp argp = {options, &parse_opt};

    if(argp_parse(&argp, argc, argv, 0, NULL, &server_conf))
    {
        fprintf(stderr, "Error parsing the command line\n");
        return EXIT_FAILURE;
    }

    if(server_conf.service == NULL)
    {
        fprintf(stderr, "--service (-s) option is mandatory\n");
        return EXIT_FAILURE;
    }

    struct addrinfo req = init_connection_req(SERVER), *connection_confs;

    return_code = getaddrinfo(NULL, server_conf.service, &req, &connection_confs);

    if(return_code != 0)
    {
        fprintf(stderr, "Connection configuration error: %s\n", gai_strerror(return_code));
        return EXIT_FAILURE;
    }

    struct addrinfo* connection_conf;
    int listen_sfd, yes = 1;

    for(connection_conf = connection_confs; connection_conf != NULL; connection_conf = connection_conf->ai_next)
    {
        listen_sfd = socket(connection_conf->ai_family, connection_conf->ai_socktype, connection_conf->ai_protocol);
    
        if(listen_sfd == -1)
        {
            perror("Socket creation error");
            fprintf(stderr, "(trying next connection configuration...)\n");
            continue;
        }

        if(setsockopt(listen_sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
        {
            perror("Unable to enable address reuse on the socket");
            fprintf(stderr, " (trying next connection configuration...)\n");
            close(listen_sfd);
            continue;
        }

        if(bind(listen_sfd, connection_conf->ai_addr, connection_conf->ai_addrlen) == -1)
        {
            perror("Socket address binding error");
            fprintf(stderr, " (trying next connection configuration...)\n");
            close(listen_sfd);
            continue;
        }

        break;
    }

    freeaddrinfo(connection_confs);

    if(connection_conf == NULL)
    {
        fprintf(stderr, "Unable to find a working connection configuration\n");
        return EXIT_FAILURE;
    }

    if(listen(listen_sfd, server_conf.listen_queue_size) == -1)
    {
        perror("Listen error");
        return EXIT_FAILURE;
    }

    printf("Listening on ");
    print_socket_address(listen_sfd, LOCAL);
    printf("\n");

    if(set_sig_handler(SIGINT, &handle_sigint) < 0)
    {
        perror("Unable to set SIGINT handler");
        return EXIT_FAILURE;        
    }

    pthread_mutex_t stdout_mux, stderr_mux;
    pthread_mutex_init(&stdout_mux, NULL);
    pthread_mutex_init(&stderr_mux, NULL);
    logbuffer logbuffer = create_logbuffer();
    counter threads_count = create_counter();

    struct thread_in log_thread_in = {
        .server_conf = &server_conf, 
        .threads_count = &threads_count,
        .logbuffer = &logbuffer, 
        .stdout_mux = &stdout_mux,
        .stderr_mux = &stderr_mux
    };

    counter_up(&threads_count);

    pthread_t log_thread;
    return_code = pthread_create(&log_thread, NULL, &flush_logbuffer, &log_thread_in);

    if(return_code)
    {
        counter_down(&threads_count);
        pthread_mutex_destroy(&stdout_mux);
        pthread_mutex_destroy(&stderr_mux);
        free_logbuffer(&logbuffer);
        free_counter(&threads_count);
        close(listen_sfd);
        fprintf(stderr, "Unable to create the logger thread: %s\n", strerror(return_code));
        return EXIT_FAILURE;
    }
   
    pthread_detach(log_thread);

    while(!stop)
    {
        int connection_sfd = accept(listen_sfd, NULL, 0);

        if(connection_sfd == -1)
        {
            if (errno != EINTR)
            {
                pthread_mutex_lock(&stderr_mux);
                perror("Unable to accept an incoming connection");
                pthread_mutex_unlock(&stderr_mux);
            }

            continue; 
        }

        struct thread_in* conn_thread_in = (struct thread_in*)malloc(sizeof(struct thread_in));

        if(conn_thread_in == NULL)
        {
            close(connection_sfd);
            pthread_mutex_lock(&stderr_mux);
            perror("Incoming connection refused due to thread memory allocation error");
            pthread_mutex_unlock(&stderr_mux);
            continue;
        }

        conn_thread_in->server_conf = &server_conf;
        conn_thread_in->connection_sfd = connection_sfd;
        conn_thread_in->threads_count = &threads_count;
        conn_thread_in->logbuffer = &logbuffer;
        conn_thread_in->stdout_mux = &stdout_mux;
        conn_thread_in->stderr_mux = &stderr_mux;

        counter_up(&threads_count);

        pthread_t thread;
        return_code = pthread_create(&thread, NULL, &connection_handler, conn_thread_in);

        if(return_code)
        {
            counter_down(&threads_count);
            close(connection_sfd);
            free(conn_thread_in);
            pthread_mutex_lock(&stderr_mux);
            fprintf(stderr, "Unable to create the connection handler thread: %s\n", strerror(return_code));
            pthread_mutex_unlock(&stderr_mux);
            continue;
        };

        pthread_detach(thread);
        
        pthread_mutex_lock(&stdout_mux);
        printf("Accepted connection from ");
        print_socket_address(connection_sfd, REMOTE);
        printf("\n");
        pthread_mutex_unlock(&stdout_mux);
    }

    close(listen_sfd);
    pthread_mutex_lock(&stdout_mux);
    printf("\nListener closed\n");
    pthread_mutex_unlock(&stdout_mux);

    pthread_mutex_lock(&stdout_mux);
    printf("Waiting the closure of connections with the client\n");
    pthread_mutex_unlock(&stdout_mux);

    wait_until_zero(&threads_count);

    printf("All clients disconnected\nLogger terminated\n");

    printf("Forcing logs flush\n");
    consume_logs(&logbuffer, &flush_consumer);
    printf("Exit\n");

    if(logfile != NULL)
        fclose(logfile);

    pthread_mutex_destroy(&stdout_mux);
    pthread_mutex_destroy(&stderr_mux);
    free_logbuffer(&logbuffer);
    free_counter(&threads_count);

    return EXIT_SUCCESS;
}