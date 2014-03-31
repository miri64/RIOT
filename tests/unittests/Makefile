export PROJECT = unittests
include ../Makefile.tests_common

USEMODULE += embunit

INCLUDES += -I$(RIOTBASE)/tests/unittests/embunit

ifeq ($(OUTPUT),XML)
	CFLAGS += -DOUTPUT=OUTPUT_XML
	USEMODULE += embunit_textui
else ifeq ($(OUTPUT),TEXT)
	CFLAGS += -DOUTPUT=OUTPUT_TEXT
	USEMODULE += embunit_textui
else ifeq ($(OUTPUT),COMPILER)
	CFLAGS += -DOUTPUT=OUTPUT_COMPILER
	USEMODULE += embunit_textui
endif

.PHONY : all clean core tests-core

include $(RIOTBASE)/Makefile.include

tests-core : CFLAGS += -DTEST_NOT_ALL -DTEST_CORE_ENABLED
tests-core : SRC := $(wildcard tests-core*.c) main.c
tests-core : OBJ := $(SRC:%.c=${BINDIR}${PROJECT}/%.o)
tests-core : all
