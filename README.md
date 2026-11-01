# kernel-study

LINUX-DEEP-DIVE의 리눅스 커널 딥다이브 스터디 레포입니다.
**커널 공간은 C**, **유저 공간 도구와 컨테이너 런타임은 Go**로 직접 만들어 보며 커널 내부를 공부합니다.

## 진행 순서

| 단계 | 폴더 | 주제 | 언어 |
|---|---|---|---|
| 0 | [00-env](00-env) | 실습 환경, 커널 빌드, 첫 커널 모듈 | C, shell |
| 1 | [01-syscall](01-syscall) | 시스템 콜 진입 경로 | C, Go |
| 2 | [02-process](02-process) | 프로세스, `clone`, 스케줄러 | C, Go |
| 3 | [03-memory](03-memory) | 가상 메모리, page fault, 할당자 | C, Go |
| 4 | [04-module](04-module) | 커널 모듈, 캐릭터 디바이스, 동기화 | C |
| 5 | [05-container](05-container) | namespace, cgroup v2, 미니 컨테이너 런타임 (k8s 연결) | Go |
| 6 | [06-ebpf](06-ebpf) | eBPF 관측 도구 | C, Go |

자세한 주차별 내용은 [docs/roadmap.md](docs/roadmap.md)를 보세요.

## 공부 방식

각 단계는 **읽기 → 실습 코드 → 관찰 → 정리** 순서로 진행합니다.

1. **읽기**: 해당 커널 소스를 [elixir.bootlin.com](https://elixir.bootlin.com/linux/latest/source)에서 따라가 봅니다.
2. **실습**: 각 단계 폴더의 예제를 빌드하고 `EXERCISES`를 풉니다.
3. **관찰**: `strace`, `perf`, `bpftrace`, `/proc`으로 실제 동작을 확인합니다.
4. **정리**: [notes/TEMPLATE.md](notes/TEMPLATE.md)를 복사해 `notes/<단계>-<이름>.md`로 남깁니다.

## 시작하기

```bash
# Ubuntu 24.04 기준 도구 설치
./00-env/scripts/setup.sh

# 첫 커널 모듈 올려보기 (VM에서 권장)
make -C 00-env/hello-module
sudo insmod 00-env/hello-module/hello.ko && sudo dmesg | tail -3
sudo rmmod hello

# 시스템 콜 예제
make -C 01-syscall/c && ./01-syscall/c/raw_write
cd 01-syscall/go && go run .
```

> 커널 모듈은 커널을 망가뜨릴 수 있으니 **반드시 VM이나 QEMU에서** 실습하세요. 방법은 [00-env](00-env)에 있습니다.
