// 프로세스 가상 주소 공간의 지도를 그려 본다.
//   ./vm_layout           두 번 실행해서 주소가 바뀌는 것(ASLR)도 확인
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int g_init = 1;        // .data
int g_zero;            // .bss
const char *g_ro = "x"; // 문자열 자체는 .rodata

int main(void)
{
	int local = 0;
	void *heap_small = malloc(16);          // brk 영역 (heap)
	void *heap_big = malloc(1 << 20);       // 128KiB 이상은 glibc가 mmap으로 받는다
	void *anon = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	printf("%-22s %p\n", "text   (main)", (void *)main);
	printf("%-22s %p\n", "rodata (\"x\")", (void *)g_ro);
	printf("%-22s %p\n", "data   (g_init)", (void *)&g_init);
	printf("%-22s %p\n", "bss    (g_zero)", (void *)&g_zero);
	printf("%-22s %p\n", "heap   (malloc 16)", heap_small);
	printf("%-22s %p\n", "mmap   (malloc 1MiB)", heap_big);
	printf("%-22s %p\n", "mmap   (anon 4KiB)", anon);
	printf("%-22s %p\n", "libc   (printf)", (void *)printf);
	printf("%-22s %p\n", "stack  (local)", (void *)&local);

	printf("\n--- /proc/self/maps ---\n");
	FILE *f = fopen("/proc/self/maps", "r");
	char line[512];
	if (!f) {
		perror("fopen /proc/self/maps");
		return 1;
	}
	while (fgets(line, sizeof(line), f))
		fputs(line, stdout);
	fclose(f);
	return 0;
}
