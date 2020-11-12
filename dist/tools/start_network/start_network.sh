#!/bin/sh

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)

SETUP_PARAMS="--address fe80::1"
ETHOS=$(readlink -f "${SCRIPT_DIR}/../ethos/ethos")
PYTERM=$(readlink -f "${SCRIPT_DIR}/../pyterm/pyterm")
SLIPTTY=$(readlink -f "${SCRIPT_DIR}/../sliptty/sliptty")

UPLINK=$1

usage() {
    # TODO
    echo "wrong!" >&2
}

case "$UPLINK" in
    ethos|slip)
        if [ $# -lt 2 ]; then
            usage
            exit 2
        fi
        PORT=$2
        SETUP_PARAMS="${SETUP_PARAMS} --route $3 fe80::2"
        shift 3
        if [ "$1" = "-b" ]; then
            if [ $# -lt 2 ]; then
                usage
            exit 2
            fi
            BAUDRATE=$2
            shift 2
        fi
        ;;
    native)
        if [ $# -lt 3 ]; then
            usage
            exit 2
        fi
        ELFFILE=$2
        SETUP_PARAMS="${SETUP_PARAMS} --route $3 fe80::2"
        ;;
    # TODO USB CDC-ECM
    *)
        usage
        exit 2
esac

. ${SCRIPT_DIR}/setup_network.sh -s ${SETUP_PARAMS} $@

case "${UPLINK}" in
    ethos)
        ${ETHOS}
esac
