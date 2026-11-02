obj-m := deepdive.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all: module stats

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

stats: stats.c deepdive_ioctl.h
	$(CC) -O2 -Wall -Wextra -o $@ stats.c

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f stats

.PHONY: all module clean
