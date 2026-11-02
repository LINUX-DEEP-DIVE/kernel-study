// /dev/deepdive의 통계를 ioctl로 읽는 유저 프로그램
//   ./stats          통계 출력
//   ./stats reset    버퍼 비우기
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "deepdive_ioctl.h"

int main(int argc, char **argv)
{
	int fd = open("/dev/deepdive", O_RDWR);
	if (fd < 0) {
		perror("open /dev/deepdive (모듈을 올렸나요?)");
		return 1;
	}

	if (argc > 1 && strcmp(argv[1], "reset") == 0) {
		if (ioctl(fd, DEEPDIVE_IOC_RESET) < 0) {
			perror("ioctl RESET");
			return 1;
		}
		puts("reset");
		return 0;
	}

	struct deepdive_stats st;
	if (ioctl(fd, DEEPDIVE_IOC_GET_STATS, &st) < 0) {
		perror("ioctl GET_STATS");
		return 1;
	}
	printf("ioctl cmd = %#lx\n", (unsigned long)DEEPDIVE_IOC_GET_STATS);
	printf("written=%llu read=%llu used=%u/%u\n",
	       (unsigned long long)st.bytes_written, (unsigned long long)st.bytes_read,
	       st.used, st.capacity);
	close(fd);
	return 0;
}
