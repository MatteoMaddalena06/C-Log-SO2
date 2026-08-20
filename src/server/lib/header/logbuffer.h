#ifndef LOGBUFFER_H 
#define LOGBUFFER_H 

#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <netdb.h>

struct log {
    time_t timestamp;
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
    int data;
};

typedef struct {
    struct log* data;
    unsigned long head;
    size_t size;
    pthread_mutex_t sync_mux;

} logbuffer;

logbuffer create_logbuffer();
bool push_log(logbuffer*, struct log);
void consume_logs(logbuffer*, void(*)(struct log));
void free_logbuffer(logbuffer*);
#endif