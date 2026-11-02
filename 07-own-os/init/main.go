// deepdive-init: 우리 OS의 PID 1.
//
// 커널은 부팅을 마치면 initramfs의 /init을 PID 1로 실행하고, 그 뒤로는 아무것도 하지 않는다.
// 나머지 전부(파일시스템 마운트, 드라이버 로드, 서비스 실행, 고아 프로세스 회수, 종료)는 PID 1의 몫이다.
// systemd, busybox init, Talos의 machined가 하는 일을 Go로 최소한만 구현한다.
package main

import (
	"bufio"
	"fmt"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"golang.org/x/sys/unix"
)

func logf(format string, a ...any) {
	fmt.Fprintf(os.Stderr, "\x1b[1;36m[deepdive-init]\x1b[0m "+format+"\n", a...)
}

func main() {
	if os.Getpid() != 1 {
		logf("warning: PID %d로 실행 중. 실제 init은 PID 1이어야 한다", os.Getpid())
	}

	mountAll()
	setHostname()
	loadModules()

	testMode := cmdlineHas("deepdive.test=1")
	services := loadServices(testMode)
	logf("boot done in %s (kernel cmdline test mode=%v)", uptime(), testMode)

	supervise(services)
}

// ---------- 1. 가상 파일시스템 마운트 ----------

func mountAll() {
	mounts := []struct{ src, target, fstype string }{
		{"proc", "/proc", "proc"},
		{"sysfs", "/sys", "sysfs"},
		{"devtmpfs", "/dev", "devtmpfs"}, // 커널이 장치 파일(/dev/deepdive 등)을 자동으로 만들어 주는 곳
		{"tmpfs", "/tmp", "tmpfs"},
		{"tmpfs", "/run", "tmpfs"},
		{"cgroup2", "/sys/fs/cgroup", "cgroup2"},      // 5단계 minibox용
		{"tracefs", "/sys/kernel/tracing", "tracefs"}, // 6단계 eBPF 트레이스포인트용
		{"bpf", "/sys/fs/bpf", "bpf"},
	}
	for _, m := range mounts {
		os.MkdirAll(m.target, 0o755)
		if err := syscall.Mount(m.src, m.target, m.fstype, 0, ""); err != nil && err != syscall.EBUSY {
			logf("mount %s: %v", m.target, err)
		}
	}
}

func setHostname() {
	name := "deepdive"
	if b, err := os.ReadFile("/etc/hostname"); err == nil {
		name = strings.TrimSpace(string(b))
	}
	syscall.Sethostname([]byte(name))
}

// ---------- 2. 커널 모듈 로드 (insmod가 하는 일) ----------

// /etc/deepdive/modules: 한 줄에 "모듈경로 [파라미터...]"
func loadModules() {
	for _, line := range readLines("/etc/deepdive/modules") {
		fields := strings.Fields(line)
		f, err := os.Open(fields[0])
		if err != nil {
			logf("module %s: %v", fields[0], err)
			continue
		}
		// finit_module(fd, params, flags): 파일 디스크립터로 모듈을 커널에 올리는 시스템 콜
		err = unix.FinitModule(int(f.Fd()), strings.Join(fields[1:], " "), 0)
		f.Close()
		if err != nil {
			logf("finit_module %s: %v", fields[0], err)
		} else {
			logf("loaded module %s", fields[0])
		}
	}
}

// ---------- 3. 서비스 실행과 감시 ----------

type service struct {
	name    string
	respawn bool // 종료되면 다시 띄울까?
	argv    []string
	pid     int
}

// /etc/deepdive/services: 한 줄에 "이름 respawn|once 명령 [인자...]"
// 테스트 모드에서는 /etc/deepdive/services.test를 쓴다
func loadServices(testMode bool) []*service {
	path := "/etc/deepdive/services"
	if testMode {
		path = "/etc/deepdive/services.test"
	}
	var svcs []*service
	for _, line := range readLines(path) {
		f := strings.Fields(line)
		if len(f) < 3 {
			continue
		}
		svcs = append(svcs, &service{name: f[0], respawn: f[1] == "respawn", argv: f[2:]})
	}
	return svcs
}

