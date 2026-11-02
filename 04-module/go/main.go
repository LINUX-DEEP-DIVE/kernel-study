// /dev/deepdive 커널 모듈을 Go에서 쓰기: write, read, ioctl
//
//	go run .            쓰고, 통계 보고, 읽기
//	go run . watch      블로킹 read로 들어오는 데이터를 계속 출력 (다른 터미널에서 echo hi > /dev/deepdive)
package main

import (
	"fmt"
	"os"
	"syscall"
	"unsafe"
)

// C 헤더의 struct deepdive_stats와 메모리 배치가 정확히 같아야 한다 (24바이트)
type stats struct {
	BytesWritten uint64
	BytesRead    uint64
	Used         uint32
	Capacity     uint32
}

// 리눅스 ioctl 번호 = dir<<30 | size<<16 | type<<8 | nr  (include/uapi/asm-generic/ioctl.h)
const (
	iocNone = 0
	iocRead = 2
	magic   = 'd'
)

func ioc(dir, nr, size uintptr) uintptr { return dir<<30 | size<<16 | magic<<8 | nr }

var (
	iocReset    = ioc(iocNone, 1, 0)
	iocGetStats = ioc(iocRead, 2, unsafe.Sizeof(stats{}))
)

func getStats(fd uintptr) (stats, error) {
	var st stats
	_, _, errno := syscall.Syscall(syscall.SYS_IOCTL, fd, iocGetStats, uintptr(unsafe.Pointer(&st)))
	if errno != 0 {
		return st, errno
	}
	return st, nil
}

func main() {
	f, err := os.OpenFile("/dev/deepdive", os.O_RDWR, 0)
	if err != nil {
		fmt.Fprintln(os.Stderr, "모듈을 먼저 올리세요:", err)
		os.Exit(1)
	}
	defer f.Close()

	if len(os.Args) > 1 && os.Args[1] == "watch" {
		buf := make([]byte, 256)
		for {
			n, err := f.Read(buf) // 비어 있으면 커널의 wait queue에서 잠든다
			if err != nil {
				fmt.Fprintln(os.Stderr, err)
				return
			}
			fmt.Printf("got %d bytes: %q\n", n, buf[:n])
		}
	}

	fmt.Printf("ioctl GET_STATS = %#x (C의 DEEPDIVE_IOC_GET_STATS와 같아야 함)\n", iocGetStats)
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, f.Fd(), iocReset, 0); errno != 0 {
		fmt.Fprintln(os.Stderr, "reset:", errno)
		os.Exit(1)
	}

	f.Write([]byte("hello from Go\n"))
	st, err := getStats(f.Fd())
	if err != nil {
		fmt.Fprintln(os.Stderr, "stats:", err)
		os.Exit(1)
	}
	fmt.Printf("after write: %+v\n", st)

	buf := make([]byte, 64)
	n, _ := f.Read(buf)
	fmt.Printf("read back:   %q\n", buf[:n])

	st, _ = getStats(f.Fd())
	fmt.Printf("after read:  %+v\n", st)
}
