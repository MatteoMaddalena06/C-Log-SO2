#include <asm-generic/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <argp.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>

#define REMOTE true 
#define LOCAL false

static volatile sig_atomic_t stop = 0;

static void handle_sigint(int sig)
{ stop = 1; }

struct user_in {
    char* server_host;
    char* server_service;
    unsigned long data_number;
    bool data_number_selected;
    bool infinite_data;
};

static int parse_opt(int key, char* arg, struct argp_state* state)
{
    struct user_in* user_in = state->input;

    switch(key)
    {
        case 'h':
            user_in->server_host = arg;
            break;

        case 's':
            user_in->server_service = arg;
            break;

        case 't':
            user_in->data_number = strtoul(arg, NULL, 10);
            user_in->data_number_selected = true;
            break;

        case 'i':
            user_in->infinite_data = true;
            break;

        default: return ARGP_ERR_UNKNOWN;     
    }

    return 0;
}

static struct addrinfo init_connection_req()
{
    struct addrinfo req;

    memset(&req, 0, sizeof req);
    req.ai_family = AF_UNSPEC;
    req.ai_socktype = SOCK_STREAM;

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

int main(int argc, char* argv[]) 
{
    int return_code;

    struct user_in user_in = {NULL, NULL, 0, false, false};

    struct argp_option options[] = {
        {"host",     'h', "HOSTNAME",    0, "Select server host name (mandatory)"},
        {"service",  's', "SERVICENAME", 0, "Select server service name (mandatory)"},
        {"times",    't', "TIMES",       0, "Select number of data transmitted data items"},
        {"infinite", 'i', NULL,          0, "Enables continuos data transmission"}, 
        {0}
    };

    struct argp argp = {options, &parse_opt};

    if(argp_parse(&argp, argc, argv, 0, NULL, &user_in))
    {
        fprintf(stderr, "Error parsing the command line\n");
        return EXIT_FAILURE;
    }

    if(user_in.server_host == NULL)
    {
        fprintf(stderr, "--host (-h) is mandatory\n");
        return EXIT_FAILURE;
    }

    if(user_in.server_service == NULL)
    {
        fprintf(stderr, "--service (-s) is mandatory\n");
        return EXIT_FAILURE;
    }

    if(!user_in.data_number_selected && !user_in.infinite_data)
    {
        fprintf(stderr, "--times (-t) is mandatory if --infinite (-i) not selected\n");
        return EXIT_FAILURE;
    }

    struct addrinfo req = init_connection_req(), *connection_confs;

    return_code = getaddrinfo(user_in.server_host, user_in.server_service, &req, &connection_confs);

    if(return_code)
    {
        fprintf(stderr, "Connection configuration error: %s\n", gai_strerror(return_code));
        return EXIT_FAILURE;;
    }

    struct addrinfo* connection_conf;
    int connection_sfd;

    for(connection_conf = connection_confs; connection_conf != NULL; connection_conf = connection_conf->ai_next) 
    {
        connection_sfd = socket(connection_conf->ai_family, connection_conf->ai_socktype, connection_conf->ai_protocol);
       
        if(connection_sfd == -1) 
        {
            perror("Socket creation error");
            fprintf(stderr, "(trying next connection configuration...)\n");
            continue;
        }

        if(connect(connection_sfd, connection_conf->ai_addr, connection_conf->ai_addrlen) == -1)
        {
            close(connection_sfd);
            perror("Unable to perform connection to the server");
            fprintf(stderr, "(trying next connection configuration...)\n");
            continue;
        }

        printf("Connection established with ");
        print_socket_address(connection_sfd, REMOTE);
        printf(" from ");
        print_socket_address(connection_sfd, LOCAL);
        printf("\n");

        break;
    }

    freeaddrinfo(connection_confs);
    
    if(connection_conf == NULL) 
    {
        fprintf(stderr, "Unable to find a working connection configuration\n");
        return EXIT_FAILURE;
    }

    printf("Connection succed\n");

    srand(time(NULL));

    if(set_sig_handler(SIGINT, &handle_sigint) < 0)
    {
        perror("Unable to set SIGINT handler");
        return EXIT_FAILURE;        
    }

    if(set_sig_handler(SIGPIPE, SIG_IGN) < 0)
    {
        perror("Unable to ignore SIGPIPE");
        return EXIT_FAILURE;        
    }

    for(unsigned long i = 0; (i < user_in.data_number || user_in.infinite_data) && !stop; i++)
    {
        int random_data = htonl(rand());

        if(send(connection_sfd, &random_data, sizeof(int), 0) == -1)
        {
            if(errno == EPIPE || errno == ECONNRESET)
            {
                printf("Server closed the connection\n");
                break;
            }

            perror("Unable to send data");
            break;
        }
            
        printf("Send %d\n", random_data);
    }
    
    close(connection_sfd);
    printf("Closing the connection with the server. Goodbye\n");

    return EXIT_SUCCESS;
}