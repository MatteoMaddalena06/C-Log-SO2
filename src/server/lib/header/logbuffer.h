#ifndef LOGBUFFER_H 
#define LOGBUFFER_H 

#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <netdb.h>

#define ANYTHING 0
#define DISCONNECTED true 
#define ELSE false

struct log {
    time_t timestamp;
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
    int data;
    bool disconnected;
};

typedef struct {
    struct log* data;
    unsigned long head;
    size_t size;
    pthread_mutex_t sync_mux;

} logbuffer;

logbuffer create_logbuffer();
struct log create_log(time_t, char*, char*, int, bool);
bool push_log(logbuffer*, struct log);
bool consume_logs(logbuffer*, bool(*)(struct log));
void free_logbuffer(logbuffer*);

#endif