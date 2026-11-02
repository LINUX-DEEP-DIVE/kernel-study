# 7단계. 나만의 OS 만들기: DeepDive OS

목표: 지금까지 만든 조각들을 하나로 묶어서 **부팅되는 OS 이미지**를 만든다. 커널은 리눅스를 쓰고, 그 위의 모든 것(PID 1, 서비스 관리, 드라이버 로드, 컨테이너 런타임, 관측 도구)은 우리가 직접 만든 것으로 채운다.

회사들이 "자체 OS"를 만든다고 할 때 대부분 이 방식입니다. 커널은 리눅스를 가져오고, 설정과 init, 유저랜드를 자기 용도에 맞게 깎고 붙입니다.

| 자체 OS | 커널 | 위에 올린 것 |
|---|---|---|
| Android | 리눅스 (패치) | 자체 init, Binder, ART |
| ChromeOS | 리눅스 | upstart, 읽기 전용 루트, 자동 업데이트 |
| [Talos Linux](https://github.com/siderolabs/talos) | 리눅스 | **Go로 짠 PID 1(machined)**, 셸 없음, API로만 관리, k8s 전용 |
| [Bottlerocket](https://github.com/bottlerocket-os/bottlerocket) (AWS) | 리눅스 | Rust 에이전트, 컨테이너 전용 |
| [gokrazy](https://github.com/gokrazy/gokrazy) | 리눅스 | Go 프로그램만으로 구성된 OS |
| **DeepDive OS (이 폴더)** | 리눅스 | Go init + busybox + 4단계 모듈 + 5단계 minibox + 6단계 execsnoop |

## 구성

```
 out/bzImage            ← 리눅스 커널 (0단계에서 직접 빌드한 것 또는 호스트 커널)
 out/initramfs.cpio.gz  ← 아래 트리를 cpio로 묶은 것. 커널이 메모리에 풀고 /init을 실행
   /init                         Go로 짠 PID 1  (init/main.go)
   /bin/busybox + 심볼릭 링크     셸과 기본 명령어
   /bin/minibox                  5단계 컨테이너 런타임
   /bin/execsnoop                6단계 eBPF 도구
   /bin/deepdive-ioctl           4단계 Go 클라이언트
   /lib/modules/deepdive/*.ko    0, 2, 4단계 커널 모듈
   /containers/busybox/          컨테이너용 루트 파일시스템
   /etc/deepdive/modules         부팅 시 올릴 모듈 목록
   /etc/deepdive/services        부팅 시 띄울 서비스 목록
```

부팅 순서:

```
 QEMU(BIOS) → 커널 압축 해제 → 하드웨어 초기화 → initramfs 풀기 → /init 실행 (PID 1)
                                                                    │
   deepdive-init:  proc, sys, dev, cgroup2, tracefs 마운트
                   hostname 설정
                   finit_module()으로 hello.ko, deepdive.ko 로드
                   services 목록대로 프로세스 실행 (콘솔 셸은 죽으면 다시 띄움)
                   SIGCHLD마다 좀비 회수, SIGUSR2(poweroff)면 reboot() 시스템 콜로 종료
```

## 빌드와 부팅

```bash
sudo apt install busybox-static qemu-system-x86 cpio clang libbpf-dev

# 방법 1: 0단계에서 빌드한 내 커널로 (권장)
KERNEL_SRC=../linux ./build.sh

# 방법 2: 지금 쓰는 Ubuntu 커널로
# Ubuntu는 /boot/vmlinuz-*를 root만 읽을 수 있게 해 둔다. build.sh 전체를 sudo로 돌리면
# out/이 root 소유가 되어 run.sh가 못 읽으니, 커널 파일만 읽기 권한을 열어 준다 (CI도 같은 방법)
sudo chmod a+r /boot/vmlinuz-$(uname -r)
./build.sh

./run.sh          # 부팅. 콘솔에서 이것저것 해 보고 poweroff
./run.sh test     # 자동 selftest (CI에서도 이걸 돌린다)
```

```
[deepdive-init] loaded module /lib/modules/deepdive/hello.ko
[deepdive-init] loaded module /lib/modules/deepdive/deepdive.ko
[deepdive-init] boot done in 3.95s
  ____                  ____  _              ___  ____
 |  _ \  ___  ___ _ __ |  _ \(_)_   _____   / _ \/ ___|
 ...
root@deepdive:/# minibox run -mem 32M /containers/busybox /bin/sh
```

## 읽을 커널 코드와 자료

1. [`Documentation/filesystems/ramfs-rootfs-initramfs.rst`](https://docs.kernel.org/filesystems/ramfs-rootfs-initramfs.html): initramfs의 원리
2. [`kernel_init`](https://elixir.bootlin.com/linux/latest/A/ident/kernel_init) → [`run_init_process`](https://elixir.bootlin.com/linux/latest/A/ident/run_init_process): 커널이 `/init`을 찾아 실행하는 곳. 못 찾으면 무슨 메시지가 나올까?
3. [`do_exit`](https://elixir.bootlin.com/linux/latest/A/ident/do_exit)에서 PID 1이 죽을 때: `"Attempted to kill init!"`
4. [`find_new_reaper`](https://elixir.bootlin.com/linux/latest/A/ident/find_new_reaper): 고아 프로세스가 PID 1에게 가는 과정
5. [Talos의 machined](https://github.com/siderolabs/talos/tree/main/internal/app/machined), [gokrazy init](https://github.com/gokrazy/gokrazy): Go로 짠 실제 PID 1들

## 실습 과제

**부팅 이해하기**
- [ ] `run.sh`에서 `quiet`을 빼고 부팅 로그 전체를 읽기. initramfs를 푸는 줄, `Run /init as init process` 줄을 찾기
- [ ] `/init`을 지우고 이미지를 만들면 커널은 무슨 메시지를 남기나? `run_init_process` 코드와 비교하기
- [ ] 셸에서 `kill -9 1`은 왜 아무 일도 안 일어날까? (힌트: `SIGNAL_UNKILLABLE`)
- [ ] `init/main.go`의 `reap()`을 비워 두고 빌드한 뒤 `sh -c 'sleep 1 &'`을 여러 번. `ps`에서 무엇이 쌓이나?
- [ ] `/etc/deepdive/services`에 `sleep 5`를 respawn으로 추가해서 감시와 재시작 동작 확인하기

**OS를 깎기 (0단계 커널 빌드와 연결)**
- [ ] 부팅 시간 재기: `dmesg`의 마지막 타임스탬프와 `boot done in` 값
- [ ] `make tinyconfig`에서 시작해서 이 OS가 부팅되는 **최소 커널 설정** 찾기. 필요한 것: `BLK_DEV_INITRD`, `DEVTMPFS`, `PROC_FS`, `SYSFS`, `TTY`, `SERIAL_8250_CONSOLE`, `BINFMT_ELF`, `MODULES`, `CGROUPS`, `MEMCG`, `NAMESPACES`, `BPF_SYSCALL` ... 커널 크기가 15MB에서 얼마까지 줄어드나?
- [ ] 커널에 `CONFIG_INITRAMFS_SOURCE`로 initramfs를 **커널 안에 넣어서** 파일 하나짜리 OS 만들기
- [ ] 모듈을 `.ko`가 아니라 커널 트리 안에 넣고 `=y`로 빌트인 하기 (`drivers/misc/Kconfig`, `Makefile`)

**OS를 키우기**
- [ ] busybox `sh` 대신 Go로 짠 나만의 셸 만들기 ([u-root](https://github.com/u-root/u-root) 참고)
- [ ] 네트워크: QEMU에 `-nic user`를 주고 init에서 `eth0`를 올리고 DHCP 받기 (busybox `udhcpc`)
- [ ] Talos처럼 셸을 없애고, init에 HTTP API를 열어서 `curl`로 서비스 목록과 모듈 상태를 조회하게 만들기
- [ ] 디스크에 설치하기: 이미지에 파티션을 만들고 GRUB 또는 systemd-boot로 부팅, 미니 PC 실제 하드웨어에서 USB로 부팅해 보기
- [ ] A/B 파티션으로 업데이트와 롤백 (ChromeOS, Android, Bottlerocket 방식)

## 다음: 커널까지 직접 만들고 싶다면

여기까지는 "리눅스 커널 위에 내 OS"입니다. 커널 자체를 처음부터 만들어 보고 싶다면, 이 레포에서 배운 개념(시스템 콜, 스케줄러, 페이지 테이블, 장치 드라이버)을 그대로 들고 다음으로 넘어가면 됩니다.

- [xv6-riscv](https://github.com/mit-pdos/xv6-riscv) + [MIT 6.1810](https://pdos.csail.mit.edu/6.1810/): 약 1만 줄짜리 유닉스. 시스템 콜, 페이지 테이블, 스케줄러를 직접 구현하는 랩
- [OSDev Wiki](https://wiki.osdev.org/): 부트로더부터 x86 커널을 맨바닥에서
- [Writing an OS in Rust](https://os.phil-opp.com/)
