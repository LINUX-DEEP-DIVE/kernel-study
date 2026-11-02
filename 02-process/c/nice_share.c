// 같은 CPU 하나에서 nice 값이 다른 프로세스들을 경쟁시키고
// 각자 얼마나 CPU를 받았는지 잰다.
//   ./nice_share 0 5          -> nice 0 과 nice 5 경쟁
//   ./nice_share 0 0 0        -> 셋이 공평하게
//   ./nice_share 0 1 10 19
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define RUN_SEC 3
#define MAX 8

static double cpu_sec(const struct rusage *ru)
{
	return ru->ru_utime.tv_sec + ru->ru_utime.tv_usec / 1e6 +
	       ru->ru_stime.tv_sec + ru->ru_stime.tv_usec / 1e6;
}

int main(int argc, char **argv)
{
	int n = argc - 1;
	if (n < 1 || n > MAX) {
		fprintf(stderr, "usage: %s NICE [NICE...] (최대 %d개)\n", argv[0], MAX);
		return 1;
	}

	int nices[MAX];
	for (int i = 0; i < n; i++) {
		char *end;
		errno = 0;
		long v = strtol(argv[i + 1], &end, 10);
		if (errno || *end || end == argv[i + 1] || v < -20 || v > 19) {
			fprintf(stderr, "nice 값은 -20 ~ 19 정수여야 합니다: %s\n", argv[i + 1]);
			return 1;
		}
		nices[i] = (int)v;
	}

	// 모두 CPU 하나에 묶는다. 안 묶으면 각자 다른 코어에서 돌아서 경쟁이 없다.
	// cpuset 때문에 CPU 0을 못 쓰는 환경도 있으니, 허용된 CPU 중 첫 번째를 고른다.
	cpu_set_t set;
	if (sched_getaffinity(0, sizeof(set), &set) < 0) {
		perror("sched_getaffinity");
		return 1;
	}
	int cpu = 0;
	while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &set))
		cpu++;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set) < 0) {
		perror("sched_setaffinity");
		return 1;
	}
	printf("모든 워커를 CPU %d에 고정\n", cpu);

	pid_t pids[MAX];
	for (int i = 0; i < n; i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			if (setpriority(PRIO_PROCESS, 0, nices[i]) < 0) {
				perror("setpriority (음수 nice는 sudo 필요)");
				_exit(1); // 요청한 nice로 못 바꿨으면 측정 자체가 틀리므로 중단
			}
			volatile unsigned long x = 0;
			time_t end = time(NULL) + RUN_SEC;
			while (time(NULL) < end)
				for (int k = 0; k < 1000000; k++)
					x++;
			_exit(0);
		}
	}

	double total = 0, got[MAX];
	int failed = 0;
	for (int i = 0; i < n; i++) {
		struct rusage ru;
		int status;
		wait4(pids[i], &status, 0, &ru);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			failed = 1;
		got[i] = cpu_sec(&ru);
		total += got[i];
	}
	if (failed) {
		fprintf(stderr, "워커가 nice 설정에 실패해서 결과를 출력하지 않습니다\n");
		return 1;
	}

	printf("%-6s %-8s %-10s %s\n", "nice", "pid", "cpu(sec)", "share");
	for (int i = 0; i < n; i++)
		printf("%-6d %-8d %-10.2f %5.1f%%\n", nices[i], pids[i], got[i],
		       100 * got[i] / total);
	return 0;
}
