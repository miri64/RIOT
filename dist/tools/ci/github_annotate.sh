# Copyright 2020 Martine S. Lenders <m.lenders@fu-berlin.sh>
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.

LOG=cat
LOGFILE=
OUTFILE=github_annotate_outfile.log

github_annotate_setup() {
    if [ -n "${GITHUB_RUN_ID}" ]; then
        LOGFILE=run-${GITHUB_RUN_ID}.log
        LOG="tee -a ${LOGFILE}"
    fi
}

github_annotate_is_on() {
    test -n "${LOGFILE}"
    return $?
}

github_annotate_add() {
    FILENAME="${1}"
    LINENUM="${2}"
    DETAILS="${3}"
    echo "::error file=${FILENAME},line=${LINENR}::${DETAILS}" >> ${OUTFILE}
}

github_annotate_teardown() {
    if [ -n "${LOGFILE}" ]; then
        rm -f ${LOGFILE}
        LOGFILE=
    fi
}

github_annotate_report_last_run() {
    if [ -f "${OUTFILE}" ]; then
        cat ${OUTFILE} >&2
        rm ${OUTFILE}
    fi
}
