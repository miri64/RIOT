/*
 * Copyright (C) 2013 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H    1

#include <errno.h>
#include <time.h>

#include "sem.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Value returned if `sem_open' failed.  */
#define SEM_FAILED      ((sem_t *) 0)

/**
 * @brief Initialize semaphore object SEM to VALUE.
 *
 * @param sem Semaphore to initialize
 * @param pshared unused
 * @param value Value to set
 */
#define sem_init(sem, pshared, value)   sem_create(sem, value)

/*
 * @brief Open a named semaphore NAME with open flags OFLAG.
 *
 * @brief WARNING: named semaphore are currently not supported
 *
 * @param name Name to set
 * @param oflag Flags to set
 */
#define sem_open(name, oflag, ...)      (SEM_FAILED)

/**
 * @brief Close descriptor for named semaphore SEM.
 *
 * @brief WARNING: named semaphore are currently not supported
 *
 * @param sem Semaphore to close
 */
#define sem_close(sem)                  (-1)

/**
 * @brief Remove named semaphore NAME.
 *
 * @brief WARNING: named semaphore are currently not supported
 *
 * @param name Name to unlink
 */
#define sem_unlink(name)                (-1)

/**
 * @brief Similar to `sem_wait' but wait only until ABSTIME.
 *
 * @brief WARNING: currently not supported
 *
 * @param sem Semaphore to wait on
 * @param abstime Max time to wait for a post
 *
 */
static inline int sem_timedwait(sem_t *sem, const struct timespec *abstime);

/**
 * @brief Test whether SEM is posted.
 *
 * @param sem Semaphore to trywait on
 *
 */
int sem_trywait(sem_t *sem);

/**
 * @brief Get current value of SEM and store it in *SVAL.
 *
 * @param sem Semaphore to get value from
 * @param sval place whre value goes to
 */
static inline int sem_getvalue(sem_t *sem, int *sval)
{
    if (sem != NULL) {
        *sval = (int)sem->value;
        return 0;
    }
    return -EINVAL;
}

#ifdef __cplusplus
}
#endif

#endif  /* semaphore.h */
