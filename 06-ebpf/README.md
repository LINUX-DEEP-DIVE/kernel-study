# 6단계. eBPF: 커널을 고치지 않고 커널 안에 코드 넣기

목표: 지금까지 읽기만 했던 커널 함수들(`do_syscall_64`, `handle_mm_fault`, `copy_process` ...)에 **실행 중에** 훅을 걸어서 관찰한다. 4단계의 커널 모듈과 달리, eBPF는 커널의 verifier가 검사를 통과한 코드만 받아 주므로 커널을 망가뜨리지 않는다.

## 1부. bpftrace 원라이너 (설치: `sudo apt install bpftrace`)

앞 단계에서 본 것들을 커널 쪽에서 다시 봅니다. 모두 `sudo`로 실행하고 Ctrl+C로 멈추면 결과가 나옵니다.

```bash
# [1단계] 어떤 프로세스가 시스템 콜을 가장 많이 부르나
bpftrace -e 'tracepoint:raw_syscalls:sys_enter { @[comm] = count(); }'

# [1단계] raw_write가 부르는 시스템 콜 번호를 순서대로
bpftrace -e 'tracepoint:raw_syscalls:sys_enter /comm == "raw_write"/ { printf("%d\n", args->id); }'

# [2단계] 새 프로세스 생성 (fork/clone)과 그 부모
bpftrace -e 'tracepoint:sched:sched_process_fork { printf("%s(%d) -> %d\n", args->parent_comm, args->parent_pid, args->child_pid); }'

# [2단계] 컨텍스트 스위치: 누가 CPU를 내주고 누가 받나
bpftrace -e 'tracepoint:sched:sched_switch { @[args->prev_comm, args->prev_pid, args->next_comm, args->next_pid] = count(); }'

# [3단계] 프로세스별 page fault 수 (lazy_alloc 실행하면서)
bpftrace -e 'software:page-faults:1 { @[comm] = count(); }'

# [3단계] COW가 일어나는 순간 (fork_cow 실행하면서)
bpftrace -e 'kprobe:do_wp_page { @[comm] = count(); }'

# [4단계] /dev/deepdive의 read가 얼마나 오래 블록되나 (ns 히스토그램)
bpftrace -e 'kprobe:deepdive_read { @start[tid] = nsecs; } kretprobe:deepdive_read /@start[tid]/ { @ns = hist(nsecs - @start[tid]); delete(@start[tid]); }'

# [5단계] cgroup 메모리 한도 때문에 OOM이 나는 순간
bpftrace -e 'kprobe:mem_cgroup_out_of_memory { printf("memcg OOM in %s(%d)\n", comm, pid); }'
```

어떤 훅을 걸 수 있는지는 `sudo bpftrace -l 'tracepoint:sched:*'`, `sudo bpftrace -l 'kprobe:*page_fault*'`로 찾습니다.

## 2부. Go + C로 나만의 eBPF 도구: execsnoop

| 파일 | 내용 |
|---|---|
| [execsnoop/bpf/execsnoop.bpf.c](execsnoop/bpf/execsnoop.bpf.c) | 커널에서 도는 부분. `sched_process_exec` 트레이스포인트, CO-RE로 `task->real_parent->tgid` 읽기, ring buffer |
| [execsnoop/main.go](execsnoop/main.go) | 유저 공간 부분. [cilium/ebpf](https://github.com/cilium/ebpf)로 로드, 부착, 이벤트 출력 |

```bash
sudo apt install clang libbpf-dev
cd execsnoop && make
sudo ./execsnoop
# 다른 터미널에서 아무 명령이나 실행해 보기
```

```
PID      PPID     CGROUP       COMM             FILENAME
5228     5216     8213         true             /bin/true
5229     5216     8213         ls               /usr/bin/ls
```

```
 clang -target bpf            cilium/ebpf (Go)                  커널
 ────────────────────         ──────────────────────────        ─────────────────────────
 execsnoop.bpf.c  ──▶ .o ──▶  LoadCollectionSpec               
                              NewCollection   ── bpf(PROG_LOAD) ─▶ verifier 검사, JIT 컴파일
                              link.Tracepoint ── perf_event_open ─▶ sched_process_exec에 부착
                              ringbuf.Reader  ◀── epoll ──────────  bpf_ringbuf_submit()
```

## 읽을 자료

1. [`Documentation/bpf`](https://docs.kernel.org/bpf/index.html), 특히 [verifier](https://docs.kernel.org/bpf/verifier.html)
2. 커널 쪽: [`bpf_check`](https://elixir.bootlin.com/linux/latest/A/ident/bpf_check) (verifier 진입점), [`bpf_ringbuf_submit`](https://elixir.bootlin.com/linux/latest/A/ident/bpf_ringbuf_submit)
3. [BPF CO-RE reference guide](https://nakryiko.com/posts/bpf-core-reference-guide/) (Andrii Nakryiko)
4. [cilium/ebpf examples](https://github.com/cilium/ebpf/tree/main/examples)
5. Brendan Gregg, *BPF Performance Tools*

## 실습 과제

**bpftrace**
- [ ] 위 원라이너를 각각 앞 단계 예제와 함께 실행해서, 앞에서 C/Go로 잰 숫자(page fault 수, 시스템 콜 수)와 커널 쪽에서 센 숫자가 맞는지 비교하기
- [ ] `sched_switch` 결과에서 `nice_share`(2단계)의 두 프로세스가 서로 몇 번 CPU를 주고받았는지 찾기

**execsnoop**
- [ ] `bpf/execsnoop.bpf.c`에서 `if (!e) return 0;`을 지우고 다시 빌드, 실행. verifier가 무슨 말로 거부하나? 왜 그게 위험한가?
- [ ] `sudo bpftool prog list`, `sudo bpftool prog dump xlated id <id>`로 커널에 올라간 내 프로그램을 보기. `dump jited`는?
- [ ] `CGROUP` 열의 숫자가 5단계 minibox 컨테이너 안에서 실행한 명령만 다르게 나온다. `stat -c %i /sys/fs/cgroup/minibox/<pid>`와 비교하기. `-cgroup` 옵션으로 그 컨테이너만 보기
- [ ] `struct task_struct`를 정의할 때 `preserve_access_index`를 지우면 어떻게 될까? CO-RE가 해결해 주는 문제는 무엇인가? (`bpftool btf dump file /sys/kernel/btf/vmlinux format c | grep -A5 "^struct task_struct {"`)

**확장**
- [ ] 실행 인자(argv)까지 출력하기 (힌트: `tracepoint/syscalls/sys_enter_execve`)
- [ ] `opensnoop` 만들기: 어떤 프로세스가 어떤 파일을 여는지 (`sys_enter_openat`)
- [ ] 프로세스별 page fault를 BPF hash map에 모으고 Go에서 1초마다 읽어서 top처럼 출력하기
- [ ] k8s: 노드에서 execsnoop을 돌리고 Pod 안에서 `kubectl exec`를 해 보기. cgroup id로 어떤 Pod인지 역추적하기 (Cilium, Tetragon, Falco가 하는 일의 출발점)

## 다음 단계와의 연결

[7단계](../07-own-os)에서는 지금까지 만든 것들(커널 설정, 모듈, minibox, execsnoop)을 하나로 묶어서 **내 OS 이미지**를 만듭니다.
