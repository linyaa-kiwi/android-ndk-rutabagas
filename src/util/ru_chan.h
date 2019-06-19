#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>

#include "ru_queue.h"

typedef struct RuChan {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    RuQueue queue;
} RuChan;

void ru_chan_init(RuChan *ch, size_t elem_size, size_t init_capacity);
void ru_chan_finish(RuChan *ch);

void ru_chan_push(RuChan *ch, void *elem);
void ru_chan_pop_wait(RuChan *ch, void *elem);
bool ru_chan_pop_nowait(RuChan *ch, void *elem) _must_use_result_;

void ru_chan_push_locked(RuChan *ch, void *elem);
void ru_chan_pop_wait_locked(RuChan *ch, void *elem);
bool ru_chan_pop_nowait_locked(RuChan *ch, void *elem) _must_use_result_;
