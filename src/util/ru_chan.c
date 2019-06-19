#include <assert.h>

#include "alloc.h"
#include "check.h"
#include "log.h"
#include "macros.h"

#include "ru_chan.h"
#include "ru_queue.h"
#include "ru_thread.h"

void
ru_chan_init(RuChan *ch, size_t elem_size, size_t init_capacity) {
    if (pthread_mutex_init(&ch->mutex, NULL))
        abort();

    if (pthread_cond_init(&ch->cond, NULL))
        abort();

    ru_queue_init(&ch->queue, elem_size, init_capacity);
}

void
ru_chan_finish(RuChan *ch) {
    pthread_mutex_destroy(&ch->mutex);
    pthread_cond_destroy(&ch->cond);
    ru_queue_finish(&ch->queue);
}

void
ru_chan_push(RuChan *ch, void *elem) {
    ru_mutex_lock_scoped(&ch->mutex);
    ru_chan_push_locked(ch, elem);
}

void
ru_chan_push_locked(RuChan *ch, void *elem) {
    ru_queue_push(&ch->queue, elem);

    if (pthread_cond_broadcast(&ch->cond))
        abort();
}

// Blocks until the queue is non-empty.
void
ru_chan_pop_wait(RuChan *ch, void *elem) {
    ru_mutex_lock_scoped(&ch->mutex);
    ru_chan_pop_wait_locked(ch, elem);
}

// Blocks until the queue is non-empty.
void
ru_chan_pop_wait_locked(RuChan *ch, void *elem) {
    while (ru_queue_is_empty(&ch->queue)) {
        if (pthread_cond_wait(&ch->cond, &ch->mutex)) {
            abort();
        }
    }

    (void) ru_queue_pop(&ch->queue, elem);
}

// Return false if queue is empty.
bool
ru_chan_pop_nowait(RuChan *ch, void *elem) {
    ru_mutex_lock_scoped(&ch->mutex);
    return ru_chan_pop_nowait_locked(ch, elem);
}

// Return false if queue is empty.
bool
ru_chan_pop_nowait_locked(RuChan *ch, void *elem) {
    if (ru_queue_is_empty(&ch->queue))
        return false;

    return ru_queue_pop(&ch->queue, elem);
}
