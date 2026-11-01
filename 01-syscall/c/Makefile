CFLAGS ?= -O2 -Wall -Wextra

all: raw_write syscall_vs_libc

# libc와 시작 코드 없이 정적 링크
raw_write: raw_write.c
	$(CC) $(CFLAGS) -nostdlib -static -fno-stack-protector -o $@ $<

syscall_vs_libc: syscall_vs_libc.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f raw_write syscall_vs_libc

.PHONY: all clean
