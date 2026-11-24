/*
 * Internal definitions for the baltic library.
 * SPDX-License-Identifier: MIT
 */

#ifndef BALTIC_INTERNAL_H
#define BALTIC_INTERNAL_H

#include "baltic/baltic.h"

#include <proj.h>

#include <stddef.h>

#if defined(_WIN32)
#include <windows.h>
typedef SRWLOCK baltic_mutex;
static inline int baltic_mutex_init(baltic_mutex *m) {
    InitializeSRWLock(m);
    return 0;
}
static inline void baltic_mutex_destroy(baltic_mutex *m) { (void)m; }
static inline void baltic_mutex_lock(baltic_mutex *m) { AcquireSRWLockExclusive(m); }
static inline void baltic_mutex_unlock(baltic_mutex *m) { ReleaseSRWLockExclusive(m); }
#else
#include <pthread.h>
typedef pthread_mutex_t baltic_mutex;
static inline int baltic_mutex_init(baltic_mutex *m) {
    return pthread_mutex_init(m, NULL);
}
static inline void baltic_mutex_destroy(baltic_mutex *m) {
    (void)pthread_mutex_destroy(m);
}
static inline void baltic_mutex_lock(baltic_mutex *m) {
    (void)pthread_mutex_lock(m);
}
static inline void baltic_mutex_unlock(baltic_mutex *m) {
    (void)pthread_mutex_unlock(m);
}
#endif

struct baltic_ctx {
    PJ_CONTEXT *geoid_ctx;
    PJ_CONTEXT *correction_ctx;
    PJ *geoid_op;      /* pipeline: degrees -> rad, vgridshift(EGM2008), rad -> degrees */
    PJ *correction_op; /* optional, same pipeline shape */
    baltic_mutex mutex;
    double offset_m;
    baltic_grid_info geoid_info;
    char *geoid_path;
    char *correction_path;
};
#endif /* BALTIC_INTERNAL_H */
