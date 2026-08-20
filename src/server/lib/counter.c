#include "header/counter.h"
#include <pthread.h>

counter create_counter()
{
    counter count;

    count.count = 0;
    pthread_mutex_init(&count.sync_mux, NULL);
    pthread_cond_init(&count.count_cond, NULL);

    return count;
}

void counter_up(counter* count)
{
    pthread_mutex_lock(&count->sync_mux);
    count->count++;
    pthread_mutex_unlock(&count->sync_mux);
}

void counter_down(counter* count)
{
    pthread_mutex_lock(&count->sync_mux);
    count->count--;
    pthread_mutex_unlock(&count->sync_mux);
}

void wait_until_zero(counter* count)
{
    pthread_mutex_lock(&count->sync_mux);

    while(count->count)
        pthread_cond_wait(&count->count_cond, &count->sync_mux);

    pthread_mutex_unlock(&count->sync_mux);
}

void notify_on_zero(counter* count)
{
    pthread_mutex_lock(&count->sync_mux);

    if(!count->count)
        pthread_cond_signal(&count->count_cond);

    pthread_mutex_unlock(&count->sync_mux);
}

void free_counter(counter* count)
{
    count->count = 0;
    pthread_mutex_destroy(&count->sync_mux);
    pthread_cond_destroy(&count->count_cond);
}
