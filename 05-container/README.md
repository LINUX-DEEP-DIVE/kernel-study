# 5단계. namespace와 cgroup: Go로 미니 컨테이너 런타임 만들기

목표: "컨테이너"라는 커널 기능은 없다. **namespace(무엇이 보이는가) + cgroup(얼마나 쓸 수 있는가) + 루트 파일시스템 교체**의 조합일 뿐이라는 것을, 직접 런타임을 만들어서 확인한다.

## 예제

| 파일 | 내용 |
|---|---|
| [minibox/main.go](minibox/main.go) | 약 150줄짜리 컨테이너 런타임. UTS/PID/Mount/IPC/(Net) namespace, `pivot_root`, `/proc` 마운트, cgroup v2 메모리·PID·CPU 제한 |
| [make-rootfs.sh](make-rootfs.sh) | busybox 하나로 컨테이너용 루트 파일시스템 만들기 |

```bash
sudo apt install busybox-static
./make-rootfs.sh rootfs
cd minibox && CGO_ENABLED=0 go build -o minibox . && cd ..

sudo ./minibox/minibox run ./rootfs /bin/sh
# 컨테이너 안에서:
hostname; echo $$; ps; ls /; mount
```

## minibox가 하는 일

```
 호스트                                    새 namespace 안
 ───────────────────────────────           ─────────────────────────────────────
 minibox run                               minibox init   (PID 1, hostname=minibox)
   │ clone(CLONE_NEWUTS|NEWPID|NEWNS|
   │       NEWIPC[|NEWNET])  ────────────▶   │ 동기화 파이프(fd 3)에서 대기
   │ mkdir /sys/fs/cgroup/minibox/<pid>      │   (cgroup에 들어가기 전엔 아무것도 안 함)
   │ echo 64M > memory.max                   │
   │ echo <pid> > cgroup.procs               │
   │ 파이프에 1바이트 쓰기 ───────────────▶   │ sethostname
   │                                         │ mount --make-rprivate /
   │ wait()                                  │ bind mount rootfs, pivot_root, umount old root
   │                                         │ mount proc /proc, tmpfs /tmp
   │                                         │ execve("/bin/sh")  ← 이제 PID 1은 sh
   ▼ memory.peak, memory.events 출력         ▼ 사용자 명령 실행
```

`docker run`도 결국 containerd → runc를 거쳐 **같은 시스템 콜**로 내려옵니다. runc의 [`libcontainer/nsenter`](https://github.com/opencontainers/runc/tree/main/libcontainer)와 비교해 보세요.

## 읽을 커널 코드

1. [`struct nsproxy`](https://elixir.bootlin.com/linux/latest/A/ident/nsproxy): 프로세스가 속한 namespace 묶음. 2단계 `copy_process`의 [`copy_namespaces`](https://elixir.bootlin.com/linux/latest/A/ident/copy_namespaces)
2. PID namespace: [`alloc_pid`](https://elixir.bootlin.com/linux/latest/A/ident/alloc_pid). 프로세스 하나가 namespace 층마다 다른 PID 번호를 가진다
3. [`pivot_root`](https://elixir.bootlin.com/linux/latest/A/ident/pivot_root) 시스템 콜과 `man 2 pivot_root`의 조건들
4. cgroup v2: [`Documentation/admin-guide/cgroup-v2.rst`](https://docs.kernel.org/admin-guide/cgroup-v2.html), 메모리 한도 초과 시 [`mem_cgroup_out_of_memory`](https://elixir.bootlin.com/linux/latest/A/ident/mem_cgroup_out_of_memory)
5. `man 7 namespaces`, `man 7 cgroups`, `man 7 user_namespaces`

## 실습 과제

**namespace 하나씩 확인하기**
- [ ] 컨테이너 안과 밖에서 `ls -l /proc/self/ns`를 비교하기. 숫자(inode)가 같은 것과 다른 것은?
- [ ] 컨테이너 안에서 `echo $$`는 1인데, 호스트에서 `ps -ef | grep sh`로 보면 다른 번호다. 같은 프로세스가 두 번호를 갖는 원리는? `grep NSpid /proc/<호스트pid>/status`
- [ ] 컨테이너 안의 PID 1이 종료되면 나머지 프로세스는 어떻게 될까? 컨테이너 안에서 `sleep 100 &` 후 `exit`
- [ ] `main.go`에서 `/proc` 마운트를 빼고 빌드하면 컨테이너 안 `ps`는 무엇을 보여 줄까?
- [ ] `pivot_root` 대신 `chroot`를 쓰면 무엇이 문제인가? (힌트: chroot 탈출)
- [ ] `-net` 옵션으로 실행하고 `ip link`. 호스트와 통신하려면? **도전**: veth 쌍을 만들어 한쪽을 컨테이너 namespace에 넣고 IP를 붙여 `ping` 성공시키기 (`ip link add veth0 type veth peer name veth1`, `ip link set veth1 netns <pid>`)

**cgroup**
- [ ] `-mem 32M`로 실행하고 컨테이너 안에서 메모리 먹기: `sh -c 'x=; while true; do x="$x$x"; [ -z "$x" ] && x=aaaaaaaa; done'`. 종료 후 출력되는 `memory.events`의 `oom_kill` 확인
- [ ] `-pids 10`으로 실행하고 `for i in $(seq 20); do sleep 100 & done`. 몇 번째에서 실패하나? 에러 메시지는?
- [ ] `-cpu "20000 100000"`으로 실행하고 `yes > /dev/null`. 호스트의 `top`에서 CPU 사용률은? 2단계 `nice`와 무엇이 다른가?
- [ ] 실행 중에 호스트에서 `cat /sys/fs/cgroup/minibox/<pid>/memory.current`, `cpu.stat`

**k8s와 연결**
- [ ] k8s 노드에서 `cat /proc/<컨테이너pid>/cgroup` 으로 Pod의 cgroup 경로를 찾아보기 (`kubepods.slice/kubepods-burstable.slice/...`)
- [ ] Pod의 `resources.limits.memory: 64Mi`가 그 경로의 `memory.max`에 그대로 들어가 있는지 확인. `limits.cpu: 500m`은 `cpu.max`에 무엇으로 들어가 있나?
- [ ] QoS 클래스(Guaranteed, Burstable, BestEffort)가 cgroup 트리에서 어떻게 나뉘는지 그려 보기
- [ ] `kubectl exec`은 새 컨테이너를 만드는 게 아니라 **기존 namespace에 들어가는 것**이다. `nsenter -t <pid> -a sh`로 같은 일을 해 보기. minibox에 `exec` 명령을 추가한다면 어떤 시스템 콜(`setns`)이 필요할까?

**확장 도전**
- [ ] User namespace(`CLONE_NEWUSER`)를 추가해서 **sudo 없이** 실행되게 만들기 (`UidMappings`, `GidMappings`)
- [ ] OverlayFS로 rootfs를 읽기 전용 하위 층 + 쓰기 가능한 상위 층으로 나누기 (컨테이너 이미지 레이어의 원리)
- [ ] `seccomp`로 특정 시스템 콜(예: `mount`) 막기

## 다음 단계와의 연결

[6단계 eBPF](../06-ebpf)에서는 컨테이너 안에서 실행되는 프로세스를 커널 쪽에서 추적하고, [7단계](../07-own-os)에서는 minibox를 내 OS의 기본 컨테이너 런타임으로 넣습니다.
