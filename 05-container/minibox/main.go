// minibox: namespace와 cgroup v2만으로 만드는 최소 컨테이너 런타임.
// docker run, containerd, runc가 결국 하는 일의 핵심만 남겼다.
//
//	sudo ./minibox run [옵션] <rootfs> <명령> [인자...]
//	  -hostname  컨테이너 호스트 이름        (기본 minibox)
//	  -mem       메모리 한도 (cgroup memory.max, 예: 64M)
//	  -pids      프로세스 수 한도 (pids.max)
//	  -cpu       CPU 한도 (cpu.max, 예: "50000 100000" = 0.5코어)
//	  -net       새 network namespace (lo만 있음)
//
// 예) sudo ./minibox run -mem 64M -pids 20 ./rootfs /bin/sh
package main

import (
	"flag"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strconv"
	"syscall"
)

const cgroupRoot = "/sys/fs/cgroup/minibox"

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	switch os.Args[1] {
	case "run":
		os.Exit(run(os.Args[2:])) // os.Exit은 defer를 실행하지 않으므로 run 안에서 정리를 끝내고 나온다
	case "init": // run이 새 namespace 안에서 자기 자신을 다시 실행할 때 쓰는 내부 명령
		containerInit(os.Args[2:])
	default:
		usage()
	}
}

func usage() {
	fmt.Fprintln(os.Stderr, "usage: minibox run [-hostname h] [-mem 64M] [-pids 20] [-cpu \"50000 100000\"] [-net] <rootfs> <cmd> [args...]")
	os.Exit(2)
}

// ---------- 1. 부모: namespace를 만들고 cgroup에 넣는다 ----------

func run(args []string) int {
	fs := flag.NewFlagSet("run", flag.ExitOnError)
	hostname := fs.String("hostname", "minibox", "container hostname")
	mem := fs.String("mem", "", "memory.max (e.g. 64M)")
	pids := fs.String("pids", "", "pids.max")
	cpu := fs.String("cpu", "", `cpu.max (e.g. "50000 100000")`)
	newNet := fs.Bool("net", false, "new network namespace")
	fs.Parse(args)
	if fs.NArg() < 2 {
		usage()
	}
	rootfs, err := filepath.Abs(fs.Arg(0))
	must(err)

	// /proc/self/exe = 지금 실행 중인 minibox 바이너리. 새 namespace 안에서 "init"으로 다시 실행한다.
	// 왜 바로 사용자 명령을 실행하지 않을까? 새 namespace 안에서 exec 전에 할 일(mount, pivot_root)이 있기 때문.
	cmd := exec.Command("/proc/self/exe", append([]string{"init", *hostname, rootfs}, fs.Args()[1:]...)...)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr

	// 자식이 cgroup에 들어가기 전에 일을 시작하면 제한 없이 자원을 쓸 수 있다.
	// 파이프를 하나 넘겨 주고, 부모가 cgroup 설정을 끝내고 1바이트를 쓸 때까지 자식은 기다린다. (runc도 같은 방식)
	syncR, syncW, err := os.Pipe()
	must(err)
	cmd.ExtraFiles = []*os.File{syncR} // 자식에서 fd 3

	flags := syscall.CLONE_NEWUTS | // 호스트 이름
		syscall.CLONE_NEWPID | // PID 번호 공간: 컨테이너 안의 첫 프로세스가 PID 1
		syscall.CLONE_NEWNS | // 마운트 테이블
		syscall.CLONE_NEWIPC // System V IPC, POSIX 메시지 큐
	if *newNet {
		flags |= syscall.CLONE_NEWNET // 네트워크 장치, 라우팅, 포트
	}
	cmd.SysProcAttr = &syscall.SysProcAttr{Cloneflags: uintptr(flags)}

	must(cmd.Start()) // 내부적으로 clone(flags) 호출. 2단계의 clone과 같은 시스템 콜이다
	syncR.Close()

	cg, err := setupCgroup(cmd.Process.Pid, *mem, *pids, *cpu)
	if cg != "" {
		defer os.Remove(cg) // 비어 있는 cgroup 디렉터리는 rmdir로 지운다
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "[minibox] cgroup setup failed:", err)
		syncW.Close() // 1바이트도 못 받은 자식은 스스로 종료한다
		cmd.Process.Kill()
		cmd.Wait()
		return 1
	}
	syncW.Write([]byte{1})
	syncW.Close()

	// 감독 프로그램이 minibox에 SIGTERM을 보내면 컨테이너도 끝낸다.
	// 컨테이너의 PID 1은 핸들러가 없으면 바깥에서 온 SIGTERM을 무시하므로 SIGKILL을 보낸다.
	// SIGINT(Ctrl+C)는 터미널이 컨테이너에도 직접 보내므로 부모는 받아서 버리기만 한다.
	sigs := make(chan os.Signal, 1)
	signal.Notify(sigs, syscall.SIGTERM, syscall.SIGINT)
	go func() {
		for sig := range sigs {
			if sig == syscall.SIGTERM {
				cmd.Process.Kill()
			}
		}
	}()

	err = cmd.Wait()
	signal.Stop(sigs)
	reportCgroup(cg)
	if exitErr, ok := err.(*exec.ExitError); ok {
		return exitErr.ExitCode()
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "[minibox] error:", err)
		return 1
	}
	return 0
}

