/*
 * Copyright (C) 2015 Martine Lenders <mlenders@inf.fu-berlin.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     tests
 * @{
 *
 * @file
 * @brief       Test application for ng_tapnet network device driver
 *
 * @author      Martine Lenders <mlenders@inf.fu-berlin.de>
 *
 * @}
 */

#include <stdio.h>

#include "board.h"
#include "kernel.h"
#include "ringbuffer.h"
#include "shell.h"
#include "shell_commands.h"
#include "posix_io.h"
#include "board_uart0.h"
#include "periph/uart.h"
#include "net/ng_netbase.h"
#include "net/ng_slip.h"
#include "net/ng_pktdump.h"

#ifndef SLIP_UART
#error "error: no UART interface defined"
#endif
#ifndef SLIP_BAUDRATE
#error "error: baudrate undefined"
#endif

/**
 * @brief   Define SLIP stack configuration
 * @{
 */
#define SLIP_STACK_SIZE         (KERNEL_CONF_STACKSIZE_DEFAULT)
#define SLIP_STACK_PRIO         (PRIORITY_MAIN - 1)
/** @} */

/**
 * @brief   Buffer size used by the shell
 */
#define SHELL_BUFSIZE           (64U)

/**
 * @brief   Device descriptor of the SLIP device
 */
static ng_slip_dev_t dev;

/**
 * @brief   Stack for the SLIP device
 */
static char slip_stack[SLIP_STACK_SIZE];

/**
 * @brief   Maybe you are a golfer?!
 */
int main(void)
{
    int res;
    shell_t shell;
    ng_netreg_entry_t dump;

    puts("slip device driver test");
    printf("Initializing tapnet... \n");

    /* initialize and register pktdump */
    dump.pid = ng_pktdump_init();
    dump.demux_ctx = NG_NETREG_DEMUX_CTX_ALL;

    if (dump.pid <= KERNEL_PID_UNDEF) {
        puts("Error starting pktdump thread");
        return -1;
    }

    ng_netreg_register(NG_NETTYPE_UNDEF, &dump);

    res = ng_slip_init(&dev, SLIP_UART, SLIP_BAUDRATE,
                       slip_stack, SLIP_STACK_SIZE, SLIP_STACK_PRIO);
    if (res < 0) {
        puts("Error starting slip thread");
        return -1;
    }

    /* start the shell */
    posix_open(uart0_handler_pid, 0);
    shell_init(&shell, NULL, SHELL_BUFSIZE, uart0_readc, uart0_putc);
    shell_run(&shell);

    return 0;
}
