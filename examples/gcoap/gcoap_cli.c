/*
 * Copyright (c) 2015-2017 Ken Bannister. All rights reserved.
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       gcoap CLI support
 *
 * @author      Ken Bannister <kb2ma@runbox.com>
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 *
 * @}
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net/gcoap.h"
#include "saul_reg.h"
#include "od.h"
#include "fmt.h"
#include "jsmn.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

static ssize_t _encode_link(const coap_resource_t *resource, char *buf,
                            size_t maxlen, coap_link_encoder_ctx_t *context);
static void _resp_handler(const gcoap_request_memo_t *memo, coap_pkt_t* pdu,
                          const sock_udp_ep_t *remote);
static ssize_t _stats_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _saul_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);
static ssize_t _riot_board_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx);

/* CoAP resources. Must be sorted by path (ASCII order). */
static const coap_resource_t _resources[] = {
    { "/cli/stats", COAP_GET | COAP_PUT, _stats_handler, NULL },
    { "/led/green", COAP_GET | COAP_POST, _saul_handler, (void *)0x1 },
    { "/led/orange", COAP_GET | COAP_POST, _saul_handler, (void *)0x2 },
    { "/led/red", COAP_GET | COAP_POST, _saul_handler, (void *)0x0 },
    { "/riot/board", COAP_GET, _riot_board_handler, NULL },
    { "/sense/accel", COAP_GET, _saul_handler, (void *)0x7 },
    { "/sense/gyro", COAP_GET, _saul_handler, (void *)0x4 },
    { "/sense/light", COAP_GET, _saul_handler, (void *)0x3 },
    { "/sense/mag", COAP_GET, _saul_handler, (void *)0x8 },
    { "/sense/press", COAP_GET, _saul_handler, (void *)0x5 },
    { "/sense/temp", COAP_GET, _saul_handler, (void *)0x6 },
};

static const char *_link_params[] = {
    ";ct=0;rt=\"count\";obs",
    ";ct=50,rt=\"light\"",
    ";ct=50,rt=\"light\"",
    ";ct=50,rt=\"light\"",
    NULL,
    ";ct=50,rt=\"accel\"",
    ";ct=50,rt=\"accel\"",
    ";ct=50,rt=\"illu\"",
    ";ct=50,rt=\"mag\"",
    ";ct=50,rt=\"atm\"",
    ";ct=50,rt=\"temp\"",
};

static gcoap_listener_t _listener = {
    &_resources[0],
    ARRAY_SIZE(_resources),
    _encode_link,
    NULL
};

/* Retain request path to re-request if response includes block. User must not
 * start a new request (with a new path) until any blockwise transfer
 * completes or times out. */
#define _LAST_REQ_PATH_MAX (32)
static char _last_req_path[_LAST_REQ_PATH_MAX];

/* Counts requests sent by CLI. */
static uint16_t req_count = 0;

/* Adds link format params to resource list */
static ssize_t _encode_link(const coap_resource_t *resource, char *buf,
                            size_t maxlen, coap_link_encoder_ctx_t *context) {
    ssize_t res = gcoap_encode_link(resource, buf, maxlen, context);
    if (res > 0) {
        if (_link_params[context->link_pos]
                && (strlen(_link_params[context->link_pos]) < (maxlen - res))) {
            if (buf) {
                memcpy(buf+res, _link_params[context->link_pos],
                       strlen(_link_params[context->link_pos]));
            }
            return res + strlen(_link_params[context->link_pos]);
        }
    }

    return res;
}

/*
 * Response callback.
 */
