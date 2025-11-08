KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Uncomment if you want to compile for using the module with the DTS
# KBUILD_CFLAGS += -DUSE_DTS

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install
