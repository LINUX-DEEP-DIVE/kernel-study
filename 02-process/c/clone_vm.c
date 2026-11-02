// 프로세스와 스레드의 차이는 clone() 플래그 하나다.
//   ./clone_vm          -> CLONE_VM 없이 (fork와 비슷): 자식의 변경이 부모에게 안 보임
//   ./clone_vm share    -> CLONE_VM 포함 (스레드와 비슷): 메모리를 공유함
#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)

static int counter = 0;

static int child_fn(void *arg)
{
	(void)arg;
	for (int i = 0; i < 1000; i++)
		counter++;
	printf("[child ] pid=%d tid=%d counter=%d\n", getpid(), gettid(), counter);
	fflush(stdout); // clone 자식이 리턴하면 stdio 버퍼를 비우지 않고 바로 종료된다
	return 0;
}

int main(int argc, char **argv)
{
	int share = argc > 1 && strcmp(argv[1], "share") == 0;
	int flags = SIGCHLD | (share ? CLONE_VM : 0);

	// 스택은 아래로 자라므로 할당한 영역의 "끝" 주소를 넘긴다
	char *stack = malloc(STACK_SIZE);
	pid_t pid = clone(child_fn, stack + STACK_SIZE, flags, NULL);
	if (pid < 0) {
		perror("clone");
		return 1;
	}
	waitpid(pid, NULL, 0);

	printf("[parent] pid=%d flags=%s counter=%d\n", getpid(),
	       share ? "CLONE_VM|SIGCHLD" : "SIGCHLD", counter);
	free(stack);
	return 0;
}