func setupCgroup(pid int, mem, pids, cpu string) (string, error) {
	// 상위 cgroup에서 하위로 컨트롤러를 내려 줘야 memory.max 같은 파일이 생긴다.
	// 이미 켜져 있을 수 있으니 여기 실패는 기록만 하고, 실제 한도 쓰기에서 실패하면 그때 멈춘다.
	if err := os.MkdirAll(cgroupRoot, 0o755); err != nil {
		return "", err
	}
	for _, f := range []string{"/sys/fs/cgroup/cgroup.subtree_control", filepath.Join(cgroupRoot, "cgroup.subtree_control")} {
		if err := write(f, "+memory +pids +cpu"); err != nil {
			fmt.Fprintf(os.Stderr, "[minibox] warning: %v\n", err)
		}
	}

	cg := filepath.Join(cgroupRoot, strconv.Itoa(pid))
	if err := os.Mkdir(cg, 0o755); err != nil {
		return "", err
	}
	limits := []struct{ file, val string }{
		{"memory.max", mem}, {"pids.max", pids}, {"cpu.max", cpu},
	}
	if mem != "" {
		limits = append(limits, struct{ file, val string }{"memory.swap.max", "0"})
	}
	for _, l := range limits {
		if l.val == "" {
			continue
		}
		if err := write(filepath.Join(cg, l.file), l.val); err != nil {
			return cg, err // 요청한 한도를 못 걸었으면 컨테이너를 띄우지 않는다
		}
	}
	// 프로세스를 cgroup에 넣는 방법: PID를 cgroup.procs에 쓰기. 이후 자식들도 자동으로 따라 들어간다
	if err := write(filepath.Join(cg, "cgroup.procs"), strconv.Itoa(pid)); err != nil {
		return cg, err
	}
	fmt.Fprintf(os.Stderr, "[minibox] pid %d in cgroup %s\n", pid, cg)
	return cg, nil
}

func reportCgroup(cg string) {
	for _, f := range []string{"memory.peak", "memory.events", "pids.peak"} {
		if b, err := os.ReadFile(filepath.Join(cg, f)); err == nil {
			fmt.Fprintf(os.Stderr, "[minibox] %s: %s", f, b)
		}
	}
}

// ---------- 2. 자식(새 namespace 안): 파일시스템을 바꾸고 사용자 명령으로 변신 ----------

func containerInit(args []string) {
	hostname, rootfs, argv := args[0], args[1], args[2:]

	// 부모가 cgroup 설정을 끝낼 때까지 기다린다 (fd 3 = 동기화 파이프)
	syncPipe := os.NewFile(3, "sync")
	if n, _ := syncPipe.Read(make([]byte, 1)); n != 1 {
		os.Exit(1) // 부모가 설정에 실패하고 파이프를 닫았다
	}
	syncPipe.Close()

	must(syscall.Sethostname([]byte(hostname))) // 새 UTS namespace라 호스트에는 영향 없음

	// 마운트 이벤트가 호스트로 새어 나가지 않게 모든 마운트를 private으로
	must(syscall.Mount("", "/", "", syscall.MS_REC|syscall.MS_PRIVATE, ""))

	// pivot_root의 조건: 새 루트는 마운트 포인트여야 한다 → 자기 자신에 bind mount
	must(syscall.Mount(rootfs, rootfs, "", syscall.MS_BIND|syscall.MS_REC, ""))
	oldRoot := filepath.Join(rootfs, ".oldroot")
	must(os.MkdirAll(oldRoot, 0o700))
	switch err := syscall.PivotRoot(rootfs, oldRoot); err {
	case nil:
		must(os.Chdir("/"))
		// 옛 루트(호스트 파일시스템)를 떼어 내야 컨테이너가 호스트 파일을 못 본다
		must(syscall.Unmount("/.oldroot", syscall.MNT_DETACH))
		must(os.Remove("/.oldroot"))
	case syscall.EINVAL:
		// 현재 루트가 initramfs(rootfs)면 pivot_root가 불가능하다 (7단계 DeepDive OS가 이 경우).
		// runc --no-pivot과 같은 방법: rootfs 마운트를 /로 옮기고 chroot
		os.Remove(oldRoot)
		must(os.Chdir(rootfs))
		must(syscall.Mount(rootfs, "/", "", syscall.MS_MOVE, ""))
		must(syscall.Chroot("."))
		must(os.Chdir("/"))
	default:
		must(err)
	}

	// 새 PID namespace에 맞는 /proc. 이걸 안 하면 ps가 호스트 프로세스를 보여 준다
	must(os.MkdirAll("/proc", 0o555))
	must(syscall.Mount("proc", "/proc", "proc", syscall.MS_NOSUID|syscall.MS_NOEXEC|syscall.MS_NODEV, ""))
	must(os.MkdirAll("/tmp", 0o1777))
	must(syscall.Mount("tmpfs", "/tmp", "tmpfs", 0, ""))

	path, err := exec.LookPath(argv[0])
	if err != nil {
		path = argv[0]
	}
	// exec: 이 프로세스(PID 1)를 사용자 명령으로 갈아 끼운다. 돌아오지 않는다
	must(syscall.Exec(path, argv, []string{"PATH=/bin:/sbin:/usr/bin:/usr/sbin", "HOME=/", "TERM=xterm"}))
}

func write(path, val string) error {
	if err := os.WriteFile(path, []byte(val), 0o644); err != nil {
		return fmt.Errorf("write %q to %s: %w", val, path, err)
	}
	return nil
}

func must(err error) {
	if err != nil {
		fmt.Fprintln(os.Stderr, "[minibox] error:", err)
		os.Exit(1)
	}
}
