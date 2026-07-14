KDIR:= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

obj-m += vdaq.o
vdaq-y := vdaq_main.o vdaq_buffer.o vdaq_control.o

ccflags-y += -I$(src)/../../include

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
