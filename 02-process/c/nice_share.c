// 같은 CPU 하나에서 nice 값이 다른 프로세스들을 경쟁시키고
// 각자 얼마나 CPU를 받았는지 잰다.
//   ./nice_share 0 5          -> nice 0 과 nice 5 경쟁
//   ./nice_share 0 0 0        -> 셋이 공평하게
//   ./nice_share 0 1 10 19
#define _GNU_SOURCE
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

	// 모두 CPU 0 하나에 묶는다. 안 묶으면 각자 다른 코어에서 돌아서 경쟁이 없다.
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(0, &set);
	if (sched_setaffinity(0, sizeof(set), &set) < 0) {
		perror("sched_setaffinity");
		return 1;
	}

	pid_t pids[MAX];
	int nices[MAX];
	for (int i = 0; i < n; i++) {
		nices[i] = atoi(argv[i + 1]);
		pids[i] = fork();
		if (pids[i] == 0) {
			if (setpriority(PRIO_PROCESS, 0, nices[i]) < 0)
				perror("setpriority (음수 nice는 sudo 필요)");
			volatile unsigned long x = 0;
			time_t end = time(NULL) + RUN_SEC;
			while (time(NULL) < end)
				for (int k = 0; k < 1000000; k++)
					x++;
			_exit(0);
		}
	}

	double total = 0, got[MAX];
	for (int i = 0; i < n; i++) {
		struct rusage ru;
		wait4(pids[i], NULL, 0, &ru);
		got[i] = cpu_sec(&ru);
		total += got[i];
	}

	printf("%-6s %-8s %-10s %s\n", "nice", "pid", "cpu(sec)", "share");
	for (int i = 0; i < n; i++)
		printf("%-6d %-8d %-10.2f %5.1f%%\n", nices[i], pids[i], got[i],
		       100 * got[i] / total);
	return 0;
}
