#!/usr/bin/env bash
# 커널 소스를 받아 virtme-ng용 최소 설정으로 빌드합니다.
#   사용법: ./build-kernel.sh [태그]      예) ./build-kernel.sh v6.12
set -euo pipefail

TAG="${1:-}"
ROOT="$(git rev-parse --show-toplevel)"
SRC="$ROOT/linux"

if [ ! -d "$SRC" ]; then
  if [ -n "$TAG" ]; then
    git clone --depth 1 --branch "$TAG" \
      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git "$SRC"
  else
    git clone --depth 1 \
      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git "$SRC"
  fi
fi

cd "$SRC"
# vng --kconfig: virtme-ng 부팅에 필요한 최소 설정을 만들어 줌
vng --kconfig
# 디버깅과 eBPF 실습에 필요한 옵션
./scripts/config --enable DEBUG_INFO_DWARF5 --enable DEBUG_INFO_BTF \
  --enable BPF_SYSCALL --enable BPF_JIT --enable KPROBES --enable FTRACE \
  --enable CGROUPS --enable MEMCG --enable NAMESPACES --enable USER_NS
make olddefconfig
make -j"$(nproc)" LOCALVERSION=-deepdive

echo
echo "빌드 완료: $SRC/arch/x86/boot/bzImage"
echo "부팅: cd $SRC && vng"
