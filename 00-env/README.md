# 0단계. 실습 환경

목표: 커널을 직접 빌드해서 부팅하고, 내가 만든 커널 모듈을 올려 본다.

## 1. 도구 설치 (Ubuntu 24.04)

```bash
./scripts/setup.sh
```

빌드 도구(gcc, make, flex, bison, libelf, libssl), 관찰 도구(strace, perf, bpftrace), QEMU와 [virtme-ng](https://github.com/arighi/virtme-ng)를 설치합니다.
macOS나 Windows라면 UTM, Multipass, WSL2 위에 Ubuntu VM을 하나 만들어서 그 안에서 진행하세요.

## 2. 커널 빌드 후 QEMU로 부팅

```bash
./scripts/build-kernel.sh          # linux/ 에 소스를 받아 빌드 (기본: 최신 stable)
cd linux && vng                     # 방금 빌드한 커널로 현재 셸을 띄움
uname -r                            # 내 커널인지 확인
```

`build-kernel.sh`는 `LOCALVERSION=-deepdive`를 붙이므로 `uname -r`에 `-deepdive`가 보이면 성공입니다.
virtme-ng는 호스트 파일시스템을 그대로 공유해서, 빌드한 커널로 수 초 안에 부팅됩니다.

## 3. 첫 커널 모듈

```bash
make -C hello-module
sudo insmod hello-module/hello.ko who=deepdive
sudo dmesg | tail -3
cat /sys/module/hello/parameters/who
sudo rmmod hello
```

## 실습 과제

- [ ] 커널을 빌드하고 `uname -r`에 `-deepdive`가 붙은 커널로 부팅하기
- [ ] `hello.c`에서 현재 프로세스(`current->comm`, `current->pid`)를 출력하도록 고치기. 누가 `insmod`를 실행했는지 보일까요?
- [ ] `init` 함수에서 `-EINVAL`을 리턴하면 `insmod`는 어떻게 될까요? `strace insmod`로 `finit_module` 호출을 확인해 보기
- [ ] [`do_init_module`](https://elixir.bootlin.com/linux/latest/A/ident/do_init_module)을 따라가며 모듈이 로드되는 흐름 정리하기

## 참고

- [The Linux Kernel Module Programming Guide](https://sysprog21.github.io/lkmpg/)
- [Linux kernel docs: Building external modules](https://docs.kernel.org/kbuild/modules.html)
