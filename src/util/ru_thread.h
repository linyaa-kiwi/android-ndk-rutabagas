#pragma once

#include <pthread.h>

#include "attribs.h"
#include "macros.h"

static inline void
ru_mutex_unlock_p(pthread_mutex_t **m) {
    if (pthread_mutex_unlock(*m))
        abort();
}

#define ru_mutex_lock_scoped(mutex) \
    __ru_mutex_lock_scoped(UNIQ(_mutex), (mutex))

#define __ru_mutex_lock_scoped(uniq_mutex, mutex) \
    _cleanup_(ru_mutex_unlock_p) pthread_mutex_t *uniq_mutex = (mutex); \
    \
    if (pthread_mutex_lock(uniq_mutex)) { \
        abort(); \
    } \
    \
    ru_require_semicolon()