static void _resp_handler(const gcoap_request_memo_t *memo, coap_pkt_t* pdu,
                          const sock_udp_ep_t *remote)
{
    (void)remote;       /* not interested in the source currently */

    if (memo->state == GCOAP_MEMO_TIMEOUT) {
        printf("gcoap: timeout for msg ID %02u\n", coap_get_id(pdu));
        return;
    }
    else if (memo->state == GCOAP_MEMO_ERR) {
        printf("gcoap: error in response\n");
        return;
    }

    coap_block1_t block;
    if (coap_get_block2(pdu, &block) && block.blknum == 0) {
        puts("--- blockwise start ---");
    }

    char *class_str = (coap_get_code_class(pdu) == COAP_CLASS_SUCCESS)
                            ? "Success" : "Error";
    printf("gcoap: response %s, code %1u.%02u", class_str,
                                                coap_get_code_class(pdu),
                                                coap_get_code_detail(pdu));
    if (pdu->payload_len) {
        unsigned content_type = coap_get_content_type(pdu);
        if (content_type == COAP_FORMAT_TEXT
                || content_type == COAP_FORMAT_JSON
                || content_type == COAP_FORMAT_LINK
                || coap_get_code_class(pdu) == COAP_CLASS_CLIENT_FAILURE
                || coap_get_code_class(pdu) == COAP_CLASS_SERVER_FAILURE) {
            /* Expecting diagnostic payload in failure cases */
            printf(", %u bytes\n%.*s\n", pdu->payload_len, pdu->payload_len,
                                                          (char *)pdu->payload);
        }
        else {
            printf(", %u bytes\n", pdu->payload_len);
            od_hex_dump(pdu->payload, pdu->payload_len, OD_WIDTH_DEFAULT);
        }
    }
    else {
        printf(", empty payload\n");
    }

    /* ask for next block if present */
    if (coap_get_block2(pdu, &block)) {
        if (block.more) {
            unsigned msg_type = coap_get_type(pdu);
            if (block.blknum == 0 && !strlen(_last_req_path)) {
                puts("Path too long; can't complete blockwise");
                return;
            }

            gcoap_req_init(pdu, (uint8_t *)pdu->hdr, GCOAP_PDU_BUF_SIZE,
                           COAP_METHOD_GET, _last_req_path);
            if (msg_type == COAP_TYPE_ACK) {
                coap_hdr_set_type(pdu->hdr, COAP_TYPE_CON);
            }
            block.blknum++;
            coap_opt_add_block2_control(pdu, &block);
            int len = coap_opt_finish(pdu, COAP_OPT_FINISH_NONE);
            gcoap_req_send((uint8_t *)pdu->hdr, len, remote,
                           _resp_handler, memo->context);
        }
        else {
            puts("--- blockwise complete ---");
        }
    }
}

/*
 * Server callback for /cli/stats. Accepts either a GET or a PUT.
 *
 * GET: Returns the count of packets sent by the CLI.
 * PUT: Updates the count of packets. Rejects an obviously bad request, but
 *      allows any two byte value for example purposes. Semantically, the only
 *      valid action is to set the value to 0.
 */
static ssize_t _stats_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;

    /* read coap method type in packet */
    unsigned method_flag = coap_method2flag(coap_get_code_detail(pdu));

    switch(method_flag) {
        case COAP_GET:
            gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
            coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
            size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

            /* write the response buffer with the request count value */
            resp_len += fmt_u16_dec((char *)pdu->payload, req_count);
            return resp_len;

        case COAP_PUT:
            /* convert the payload to an integer and update the internal
               value */
            if (pdu->payload_len <= 5) {
                char payload[6] = { 0 };
                memcpy(payload, (char *)pdu->payload, pdu->payload_len);
                req_count = (uint16_t)strtoul(payload, NULL, 10);
                return gcoap_response(pdu, buf, len, COAP_CODE_CHANGED);
            }
            else {
                return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
            }
    }

    return 0;
}

static ssize_t _riot_board_handler(coap_pkt_t *pdu, uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    /* write the RIOT board name in the response buffer */
    if (pdu->payload_len >= strlen(RIOT_BOARD)) {
        memcpy(pdu->payload, RIOT_BOARD, strlen(RIOT_BOARD));
        return resp_len + strlen(RIOT_BOARD);
    }
    else {
        puts("gcoap_cli: msg buffer too small");
        return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
    }
}

static ssize_t _check_offset_error(coap_pkt_t *pdu, uint8_t *buf, size_t len,
                                   int offset)
{
    /* payload was truncated */
    if (offset >= pdu->payload_len) {
        puts("gcoap_cli: msg buffer too small");
        return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
    }
    if (offset < 0) {
        puts("gcoap_cli: error on formatting");
        return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
    }
    return 0;
}

static inline bool _jsoneq(const char *json, jsmntok_t *tok, const char *s)
{
    return ((tok->type == JSMN_STRING) &&
           ((int)strlen(s) == (tok->end - tok->start)) &&
            (strncmp(json + tok->start, s, tok->end - tok->start) == 0));
}

