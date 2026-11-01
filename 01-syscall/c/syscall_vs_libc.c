// 시스템 콜 한 번의 비용을 재 본다.
//   - getppid(): 매번 커널에 진입한다
//   - clock_gettime(): vDSO 덕분에 커널에 진입하지 않는다
// 실행: ./syscall_vs_libc  그리고  strace -c ./syscall_vs_libc 로 실제 진입 횟수 비교
#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

#define N 1000000

static double now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e9 + ts.tv_nsec;
}

int main(void)
{
	struct timespec ts;
	volatile long sink = 0;
	double t0, t1;

	t0 = now_ns();
	for (int i = 0; i < N; i++)
		sink += syscall(SYS_getppid);
	t1 = now_ns();
	printf("getppid (real syscall): %6.1f ns/call\n", (t1 - t0) / N);

	t0 = now_ns();
	for (int i = 0; i < N; i++) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		sink += ts.tv_nsec;
	}
	t1 = now_ns();
	printf("clock_gettime (vDSO):   %6.1f ns/call\n", (t1 - t0) / N);

	return sink == 42; // 최적화로 루프가 사라지지 않게
}
