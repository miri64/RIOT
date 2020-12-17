/*
 * Copyright (C) 2019 Freie Universität Berlin
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
 * @brief       emcute MQTT-SN test application
 *
 * @author      Martine Sophie Lenders <m.lenders@fu-berlin.de>
 *
 * @}
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "net/asymcute.h"
#include "net/mqttsn.h"
#include "net/ipv6/addr.h"
#include "od.h"
#include "shell.h"
#include "thread.h"
#include "net/sock/util.h"

/* get to maximum length for client ID ;-)*/
#define ASYMCUTE_ID         "asymcute test app ....."
#define LISTENER_PRIO       (THREAD_PRIORITY_MAIN - 1)

#define NUMOFREQS           (4U)
#define SHELL_BUFSIZE       (512U)  /* for sub with long topic */

static char listener_stack[THREAD_STACKSIZE_DEFAULT];
static char _shell_buffer[SHELL_BUFSIZE];
static uint8_t _pub_buf[CONFIG_EMCUTE_BUFSIZE];

static asymcute_con_t _connection;
static asymcute_req_t _requests[NUMOFREQS];
static asymcute_sub_t _subscriptions[NUMOFREQS];
static asymcute_topic_t _topics[NUMOFREQS];
static char _topic_names[NUMOFREQS][CONFIG_EMCUTE_TOPIC_MAXLEN + 1];
static char _addr_str[IPV6_ADDR_MAX_STR_LEN];

static sock_udp_ep_t _gw = { .family = AF_INET6 };

static int _con(int argc, char **argv);
static int _discon(int argc, char **argv);
static int _reg(int argc, char **argv);
static int _pub(int argc, char **argv);
static int _sub(int argc, char **argv);
static int _unsub(int argc, char **argv);
static int _will(int argc, char **argv);
static int _info(int argc, char **argv);

static const shell_command_t _shell_commands[] = {
    { "con", "connect to a MQTT-SN broker", _con },
    { "discon", "disconnect from current broker", _discon },
    { "reg", "register to a topic", _reg },
    { "pub", "publish a number of bytes under a topic", _pub },
    { "sub", "subscribe to a topic", _sub },
    { "unsub", "unsubscribe from a topic", _unsub },
    { "will", "register a last will", _will },
    { "info", "print client state", _info },
    { NULL, NULL, NULL },
};

static void *_emcute_thread(void *arg)
{
    (void)arg;
    emcute_run(CONFIG_EMCUTE_DEFAULT_PORT, EMCUTE_ID);
    return NULL;    /* should never be reached */
}

static unsigned _get_qos(const char *str)
{
    int qos = atoi(str);
    switch (qos) {
        case 1:     return MQTTSN_QOS_1;
        case 2:     return MQTTSN_QOS_2;
        default:    return MQTTSN_QOS_0;
    }
}

static asymcute_req_t *_get_req_ctx(void)
{
    for (unsigned i = 0; i < ARRAY_SIZE(_requests); i++) {
        if (!asymcute_req_in_use(&_requests[i])) {
            return &_requests[i];
        }
    }
    puts("error: no request context available\n");
    return NULL;
}

static asymcute_sub_t *_get_sub_ctx(void)
{
    for (unsigned i = 0; i < ARRAY_SIZE(_subscriptions); i++) {
        if (!asymcute_sub_active(&_subscriptions[i])) {
            return &_subscriptions[i];
        }
    }
    return NULL;
}

static asymcute_sub_t *_find_sub(const char *name)
{
    for (unsigned i = 0; i < ARRAY_SIZE(_subscriptions); i++) {
        if (asymcute_sub_active(&_subscriptions[i]) &&
            strcmp(_subscriptions[i].topic->name, name) == 0) {
            return &_subscriptions[i];
        }
    }
    return NULL;
}

static void _on_pub_evt(const asymcute_sub_t *sub, unsigned evt_type,
                        const void *data, size_t len, void *arg)
{
    (void)data;
    printf("### got publication of %u bytes for topic '%s' [%d] ###\n",
           (unsigned)len, sub->topic->name, (int)sub->topic->id);
}