static ssize_t _saul_handler(coap_pkt_t *pdu, uint8_t *buf, size_t len, void *ctx)
{
    saul_reg_t *dev;
    int num = (intptr_t)ctx;
    /* read coap method type in packet */
    unsigned method_flag = coap_method2flag(coap_get_code_detail(pdu));
    phydat_t data;

    dev = saul_reg_find_nth(num);
    if (dev == NULL) {
        puts("gcoap_cli: Unknown SAUL device");
        return gcoap_response(pdu, buf, len, COAP_CODE_404);
    }
    if (method_flag == COAP_GET) {
        int offset, dim;
        ssize_t res;

        dim = saul_reg_read(dev, &data);
        if ((dim <= 0) || ((unsigned)dim > PHYDAT_DIM)) {
            puts("gcoap_cli: error reading SAUL device");
            return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
        }
        gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
        coap_opt_add_format(pdu, COAP_FORMAT_JSON);
        size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

        offset = snprintf((char *)pdu->payload, pdu->payload_len,
                          "{\"cl\":\"%s\",\"u\":\"%s\",\"v\":[",
                          saul_class_to_str(dev->driver->type),
                          phydat_unit_to_str(data.unit));
        if ((res = _check_offset_error(pdu, buf, len, offset))) {
            return res;
        }
        for (int8_t i = 0; i < dim; i++) {
            int tmp;
            char *ptr = (char *)(pdu->payload + offset);
            uint16_t buf_len = (uint16_t)(pdu->payload_len - offset);
            char *delim = ((i + 1) == dim) ? "]}" : ",";

            if (data.scale == 0) {
                tmp = snprintf(ptr, buf_len, "%d%s", data.val[i], delim);
            }
            else if (((data.scale > -5) && (data.scale < 0))) {
                char num[8];
                size_t len = fmt_s16_dfp(num, data.val[i], data.scale);

                num[len] = '\0';
                tmp = snprintf(ptr, buf_len, "%s%s", num, delim);
            }
            else {
                tmp = snprintf(ptr, buf_len, "%dE%d%s", data.val[i], data.scale,
                               delim);
            }
            if ((res = _check_offset_error(pdu, buf, len, tmp))) {
                return res;
            }
            offset += tmp;
        }
        return resp_len + offset;
    }
    else if (method_flag == COAP_POST) {
        jsmn_parser p = { 0 };
        jsmntok_t t[16]; /* We expect no more than 16 tokens */
        int r;
        char *ptr = (char *)pdu->payload;

        r = jsmn_parse(&p, ptr, pdu->payload_len,
                       t, ARRAY_SIZE(t));
        if ((r < 0) || (r < 1) || (t[0].type != JSMN_OBJECT)) {
            puts("gcoap_cli: Failed to parse JSON or not an object");
            goto error;
        }
        for (int i = 1; i < r; i++) {
            if (_jsoneq(ptr, &t[i], "v")) {
                bool val = false;

                if (((i + 2) >= r) ||
                    (t[i + 1].type != JSMN_ARRAY) ||
                    (t[i + 1].size < 1) ||
                    (t[i + 2].type != JSMN_PRIMITIVE) ||
                    ((t[i + 2].end - t[i + 2].start) < 1)) {
                    puts("gcoap_cli: Value of unexpected type");
                    goto error;
                }
                switch (ptr[t[i + 2].start]) {
                    /* boolean or null string */
                    case 'f':
                    case 'n':
                        val = false;
                        break;
                    case 't':
                        val = true;
                        break;
                    default:
                        /* is a number */
                        if ((ptr[t[i + 2].start] == '-') ||
                            (ptr[t[i + 2].start] == '+')) {
                            t[i + 2].start++;
                        }
                        val = !((ptr[t[i + 2].start] == '0') &&
                                ((t[i + 2].end - t[i + 2].start) == 1));
                        break;
                }
                memset(&data, 0, sizeof(data));
                data.val[0] = (int16_t)val;
                if (saul_reg_write(dev, &data) <= 0) {
                    puts("gcoap_cli: Error writing to device");
                    goto error;
                }
                return gcoap_response(pdu, buf, len, COAP_CODE_CHANGED);
            }
        }
    }
error:
    return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
}

static size_t _send(uint8_t *buf, size_t len, char *addr_str, char *port_str)
{
    ipv6_addr_t addr;
    size_t bytes_sent;
    sock_udp_ep_t remote;

    remote.family = AF_INET6;

    /* parse for interface */
    char *iface = ipv6_addr_split_iface(addr_str);
    if (!iface) {
        if (gnrc_netif_numof() == 1) {
            /* assign the single interface found in gnrc_netif_numof() */
            remote.netif = (uint16_t)gnrc_netif_iter(NULL)->pid;
        }
        else {
            remote.netif = SOCK_ADDR_ANY_NETIF;
        }
    }
    else {
        int pid = atoi(iface);
        if (gnrc_netif_get_by_pid(pid) == NULL) {
            puts("gcoap_cli: interface not valid");
            return 0;
        }
        remote.netif = pid;
    }
    /* parse destination address */
    if (ipv6_addr_from_str(&addr, addr_str) == NULL) {
        puts("gcoap_cli: unable to parse destination address");
        return 0;
    }
    if ((remote.netif == SOCK_ADDR_ANY_NETIF) && ipv6_addr_is_link_local(&addr)) {
        puts("gcoap_cli: must specify interface for link local target");
        return 0;
    }
    memcpy(&remote.addr.ipv6[0], &addr.u8[0], sizeof(addr.u8));

    /* parse port */
    remote.port = atoi(port_str);
    if (remote.port == 0) {
        puts("gcoap_cli: unable to parse destination port");
        return 0;
    }

    bytes_sent = gcoap_req_send(buf, len, &remote, _resp_handler, NULL);
    if (bytes_sent > 0) {
        req_count++;
    }
    return bytes_sent;
}

