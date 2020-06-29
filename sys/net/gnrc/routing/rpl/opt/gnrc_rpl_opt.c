/*
 * Copyright (C) 2017 HAW Hamburg
 * Copyright (C) 2020 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @{
 *
 * @file
 * @author  Martin Landsmann <martin.landsmann@haw-hamburg.de>
 * @author  Martine Lenders <m.lenders@fu-berlin.de>
 */

#include <assert.h>
#include <string.h>

#include "byteorder.h"
#include "net/gnrc/rpl/dodag.h"
#include "net/gnrc/rpl/opt.h"

int gnrc_rpl_opt_process(uint8_t *opt_data, uint8_t opt_len)
{
    /* opt_len should be checked by caller, so advancing the pointer should be
     * safe */
    uint8_t *flags = opt_data++;
    uint8_t *instance_id = opt_data++;
    uint8_t *src_rank_ptr = opt_data;
    uint16_t src_rank = byteorder_bebuftohs(src_rank_ptr);

    opt_data += sizeof(uint16_t);
    assert((flags + opt_len) >= opt_data);
    for (uint8_t i = 0; i < GNRC_RPL_INSTANCES_NUMOF; ++i) {
        /* check if the option is for us */
        if (gnrc_rpl_instances[i].id == *instance_id) {
            /* check if the packet traversed in the expected direction */
            if ((gnrc_rpl_instances[i].dodag.my_rank < src_rank) &&
                (*flags & GNRC_RPL_HOP_OPT_FLAG_O)) {
                /* everything worked out as expected so we store our rank */
                byteorder_htobebufs(src_rank_ptr,
                                    gnrc_rpl_instances[i].dodag.my_rank);
                /* and push the packet further towards its destination */
                return GNRC_RPL_OPT_SUCCESS;
            }
            else {
                /* it didn't */
                if (*flags & GNRC_RPL_HOP_OPT_FLAG_R) {
                    /* its not the first time so we set error flag F */
                    *flags |= GNRC_RPL_HOP_OPT_FLAG_F;
                    /* start local repair and return */
                    gnrc_rpl_local_repair(&gnrc_rpl_instances[i].dodag);

                    return GNRC_RPL_OPT_FLAG_F_SET;
                }
                /* set error flag R and return */
                *flags |= GNRC_RPL_HOP_OPT_FLAG_R;
                return GNRC_RPL_OPT_FLAG_R_SET;
            }
        }
    }
    /* the option is not related to us, we just forward the packet further */
    return GNRC_RPL_OPT_NOT_FOR_ME;
}

/** @} */