static void _on_con_evt(asymcute_req_t *req, unsigned evt_type)
{
    printf("Request %p: ", (void *)req);
    switch (evt_type) {
        case ASYMCUTE_TIMEOUT:
            puts("Timeout");
            break;
        case ASYMCUTE_REJECTED:
            puts("Rejected by gateway");
            break;
        case ASYMCUTE_CONNECTED:
            puts("Connection to gateway established");
            break;
        case ASYMCUTE_DISCONNECTED:
            puts("Connection to gateway closed");
            _topics_clear();
            break;
        case ASYMCUTE_REGISTERED:
            puts("Topic registered");
            break;
        case ASYMCUTE_PUBLISHED:
            puts("Data was published");
            break;
        case ASYMCUTE_SUBSCRIBED:
            puts("Subscribed topic");
            break;
        case ASYMCUTE_UNSUBSCRIBED:
            puts("Unsubscribed topic");
            break;
        case ASYMCUTE_CANCELED:
            puts("Canceled");
            break;
        default:
            puts("unknown event");
            break;
    }
}

static int _con(int argc, char **argv)
{
    asymcute_will_t *will_ptr = NULL, will = {
        .topic = NULL,
        .msg = NULL,
        .msg_len = 0,
    };

    if (argc < 2) {
        printf("usage %s <addr> [<will topic> <will msg>]\n",
               argv[0]);
        return 1;
    }

    if (sock_udp_str2ep(&_gw, argv[1]) != 0) {
        puts("error: unable to parse gateway address");
        _gw.port = 0;
        return 1;
    }
    if (_gw.port == 0) {
        _gw.port = CONFIG_ASYMCUTE_DEFAULT_PORT;
    }
    /* get request context */
    asymcute_req_t *req = _get_req_ctx();
    if (req == NULL) {
        return 1;
    }
    if (argc >= 4) {
        will.topic = argv[2];
        will.msg = argv[3];
        will.msg_len = strlen(msg);
        will_ptr = &will;
    }

    if (asymcute_connect(&_connection, req, &_gw, true, ASYMCUTE_ID, true,
                         will_ptr) != ASYMCUTE_OK) {
        printf("error: unable to connect to %s\n", argv[1]);
        _gw.port = 0;
        return 1;
    }
    printf("success: connected to gateway at %s\n", argv[1]);

    return 0;
}

static int _discon(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* get request context */
    asymcute_req_t *req = _get_req_ctx();
    if (req == NULL) {
        return 1;
    }
    int res = asymcute_disconnect(&_connection, req);
    if (res == ASYMCUTE_GWERR) {
        puts("error: not connected to any broker");
        return 1;
    }
    else if (res != ASYMCUTE_BUSY) {
        puts("error: unable to disconnect");
        return 1;
    }
    puts("success: disconnect successful");
    return 0;
}

static int _topic_name_find(const char *name)
{
    int res = -1;

    for (unsigned i = 0; i < NUMOFTOPS; i++) {
        if ((_topic_names[i][0] == '\0') && (res < 0)) {
            res = i;
        }
        else if (strncmp(name, _topic_names[i], CONFIG_EMCUTE_TOPIC_MAXLEN) == 0) {
            return i;
        }
    }

    return res;
}

static int _reg(int argc, char **argv)
{
    emcute_topic_t *t;
    int idx;
    bool was_set = false;

    if (argc < 2) {
        printf("usage: %s <topic name>\n", argv[0]);
        return 1;
    }

    idx = _topic_name_find(argv[1]);
    if (idx < 0) {
        puts("error: no space left to register");
        return 1;
    }
    if (_topic_names[idx][0] != '\0') {
        was_set = true;
    }
    else {
        strncpy(_topic_names[idx], argv[1], CONFIG_EMCUTE_TOPIC_MAXLEN);
    }
    t = &_topics[idx];
    t->name = _topic_names[idx];
    if (emcute_reg(t) != EMCUTE_OK) {
        puts("error: unable to obtain topic ID");
        if (was_set) {
            _topic_names[idx][0] = '\0';
        }
        return 1;
    }

    printf("success: registered to topic '%s [%d]'\n", t->name, t->id);

    return 0;
}

