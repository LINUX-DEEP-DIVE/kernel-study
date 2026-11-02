# LINUX-DEEP-DIVE 커널 스터디 로드맵 (초안)

원칙: **커널 모듈·커널 내부 실험은 C**, **유저스페이스 도구·컨테이너 런타임은 Go**. C++은 커널에서 쓰지 않으므로 제외를 권장합니다.
각 주차는 "읽기(소스/문서) → 실습 코드 → 관찰(strace/perf/bpftrace) → 정리 노트" 순서로 진행합니다.

## 0주차. 실습 환경
- QEMU/KVM 또는 VM에 실습용 커널 빌드 (`make defconfig && make -j`), virtme-ng로 빠르게 부팅
- 도구: `strace`, `ltrace`, `perf`, `bpftrace`, `gdb` + `CONFIG_DEBUG_INFO`, elixir.bootlin.com
- 실습: 커널 직접 빌드 후 `uname -r`에 내 이름 붙여 부팅해 보기

## 1단계. 시스템 콜 (1~2주)
- 읽기: `entry_SYSCALL_64`, `sys_call_table`, `SYSCALL_DEFINE` 매크로
- C: libc 없이 인라인 어셈블리로 `write`/`exit` 호출하기
- Go: `syscall.RawSyscall`로 같은 일 하기, Go 런타임이 syscall 진입 시 하는 일(`entersyscall`) 추적
- 도전: 커널에 나만의 syscall 추가 후 호출

## 2단계. 프로세스와 스케줄링 (2주)
- 읽기: `task_struct`, `fork/clone` 경로, CFS→EEVDF 스케줄러, `/proc/<pid>/sched`
- C: `clone()` 플래그별 동작 비교, CPU affinity·nice 실험
- Go: goroutine M:N 스케줄러(GMP)와 커널 스레드 관계를 `GODEBUG=schedtrace` + `perf sched`로 관찰
- 도전: 커널 모듈로 프로세스 트리 출력 (`for_each_process`)

## 3단계. 메모리 (2주)
- 읽기: 가상 메모리, 페이지 테이블, page fault 처리, `mm_struct`, 버디/slab 할당자
- C: `mmap`/`madvise`/`userfaultfd` 실험, `/proc/<pid>/pagemap`으로 물리 주소 확인
- Go: GC와 RSS 관계 관찰 (`GOMEMLIMIT`, `MADV_FREE` vs `DONTNEED`)
- 도전: OOM killer 동작을 cgroup 메모리 제한으로 재현

## 4단계. 커널 모듈 (C) (2주)
- hello 모듈 → `/proc`/`sysfs` 엔트리 → 캐릭터 디바이스 드라이버(`read/write/ioctl`)
- 커널 동기화: spinlock, mutex, RCU 맛보기
- 도전: 링버퍼 캐릭터 디바이스를 만들고 Go 클라이언트로 ioctl 호출

## 5단계. 네임스페이스와 cgroups → k8s 연결 (2~3주)
- 읽기: `nsproxy`, `unshare/setns`, cgroup v2 컨트롤러(cpu, memory, pids)
- Go: **미니 컨테이너 런타임 직접 만들기** (UTS/PID/Mount/Net 네임스페이스, `pivot_root`, cgroup 제한, veth 네트워크)
- k8s 연결: kubelet → containerd → runc 흐름, Pod의 QoS 클래스가 cgroup에 어떻게 매핑되는지 실제 노드에서 확인

## 6단계. eBPF (2~3주)
- bpftrace 원라이너로 syscall/스케줄러/페이지폴트 관찰
- C(BPF 프로그램) + Go(`cilium/ebpf` 로더)로 도구 만들기: execsnoop, TCP 연결 추적
- k8s 연결: Cilium, Pod 단위 관측 도구로 확장

## (선택) 7단계. 파일시스템·네트워크 스택
- VFS(`inode`, `dentry`, `file`), FUSE를 Go로 구현
- 패킷 경로: NIC → `sk_buff` → netfilter → 소켓, XDP 실습

## 제안 레포 구조
```
LINUX-DEEP-DIVE/
  kernel-study/          # 하나의 모노레포 (권장)
    00-env/  01-syscall/  02-process/  03-memory/
    04-module/  05-container/  06-ebpf/
    notes/               # 주차별 정리
```

## 7단계. 나만의 OS: DeepDive OS (2~3주)
- 읽기: initramfs, `kernel_init`, PID 1의 책임(좀비 회수, 종료), Talos/gokrazy의 Go init
- Go: PID 1 init 직접 작성 (마운트, `finit_module`, 서비스 감시, `reboot`)
- 조립: 직접 빌드한 커널 + initramfs(busybox, 4단계 모듈, 5단계 minibox, 6단계 execsnoop) → QEMU 부팅, CI에서 selftest
- 도전: tinyconfig로 커널 최소화, 커널에 initramfs 내장, 미니 PC 실물 부팅, A/B 업데이트
- 그다음: xv6 / OSDev로 커널 자체를 처음부터
