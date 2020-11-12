#! /bin/sh

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
TEMPFILE=$(mktemp)
SUDO=sudo

CREATE_IFACE=${CREATE_IFACE:-1}
CREATE_TUN=0
ENDLESS_LOOP=${ENDLESS_LOOP:-1}

BRIDGE_NAME=
ENABLE_FORWARDING=0
IFACE_NAME_PREFIX=tap
IFACE_NAMES=
IFACE_NUM=${IFACE_NUM:-1}
IFACE_ADDRESSES=
PREFIX=""
ACTIVATED_FORWARDING=
ADDRESSES_ADDED=
ROUTES_ADDED=
SEARCH_USB_CDC_ECM_IFACE=0
USE_DHCPV6=0
USE_UHCP=0
USE_ZEP_DISPATCH=0
_USER="${USER}"

TAPSETUP="${TAPSETUP:-"$(readlink -f $(dirname $0)"/../tapsetup")/tapsetup"}"
DHCPD="${DHCPD:-"$(readlink -f $(dirname $0)"/../dhcpdv6-pd_ia")/dhcpv6-pd_ia.py"}"
UHCPD="${UHCPD:-"$(readlink -f $(dirname $0)"/../uhcpd/bin")/uhcpd"}"

if ! command -v sudo 2>&1 > /dev/null; then
    echo "sudo command not found" >&2
    exit 1
else
    echo "sudo required. Checking for permissions." >&2
    sudo true
    if [ "$?" -ne 0 ]; then
        echo "No sudo permissions" >&2
        exit 1
    fi
fi

. $(dirname ${TAPSETUP})/functions.sh

usage() {
    # TODO
    echo "wrong!" >&2
}

create_interface() {
    if [ ${CREATE_IFACE} -ne 0 ]; then
        PARAMS=""
        if [ ${CREATE_TUN} -ne 0 ]; then
            PARAMS="${PARAMS} --create 1 --tun ${IFACE_NAME_PREFIX}"
        else
            PARAMS="${PARAMS} --create ${IFACE_NUM}"
            PARAMS="${PARAMS} --bridge ${IFACE_NAME_PREFIX}br0"
            PARAMS="${PARAMS} --tap ${IFACE_NAME_PREFIX}"
        fi
        ${TAPSETUP} ${PARAMS} || exit 1
    else
        # output to match expected output of tapsetup
        echo "creating ${IFACE_NAME_PREFIX}0"
    fi
}

cleanup_interface() {
    if [ -n "${ROUTES_ADDED}" ]; then
        BRIDGE_ROUTES="${ROUTES_ADDED}" del_ipv6_routes || exit 1
    fi
    if [ -n "${ADDRESSES_ADDED}" ]; then
        BRIDGE_ADDRS=${ADDRESSES_ADDED} del_ipv6_addrs || exit 1
    fi
    if [ -n "${ACTIVATED_FORWARDING}" ]; then
        deactivate_forwarding || exit 1
    fi
    if [ ${CREATE_IFACE} -ne 0 ]; then
        PARAMS="--delete"
        if [ "${CREATE_TUN}" -ne 0 ]; then
            PARAMS="${PARAMS} --tun ${IFACE_NAME_PREFIX}"
        else
            PARAMS="${PARAMS} --bridge ${IFACE_NAME_PREFIX}br0"
        fi
        ${TAPSETUP} ${PARAMS} || exit 1
    fi
    rm -f ${TEMPFILE}
}

cleanup_setup() {
    echo "Cleaning up..."
    cleanup_interface
    if [ -n "${ZEP_DISPATCH_PID}" ]; then
        kill "${ZEP_DISPATCH_PID}"
    fi
    if [ -n "${UHCPD_PID}" ]; then
        kill "${UHCPD_PID}"
    fi
    if [ -n "${DHCPD_PIDFILE}" ]; then
        kill "$(cat ${DHCPD_PIDFILE})"
        rm -f "${DHCPD_PIDFILE}"
    fi
    trap "" INT QUIT TERM EXIT
}

