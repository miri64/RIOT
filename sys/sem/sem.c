/*
 * Copyright (C) 2013-15 Freie Universität Berlin
 * Copyright (C) 2015 Martine Lenders <mlenders@inf.fu-berlin.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @{
 *
 * @file
 */

#include <assert.h>
#include <errno.h>
#include <limits.h>

#include "irq.h"
#include "msg.h"
#include "vtimer.h"

#include "sem.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

#define _MSG_SIGNAL         (0x0501)
#define _MSG_TIMEOUT        (0x0502)
#define _MSG_DESTROYED      (0x0503)

int sem_create(sem_t *sem, unsigned int value)
{
    if (sem == NULL) {
        return -EINVAL;
    }
    sem->value = value;
    /* waiters for the mutex */
    sem->queue.first = NULL;
    return 0;
}

int sem_destroy(sem_t *sem)
{
    unsigned int old_state;
    priority_queue_node_t *next;
    if (sem == NULL) {
        return -EINVAL;
    }
    old_state = disableIRQ();
    next = priority_queue_remove_head(&sem->queue);
    while (next) {
        msg_t msg;
        kernel_pid_t pid = (kernel_pid_t)next->data;
        msg.type = _MSG_DESTROYED;
        msg_send_int(&msg, pid);
    }
    restoreIRQ(old_state);
    return 0;
}

int sem_wait_timed(sem_t *sem, timex_t *timeout)
{
    if (sem == NULL) {
        return -EINVAL;
    }
    assert(sched_active_thread->msg_array != NULL);
    while (1) {
        unsigned old_state = disableIRQ();
        priority_queue_node_t n;
        vtimer_t timeout_timer;
        msg_t msg;

        unsigned value = sem->value;
        if (value != 0) {
            sem->value = value - 1;
            restoreIRQ(old_state);
            return 0;
        }

        /* I'm going blocked */
        n.priority = (uint32_t)sched_active_thread->priority;
        n.data = (unsigned int)sched_active_pid;
        n.next = NULL;
        priority_queue_add(&sem->queue, &n);

        DEBUG("sem_wait: %" PRIkernel_pid ": Adding node to semaphore queue: prio: %" PRIu32 "\n",
              sched_active_thread->pid, sched_active_thread->priority);

        if (timeout != NULL) {
            vtimer_set_msg(&timeout_timer, *timeout, sched_active_pid,
                           _MSG_TIMEOUT, sem);
        }

        restoreIRQ(old_state);
        msg_receive(&msg);
        vtimer_remove(&timeout_timer);  /* remove timer just to be sure */
        switch (msg.type) {
            case _MSG_SIGNAL:
                continue;
            case _MSG_TIMEOUT:
                return -ETIMEDOUT;
            case _MSG_DESTROYED:
            default:
                return -ECANCELED;
        }
    }
}

int sem_post(sem_t *sem)
{
    unsigned int old_state, value;
    priority_queue_node_t *next;
    if (sem == NULL) {
        return -EINVAL;
    }
    old_state = disableIRQ();
    value = sem->value;
    if (value == UINT_MAX) {
        restoreIRQ(old_state);
        return -EOVERFLOW;
    }
    ++sem->value;
    next = priority_queue_remove_head(&sem->queue);
    if (next) {
        uint16_t prio = (uint16_t)next->priority;
        kernel_pid_t pid = (kernel_pid_t) next->data;
        msg_t msg;
        DEBUG("sem_post: %" PRIkernel_pid ": waking up %" PRIkernel_pid "\n",
              sched_active_thread->pid, next_process->pid);
        msg.type = _MSG_SIGNAL;
        msg_send_int(&msg, pid);
        restoreIRQ(old_state);
        sched_switch(prio);
    }
    else {
        restoreIRQ(old_state);
    }

    return 1;
}

/** @} */
