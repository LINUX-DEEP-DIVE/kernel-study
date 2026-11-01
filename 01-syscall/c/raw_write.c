// libc 없이 x86-64 syscall 명령으로 직접 커널을 호출한다.
// 빌드: gcc -nostdlib -static -O2 -o raw_write raw_write.c
//
// x86-64 시스템 콜 규약:
//   rax = 시스템 콜 번호, 인자는 rdi, rsi, rdx, r10, r8, r9
//   syscall 명령이 rcx(복귀 주소)와 r11(rflags)을 덮어쓴다
//   반환값은 rax, 실패하면 -errno

#define SYS_write 1
#define SYS_exit 60

static long raw_syscall3(long n, long a1, long a2, long a3)
{
	long ret;
	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(n), "D"(a1), "S"(a2), "d"(a3)
			 : "rcx", "r11", "memory");
	return ret;
}

static long my_strlen(const char *s)
{
	long n = 0;
	while (s[n])
		n++;
	return n;
}

// libc가 없으니 main 대신 _start가 진입점이다.
void _start(void)
{
	const char *msg = "hello from a raw syscall\n";
	long n = raw_syscall3(SYS_write, 1, (long)msg, my_strlen(msg));

	// 잘못된 fd에 쓰면 커널은 -EBADF(-9)를 돌려준다
	long bad = raw_syscall3(SYS_write, 999, (long)msg, 1);

	raw_syscall3(SYS_exit, (n > 0 && bad == -9) ? 0 : 1, 0, 0);
	__builtin_unreachable();
}