find_cdc_ecm_iface() {
    if [ "${PLATFORM}" != "Linux" ]; then
        unsupported_platform
        exit 1
    fi
    INTERFACE_CHECK_COUNTER=5
    while [ ${INTERFACE_CHECK_COUNTER} -ne 0 -a -z "${IFACE_NAMES}" ]; do
        INTERFACE=$(ls -A /sys/bus/usb/drivers/cdc_ether/*/net/ 2>/dev/null | head -n 1)
        # We want to have multiple opportunities to find the USB interface
        # as sometimes it can take a few seconds for it to enumerate after
        # the device has been flashed.
        sleep 1
        if [ -n "${INTERFACE}" ]; then
            IFACE_NAMES=$(basename "${INTERFACE}")
        fi
        INTERFACE_CHECK_COUNTER=$(( INTERFACE_CHECK_COUNTER - 1))
    done
    if [ -z "${IFACE_NAMES}" ]; then
        echo "Unable to find USB network interface" >&2
        exit 1
    else
        echo "Found USB network interface: ${IFACE_NAMES}" >&2
    fi
}

daemon_running() {
    ps -eo args | grep -q "^$1" || exit 1
}

start_dhcpd() {
    if ! daemon_running ${DHCPD}; then
        DHCPD_PIDFILE=$(mkfile)
        ${DHCPD} -d -p ${DHCPD_PIDFILE} ${BRIDGE_NAME} ${PREFIX} 2> /dev/null
    fi
}

start_uhcpd() {
    if ! daemon_running ${UHCPD}; then
        ${UHCPD} ${BRIDGE_NAME} ${PREFIX}
        UHCPD_PID=$!
    fi
}

start_zep_dispatch() {
    if ! daemon_running ${ZEP_DISPATCH}; then
        ${ZEP_DISPATCH} :: ${ZEP_PORT_BASE}
        ZEP_DISPATCH_PID=$!
    fi
}

trap "cleanup_setup" INT QUIT TERM EXIT

while true ; do
    case "$1" in
        -a|--address)
            if echo "$2" | grep -q "^[a-f0-9:]\+\(/[0-9]\+\)\?$"; then
                IFACE_ADDRESSES="${IFACE_ADDRESSES} $2"
                shift 2
            else
                usage
                exit 2
            fi
            ;;
        -c|--interface-count)
            case "$2" in
                ""|*[!0-9]*)
                    usage
                    exit 2
                    ;;
                *)  IFACE_NUM="$2"
                    shift 2
                    ;;
            esac
            ;;
        -d|--dhcpv6)
            USE_DHCPV6=1
            shift 1
            ;;
        -e|--usb-cdc-ecm)
            case "$2" in
                "")
                    usage
                    exit 2
                    ;;
                *)
                    if [ "$2" = "find" ]; then
                        SEARCH_USB_CDC_ECM_IFACE=1
                    else
                        IFACE_NAMES="$2"
                    fi
                    shift
                    ;;
            esac;
            IFACE_NUM=1
            CREATE_IFACE=0
            shift 1
            ;;
        -f|--forwarding)
            ENABLE_FORWARDING=1
            shift 1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -n|--name-prefix)
            case "$2" in
                "") usage
                    exit 2
                    ;;
                *)  IFACE_NAME_PREFIX="$2"
                    shift 2
                    ;;
            esac
            ;;
        -r|--route)
            # check if valid address + optional prefix length
            if ! echo "$2" | grep -q "^[a-f0-9:]\+\(/[0-9]\+\)\?$"; then
                usage
                exit 2
            fi
            # check if valid next hop
            if ! echo "$3" | grep -q "^[a-f0-9:]\+$"; then
                usage
                exit 2
            fi
            BRIDGE_ROUTE="$2-$3"
            PREFIX=$2
            shift 3
            ;;
        -s|--stop)
            ENDLESS_LOOP=0
            shift 1
            ;;
        -t|--tun)
            CREATE_TUN=1
            shift 1
            ;;
        -u|--uhcp)
            USE_UHCP=1
            shift 1
            ;;
        -U|--user)
            case "$2" in
                "")
                    usage
                    exit 2 ;;
                *)
                    id "$2" > /dev/null || exit 2
                    _USER="$2"
                    shift 2 ;;
            esac ;;
        -z|--zep-dispatch)
            USE_ZEP_DISPATCH=1
            shift 1
            ;;
        "") break
            ;;
        *)  ;;
    esac
done

if [ ${CREATE_IFACE} -eq 1 ] && \
   ${TAPSETUP} -l ${IFACE_NAME_PREFIX}0 > /dev/null 2> /dev/null; then
    if [ ${IFACE_NUM} -eq 1 ]; then
        CREATE_IFACE=0
    else
        true
        # TODO
    fi
else
    CREATE_IFACE=1
fi


create_interface | tee "${TEMPFILE}" || exit 1
if [ ${CREATE_IFACE} -eq 1 ]; then
    IFACE_NAMES=$(grep "^creating [^ ]\+[0-9]\+$" "${TEMPFILE}" | \
                  cut -d' ' -f2 | tr '\n' ' ')
    BRIDGE_NAME=$(grep "^creating bridge [^ ]\+[0-9]\+$" "${TEMPFILE}" | \
                  cut -d' ' -f3)
elif [ ${SEARCH_USB_CDC_ECM_IFACE} -eq 1 ]; then
    find_cdc_ecm_iface || exit 1
fi
if [ -z "${BRIDGE_NAME}" ]; then
    if [ "${IFACE_NUM}" -ne 1 ]; then
        echo "Something went wrong! We have multiple interfaces but no bridge." >&2
        exit 2
    fi
    BRIDGE_NAME=$(echo ${IFACE_NAMES} | cut -d' ' -f1)
fi
if [ ${ENABLE_FORWARDING} -eq 1 ]; then
    BRNAME="${BRIDGE_NAME}" activate_forwarding && ACTIVATED_FORWARDING=1
fi
for addr in ${IFACE_ADDRESSES}; do
    BRNAME="${BRIDGE_NAME}" add_ipv6_addr "${addr}" && \
        ADDRESSES_ADDED="${ADDRESSES_ADDED} ${addr}"
done
if [ -n "${BRIDGE_ROUTE}" ]; then
    BRNAME="${BRIDGE_NAME}" add_ipv6_route "${BRIDGE_ROUTE}" && \
        ROUTES_ADDED="${ROUTES_ADDED} ${BRIDGE_ROUTE}"
fi

if [ ${USE_DHCPV6} -eq 1 ]; then
    start_dhcpd &
fi
if [ ${USE_UHCP} -eq 1 ]; then
    start_uhcpd &
fi
if [ ${USE_ZEP_DISPATCH} -eq 1 ]; then
    start_zep_dispatch &
fi

if [ ${ENDLESS_LOOP} -eq 1 ]; then
    echo "info file: ${TEMPFILE}"
    # check existence of TEMPFILE to ensure the script finishes after
    # cleanup_interface was called by trap
    while [ -f "${TEMPFILE}" ]; do
        sleep 60
    done
fi
