include ../Makefile.tests_common

USEMODULE += event
USEMODULE += event_timeout_ztimer
USEMODULE += event_thread
USEMODULE += fmt
USEMODULE += ipv6_addr
USEMODULE += od
USEMODULE += shell
USEMODULE += ztimer_msec

USEPKG += libschc
USEMODULE += libschc_coap

CFLAGS += -DEVENT_THREAD_STACKSIZE_DEFAULT=THREAD_STACKSIZE_DEFAULT

CFLAGS +=  -fprofile-arcs -ftest-coverage
CFLAGS_OPT += -O0
LINKFLAGS += -fprofile-arcs -ftest-coverage

include $(RIOTBASE)/Makefile.include

COVERAGETESTDEPS ?= $(filter term test,$(MAKECMDGOALS))

coverage-report: $(COVERAGETESTDEPS)
	gcovr -b -e "../../(boards|core|cpu|driver|sys)" --txt --html-details coverage/coverage.html -r ../../
