#include "lib/header/counter.h"
#include "lib/header/logbuffer.h"
#include <pthread.h>
#include <unistd.h>

struct thread_in {
    struct server_conf* server_conf;
    int connection_sfd;
    counter* threads_count;
    logbuffer* logbuffer;
    pthread_mutex_t* stdout_mux;
    pthread_mutex_t* stderr_mux;
};

void* connection_handler(void* thread_in)
{
    struct thread_in* in = (struct thread_in*)thread_in;
    close(in->connection_sfd);
    counter_down(in->threads_count);
}