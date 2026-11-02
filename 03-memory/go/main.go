// Go의 힙과 커널이 보는 메모리(RSS)는 다르다.
// GC가 객체를 치워도 Go 런타임이 그 메모리를 커널에 바로 돌려주지는 않는다.
//
//	go run .
//	GOMEMLIMIT=200MiB go run .     메모리 한도를 주면 언제 GC가 도는지 비교
//	GODEBUG=gctrace=1 go run .     GC가 돌 때마다 한 줄씩 출력
package main

import (
	"fmt"
	"os"
	"runtime"
	"runtime/debug"
	"strings"
)

func rssMiB() float64 {
	b, _ := os.ReadFile("/proc/self/status")
	for _, line := range strings.Split(string(b), "\n") {
		var kib float64
		if _, err := fmt.Sscanf(line, "VmRSS: %f kB", &kib); err == nil {
			return kib / 1024
		}
	}
	return -1
}

func report(stage string) {
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	fmt.Printf("%-26s RSS=%7.1f MiB  HeapInuse=%7.1f MiB  HeapReleased=%7.1f MiB  NumGC=%d\n",
		stage, rssMiB(), float64(m.HeapInuse)/(1<<20), float64(m.HeapReleased)/(1<<20), m.NumGC)
}

var keep [][]byte

func main() {
	report("start")

	for i := 0; i < 400; i++ {
		b := make([]byte, 1<<20) // 1MiB
		for j := 0; j < len(b); j += 4096 {
			b[j] = 1 // 실제로 만져야 RSS가 오른다 (3단계 lazy_alloc 참고)
		}
		keep = append(keep, b)
	}
	report("allocated 400 MiB")

	keep = nil
	runtime.GC()
	report("after GC")

	debug.FreeOSMemory() // GC + 남는 힙을 커널에 돌려줌 (madvise)
	report("after FreeOSMemory")
}
