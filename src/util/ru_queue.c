#include "alloc.h"
#include "check.h"
#include "log.h"
#include "macros.h"
#include "ru_math.h"

#include "ru_queue.h"
#include "ru_thread.h"

#define RU_QUEUE_INIT_CAPACITY 8

static inline void
ru_queue_check(RuQueue *q) {
    assert(q->elems != NULL);
    assert(q->elem_size > 0);
    assert(q->mod > 1);
    assert(q->head < q->mod);
    assert(q->tail < q->mod);
}

void
ru_queue_init(RuQueue *q, size_t elem_size, size_t init_capacity) {
    assert(elem_size > 0);

    size_t mod;
    if (__builtin_add_overflow(init_capacity, 1, &mod))
        oom();

    *q = (RuQueue) {
        .elems = xmallocn(elem_size, mod),
        .elem_size = elem_size,
        .head = 0,
        .tail = 0,
        .mod = mod,
    };
}

void
ru_queue_finish(RuQueue *q) {
    free(q->elems);
}

void
ru_queue_grow(RuQueue *q, size_t elem_count) {
    ru_queue_check(q);

    let old_mod = q->mod;

    if (q->tail < q->head)
        elem_count = ru_max(elem_count, q->tail);

    if (__builtin_add_overflow(q->mod, elem_count, &q->mod))
        oom();

    q->elems = xreallocn(q->elems, q->mod, q->elem_size);

    if (q->tail < q->head) {
        memmove(q->elems + (old_mod * q->elem_size), q->elems, q->tail * q->elem_size);
        q->tail += old_mod;
    }
}

void
ru_queue_push(RuQueue *q, void *elem) {
    ru_queue_check(q);
    assert(elem);

    let new_tail = (q->tail + 1) % q->mod;

    if (new_tail == q->head) {
        // double the capacity
        size_t old_cap = q->mod - 1;

        size_t new_cap;
        if (__builtin_mul_overflow((size_t) 2, old_cap, &new_cap))
            new_cap = SIZE_MAX - 1;

        if (new_cap == old_cap)
            oom();

        ru_queue_grow(q, new_cap - old_cap);
        ru_queue_push(q, elem);

        return;
    }

    memcpy(q->elems + q->tail * q->elem_size, elem, q->elem_size);
    q->tail = new_tail;
}

// Return false if queue is empty.
bool
ru_queue_pop(RuQueue *q, void *elem) {
    ru_queue_check(q);

    if (!ru_queue_peek(q, elem))
        return false;

    q->head++;
    q->head %= q->mod;

    return true;
}

// Return false if queue is empty.
bool
ru_queue_peek(RuQueue *q, void *elem) {
    ru_queue_check(q);

    if (ru_queue_is_empty(q))
        return false;

    if (elem) {
        memcpy(elem, q->elems + q->head * q->elem_size, q->elem_size);
    }

    return true;
}
