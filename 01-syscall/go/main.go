// Go에서 시스템 콜을 부르는 세 가지 방법을 비교한다.
//
//	syscall.RawSyscall: 런타임에 알리지 않고 바로 syscall 명령 실행 (짧고 블록되지 않는 호출용)
//	syscall.Syscall:    runtime.entersyscall/exitsyscall로 감싸서, 블록되면 P를 다른 M에 넘긴다
//	os.Getppid 등:      위를 감싼 표준 라이브러리
//
// 실행: go run .   관찰: strace -f -c go run .  또는  GODEBUG=schedtrace=100 go run .
package main

import (
	"fmt"
	"os"
	"syscall"
	"time"
	"unsafe"
)

const n = 1_000_000

func bench(name string, f func()) {
	start := time.Now()
	for i := 0; i < n; i++ {
		f()
	}
	fmt.Printf("%-24s %6.1f ns/call\n", name, float64(time.Since(start).Nanoseconds())/n)
}

func main() {
	msg := []byte("hello from syscall.RawSyscall\n")
	nw, _, errno := syscall.RawSyscall(syscall.SYS_WRITE, 1,
		uintptr(unsafe.Pointer(&msg[0])), uintptr(len(msg)))
	if errno != 0 {
		fmt.Fprintln(os.Stderr, "write failed:", errno)
		os.Exit(1)
	}
	fmt.Printf("wrote %d bytes\n", nw)

	// 잘못된 fd: 커널이 돌려준 -EBADF가 Errno로 변환된다
	_, _, errno = syscall.RawSyscall(syscall.SYS_WRITE, 999, 0, 0)
	fmt.Printf("write(999) -> errno %d (%v)\n\n", errno, errno)

	bench("RawSyscall(getppid)", func() { syscall.RawSyscall(syscall.SYS_GETPPID, 0, 0, 0) })
	bench("Syscall(getppid)", func() { syscall.Syscall(syscall.SYS_GETPPID, 0, 0, 0) })
	bench("os.Getppid()", func() { _ = os.Getppid() })
	bench("time.Now() (vDSO)", func() { _ = time.Now() })
}
