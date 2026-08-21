#include "lib/header/logbuffer.h"
#include "lib/header/counter.h"
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

#define STD_PERIOD 500
#define STD_LISTEN_QUEUE_SIZE 10
#define STD_MAX_LOFGILE_SIZE 200
#define STD_LOGDIR_PATHNAME "."
#define LOCAL false 
#define REMOTE true

extern void* connection_handler(void*);
extern void* flush_logbuffer(void*);
extern bool  flush_consumer(struct log);

volatile sig_atomic_t stop = 0;

static void handle_sigint(int sig)
{ stop = 1; }

struct server_conf {
    char* service;
    unsigned long period;
    size_t listen_queue_size;
    bool verbose_selected;
    size_t logfile_max_size;
    char* logdir_pathname;
};

struct thread_in {
    struct server_conf* server_conf;
    int connection_sfd;
    counter* threads_count;
    logbuffer* logbuffer;
    pthread_mutex_t* stdout_mux;
    pthread_mutex_t* stderr_mux;
};

static struct addrinfo init_connection_req()
{
    struct addrinfo req;

    memset(&req, 0, sizeof(req));
    req.ai_family = AF_INET;
    req.ai_socktype = SOCK_STREAM;
    req.ai_flags = AI_PASSIVE;

    return req;
}

static int set_sig_handler(int sig, void (*handler)(int))
{
    struct sigaction signal_action;

    memset(&signal_action, 0, sizeof(signal_action));
    signal_action.sa_handler = handler;
    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = 0;

    if(sigaction(sig, &signal_action, NULL) == -1)
        return -1;

    return 0;
}  

static void print_socket_address(int sock_fd, bool remote)
{
    struct sockaddr_storage address;
    socklen_t addrlen = sizeof(address);

    if(!remote && getsockname(sock_fd, (struct sockaddr* )&address, &addrlen) == -1)
    {
        perror("...unable to get socket local address");
        return;
    }
    else if(remote && getpeername(sock_fd, (struct sockaddr *)&address, &addrlen) == -1)
    {
        perror("...unable to get socket remote address");
        return;
    }

    char host[NI_MAXHOST], service[NI_MAXSERV];
    int return_code = getnameinfo((struct sockaddr *)&address, addrlen, host, sizeof(host), service, sizeof(service), 
        NI_NUMERICHOST | NI_NUMERICSERV);

    if(!return_code)
        printf("%s:%s", host, service);

    else
        fprintf(stderr, "...unable to get socket address: %s\n", gai_strerror(return_code));
}

static int parse_opt(int key, char* arg, struct argp_state* state)
{
    struct server_conf* conf = state->input;

    switch(key)
    {
        case 's':
            conf->service = arg;
            break;

        case 'p':
            conf->period = atoi(arg);
            break; 

        case 'l':
            conf->listen_queue_size = atoi(arg);
            break;

        case 'v':
            conf->verbose_selected = true;
            break;

        case 'm':
            conf->logfile_max_size = atoi(arg);
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

    struct addrinfo req = init_connection_req(), *connection_confs;

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
    else 
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
    printf("Forcing the closure of connections with the client\n");
    pthread_mutex_unlock(&stdout_mux);

    wait_until_zero(&threads_count);

    printf("All clients disconnected\nLogger terminated\n");

    printf("Forcing logs flush\n");
    consume_logs(&logbuffer, &flush_consumer);
    printf("Exit\n");

    pthread_mutex_destroy(&stdout_mux);
    pthread_mutex_destroy(&stderr_mux);
    free_logbuffer(&logbuffer);
    free_counter(&threads_count);

    return EXIT_SUCCESS;
}