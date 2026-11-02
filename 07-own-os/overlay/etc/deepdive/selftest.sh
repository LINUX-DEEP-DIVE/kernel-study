#!/bin/sh
# shellcheck disable=SC2016  # check()는 일부러 작은따옴표 식을 eval 한다
# 우리 OS가 제대로 부팅됐는지 확인하는 자동 테스트. CI에서 QEMU로 돌린다.
fail=0
check() { if eval "$2" >/dev/null 2>&1; then echo "SELFTEST ok   $1"; else echo "SELFTEST FAIL $1"; fail=1; fi; }

echo "SELFTEST kernel $(uname -r), init=$(cat /proc/1/comm)"
check "PID 1 is deepdive init"    '[ "$(cat /proc/1/comm)" = init ]'
check "hostname"                   '[ "$(hostname)" = deepdive ]'
check "cgroup2 mounted"            'grep -q " /sys/fs/cgroup cgroup2 " /proc/mounts'
check "hello module loaded"        'grep -q "^hello " /proc/modules'
check "deepdive device"            '[ -c /dev/deepdive ]'
check "deepdive write/read"        'echo selftest > /dev/deepdive && [ "$(head -c 9 /dev/deepdive)" = selftest ]'
check "ioctl from Go"              'deepdive-ioctl | grep -q "after read"'
check "minibox pid namespace"      '[ "$(minibox run /containers/busybox /bin/sh -c "echo \$\$" 2>/dev/null)" = 1 ]'
check "minibox hostname"           '[ "$(minibox run -hostname box1 /containers/busybox /bin/hostname 2>/dev/null)" = box1 ]'
check "minibox memory limit (OOM)" 'minibox run -mem 16M /containers/busybox /bin/sh -c "x=aaaaaaaa; while true; do x=\$x\$x; done" 2>&1 | grep -q "oom_kill [1-9]"'
check "minibox rejects bad limit"  '! minibox run -mem bogus /containers/busybox /bin/true'
minibox run /containers/busybox /bin/sleep 100 >/dev/null 2>&1 &
mb=$!; sleep 1; kill -TERM $mb
check "minibox stops on SIGTERM"   'wait $mb; ! kill -0 $mb'
check "minibox cleans up cgroups"  '[ -z "$(find /sys/fs/cgroup/minibox -mindepth 1 -maxdepth 1 -type d)" ]'

if [ -x /bin/execsnoop ]; then
  execsnoop > /tmp/exec.log 2>&1 &
  sleep 2; /bin/true; sleep 1; kill $! 2>/dev/null; sleep 0.5
  check "eBPF execsnoop sees exec" 'grep -q "/bin/true" /tmp/exec.log'
fi

[ $fail = 0 ] && echo "DEEPDIVE-SELFTEST: PASS" || echo "DEEPDIVE-SELFTEST: FAIL"
