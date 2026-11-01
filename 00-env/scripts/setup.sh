#!/usr/bin/env bash
# Ubuntu 24.04용 실습 도구 설치 스크립트
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
  build-essential bc flex bison libelf-dev libssl-dev libncurses-dev dwarves \
  git curl \
  "linux-headers-$(uname -r)" \
  strace ltrace gdb \
  linux-tools-common "linux-tools-$(uname -r)" bpftrace \
  qemu-system-x86 \
  python3-pip pipx golang-go

# virtme-ng: 빌드한 커널을 QEMU로 빠르게 부팅
pipx install virtme-ng || pipx upgrade virtme-ng
pipx ensurepath

echo
echo "설치 완료. 새 셸을 열면 vng 명령을 쓸 수 있습니다."
