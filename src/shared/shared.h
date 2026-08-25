#ifndef SHARED_H
#define SHARED_H

#include <netdb.h>
#include <stdbool.h>

#define REMOTE  true 
#define LOCAL   false
#define CLIENT  false 
#define SERVER  true

struct addrinfo init_connection_req(bool);
int set_sig_handler(int, void(*)(int));
void print_socket_address(int, bool);

#endif