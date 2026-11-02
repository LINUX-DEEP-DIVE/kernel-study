// fork()와 Copy-on-Write 관찰
//   빌드 후: ./fork_cow
//   관찰:    strace -f ./fork_cow   (fork()가 실제로 어떤 시스템 콜로 바뀌는지)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define SIZE (64UL << 20) // 64 MiB

static long minflt(void)
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	return ru.ru_minflt;
}

int main(void)
{
	int value = 42;
	char *big = malloc(SIZE);
	memset(big, 'P', SIZE); // 부모가 먼저 모든 페이지를 실제로 만들어 둔다

	printf("[parent] pid=%d, &value=%p, value=%d\n", getpid(), (void *)&value, value);

	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		return 1;
	}

	if (pid == 0) {
		// 자식: 주소는 부모와 같은데, 값을 바꾸면 부모에게 보일까?
		value = 100;
		printf("[child ] pid=%d ppid=%d, &value=%p, value=%d\n",
		       getpid(), getppid(), (void *)&value, value);

		// 64 MiB를 읽기만 할 때와 쓸 때 page fault 수 비교
		long f0 = minflt();
		volatile char sum = 0;
		for (size_t i = 0; i < SIZE; i += 4096)
			sum += big[i];
		long f1 = minflt();
		for (size_t i = 0; i < SIZE; i += 4096)
			big[i] = 'C';
		long f2 = minflt();

		printf("[child ] read  64MiB: %5ld minor faults\n", f1 - f0);
		printf("[child ] write 64MiB: %5ld minor faults (4KiB 페이지 = %lu개)\n",
		       f2 - f1, SIZE / 4096);
		exit(7);
	}

	int status;
	waitpid(pid, &status, 0);
	printf("[parent] child %d exited with %d, value=%d, big[0]=%c\n",
	       pid, WEXITSTATUS(status), value, big[0]);
	free(big);
	return 0;
}
