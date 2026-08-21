#ifndef COUNTER_H
#define COUNTER_H

#include <pthread.h>

typedef struct {
    unsigned long count;
    pthread_mutex_t sync_mux;
    pthread_cond_t count_cond;

} counter;

counter create_counter();
void counter_up(counter*);
void counter_down(counter*);
void wait_until_zero(counter*);
void free_counter(counter*);

#endif