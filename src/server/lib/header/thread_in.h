#ifndef THREAD_IN 
#define THREAD_IN 

#include "logbuffer.h"
#include "counter.h"

#define STD_PERIOD 500000000
#define STD_LISTEN_QUEUE_SIZE 10
#define STD_MAX_LOFGILE_SIZE 200
#define STD_LOGDIR_PATHNAME "."

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

#endif