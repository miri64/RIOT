/*
 * Copyright (C) 2015 Martine Lenders <mlenders@inf.fu-berlin.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    sys_sem Semaphores
 * @ingroup     sys
 * @brief       Lightweight semaphore implementation
 * @{
 *
 * @file
 * @brief   Semaphore definitions
 *
 * @author  Martine Lenders <mlenders@inf.fu-berlin.de>
 * @author  Christian Mehlis <mehlis@inf.fu-berlin.de>
 * @author  René Kijewski <kijewski@inf.fu-berlin.de>
 */
#ifndef SEM_H_
#define SEM_H_

#include "priority_queue.h"
#include "timex.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Semaphore.
 */
typedef struct {
    volatile unsigned int value;    /**< value of the semaphore */
    priority_queue_t queue;         /**< list of threads waiting for the semaphore */
} sem_t;

/**
 * @brief   Creates semaphore.
 *
 * @param[out] sem  The created semaphore.
 * @param[in] value Initial value for the semaphore.
 *
 * @return  0 on success.
 * @return  -EINVAL, if semaphore is invalid.
 */
int sem_create(sem_t *sem, unsigned int value);

/**
 * @brief   Destroys a semaphore.
 *
 * @param[in] sem   The semaphore to destroy.
 *
 * @return  0 on success.
 * @return  -EINVAL, if semaphore is invalid.
 */
int sem_destroy(sem_t *sem);

/**
 * @brief   Wait for a semaphore being posted.
 *
 * @pre Message queue of active thread is initialized (see @ref msg_init_queue()).
 *
 * @param[in] sem       A semaphore.
 * @param[in] timeout   Time until the semaphore times out. NULL for no timeout.
 *
 * @return  0 on success
 * @return  -EINVAL, if semaphore is invalid.
 * @return  -ETIMEDOUT, if the semaphore times out.
 * @return  -ECANCELED, if the semaphore was destroyed or landed in an undefined
 *          state.
 */
int sem_wait_timed(sem_t *sem, timex_t *timeout);

/**
 * @brief   Wait for a semaphore being posted (without timeout).
 *
 * @param[in] sem   A semaphore.
 *
 * @return  0 on success
 * @return  -EINVAL, if semaphore is invalid.
 */
static inline int sem_wait(sem_t *sem)
{
    return sem_wait_timed(sem, NULL);
}

/**
 * @brief   Signal semaphore.
 *
 * @param[in] sem   A semaphore.
 *
 * @return  -EINVAL, if semaphore is invalid.
 * @return  -EOVERFLOW, if the semaphore's value would overflow.
 */
int sem_post(sem_t *sem);

#ifdef __cplusplus
}
#endif

#endif /* SEM_H_ */
/** @} */
