# 1단계. 시스템 콜

목표: 유저 공간 코드가 `syscall` 명령 하나로 커널에 들어가서 돌아오기까지의 경로를 이해한다.

## 예제

| 파일 | 내용 |
|---|---|
| [c/raw_write.c](c/raw_write.c) | libc 없이 인라인 어셈블리로 `write`, `exit` 호출 |
| [c/syscall_vs_libc.c](c/syscall_vs_libc.c) | 실제 시스템 콜과 vDSO 호출의 비용 비교 |
| [go/main.go](go/main.go) | Go의 `RawSyscall`, `Syscall`, `os.*` 비교 |

```bash
make -C c
./c/raw_write; echo "exit=$?"
./c/syscall_vs_libc
strace -c ./c/syscall_vs_libc        # getppid는 백만 번, clock_gettime은 거의 0번
cd go && go run .
```

## 읽을 커널 코드

1. [`entry_SYSCALL_64`](https://elixir.bootlin.com/linux/latest/source/arch/x86/entry/entry_64.S): `syscall` 명령이 도착하는 곳. 스택 전환과 레지스터 저장
2. [`do_syscall_64`](https://elixir.bootlin.com/linux/latest/A/ident/do_syscall_64): 번호로 `sys_call_table`을 찾아 호출
3. [`syscall_64.tbl`](https://elixir.bootlin.com/linux/latest/source/arch/x86/entry/syscalls/syscall_64.tbl): 번호와 함수의 대응표
4. [`SYSCALL_DEFINE3(write, ...)`](https://elixir.bootlin.com/linux/latest/A/ident/ksys_write): `write`의 실제 구현
5. Go 쪽: [`runtime.entersyscall`](https://github.com/golang/go/blob/master/src/runtime/proc.go)

## 실습 과제

- [ ] `raw_write`를 `objdump -d`로 열어 `syscall` 명령과 레지스터 세팅 확인하기
- [ ] `strace ./c/raw_write`의 출력이 `strace /bin/echo hi`보다 왜 훨씬 짧은지 설명하기
- [ ] `raw_write.c`에 `getpid`(39번)를 추가하고 결과를 출력하기 (정수를 문자열로 바꾸는 것도 직접)
- [ ] `syscall_vs_libc` 결과에서 시스템 콜 한 번의 비용이 몇 ns인지, vDSO가 왜 빠른지 정리하기 (`cat /proc/self/maps | grep vdso`)
- [ ] Go 벤치에서 `RawSyscall`과 `Syscall`의 차이가 어디서 오는지 `entersyscall`을 읽고 설명하기
- [ ] `sudo bpftrace -e 'tracepoint:raw_syscalls:sys_enter /comm == "raw_write"/ { printf("%d\n", args->id); }'`로 커널 쪽에서 호출 관찰하기
- [ ] 도전: 0단계에서 빌드한 커널에 나만의 시스템 콜을 추가하고 `raw_syscall3`로 호출하기
