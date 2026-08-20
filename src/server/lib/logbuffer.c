#include "header/logbuffer.h"
#include <string.h>

logbuffer create_logbuffer()
{
    logbuffer logbuffer;

    logbuffer.data = (struct log*)malloc(sizeof(struct log));
    logbuffer.head = 0;
    logbuffer.size = 1; 
    pthread_mutex_init(&logbuffer.sync_mux, NULL);

    return logbuffer;
}

bool push_log(logbuffer* logbuffer, struct log log)
{
    pthread_mutex_lock(&logbuffer->sync_mux);

    logbuffer->data[logbuffer->head] = log;
    logbuffer->head++;

    if(logbuffer->head >= logbuffer->size)
    {
        struct log* tmp_ptr = 
            (struct log*)realloc(logbuffer->data, logbuffer->size * 2 * sizeof(struct log));

        if(tmp_ptr == NULL)
        {
            pthread_mutex_unlock(&logbuffer->sync_mux);
            return false;
        }

        logbuffer->data = tmp_ptr;
        logbuffer->size *= 2;
    }

    pthread_mutex_unlock(&logbuffer->sync_mux);

    return true;
}

void consume_logs(logbuffer* logbuffer, void(*consumer)(struct log ))
{
    pthread_mutex_lock(&logbuffer->sync_mux);

    for(unsigned long i = 0; i < logbuffer->head; i++)
        consumer(logbuffer->data[i]);

    logbuffer->head = 0;

    pthread_mutex_unlock(&logbuffer->sync_mux);
}

void free_logbuffer(logbuffer* logbuffer)
{
    free(logbuffer->data);
    logbuffer->data = NULL;
    logbuffer->head = -1;
    logbuffer->size = 0;
}