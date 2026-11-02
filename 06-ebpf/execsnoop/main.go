// execsnoop: eBPF로 시스템 전체의 프로그램 실행을 실시간으로 본다.
//
//	make && sudo ./execsnoop
//	sudo ./execsnoop -cgroup 12345     특정 cgroup(컨테이너)만 보기
//
// Go 프로그램이 하는 일: BPF 오브젝트(.o)를 커널에 올리고, 트레이스포인트에 붙이고,
// ring buffer에서 이벤트를 읽어 출력한다. 실제 추적은 커널 안의 BPF 코드가 한다.
package main

import (
	"bytes"
	_ "embed"
	"encoding/binary"
	"errors"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/ringbuf"
	"github.com/cilium/ebpf/rlimit"
)

//go:embed execsnoop.bpf.o
var bpfObject []byte

// execsnoop.bpf.c의 struct event와 같은 배치
type event struct {
	Pid      uint32
	Ppid     uint32
	CgroupID uint64
	Comm     [16]byte
	Filename [256]byte
}

func cstr(b []byte) string {
	if i := bytes.IndexByte(b, 0); i >= 0 {
		return string(b[:i])
	}
	return string(b)
}

func main() {
	onlyCgroup := flag.Uint64("cgroup", 0, "이 cgroup id의 이벤트만 출력 (0이면 전부)")
	flag.Parse()

	// 5.11 이전 커널은 BPF 메모리를 RLIMIT_MEMLOCK으로 계산했다
	if err := rlimit.RemoveMemlock(); err != nil {
		log.Fatal(err)
	}

	spec, err := ebpf.LoadCollectionSpecFromReader(bytes.NewReader(bpfObject))
	if err != nil {
		log.Fatalf("load spec: %v", err)
	}
	coll, err := ebpf.NewCollection(spec) // 여기서 bpf(BPF_PROG_LOAD) → verifier 검사
	if err != nil {
		var ve *ebpf.VerifierError
		if errors.As(err, &ve) {
			log.Fatalf("verifier rejected the program:\n%+v", ve)
		}
		log.Fatalf("new collection: %v", err)
	}
	defer coll.Close()

	tp, err := link.Tracepoint("sched", "sched_process_exec", coll.Programs["handle_exec"], nil)
	if err != nil {
		log.Fatalf("attach: %v", err)
	}
	defer tp.Close()

	rd, err := ringbuf.NewReader(coll.Maps["events"])
	if err != nil {
		log.Fatal(err)
	}
	defer rd.Close()

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt, syscall.SIGTERM)
	go func() { <-sig; rd.Close() }()

	fmt.Printf("%-8s %-8s %-12s %-16s %s\n", "PID", "PPID", "CGROUP", "COMM", "FILENAME")
	for {
		rec, err := rd.Read()
		if err != nil {
			if errors.Is(err, ringbuf.ErrClosed) {
				return
			}
			log.Printf("read: %v", err)
			continue
		}
		var e event
		if err := binary.Read(bytes.NewReader(rec.RawSample), binary.NativeEndian, &e); err != nil {
			log.Printf("decode: %v", err)
			continue
		}
		if *onlyCgroup != 0 && e.CgroupID != *onlyCgroup {
			continue
		}
		fmt.Printf("%-8d %-8d %-12d %-16s %s\n", e.Pid, e.Ppid, e.CgroupID, cstr(e.Comm[:]), cstr(e.Filename[:]))
	}
}