func start(s *service) {
	console, err := os.OpenFile("/dev/console", os.O_RDWR, 0)
	if err != nil {
		logf("open console: %v", err)
		return
	}
	defer console.Close()
	attr := &os.ProcAttr{
		Env:   []string{"PATH=/bin:/sbin:/usr/bin", "HOME=/root", "USER=root", "TERM=linux"},
		Dir:   "/",
		Files: []*os.File{console, console, console},
		// 새 세션을 만들고 콘솔을 제어 터미널로: 그래야 셸에서 Ctrl+C가 동작한다
		Sys: &syscall.SysProcAttr{Setsid: true, Setctty: true, Ctty: 0},
	}
	p, err := os.StartProcess(s.argv[0], s.argv, attr)
	if err != nil {
		logf("start %s: %v", s.name, err)
		return
	}
	s.pid = p.Pid
	logf("started %s (pid %d)", s.name, s.pid)
}

func supervise(svcs []*service) {
	// PID 1이 받는 시그널. busybox의 poweroff/reboot 명령은 PID 1에 시그널을 보낸다
	sigs := make(chan os.Signal, 8)
	signal.Notify(sigs, syscall.SIGCHLD, syscall.SIGUSR2, syscall.SIGTERM, syscall.SIGINT)

	for _, s := range svcs {
		start(s)
	}

	for sig := range sigs {
		switch sig {
		case syscall.SIGUSR2: // busybox poweroff
			shutdown(syscall.LINUX_REBOOT_CMD_POWER_OFF)
		case syscall.SIGTERM, syscall.SIGINT: // busybox reboot, Ctrl+Alt+Del
			shutdown(syscall.LINUX_REBOOT_CMD_RESTART)
		case syscall.SIGCHLD:
			reap(svcs)
		}
	}
}

// 좀비 회수: 부모가 먼저 죽은 프로세스(고아)는 전부 PID 1의 자식이 된다.
// PID 1이 wait()해 주지 않으면 좀비가 영원히 남는다. 컨테이너에서 tini가 필요한 이유이기도 하다.
func reap(svcs []*service) {
	for {
		var ws syscall.WaitStatus
		pid, err := syscall.Wait4(-1, &ws, syscall.WNOHANG, nil)
		if pid <= 0 || err != nil {
			return
		}
		for _, s := range svcs {
			if s.pid != pid {
				continue
			}
			logf("%s (pid %d) exited with %d", s.name, pid, ws.ExitStatus())
			s.pid = 0
			if s.respawn {
				time.Sleep(500 * time.Millisecond)
				start(s)
			} else if s.name == "selftest" {
				shutdown(syscall.LINUX_REBOOT_CMD_POWER_OFF)
			}
		}
	}
}

func shutdown(cmd int) {
	logf("shutting down")
	syscall.Kill(-1, syscall.SIGTERM) // PID 1을 제외한 모든 프로세스에
	time.Sleep(time.Second)
	syscall.Kill(-1, syscall.SIGKILL)
	syscall.Sync()
	drainConsole()
	// PID 1이 그냥 exit하면 커널은 "Attempted to kill init!" 패닉을 낸다. 반드시 reboot 시스템 콜로 끝낸다
	syscall.Reboot(cmd)
}

// 콘솔(시리얼)에 아직 나가지 못한 출력이 남아 있으면 전원을 끌 때 사라진다.
// tcdrain(TCSBRK, 1)으로 전송이 끝날 때까지 기다린다. KVM처럼 빠른 환경에서 특히 필요하다.
func drainConsole() {
	f, err := os.OpenFile("/dev/console", os.O_WRONLY, 0)
	if err != nil {
		return
	}
	defer f.Close()
	unix.IoctlSetInt(int(f.Fd()), unix.TCSBRK, 1)
	time.Sleep(200 * time.Millisecond)
}

// ---------- 유틸 ----------

func readLines(path string) []string {
	f, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer f.Close()
	var lines []string
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		l := strings.TrimSpace(sc.Text())
		if l != "" && !strings.HasPrefix(l, "#") {
			lines = append(lines, l)
		}
	}
	return lines
}

func cmdlineHas(s string) bool {
	b, _ := os.ReadFile("/proc/cmdline")
	for _, f := range strings.Fields(string(b)) {
		if f == s {
			return true
		}
	}
	return false
}

func uptime() string {
	b, _ := os.ReadFile("/proc/uptime")
	if f := strings.Fields(string(b)); len(f) > 0 {
		return f[0] + "s"
	}
	return "?"
}