static int _pub(int argc, char **argv)
{
    unsigned flags = EMCUTE_QOS_0;
    int len;
    emcute_topic_t *t;
    int idx;

    if (argc < 3) {
        printf("usage: %s <topic name> <data_len> [QoS level]\n", argv[0]);
        return 1;
    }

    if (argc >= 4) {
        flags |= _get_qos(argv[3]);
    }

    idx = _topic_name_find(argv[1]);
    if ((idx < 0) || !(_topics[idx].name)) {
        puts("error: topic not registered");
        return 1;
    }
    t = &_topics[idx];
    len = atoi(argv[2]);
    if ((unsigned)len > sizeof(_pub_buf)) {
        printf("error: len %d > %lu\n", len, (unsigned long)sizeof(_pub_buf));
        return 1;
    }
    memset(_pub_buf, 92, len);
    if (emcute_pub(t, _pub_buf, len, flags) != EMCUTE_OK) {
        printf("error: unable to publish data to topic '%s [%d]'\n",
                t->name, (int)t->id);
        return 1;
    }

    printf("success: published %d bytes to topic '%s [%d]'\n",
           (int)len, t->name, t->id);

    return 0;
}

static int _sub(int argc, char **argv)
{
    unsigned flags = EMCUTE_QOS_0;
    int idx;
    bool was_set = false;

    if (argc < 2) {
        printf("usage: %s <topic name> [QoS level]\n", argv[0]);
        return 1;
    }

    if (strlen(argv[1]) > CONFIG_EMCUTE_TOPIC_MAXLEN) {
        puts("error: topic name exceeds maximum possible size");
        return 1;
    }
    if (argc >= 3) {
        flags |= _get_qos(argv[2]);
    }

    idx = _topic_name_find(argv[1]);
    if (idx < 0) {
        puts("error: no space to subscribe");
    }

    _subscriptions[idx].cb = _on_pub;
    if (_topic_names[idx][0] != '\0') {
        was_set = true;
    }
    else {
        strncpy(_topic_names[idx], argv[1], CONFIG_EMCUTE_TOPIC_MAXLEN);
    }
    _subscriptions[idx].topic.name = _topic_names[idx];
    if (emcute_sub(&_subscriptions[idx], flags) != EMCUTE_OK) {
        printf("error: unable to subscribe to %s\n", argv[1]);
        if (was_set) {
            _topic_names[idx][0] = '\0';
        }
        memset(&_subscriptions[idx], 0, sizeof(emcute_sub_t));
        return 1;
    }

    printf("success: now subscribed to %s\n", argv[1]);
    return 0;
}

static int _unsub(int argc, char **argv)
{
    int idx;

    if (argc < 2) {
        printf("usage %s <topic name>\n", argv[0]);
        return 1;
    }

    idx = _topic_name_find(argv[1]);
    if ((idx < 0) || !_subscriptions[idx].topic.name) {
        printf("error: no subscription for topic '%s' found\n", argv[1]);
    }
    else if (emcute_unsub(&_subscriptions[idx]) != EMCUTE_OK) {
        printf("error: Unsubscription form '%s' failed\n", argv[1]);
    }
    else {
        memset(&_subscriptions[idx], 0, sizeof(emcute_sub_t));
        printf("success: unsubscribed from '%s'\n", argv[1]);
        return 0;
    }
    return 1;
}

static int _will(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage %s <will topic name> <will message content>\n", argv[0]);
        return 1;
    }

    if (emcute_willupd_topic(argv[1], 0) != EMCUTE_OK) {
        puts("error: unable to update the last will topic");
        return 1;
    }
    if (emcute_willupd_msg(argv[2], strlen(argv[2])) != EMCUTE_OK) {
        puts("error: unable to update the last will message");
        return 1;
    }

    puts("success: updated last will topic and message");
    return 0;
}

static int _info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (_gw.port > 0) {
        printf("Broker: '[%s]:%u'\n",
               ipv6_addr_to_str(_addr_str, (ipv6_addr_t *)_gw.addr.ipv6,
                                sizeof(_addr_str)), _gw.port);
        puts("- Topics:");
        for (unsigned i = 0; i < NUMOFTOPS; i++) {
            if (_topics[i].name) {
                printf("  %2u: %s\n", _topics[i].id,
                       _topics[i].name);
            }
        }
        puts("- Subscriptions:");
        for (unsigned i = 0; i < NUMOFTOPS; i++) {
            if (_subscriptions[i].topic.name) {
                printf("  %2u: %s\n", _subscriptions[i].topic.id,
                       _subscriptions[i].topic.name);
            }
        }
    }
    return 0;
}

int main(void)
{
    puts("success: starting test application");
    /* setup the connection context */
    asymcute_listener_run(&_connection, listener_stack, sizeof(listener_stack),
                          LISTENER_PRIO, _on_con_evt);
    /* start shell */
    shell_run(_shell_commands, _shell_buffer, sizeof(_shell_buffer));
    return 0;
}
