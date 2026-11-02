obj-m := ptree.o

# 기본값은 현재 실행 중인 커널. 직접 빌드한 커널에 맞추려면:
#   make KDIR=../../linux
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
