#include "lib/header/logbuffer.h"
#include "lib/header/counter.h"
#include "lib/header/thread_in.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <time.h>

extern volatile sig_atomic_t stop;

void* connection_handler(void* arg)
{
    int return_code;

    struct thread_in* in = (struct thread_in*)arg;
    char host[NI_MAXHOST], service[NI_MAXSERV]; 

    struct sockaddr_storage address;
    socklen_t address_len = sizeof(address);

    if(getpeername(in->connection_sfd, (struct sockaddr*)&address, &address_len) != -1) 
    {
        return_code = getnameinfo((struct sockaddr*)&address, address_len, host, sizeof(host),
            service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV);

        if(return_code)  
        {
            strcpy(host, "U");
            strcpy(service, "U");

            pthread_mutex_lock(in->stderr_mux);
            fprintf(stderr, "Unable to get client address: %s\n", gai_strerror(return_code));
            pthread_mutex_unlock(in->stderr_mux);
        }
    }
    else 
    {
        strcpy(host, "U");
        strcpy(service, "U");

        pthread_mutex_lock(in->stderr_mux);
        perror("Unable to get client address");
        pthread_mutex_unlock(in->stderr_mux);
    }

    while(!stop)
    {
        fd_set readfds;  
        FD_ZERO(&readfds);  
        FD_SET(in->connection_sfd, &readfds); 

        struct timeval timeout = {0, 100000};

        int return_code = select(in->connection_sfd + 1, &readfds, NULL, NULL,&timeout);
        
        if(return_code < 0) 
        {
            if(errno == EINTR)
                continue;

            pthread_mutex_lock(in->stderr_mux);
            perror("select(): %s\n");
            pthread_mutex_unlock(in->stderr_mux);

            break;
        }

        if(return_code == 0)  
            continue;

        if(FD_ISSET(in->connection_sfd, &readfds))  
        {
            int data;

            ssize_t bytes = recv(in->connection_sfd, &data, sizeof(int), 0); 
            data = ntohl(data);
            
            if(bytes == 0)  
                break;

            if(bytes < 0)  
            {
                if(errno == EINTR) 
                    continue;

                pthread_mutex_lock(in->stderr_mux);
                fprintf(stderr, "recv(): %s\n", strerror(errno));
                pthread_mutex_unlock(in->stderr_mux);

                break;
            }

            if(!push_log(in->logbuffer, create_log(time(NULL), host, service, data, ELSE)))
            {
                pthread_mutex_lock(in->stderr_mux);
                fprintf(stderr,"Unable to push log into logbuffer\n");
                pthread_mutex_unlock(in->stderr_mux);

                break;
            }
        }
    }

    close(in->connection_sfd); 

    if(!push_log(in->logbuffer, create_log(time(NULL), host, service, ANYTHING, DISCONNECTED)))
    {
        pthread_mutex_lock(in->stderr_mux);
        fprintf(stderr,"Unable to push disconnect log into logbuffer\n");
        pthread_mutex_unlock(in->stderr_mux);
    }

    pthread_mutex_lock(in->stdout_mux);
    printf("Closed connection to %s:%s\n", host, service);
    pthread_mutex_unlock(in->stdout_mux);

    counter_down(in->threads_count);  //thread termina
    free(in);  
}