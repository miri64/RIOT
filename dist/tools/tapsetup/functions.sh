unsupported_platform() {
    echo "unsupported platform" >&2
    echo "(currently supported \`uname -s\` 'Darwin', 'FreeBSD', and 'Linux')" >&2
}

update_uplink() {
    if command -v dhclient > /dev/null; then
        dhclient $1
    else
        echo "DHCP client \`dhclient\` not found." >&2
        echo "Please reconfigure your DHCP client for interface $1" >&2
        echo "to keep up-link's IPv4 connection." >&2
    fi
}

activate_forwarding() {
    if [ ${ENABLE_FORWARDING} -eq 1 ]; then
        case "${PLATFORM}" in
            FreeBSD|OSX)
                ${SUDO} sysctl -w net.inet.ip.forwarding=1 || exit 1 ;;
            Linux)
                ${SUDO} sysctl -w net.ipv6.conf.${BRNAME}.forwarding=1 || exit 1
                if [ -z ${TUNNAME} ]; then
                    ${SUDO} sysctl -w net.ipv6.conf.${BRNAME}.accept_ra=0 || exit 1
                else
                    echo "Setting accept_ra=2 for all interfaces!" >&2
                    # See https://github.com/RIOT-OS/RIOT/issues/14689#issuecomment-668500682
                    for iface in $(ip link | grep -o "^[0-9]\+: [^:]\+" | cut -f2 -d' '); do
                        ${SUDO} sysctl -w net.ipv6.conf.${iface}.accept_ra=2 || exit 1
                    done
                fi
                ${SUDO} sysctl -w net.ipv6.conf.all.forwarding=1 || exit 1 ;;
            *)  ;;
        esac
    fi
}

add_ipv6_addr() {
    ADDRESS="$(echo "${1}" | cut -d/ -f1)"
    PREFIX_LEN="$(echo "${1}" | cut -d/ -f2)"

    if [ "${1}" = "${PREFIX_LEN}" ]; then
        # prefix length is not defined
        PREFIX_LEN=${DEFAULT_PREFIX_LEN}
    fi
    case "${PLATFORM}" in
        FreeBSD|OSX)
            ${SUDO} ifconfig ${BRNAME} inet6 ${ADDRESS} prefixlen ${PREFIX_LEN} || exit 1
            ;;
        Linux)
            ${SUDO} ip address add ${ADDRESS}/${PREFIX_LEN} dev ${BRNAME} || exit 1
            ;;
        *)  ;;
    esac
}

add_ipv6_addrs() {
    for a in ${BRIDGE_ADDRS}; do
        add_ipv6_addr "${a}"
    done
}

add_ipv6_route() {
    ROUTE=$(echo "${1}" | cut -d- -f1)
    NEXT_HOP=$(echo "${1}" | cut -d- -f2)
    ROUTE_PREFIX=$(echo "${ROUTE}" | cut -d/ -f1)
    ROUTE_PREFIX_LEN=$(echo "${ROUTE}" | cut -d/ -f2)
    if [ "${ROUTE}" = "${ROUTE_PREFIX_LEN}" ]; then
        # prefix length is not defined
        ROUTE_PREFIX_LEN=${DEFAULT_PREFIX_LEN}
    fi
    case "${PLATFORM}" in
        FreeBSD|OSX)
            ${SUDO} route -6n add ${ROUTE_PREFIX} -interface ${BRNAME} \
                ${NEXT_HOP} || exit 1
            ;;
        Linux)
            ${SUDO} ip route add ${ROUTE_PREFIX}/${ROUTE_PREFIX_LEN} \
                via ${NEXT_HOP} dev ${BRNAME} || exit 1
            ;;
        *)  ;;
    esac
}

add_ipv6_routes() {
    for r in ${BRIDGE_ROUTES}; do
        add_ipv6_route "${r}"
    done
}

create_bridge() {
    echo "creating bridge ${BRNAME}"

    case "${PLATFORM}" in
        FreeBSD)
            ${SUDO} kldload if_bridge       # module might be already loaded => error
            ${SUDO} ifconfig ${BRNAME} create || exit 1 ;;
        Linux)
            ${SUDO} ip link add name ${BRNAME} type bridge || exit 1
            if [ -n "${UPLINK}" ]; then
                echo "using uplink ${UPLINK}"
                ${SUDO} ip link set dev ${UPLINK} master ${BRNAME} || exit 1
            fi
            if [ -n "${DEACTIVATE_IPV6}" ]; then
                ${SUDO} sysctl -w net.ipv6.conf.${BRNAME}.disable_ipv6=1 || exit 1
            fi ;;
        OSX)
            ${SUDO} ifconfig ${BRNAME} create || exit 1 ;;
        *)
            ;;
    esac
}

