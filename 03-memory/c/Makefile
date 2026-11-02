CFLAGS ?= -O2 -Wall -Wextra
BINS := vm_layout lazy_alloc pagemap

all: $(BINS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(BINS)

.PHONY: all clean
