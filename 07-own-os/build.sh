#!/usr/bin/env bash
# DeepDive OS 이미지 만들기: 커널 + initramfs(우리 init, busybox, 모듈, minibox, execsnoop)
#
#   ./build.sh                      호스트 커널(/boot/vmlinuz-$(uname -r))로
#   KERNEL_SRC=../linux ./build.sh  0단계에서 직접 빌드한 커널로 (권장)
#
# 결과물: out/bzImage, out/initramfs.cpio.gz  → ./run.sh 로 부팅
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(git rev-parse --show-toplevel)"
OUT="$PWD/out"
RFS="$OUT/rootfs"

# ---- 1. 커널과, 그 커널에 맞는 모듈 빌드 디렉터리 고르기 ----
if [ -n "${KERNEL_SRC:-}" ]; then
  KERNEL_SRC="$(cd "$KERNEL_SRC" && pwd)"
  KERNEL_IMG="$KERNEL_SRC/arch/x86/boot/bzImage"
  KDIR="$KERNEL_SRC"
else
  KREL="${KREL:-$(uname -r)}"
  KERNEL_IMG="/boot/vmlinuz-$KREL"
  KDIR="/lib/modules/$KREL/build"
fi
[ -r "$KERNEL_IMG" ] || { echo "커널 이미지를 읽을 수 없음: $KERNEL_IMG (Ubuntu는 sudo 필요하거나 KERNEL_SRC 사용)" >&2; exit 1; }
echo "kernel : $KERNEL_IMG"
echo "modules: built against $KDIR"

rm -rf "$OUT" && mkdir -p "$RFS"/{bin,sbin,etc,proc,sys,dev,tmp,run,root,lib/modules/deepdive,containers}
cp "$KERNEL_IMG" "$OUT/bzImage"

# ---- 2. PID 1: Go init ----
(cd init && CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o "$RFS/init" .)

# ---- 3. 유저랜드: busybox ----
BUSYBOX="$(command -v busybox)"
file -L "$BUSYBOX" | grep -q "statically linked" || { echo "sudo apt install busybox-static" >&2; exit 1; }
cp "$BUSYBOX" "$RFS/bin/busybox"
for a in $("$RFS/bin/busybox" --list); do
  [ "$a" = busybox ] || [ "$a" = init ] || ln -sf busybox "$RFS/bin/$a"
done

# ---- 4. 앞 단계에서 만든 것들 ----
make -s -C "$ROOT/00-env/hello-module" KDIR="$KDIR"
make -s -C "$ROOT/04-module/ringbuf" module KDIR="$KDIR"
cp "$ROOT/00-env/hello-module/hello.ko" "$ROOT/04-module/ringbuf/deepdive.ko" "$RFS/lib/modules/deepdive/"
make -s -C "$ROOT/02-process/ptree-module" KDIR="$KDIR" && cp "$ROOT/02-process/ptree-module/ptree.ko" "$RFS/lib/modules/deepdive/"

(cd "$ROOT/04-module/go" && CGO_ENABLED=0 go build -o "$RFS/bin/deepdive-ioctl" .)
(cd "$ROOT/05-container/minibox" && CGO_ENABLED=0 go build -o "$RFS/bin/minibox" .)
"$ROOT/05-container/make-rootfs.sh" "$RFS/containers/busybox" >/dev/null

if command -v clang >/dev/null && [ -d /usr/include/bpf ]; then
  make -s -C "$ROOT/06-ebpf/execsnoop" && cp "$ROOT/06-ebpf/execsnoop/execsnoop" "$RFS/bin/"
else
  echo "clang/libbpf-dev 없음: execsnoop 생략"
fi

# ---- 5. 설정 파일 덮어쓰기 후 cpio로 묶기 ----
cp -r overlay/. "$RFS/"
(cd "$RFS" && find . -print0 | cpio --null -o -H newc --quiet | gzip -9) > "$OUT/initramfs.cpio.gz"

echo
du -h "$OUT/bzImage" "$OUT/initramfs.cpio.gz"
echo "부팅: ./run.sh   (자동 테스트: ./run.sh test)"
