# 4단계. 커널 모듈: 캐릭터 디바이스 만들기

목표: 유저 프로그램의 `read`/`write`/`ioctl` 시스템 콜이 **내가 짠 커널 함수**에 도착하게 만든다. 1단계에서 본 `ksys_write`의 끝에 내 코드가 붙는 셈이다.

> ⚠️ 커널 코드의 버그는 프로세스가 아니라 **머신 전체**를 멈춥니다. 처음에는 VM이나 [0단계](../00-env)의 `vng`로 띄운 커널에서 해 보세요.

## 예제

| 파일 | 내용 |
|---|---|
| [ringbuf/deepdive.c](ringbuf/deepdive.c) | `/dev/deepdive`: 4KiB 링 버퍼. 블로킹 read, mutex, wait queue, ioctl |
| [ringbuf/deepdive_ioctl.h](ringbuf/deepdive_ioctl.h) | 커널과 유저가 공유하는 ioctl 번호와 구조체 |
| [ringbuf/stats.c](ringbuf/stats.c) | C로 ioctl 호출 |
| [go/main.go](go/main.go) | Go로 같은 ioctl 번호를 직접 계산해서 호출 |

```bash
cd ringbuf && make
sudo insmod deepdive.ko
ls -l /dev/deepdive; cat /sys/class/misc/deepdive/dev

echo "hello kernel" > /dev/deepdive
./stats
cat /dev/deepdive          # 내용이 나오고 나서 멈춰 있다. 왜? (Ctrl+C)

cd ../go && go run .       # Go 클라이언트
go run . watch &           # 블로킹 read로 대기
echo ping > /dev/deepdive  # watch가 깨어나서 출력

sudo rmmod deepdive
```

## 핵심 개념

```
 유저:  write(fd, "hi", 2)
          │ syscall
 커널:  ksys_write → vfs_write → file->f_op->write
                                     │
                                     ▼
                            deepdive_write()      ← 내 코드
                              copy_from_user()    ← 유저 포인터는 이걸로만 만진다
                              mutex_lock()        ← 동시에 여러 프로세스가 올 수 있다
                              wake_up_interruptible(&readers)
                                     │
                                     ▼
          (wait queue에서 자고 있던 cat 프로세스가 깨어남)
```

## 읽을 커널 코드

1. [`struct file_operations`](https://elixir.bootlin.com/linux/latest/A/ident/file_operations): 파일 하나가 받을 수 있는 모든 연산
2. [`vfs_write`](https://elixir.bootlin.com/linux/latest/A/ident/vfs_write): 1단계의 `ksys_write`가 여기서 `f_op->write`를 부른다
3. [`misc_register`](https://elixir.bootlin.com/linux/latest/A/ident/misc_register): `/dev/deepdive`가 생기는 과정 (udev와 devtmpfs)
4. [`wait_event_interruptible`](https://elixir.bootlin.com/linux/latest/A/ident/wait_event_interruptible): 프로세스를 `TASK_INTERRUPTIBLE`로 바꾸고 `schedule()` 호출. 2단계 스케줄러와 연결
5. [`include/uapi/asm-generic/ioctl.h`](https://elixir.bootlin.com/linux/latest/source/include/uapi/asm-generic/ioctl.h): ioctl 번호 인코딩
6. [Documentation: Unreliable Guide To Locking](https://docs.kernel.org/kernel-hacking/locking.html)

## 실습 과제

**동작 이해**
- [ ] `cat /dev/deepdive`가 내용을 출력한 뒤 끝나지 않는다. 코드의 어느 줄 때문인가? `cat /proc/$(pgrep -n cat)/wchan`으로 커널 어디서 자고 있는지 확인하기
- [ ] 그 상태에서 다른 터미널에서 `echo hi > /dev/deepdive`를 하면? 깨우는 쪽 코드를 찾기
- [ ] `stats.c`가 출력한 `ioctl cmd = 0x80186402`를 비트 단위로 분해하기. `0x80`, `0x18`, `0x64`, `0x02`는 각각 무엇인가?
- [ ] Go의 `unsafe.Sizeof(stats{})`가 24가 아니면 무슨 일이 생길까? 구조체 필드 순서를 바꿔서 실험하기 (패딩)

**버그를 일부러 내 보기 (VM에서!)**
- [ ] `copy_to_user` 대신 `memcpy(ubuf, ...)`로 바꾸면? `dmesg`에 무엇이 찍히나? (SMAP)
- [ ] `deepdive_write`에서 `mutex_unlock`을 하나 빼먹으면? 두 번째 write가 어떻게 되나? `echo t | sudo tee /proc/sysrq-trigger` 후 dmesg에서 걸린 태스크 찾기
- [ ] 동시성 테스트: `for i in $(seq 8); do (yes abc | head -c 100000 > /dev/deepdive &) ; done` 로 여러 writer를 동시에. mutex가 없으면 어떻게 될지 추론하기

**확장**
- [ ] 버퍼가 가득 차면 `-ENOSPC` 대신 **writer가 잠들도록** 바꾸기 (wait queue 하나 더)
- [ ] `poll`을 구현해서 Go에서 `select`/epoll로 기다릴 수 있게 하기
- [ ] 모듈 파라미터로 버퍼 크기를 받고 `kmalloc`으로 할당하기. 언로드할 때 `kfree`를 잊으면? (`kmemleak`)
- [ ] `/proc/deepdive` 또는 debugfs 엔트리로 통계를 노출하기

## 다음 단계와의 연결

커널 모듈은 강력하지만 위험합니다. [6단계 eBPF](../06-ebpf)는 **커널을 망가뜨리지 않으면서** 커널 안에 코드를 넣는 방법이고, [7단계](../07-own-os)에서는 이 모듈을 내 OS 이미지에 기본으로 넣어서 부팅합니다.
