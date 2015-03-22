/*
 * Copyright (C) 2015 Martine Lenders <mlenders@inf.fu-berlin.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    net_ng_slip SLIP
 * @ingroup     net
 * @brief       Provides a SLIP interface over UART utilizing
 *              @ref driver_periph_uart.
 * @see         <a href="https://www.ietf.org/rfc/rfc1055">RFC 1055</a>
 * @{
 *
 * @file
 * @brief       SLIP interface defintion
 *
 * @author      Martine Lenders <mlenders@inf.fu-berlin.de>
 */

#ifndef NG_SLIP_H_
#define NG_SLIP_H_

#include <inttypes.h>

#include "net/ng_netbase.h"
#include "periph/uart.h"
#include "ringbuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !UART_NUMOF
/**
 * @brief   Guard type for boards that do not implement @ref driver_periph_uart
 */
typedef uint8_t uart_t;
#endif

/**
 * @brief   Descriptor for the UART interface.
 */
typedef struct {
    uart_t uart;            /**< the UART interface */
    ringbuffer_t *in_buf;   /**< input buffer */
    ringbuffer_t *out_buf;  /**< output buffer */
    /**
     * @{
     * @name    Internal parameters
     * @brief   These will be overwritten on ng_slip_init()
     * @internal
     */
    uint32_t in_bytes;      /**< the number of bytes received of a currently
                             *   incoming packet */
    uint16_t in_esc;        /**< receiver is in escape mode */
    kernel_pid_t slip_pid;  /**< the PID of  preinitialized */
    /**
     * @}
     */
} ng_slip_dev_t;

/**
 * Initializes a new @ref net_ng_slip control thread for UART device @p uart.
 *
 * @param[in] priority  The priority for the thread housing the SLIP instance.
 * @param[in] dev       A preinitialized device descriptor for the UART
 * @param[in] baudrate  Symbole rate for the UART device.
 *
 * @return  PID of SLIP thread on success
 * @return  -EFAULT, if slip_dev_t::in_buf or slip_dev_t::out_buf of @p dev
 *          was NULL.
 * @return  -EINVAL, if @p priority is greater than or equal to
 *          @ref SCHED_PRIO_LEVELS
 * @return  -ENODEV, if slip_dev_t::uart of @p dev was no valid UART.
 * @return  -ENOTSUP, if board does not implement @ref driver_periph_uart
 * @return  -EOVERFLOW, if there are too many threads running already
 */
kernel_pid_t ng_slip_init(char priority, ng_slip_dev_t *dev, uint32_t baudrate);

#ifdef __cplusplus
}
#endif

#endif /* __SLIP_H_ */
/** @} */
