CFLAGS ?= -O2 -Wall -Wextra
BINS := fork_cow clone_vm nice_share

all: $(BINS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(BINS)

.PHONY: all clean