up_bridge() {
    case "${PLATFORM}" in
        FreeBSD|OSX)
            ${SUDO} ifconfig ${BRNAME} up || exit 1 ;;
        Linux)
            ${SUDO} ip link set ${BRNAME} up || exit 1

            # The bridge is now the new uplink
            if [ -n "${UPLINK}" ]; then
                update_uplink ${BRNAME}
            fi ;;
        *)
            ;;
    esac
}

deactivate_forwarding() {
    if [ ${ENABLE_FORWARDING} -eq 1 ]; then
        case "${PLATFORM}" in
            FreeBSD|OSX)
                ${SUDO} sysctl -w net.inet.ip.forwarding=0 || exit 1 ;;
            Linux)
                ${SUDO} sysctl -w net.ipv6.conf.${BRNAME}.forwarding=0 || exit 1
                if [ -z ${TUNNAME} ]; then
                    ${SUDO} sysctl -w net.ipv6.conf.${BRNAME}.accept_ra=1 || exit 1
                else
                    echo "Setting accept_ra=1 for all interfaces!" >&2
                    # See https://github.com/RIOT-OS/RIOT/issues/14689#issuecomment-668500682
                    for iface in $(ip link | grep -o "^[0-9]\+: [^:]\+" | cut -f2 -d' '); do
                        ${SUDO} sysctl -w net.ipv6.conf.${iface}.accept_ra=1 || exit 1
                    done
                fi
                ${SUDO} sysctl -w net.ipv6.conf.all.forwarding=0 || exit 1 ;;
            *)  ;;
        esac
    fi
}

del_ipv6_addr() {
    ADDRESS_ADDR=$(echo "${1}" | cut -d/ -f1)
    PREFIX_LEN=$(echo "${1}" | cut -d/ -f2)
    if [ "${1}" = "${PREFIX_LEN}" ]; then
        # prefix length is not defined
        PREFIX_LEN=${DEFAULT_PREFIX_LEN}
    fi
    case "${PLATFORM}" in
        FreeBSD|OSX)
            ${SUDO} ifconfig ${BRNAME} inet6 ${ADDRESS_ADDR} prefixlen ${PREFIX_LEN} delete || \
                exit 1
            ;;
        Linux)
            ${SUDO} ip address delete ${ADDRESS_ADDR}/${PREFIX_LEN} dev ${BRNAME} || exit 1
            ;;
        *)  ;;
    esac
}

del_ipv6_addrs() {
    for a in ${BRIDGE_ADDRS}; do
        del_ipv6_addr "${a}"
    done
}

del_ipv6_route() {
    ROUTE=$(echo "${1}" | cut -d- -f1)
    NEXT_HOP=$(echo "${1}" | cut -d- -f2)
    ROUTE_PREFIX=$(echo "${ROUTE}" | cut -d/ -f1)
    ROUTE_PREFIX_LEN=$(echo "${ROUTE}" | cut -d/ -f2)
    if [ "${ROUTE}" = "${ROUTE_PREFIX_LEN}" ]; then
        # prefix length is not defined
        ROUTE_PREFIX_LEN=${DEFAULT_PREFIX_LEN}
    fi
    case "${PLATFORM}" in
        FreeBSD|OSX)
            ${SUDO} route -6 delete ${ROUTE_PREFIX}/${ROUTE_PREFIX_LEN} \
                -interface ${BRNAME} ${NEXT_HOP} || exit 1
            ;;
        Linux)
            ${SUDO} ip route delete ${ROUTE_PREFIX}/${ROUTE_PREFIX_LEN} \
                via ${NEXT_HOP} dev ${BRNAME} || exit 1
            ;;
        *)  ;;
    esac
}

del_ipv6_routes() {
    for r in ${BRIDGE_ROUTES}; do
        del_ipv6_route "${r}"
    done
}

