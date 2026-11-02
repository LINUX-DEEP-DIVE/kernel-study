// 고루틴은 OS 스레드가 아니다.
// 고루틴 1만 개를 만들어도 커널이 보는 스레드(/proc/self/task)는 몇 개뿐이다.
// 그런데 고루틴이 "블로킹 시스템 콜"에 들어가면 Go 런타임은 스레드를 새로 만든다.
//
//	go run .            관찰: GODEBUG=schedtrace=200 go run .
//	                         ps -L -p <pid> 로 스레드 목록 보기
package main

import (
	"fmt"
	"os"
	"runtime"
	"sync"
	"syscall"
	"time"
)

func threads() int {
	entries, _ := os.ReadDir("/proc/self/task")
	return len(entries)
}

func report(stage string) {
	fmt.Printf("%-36s goroutines=%-6d OS threads=%d\n", stage, runtime.NumGoroutine(), threads())
}

func main() {
	fmt.Printf("pid=%d GOMAXPROCS=%d\n\n", os.Getpid(), runtime.GOMAXPROCS(0))
	report("start")

	// 1) 잠자는 고루틴 1만 개: 런타임 안에서만 대기하므로 스레드가 늘지 않는다
	var wg sync.WaitGroup
	stop := make(chan struct{})
	for i := 0; i < 10000; i++ {
		wg.Add(1)
		go func() { defer wg.Done(); <-stop }()
	}
	time.Sleep(200 * time.Millisecond)
	report("10000 goroutines blocked on chan")
	close(stop)
	wg.Wait()

	// 2) 블로킹 시스템 콜(read on pipe)에 들어간 고루틴 50개:
	//    스레드(M)가 커널 안에서 잠들어 버리므로, 런타임은 P를 넘겨받을 새 M을 만든다
	var fds [50][2]int
	for i := range fds {
		if err := syscall.Pipe(fds[i][:]); err != nil {
			panic(err)
		}
		wg.Add(1)
		go func(fd int) {
			defer wg.Done()
			buf := make([]byte, 1)
			syscall.Read(fd, buf) // 누가 쓸 때까지 커널 안에서 블록
		}(fds[i][0])
	}
	time.Sleep(500 * time.Millisecond)
	report("50 goroutines blocked in read(2)")

	for i := range fds {
		syscall.Write(fds[i][1], []byte{1})
	}
	wg.Wait()
	time.Sleep(200 * time.Millisecond)
	report("after reads returned")
}
