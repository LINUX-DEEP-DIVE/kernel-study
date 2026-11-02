# 2단계. 프로세스와 스케줄링

목표: 커널에게 "프로세스"와 "스레드"는 둘 다 `task_struct` 하나이고, 차이는 무엇을 공유하느냐뿐이라는 것을 확인한다. 그리고 스케줄러가 CPU를 어떻게 나눠 주는지 숫자로 본다.

## 예제

| 파일 | 내용 | 언어 |
|---|---|---|
| [c/fork_cow.c](c/fork_cow.c) | `fork()` 후 같은 주소, 다른 값. Copy-on-Write를 page fault 수로 확인 | C |
| [c/clone_vm.c](c/clone_vm.c) | `clone()`에 `CLONE_VM` 하나 넣고 빼서 프로세스 ↔ 스레드 | C |
| [c/nice_share.c](c/nice_share.c) | 한 CPU에서 nice 값이 다른 프로세스들이 CPU를 몇 %씩 받는지 | C |
| [ptree-module/ptree.c](ptree-module/ptree.c) | 커널 모듈로 `task_struct->real_parent`를 따라 init까지 올라가기 | C (커널) |
| [go/main.go](go/main.go) | 고루틴 1만 개 vs OS 스레드 수, 블로킹 syscall이 스레드를 늘리는 순간 | Go |

```bash
make -C c
./c/fork_cow
./c/clone_vm; ./c/clone_vm share
./c/nice_share 0 5
make -C ptree-module && sudo insmod ptree-module/ptree.ko pid=$$ && sudo dmesg | tail -8 && sudo rmmod ptree
cd go && go run .
```

## 읽을 커널 코드

1. [`struct task_struct`](https://elixir.bootlin.com/linux/latest/source/include/linux/sched.h): 프로세스 하나의 모든 것. `pid`, `tgid`, `mm`, `files`, `real_parent`, `se`(스케줄링 엔티티)를 찾아보기
2. [`kernel_clone`](https://elixir.bootlin.com/linux/latest/A/ident/kernel_clone) → [`copy_process`](https://elixir.bootlin.com/linux/latest/A/ident/copy_process): `fork`, `vfork`, `clone`, `pthread_create`가 전부 여기로 모인다
3. [`copy_mm`](https://elixir.bootlin.com/linux/latest/A/ident/copy_mm): `CLONE_VM`이면 `mm`을 공유하고, 아니면 [`dup_mm`](https://elixir.bootlin.com/linux/latest/A/ident/dup_mm)으로 복사(COW 설정)
4. [`sched_prio_to_weight`](https://elixir.bootlin.com/linux/latest/A/ident/sched_prio_to_weight): nice 값 → 가중치 표. `nice_share` 결과와 직접 비교
5. [`kernel/sched/fair.c`](https://elixir.bootlin.com/linux/latest/source/kernel/sched/fair.c): EEVDF 스케줄러 (6.6부터 CFS를 대체). `vruntime`, `pick_eevdf`
6. Go 쪽: [`runtime/proc.go`](https://github.com/golang/go/blob/master/src/runtime/proc.go)의 `entersyscall`, `handoffp`, `sysmon`

## 실습 과제

직접 치고, 결과를 보고, 질문에 답해 보세요. 답은 노트에 남기기.

**fork와 COW**
- [ ] `fork_cow`에서 부모와 자식의 `&value`가 같은데 값은 다르다. 같은 주소인데 어떻게 다른 값일까? (힌트: 가상 주소 vs 물리 주소)
- [ ] 자식이 64MiB를 읽을 때 fault가 0인데 쓸 때는 16384번이다. 16384는 어디서 나온 숫자일까?
- [ ] `./fork_cow | cat`으로 실행하면 `[parent] pid=...` 줄이 두 번 찍힌다. 터미널에서는 한 번. 왜? (힌트: stdio 버퍼링과 fork 순서)
- [ ] `strace -f ./fork_cow 2>&1 | grep -E 'clone|fork'`: 우리는 `fork()`를 불렀는데 커널에는 무슨 시스템 콜이 들어갔나?

**clone**
- [ ] `clone_vm` 두 가지 실행의 `counter` 차이를 설명하기
- [ ] `strace -f -e trace=clone,clone3 python3 -c "import threading; threading.Thread(target=lambda:0).start()"`로 스레드 생성 플래그를 확인하고, `fork`의 플래그와 비교하기. `CLONE_THREAD`, `CLONE_FILES`, `CLONE_SIGHAND`는 각각 무엇을 공유하나?

**스케줄러**
- [ ] `./nice_share 0 5` 결과가 약 75% : 25%다. `sched_prio_to_weight`에서 nice 0과 5의 가중치를 찾아서 이 비율을 계산으로 맞혀 보기
- [ ] `./nice_share 0 1`, `./nice_share 0 10`, `./nice_share 0 19`를 돌려서 표와 비교하기. nice 1 차이가 약 10% 차이가 되도록 표가 설계된 이유는?
- [ ] 실행 중에 다른 터미널에서 `cat /proc/<pid>/sched`로 `se.vruntime`, `nr_switches` 보기
- [ ] `sudo perf sched record ./nice_share 0 5` 후 `sudo perf sched latency`

**커널 안에서 보기**
- [ ] `ptree` 결과의 조상 체인을 `pstree -p -s $$`와 비교하기
- [ ] 모듈은 왜 `rcu_read_lock()`을 잡고 목록을 돌까? 안 잡으면 무슨 일이 생길 수 있을까?
- [ ] 도전: `ptree.c`를 고쳐서 각 프로세스의 스레드 수와 `p->mm`이 `NULL`인 프로세스(커널 스레드)를 구분해 출력하기

**Go**
- [ ] 고루틴 1만 개인데 스레드는 5개. 고루틴이 채널에서 잠들 때 OS 스레드는 무엇을 하고 있나?
- [ ] 블로킹 `read`에서 스레드가 53개로 늘었다. `entersyscall`과 `sysmon`이 어떻게 협력해서 P를 다른 M에 넘기는지 정리하기
- [ ] `GODEBUG=schedtrace=200 go run .`에서 `threads=`, `idleprocs=` 값 변화 관찰하기

## 다음 단계와의 연결

`fork_cow`에서 본 page fault와 `mm`은 [3단계 메모리](../03-memory)로, `clone`의 `CLONE_NEW*` 플래그는 [5단계 컨테이너](../05-container)로 이어집니다.
