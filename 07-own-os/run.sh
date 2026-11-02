#!/usr/bin/env bash
# QEMU로 DeepDive OS 부팅
#   ./run.sh        대화형 (콘솔에서 poweroff로 종료, 멈추면 Ctrl+A 후 X)
#   ./run.sh test   selftest 실행 후 자동 종료. PASS가 아니면 exit 1
set -euo pipefail
cd "$(dirname "$0")/out"

ACCEL=()
[ -w /dev/kvm ] && ACCEL=(-enable-kvm -cpu host)
APPEND="console=ttyS0 panic=-1 quiet"

if [ "${1:-}" = test ]; then
  timeout 300 qemu-system-x86_64 "${ACCEL[@]}" -m 512 -smp 2 -nographic -no-reboot \
    -kernel bzImage -initrd initramfs.cpio.gz -append "$APPEND deepdive.test=1" | tee boot.log
  grep -q "DEEPDIVE-SELFTEST: PASS" boot.log
else
  exec qemu-system-x86_64 "${ACCEL[@]}" -m 512 -smp 2 -nographic -no-reboot \
    -kernel bzImage -initrd initramfs.cpio.gz -append "$APPEND"
fi
