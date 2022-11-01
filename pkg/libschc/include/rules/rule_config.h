/*
 * Copyright (C) 2018 imec IDLab
 * Copyright (C) 2022 Freie Universität Berlin
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @internal
 * @author  boortmans <bart.moons@gmail.com>
 * @author  Martine S. Lenders <m.lenders@fu-berlin.de>
 */
#ifndef RULES_RULE_CONFIG_H
#define RULES_RULE_CONFIG_H

#include "xfa.h"

#include "schc.h"

#ifdef __cplusplus
extern "C" {
#endif

XFA_INIT_CONST(struct schc_device *, devices);

#define DEVICE_COUNT (XFA_LEN(struct schc_device *, devices))

#define SCHC_COMPRESSION_RULES_INIT(comp_rules_name) \
    XFA_INIT_CONST(struct schc_compression_rule_t *, _xfa_schc_comp_rules_ ## comp_rules_name)

#if (USE_IP6 == 1) && (USE_UDP == 1) && (USE_COAP == 1)
#define SCHC_COMPRESSION_RULE_INIT( \
        comp_rules_name, \
        comp_rule_name, \
        rule_id, \
        rule_id_size_bits, \
        ipv6_rule_ptr, \
        udp_rule_ptr, \
        coap_rule_ptr \
    ) { \
        .rule_id = (rule_id), \
        .rule_id_size_bits = (rule_id_size_bits), \
        .ipv6_rule = (ipv6_rule_ptr), \
        .udp_rule = (udp_rule_ptr), \
        .coap_rule = (coap_rule_ptr), \
    }
#elif (USE_IP6 == 1) && (USE_UDP == 1)
#define SCHC_COMPRESSION_RULE_INIT( \
        rule_id, \
        rule_id_size_bits, \
        ipv6_rule_ptr, \
        udp_rule_ptr, \
        coap_rule_ptr \
    ) { \
        .rule_id = (rule_id), \
        .rule_id_size_bits = (rule_id_size_bits), \
        .ipv6_rule = (ipv6_rule_ptr), \
        .udp_rule = (udp_rule_ptr), \
    }; (void)coap_rule_ptr;
#elif (USE_IP6 == 1)
#define SCHC_COMPRESSION_RULE_INIT( \
        rule_id, \
        rule_id_size_bits, \
        ipv6_rule_ptr, \
        udp_rule_ptr, \
        coap_rule_ptr \
    ) { \
        .rule_id = rule_id, \
        .rule_id_size_bits = (rule_id_size_bits), \
        .ipv6_rule = (ipv6_rule_ptr), \
    }; (void)(udp_rule_ptr); (void)(coap_rule_ptr)
#else
#endif

#if (USE_IP6 == 1) || (USE_UDP == 1) || (USE_COAP == 1)
#define SCHC_COMPRESSION_RULE_ADD( \
        comp_rules_name, \
        comp_rule_name, \
        rule_id, \
        rule_id_size_bits, \
        ipv6_rule_ptr, \
        udp_rule_ptr, \
        coap_rule_ptr \
    ) \
    XFA_USE_CONST(struct schc_compression_rule_t *, _xfa_schc_comp_rules_ ## comp_rules_name); \
    static const struct schc_compression_rule_t _xfa_schc_comp_rule_ ## comp_rule_name = \
        SCHC_COMPRESSION_RULE_INIT(rule_id, rule_id_size_bits, ipv6_rule_ptr, udp_rule_ptr, coap_rule_ptr); \
    XFA_ADD_PTR(_xfa_schc_comp_rules_ ## comp_rules_name, \
                rule_id, device_name, \
                &_xfa_schc_comp_rule_ ## comp_rule_name)
#else
#define SCHC_COMPRESSION_COAP_RULE_ADD( \
        comp_rules_name, \
        comp_rule_name, \
        rule_id, \
        rule_id_size_bits, \
        ipv6_rule_name, \
        udp_layer_name, \
        coap_layer_name \
    )
#endif

#define SCHC_FRAGMENTATION_RULES_INIT(frag_rules_name) \
    XFA_INIT_CONST(struct schc_fragmentation_rule_t *, _xfa_schc_frag_rules_ ## frag_rules_name)

#define SCHC_FRAGMENTATION_RULE_ADD( \
        frag_rules_name, \
        frag_rule_name, \
        rule_id, \
        mode, \
        dir, \
        fcn_size, \
        max_wnd_fcn, \
        window_size, \
        dtag_size \
    ) \
    XFA_USE_CONST(struct schc_fragmentation_rule_t *, _xfa_schc_frag_rules_ ## frag_rules_name); \
    static const struct schc_fragmentation_rule_t _xfa_schc_frag_rule_ ## frag_rule_name = { \
        .rule_id = (rule_id), \
        .rule_id_size_bits = (rule_id_size_bits), \
        .mode = (mode), \
        .dir = (dir), \
        .FCN_SIZE = (fcn_size), \
        .MAX_WND_FCN = (max_wnd_fcn), \
        .WINDOW_SIZE = (window_size), \
        .DTAG_SIZE = (dtag_size) \
    }; \
    XFA_ADD_PTR(_xfa_schc_frag_rules_ ## frag_rules_name, \
                rule_id, device_name, \
                &_xfa_schc_frag_rule_ ## frag_rule_name)

#define SCHC_DEVICE(device_name, uncomp_rule_id, uncomp_rule_id_size_bits, \
                    comp_rules_name, frag_rules_name) \
    XFA_USE_CONST(struct schc_device *, devices); \
    static const struct schc_device _xfa_schc_device_ ## device_name = { \
        .device_id = (XFA_LEN(struct schc_device *, devices) + 1), \
        .uncomp_rule_id = (uncomp_rule_id), \
        .uncomp_rule_id_size_bits = (uncomp_rule_id_size_bits), \
        .compression_rule_count = XFA_LEN(_xfa_schc_comp_rules_ ## comp_rules_name), \
        .compression_context = _xfa_schc_comp_rules_ ## comp_rules_name, \
        .fragmentation_rule_count = XFA_LEN(_xfa_schc_frag_rules_ ## frag_rules_name), \
        .fragmentation_context = _xfa_schc_frag_rules_ ## frag_rules_name, \
    }; \
    XFA_ADD_PTR(devices, _xfa_schc_device_ ## device_name.device_id, device_name, \
                _xfa_schc_device_ ## name);

#ifdef __cplusplus
}
#endif

#endif /* RULES_RULE_CONFIG_H */