int gcoap_cli_cmd(int argc, char **argv)
{
    /* Ordered like the RFC method code numbers, but off by 1. GET is code 0. */
    char *method_codes[] = {"get", "post", "put"};
    uint8_t buf[GCOAP_PDU_BUF_SIZE];
    coap_pkt_t pdu;
    size_t len;

    if (argc == 1) {
        /* show help for main commands */
        goto end;
    }

    if (strcmp(argv[1], "info") == 0) {
        uint8_t open_reqs = gcoap_op_state();

        printf("CoAP server is listening on port %u\n", GCOAP_PORT);
        printf(" CLI requests sent: %u\n", req_count);
        printf("CoAP open requests: %u\n", open_reqs);
        return 0;
    }

    /* if not 'info', must be a method code */
    int code_pos = -1;
    for (size_t i = 0; i < ARRAY_SIZE(method_codes); i++) {
        if (strcmp(argv[1], method_codes[i]) == 0) {
            code_pos = i;
        }
    }
    if (code_pos == -1) {
        goto end;
    }

    /* parse options */
    int apos          = 2;               /* position of address argument */
    unsigned msg_type = COAP_TYPE_NON;
    if (argc > apos && strcmp(argv[apos], "-c") == 0) {
        msg_type = COAP_TYPE_CON;
        apos++;
    }

    /*
     * "get" (code_pos 0) must have exactly apos + 3 arguments
     * while "post" (code_pos 1) and "put" (code_pos 2) and must have exactly
     * apos + 4 arguments
     */
    if (((argc == apos + 3) && (code_pos == 0)) ||
        ((argc == apos + 4) && (code_pos != 0))) {
        gcoap_req_init(&pdu, &buf[0], GCOAP_PDU_BUF_SIZE, code_pos+1, argv[apos+2]);
        coap_hdr_set_type(pdu.hdr, msg_type);

        memset(_last_req_path, 0, _LAST_REQ_PATH_MAX);
        if (strlen(argv[apos+2]) < _LAST_REQ_PATH_MAX) {
            memcpy(_last_req_path, argv[apos+2], strlen(argv[apos+2]));
        }

        size_t paylen = (argc == apos + 4) ? strlen(argv[apos+3]) : 0;
        if (paylen) {
            coap_opt_add_format(&pdu, COAP_FORMAT_TEXT);
            len = coap_opt_finish(&pdu, COAP_OPT_FINISH_PAYLOAD);
            if (pdu.payload_len >= paylen) {
                memcpy(pdu.payload, argv[apos+3], paylen);
                len += paylen;
            }
            else {
                puts("gcoap_cli: msg buffer too small");
                return 1;
            }
        }
        else {
            len = coap_opt_finish(&pdu, COAP_OPT_FINISH_NONE);
        }

        printf("gcoap_cli: sending msg ID %u, %u bytes\n", coap_get_id(&pdu),
               (unsigned) len);
        if (!_send(&buf[0], len, argv[apos], argv[apos+1])) {
            puts("gcoap_cli: msg send failed");
        }
        else {
            /* send Observe notification for /cli/stats */
            switch (gcoap_obs_init(&pdu, &buf[0], GCOAP_PDU_BUF_SIZE,
                    &_resources[0])) {
            case GCOAP_OBS_INIT_OK:
                DEBUG("gcoap_cli: creating /cli/stats notification\n");
                coap_opt_add_format(&pdu, COAP_FORMAT_TEXT);
                len = coap_opt_finish(&pdu, COAP_OPT_FINISH_PAYLOAD);
                len += fmt_u16_dec((char *)pdu.payload, req_count);
                gcoap_obs_send(&buf[0], len, &_resources[0]);
                break;
            case GCOAP_OBS_INIT_UNUSED:
                DEBUG("gcoap_cli: no observer for /cli/stats\n");
                break;
            case GCOAP_OBS_INIT_ERR:
                DEBUG("gcoap_cli: error initializing /cli/stats notification\n");
                break;
            }
        }
        return 0;
    }
    else {
        printf("usage: %s <get|post|put> [-c] <addr>[%%iface] <port> <path> [data]\n",
               argv[0]);
        printf("Options\n");
        printf("    -c  Send confirmably (defaults to non-confirmable)\n");
        return 1;
    }

    end:
    printf("usage: %s <get|post|put|info>\n", argv[0]);
    return 1;
}

void gcoap_cli_init(void)
{
    gcoap_register_listener(&_listener);
}
