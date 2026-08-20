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

#define STD_PERIOD 500
#define STD_LISTEN_QUEUE_SIZE 10
#define LOCAL false 
#define REMOTE true

extern void* connection_handler(void*);
extern void* flush_logbuffer(void*);
extern void  flush_consumer(struct log);

static volatile sig_atomic_t stop = 0;

static void handle_sigint(int sig)
{ stop = 1; }

struct server_conf {
    char* service;
    double period;
    unsigned int listen_queue_size;
    bool verbose_selected;
};

struct thread_in {
    struct server_conf* server_conf;
    int connection_sfd;
    counter* threads_count;
    logbuffer* logbuffer;
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
            conf->period = strtod(arg, NULL);
            break; 

        case 'l':
            conf->listen_queue_size = atoi(arg);
            break;

        case 'v':
            conf->verbose_selected = true;
            break;

        default: return ARGP_ERR_UNKNOWN;     
    }

    return 0;
}

int main(int argc, char** argv)
{
    struct server_conf server_conf = {NULL, STD_PERIOD, STD_LISTEN_QUEUE_SIZE, false};

    struct argp_option options[] = {
        {"service", 's', "PORT NUMBER", 0, "Select the server port number (mandatory)"},
        {"period",  'p', "TIME(ms)",    0, "select the log writing period (std period 500ms)"},
        {"lenght",  'l', "LENGHT",      0, "select the listen queue size"},
        {"verbose", 'v',  NULL,         0, "force the log to stdout"},
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

    if(server_conf.period <= 0)
    {
        fprintf(stderr, "%f is an invalid writing period (should be greater than 0)\n", server_conf.period);
        return EXIT_FAILURE;
    }

    struct addrinfo req = init_connection_req(), *connection_confs;

    int return_code = getaddrinfo(NULL, server_conf.service, &req, &connection_confs);

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

    logbuffer logbuffer = create_logbuffer();
    struct thread_in log_thread_in = {.server_conf = &server_conf, .logbuffer = &logbuffer};
    pthread_t log_thread;

    if(pthread_create(&log_thread, NULL, &flush_logbuffer, &log_thread_in ))
    {
        perror("Unable to create the flush buffer thread (forcing logs to stdout)");
        server_conf.verbose_selected = true;
    }
    else 
        pthread_detach(log_thread);

    counter threads_count = create_counter();

    while(!stop)
    {
        int connection_sfd = accept(listen_sfd, NULL, 0);

        if(connection_sfd == -1)
        {
            if (errno != EINTR)
                perror("Unable to accept an incoming connection");

            continue; 
        }

        struct thread_in* conn_thread_in = (struct thread_in*)malloc(sizeof(struct thread_in));

        if(conn_thread_in == NULL)
        {
            close(connection_sfd);
            perror("Incoming connection refused due to thread memory allocation error");
            continue;
        }

        conn_thread_in->server_conf = &server_conf;
        conn_thread_in->connection_sfd = connection_sfd;
        conn_thread_in->threads_count = &threads_count;
        conn_thread_in->logbuffer = &logbuffer;

        counter_up(&threads_count);

        pthread_t thread;

        if(pthread_create(&thread, NULL, &connection_handler, conn_thread_in))
        {
            counter_down(&threads_count);
            free(conn_thread_in);
            perror("Unable to create the connection handler thread");
            continue;
        };

        pthread_detach(thread);

        printf("Accepted connection from ");
        print_socket_address(connection_sfd, REMOTE);
        printf("\n");
    }

    close(listen_sfd);
    printf("\nListener closed\n");

    printf("Forcing the closure of connections with the client\n");
    wait_until_zero(&threads_count);
    printf("All clients disconnected\n");

    printf("Forcing logs flush");
    consume_logs(&logbuffer, &flush_consumer);
    printf("Exit");

    return EXIT_SUCCESS;
}