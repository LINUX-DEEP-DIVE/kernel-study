#!/usr/bin/env bash
# busybox 하나로 최소 루트 파일시스템을 만든다.
#   사용법: ./make-rootfs.sh [대상 폴더]   (기본: ./rootfs)
#   필요: sudo apt install busybox-static
set -euo pipefail

DEST="${1:-rootfs}"
BUSYBOX="$(command -v busybox || true)"
if [ -z "$BUSYBOX" ] || ! file "$BUSYBOX" | grep -q "statically linked"; then
  echo "정적 링크된 busybox가 필요합니다: sudo apt install busybox-static" >&2
  exit 1
fi

mkdir -p "$DEST"/{bin,sbin,etc,proc,sys,dev,tmp,root}
cp "$BUSYBOX" "$DEST/bin/busybox"
# busybox는 이름(argv[0])을 보고 어떤 명령인지 결정한다: ls, sh, ps ... 전부 같은 바이너리
for applet in $("$DEST/bin/busybox" --list); do
  [ "$applet" = busybox ] && continue
  ln -sf busybox "$DEST/bin/$applet"
done
echo "root:x:0:0:root:/root:/bin/sh" > "$DEST/etc/passwd"
echo "root:x:0:" > "$DEST/etc/group"
echo "rootfs ready: $DEST ($(find "$DEST/bin" -mindepth 1 | wc -l) applets)"