delete_iface() {
    IFACE="$1"
    echo "deleting ${IFACE}"
    case "${PLATFORM}" in
        FreeBSD|OSX)
            ${SUDO} ifconfig "${IFACE}" destroy ;;
        Linux)
            ${SUDO} ip link delete "${IFACE}" ;;
        *) ;;
    esac
}

delete_bridge() {
    case "${PLATFORM}" in
        FreeBSD)
            ${SUDO} sysctl net.link.tap.user_open=0
            for IF in $(ifconfig ${BRIDGE} | grep -oiE "member: .+ " | cut -d' ' -f2); do
                delete_iface ${IF}
            done
            delete_iface ${BRNAME} || exit 1
            ${SUDO} kldunload if_tap    # unloading might fail due to dependencies
            ${SUDO} kldunload if_bridge ;;
        Linux)
            for IF in $(ls /sys/class/net/${BRNAME}/brif); do
                if [ "${IF}" != "${UPLINK}" ]; then
                    delete_iface ${IF}
                fi
            done

            delete_iface ${BRNAME} || exit 1

            # restore the old uplink
            if [ -n "${UPLINK}" ]; then
                update_uplink ${UPLINK}
            fi ;;
        OSX)
            for IF in $(ifconfig ${BRIDGE} | grep -oiE "member: .+ " | cut -d' ' -f2); do
                delete_iface ${IF}
            done
            delete_iface ${BRNAME} || exit 1 ;;
        *)
            ;;
    esac
}

begin_iface() {
    if [ -z "${TUNNAME}" ]; then
        MODE="tap"
    else
        MODE="tun"
    fi
    case "${PLATFORM}" in
        FreeBSD)
            ${SUDO} kldload if_tap      # module might be already loaded => error
            ${SUDO} sysctl net.link.tap.user_open=1 ;;
        *)
            ;;
    esac
}

create_iface() {
    if [ -z "${TUNNAME}" ]; then
        NAME="${TAPNAME}"
        MODE="tap"
    else
        NAME="${TUNNAME}"
        MODE="tun"
    fi
    case "${PLATFORM}" in
        FreeBSD)
            echo "creating ${NAME}${N}" || exit 1
            ${SUDO} ifconfig ${NAME}${N} create || exit 1
            ${SUDO} chown ${_USER} /dev/${MODE}${N} || exit 1
            if [ -z "${TUNNAME}" ]; then
                ${SUDO} ifconfig ${BRNAME} addm ${NAME}${N} || exit 1
            fi
            ${SUDO} ifconfig tap${N} up || exit 1 ;;
        Linux)
            echo "creating ${NAME}${N}"
            ${SUDO} ip tuntap add dev ${NAME}${N} mode ${MODE} user ${_USER} || exit 1
            if [ -n "${DEACTIVATE_IPV6}" ]; then
                ${SUDO} sysctl -w net.ipv6.conf.${NAME}${N}.disable_ipv6=1 || exit 1
            fi
            if [ -z "${TUNNAME}" ]; then
                ${SUDO} ip link set dev ${NAME}${N} master ${BRNAME} || exit 1
            fi
            ${SUDO} ip link set ${NAME}${N} up || exit 1 ;;
        OSX)
            ${SUDO} chown ${_USER} /dev/${NAME}${N} || exit 1
            echo "start RIOT instance for ${NAME}${N} now and hit enter"
            read
            if [ -z "${TUNNAME}" ]; then
                ${SUDO} ifconfig ${BRNAME} addm ${NAME}${N} || exit 1
            fi
            ${SUDO} ifconfig ${NAME}${N} up || exit 1 ;;
        *)
            ;;
    esac
}

case "$(uname -s)" in
    Darwin)
        PLATFORM="OSX"
        if echo "$BRNAME" | grep -v -q "^bridge"; then
            BRNAME=bridge42
        fi
        if [ -n "${TUNNAME}" ]; then
            TUNNAME="tun"
        fi
        ;;
    FreeBSD)
        PLATFORM="FreeBSD"
        if echo "$BRNAME" | grep -v -q "^bridge"; then
            BRNAME=bridge0
        fi
        if [ -n "${TUNNAME}" ]; then
            TUNNAME="tun"
        fi
        ;;
    Linux)
        PLATFORM="Linux" ;;
    *)
        unsupported_platform
        exit 1 ;;
esac

