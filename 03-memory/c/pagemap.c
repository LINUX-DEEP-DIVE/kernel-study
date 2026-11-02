// 가상 주소 → 물리 주소 변환을 /proc/self/pagemap으로 직접 해 본다.
// 물리 프레임 번호(PFN)는 root만 볼 수 있다:  sudo ./pagemap
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned long PAGE; // 커널의 기본 페이지 크기 (x86-64는 4KiB, arm64는 4/16/64KiB)

// pagemap은 가상 페이지 하나당 8바이트 항목
//   bit 63: present,  bits 0-54: PFN
static void translate(const char *who, void *addr)
{
	uint64_t entry = 0;
	uintptr_t va = (uintptr_t)addr;
	int fd = open("/proc/self/pagemap", O_RDONLY);
	if (fd < 0) {
		perror("open /proc/self/pagemap");
		return;
	}
	ssize_t r = pread(fd, &entry, sizeof(entry), (va / PAGE) * sizeof(entry));
	close(fd);
	if (r != sizeof(entry)) {
		printf("%-8s va=%#014lx (pagemap을 읽지 못함)\n", who, (unsigned long)va);
		return;
	}

	int present = (entry >> 63) & 1;
	uint64_t pfn = entry & ((1ULL << 55) - 1);
	printf("%-8s va=%#014lx present=%d pfn=%#10lx pa=%#014lx\n", who,
	       (unsigned long)va, present, (unsigned long)pfn,
	       (unsigned long)(pfn * PAGE + va % PAGE));
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0); // fork 전후 출력이 섞이거나 사라지지 않게
	PAGE = sysconf(_SC_PAGESIZE);
	char *buf = aligned_alloc(PAGE, PAGE);
	translate("before", buf);   // 아직 안 만졌다
	buf[0] = 'A';
	translate("touched", buf);

	if (fork() == 0) {
		translate("child", buf);   // 같은 물리 페이지? (COW)
		buf[0] = 'B';
		translate("child-w", buf); // 쓰고 나면?
		_exit(0);
	}
	wait(NULL);
	translate("parent", buf);
	if (getuid() != 0)
		printf("\n(pfn이 0으로 보이면 sudo로 실행하세요)\n");
	return 0;
}
