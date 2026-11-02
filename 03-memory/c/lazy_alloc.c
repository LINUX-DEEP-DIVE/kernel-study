// 메모리를 "할당"하는 것과 "실제로 쓰는 것"은 다르다.
// 1GiB를 mmap해도 RSS(실제 물리 메모리 사용량)는 그대로다. 만지는 순간 page fault가 난다.
//   ./lazy_alloc
//   ./lazy_alloc huge     -> MADV_HUGEPAGE로 2MiB 페이지를 요청하면 fault 수가 어떻게 될까?
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#define SIZE (1UL << 30)

static long rss_kib(void)
{
	FILE *f = fopen("/proc/self/status", "r");
	char line[256];
	long kib = -1;
	if (!f)
		return -1; // RSS를 알 수 없음
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "VmRSS: %ld kB", &kib) == 1)
			break;
	fclose(f);
	return kib;
}

static long minflt(void)
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	return ru.ru_minflt;
}

static void show(const char *what, long f0)
{
	printf("%-28s RSS=%8ld KiB   minor faults=+%ld\n", what, rss_kib(), minflt() - f0);
}

int main(int argc, char **argv)
{
	int huge = argc > 1 && strcmp(argv[1], "huge") == 0;
	long f0 = minflt();
	show("start", f0);

	char *p = mmap(NULL, SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	if (huge && madvise(p, SIZE, MADV_HUGEPAGE) < 0)
		perror("madvise(MADV_HUGEPAGE)");
	f0 = minflt();
	show("after mmap 1GiB", f0);

	volatile char sum = 0;
	for (size_t i = 0; i < SIZE; i += 4096)
		sum += p[i];
	show("after READ every page", f0);

	f0 = minflt();
	for (size_t i = 0; i < SIZE / 2; i += 4096)
		p[i] = 1;
	show("after WRITE half", f0);

	madvise(p, SIZE, MADV_DONTNEED);
	show("after MADV_DONTNEED", f0);

	munmap(p, SIZE);
	return 0;
}
