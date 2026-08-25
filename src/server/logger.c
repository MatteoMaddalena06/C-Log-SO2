#include "lib/header/logbuffer.h"
#include "lib/header/counter.h"
#include "lib/header/thread_in.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>

extern volatile sig_atomic_t stop;

// configurazione per consumer
static struct thread_in* thread_in;
static char curr_filename[256];
FILE* logfile;  
static size_t logfile_size;
static unsigned long file_counter;

bool flush_consumer(struct log log)
{
    if(logfile == NULL || logfile_size >= thread_in->server_conf->logfile_max_size) //raggiunto max_size
    {
        if(logfile != NULL)
            fclose(logfile); 

        snprintf(curr_filename, sizeof(curr_filename), "%s/log_%ld_%ld.txt", thread_in->server_conf->logdir_pathname, time(NULL), file_counter);
        logfile = fopen(curr_filename, "wa"); //nuovo file

        if(logfile == NULL)
            return false;

        file_counter++;
        logfile_size = 0;
    }

    // scrittura log
    fprintf(logfile, (log.disconnected) ? "[%lld, %s:%s, DISCONNECT]\n" : "[%lld, %s:%s, %d]\n", log.timestamp, log.host, log.service, log.data); 
    fflush(logfile);
    logfile_size++;

    if(thread_in->server_conf->verbose_selected)
    {
        pthread_mutex_lock(thread_in->stdout_mux);
        printf((log.disconnected) ? "[%lld, %s:%s, DISCONNECT]\n" : "[%lld, %s:%s, %d]\n", log.timestamp, log.host, log.service, log.data); 
        pthread_mutex_unlock(thread_in->stdout_mux);
    }

    return true;
}

void* flush_logbuffer(void* arg)
{
    struct thread_in* in = (struct thread_in*)arg;

    thread_in = in;
    logfile = NULL;
    logfile_size = 0;
    file_counter = 0;

    while(!stop)
    {
        struct timespec req = {
            thread_in->server_conf->period / 1000000000L, 
            thread_in->server_conf->period % 1000000000L
        };

        nanosleep(&req, NULL); //aspetta period 

        if(!consume_logs(in->logbuffer, &flush_consumer)) // ripetere flush_consumer per ogni log in logbuffer
        {
            pthread_mutex_lock(in->stderr_mux);
            fprintf(stderr, "Unable to flush logbuffer in %s: %s\n", curr_filename, strerror(errno));
            pthread_mutex_unlock(in->stderr_mux);
        }
    }

    //termina il logger thread, decremento contatore di attivi
    counter_down(in->threads_count);
}