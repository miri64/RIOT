APPLICATION = driver_tapnet
include ../Makefile.tests_common

FEATURES_REQUIRED = periph_uart

RD_INSUFFICIENT_RAM := stm32f0discovery

# other boards are missing second UART
BOARD_WHITELIST := arduino-due mbed_lpc1768 msbiot nucleo-f091 \
                   stm32f0discovery stm32f3discovery stm32f4discovery udoo

USEMODULE += ng_netbase
USEMODULE += ng_pktdump
USEMODULE += ng_slip
USEMODULE += shell
USEMODULE += shell_commands

include $(RIOTBASE)/Makefile.include
