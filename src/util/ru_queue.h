#pragma once

#include <stdbool.h>
#include <stdlib.h>

typedef struct RuQueue {
    void *elems; // array of elements; size is `elem_size * mod`.
    size_t elem_size; // size of single element
    size_t head;
    size_t tail;
    size_t mod; // modulus for head/tail arithmetic
} RuQueue;

void ru_queue_init(RuQueue *q, size_t elem_size, size_t init_capacity);
void ru_queue_finish(RuQueue *q);
void ru_queue_grow(RuQueue *q, size_t elem_count);
void ru_queue_push(RuQueue *q, void *elem);
bool ru_queue_pop(RuQueue *q, void *elem) _must_use_result_;
bool ru_queue_peek(RuQueue *q, void *elem) _must_use_result_;

static inline size_t _must_use_result_
ru_queue_len(RuQueue *q) {
    return (q->tail - q->head + q->mod) % q->mod;
}

static inline bool _must_use_result_
ru_queue_is_empty(RuQueue *q) {
    return q->head == q->tail;
}
