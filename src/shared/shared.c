#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

struct addrinfo init_connection_req(bool server)
{
    struct addrinfo req;

    memset(&req, 0, sizeof req);
    req.ai_family = AF_UNSPEC;
    req.ai_socktype = SOCK_STREAM;

    if(server)
        req.ai_flags = AI_PASSIVE;

    return req;
}

int set_sig_handler(int sig, void (*handler)(int))
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

void print_socket_address(int sock_fd, bool remote)
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