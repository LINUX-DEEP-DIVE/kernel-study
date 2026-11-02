# 필요: sudo apt install clang libbpf-dev
CLANG ?= clang
ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

all: execsnoop

execsnoop.bpf.o: bpf/execsnoop.bpf.c
	$(CLANG) -O2 -g -target bpf -D__TARGET_ARCH_$(ARCH) \
		-I/usr/include/$(shell uname -m)-linux-gnu -c $< -o $@

execsnoop: execsnoop.bpf.o main.go go.mod go.sum
	CGO_ENABLED=0 go build -o $@ .

clean:
	rm -f execsnoop execsnoop.bpf.o

.PHONY: all clean
