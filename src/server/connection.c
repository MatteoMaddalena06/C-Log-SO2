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
    struct thread_in* in = (struct thread_in*)arg;
    char host[NI_MAXHOST];  //per IP
    char service[NI_MAXSERV]; // per porta client

    //Recuperiamo l'indirizzo del client.
    
    struct sockaddr_storage address;
    socklen_t address_len = sizeof(address);

    //ogni client ha il suo socket in->connection_sfd
    if(getpeername(in->connection_sfd, (struct sockaddr*)&address, &address_len) != -1) 
    {
        //convertire indirizzo e porta in stringhe dentro host e service
        int return_code = getnameinfo(
            (struct sockaddr*)&address,
            address_len,
            host,
            sizeof(host),
            service,
            sizeof(service),
            NI_NUMERICHOST | NI_NUMERICSERV
        );

        // se getnameinfo() da errore
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

    while(stop != 1)
    {
        fd_set readfds;  // set di file descriptors
        FD_ZERO(&readfds);  //svuoto l'insieme
        FD_SET(in->connection_sfd, &readfds);  //aggiungo socket del client

        struct timeval timeout = {0 /*sec*/, 100000 /*ms*/};

        int ret = select(in->connection_sfd + 1, &readfds, NULL, NULL,&timeout);
        
        //val di ret: >0 c'è un socketb pronto, 0 timeout, <0 errore
        if(ret < 0) // errore di select
        {
            if(errno == EINTR)
                continue;

            pthread_mutex_lock(in->stderr_mux);
            perror("select(): %s\n");
            pthread_mutex_unlock(in->stderr_mux);

            break;
        }

        if(ret == 0)   //timeout, nessun dato disponibile, torna a controllare stop
            continue;

        if(FD_ISSET(in->connection_sfd, &readfds))  // socket è pronto per essere letto?
        {
            int data;

            ssize_t n = recv(in->connection_sfd, &data, sizeof(int), 0); //ricezione msg, n num byte ricevuti
            data = ntohl(data);
            
            if(n == 0)  // client ha chiuso connection
                break;

            if(n < 0)  //errore recv
            {
                if(errno == EINTR) // recv() è stato interrotto da un segnale
                    continue;

                pthread_mutex_lock(in->stderr_mux);
                fprintf(stderr, "recv(): %s\n", strerror(errno));
                pthread_mutex_unlock(in->stderr_mux);

                break;
            }

            // mettere log nel buffer.
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
    free(in);  //Liberiamo la memoria allocata dal main per questo thread.
}