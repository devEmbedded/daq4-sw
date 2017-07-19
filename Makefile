###############################################################################
# Platform config

BINNAME = daq4.elf
LDSCRIPT = src/stm32f042.ld
CC      = arm-none-eabi-gcc
SIZE    = arm-none-eabi-size 
CFLAGS  = -I . -std=gnu99
CFLAGS += -fdata-sections -ffunction-sections -Os
CFLAGS += -ggdb3 -mcpu=cortex-m0 -mthumb -DSTM32F0 -msoft-float
CFLAGS += -Wall
LFLAGS  = -T$(LDSCRIPT) -lgcc -nostartfiles -Wl,--gc-sections

###############################################################################
# libopencm3
OPENCM3_DIR ?= libopencm3
CFLAGS += -I $(OPENCM3_DIR)/include
LFLAGS += -L$(OPENCM3_DIR)/lib
LIBS += $(OPENCM3_DIR)/lib/libopencm3_stm32f0.a

###############################################################################
# baselibc
BASELIBC_DIR ?= baselibc
CFLAGS += -I $(BASELIBC_DIR)/include
LIBS += $(BASELIBC_DIR)/libc.a

###############################################################################
# Source code files
CSRC = src/main.c src/board.c
CSRC += src/buffer.c src/usbnet.c src/usbnet_descriptors.c
CSRC += src/tcpip.c src/tcpip_diagnostics.c
CSRC += src/http.c src/http_index.c
CSRC += src/libc_glue.c

###############################################################################
# Build rules

all: $(BINNAME)

clean:
	rm -f *.elf *.o

libopencm3/Makefile baselibc/Makefile:
	git submodule init
	git submodule update

libopencm3/lib/opencm3_stm32f0.a: libopencm3/Makefile
	make -C libopencm3

baselibc/libc.a: baselibc/Makefile
	make -C baselibc PLATFORM=cortex-m0

-include .deps

$(BINNAME): $(CSRC) $(LDSCRIPT) $(LIBS) Makefile
	$(CC) $(CFLAGS) -M -MT daq4.elf $(CSRC) > .deps
	$(CC) $(CFLAGS) -o $@ -Wl,--start-group $(LFLAGS) $(LIBS) $(CSRC) -Wl,--end-group
	$(SIZE) -t $@

###############################################################################
# OpenOCD program / debug

GDB = arm-none-eabi-gdb
OOCD = openocd
OOCDFLAGS = -f interface/stlink-v2.cfg -f target/stm32f0x.cfg

program: $(BINNAME)
	$(OOCD) $(OOCDFLAGS) -c "program $< verify reset exit"

debug: $(BINNAME)
	$(GDB) -iex 'target remote | $(OOCD) $(OOCDFLAGS) -c "gdb_port pipe"' \
               -iex 'mon halt' $<

