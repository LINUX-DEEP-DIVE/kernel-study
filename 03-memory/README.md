# 3단계. 메모리

목표: "메모리를 할당했다"와 "물리 메모리를 쓰고 있다"가 다르다는 것을 숫자로 확인하고, 가상 주소가 물리 주소로 바뀌는 과정을 직접 따라간다.

## 예제

| 파일 | 내용 | 언어 |
|---|---|---|
| [c/vm_layout.c](c/vm_layout.c) | text, data, bss, heap, mmap, stack의 실제 주소와 `/proc/self/maps` | C |
| [c/lazy_alloc.c](c/lazy_alloc.c) | 1GiB `mmap` 후 읽기/쓰기/`MADV_DONTNEED`마다 RSS와 page fault 변화. `huge` 옵션으로 2MiB 페이지 | C |
| [c/pagemap.c](c/pagemap.c) | `/proc/self/pagemap`으로 가상 주소 → 물리 주소 변환, fork 후 COW로 물리 페이지가 갈라지는 순간 | C |
| [go/main.go](go/main.go) | GC 후에도 RSS가 안 줄어드는 이유, `FreeOSMemory`, `GOMEMLIMIT` | Go |

```bash
make -C c
./c/vm_layout | less
./c/lazy_alloc; ./c/lazy_alloc huge
sudo ./c/pagemap
cd go && go run . && GOMEMLIMIT=200MiB go run .
```

## 읽을 커널 코드

1. [`struct mm_struct`](https://elixir.bootlin.com/linux/latest/A/ident/mm_struct)와 [`struct vm_area_struct`](https://elixir.bootlin.com/linux/latest/A/ident/vm_area_struct): `/proc/self/maps`의 한 줄이 VMA 하나
2. [`exc_page_fault`](https://elixir.bootlin.com/linux/latest/A/ident/exc_page_fault) → [`handle_mm_fault`](https://elixir.bootlin.com/linux/latest/A/ident/handle_mm_fault) → [`do_anonymous_page`](https://elixir.bootlin.com/linux/latest/A/ident/do_anonymous_page): 처음 만지는 익명 페이지
3. [`do_wp_page`](https://elixir.bootlin.com/linux/latest/A/ident/do_wp_page): 쓰기 금지 페이지에 쓸 때, 즉 COW가 실제로 복사되는 곳
4. 페이지 테이블 4단계(5단계): `pgd → p4d → pud → pmd → pte`, [`Documentation/mm/page_tables.rst`](https://docs.kernel.org/mm/page_tables.html)
5. 물리 메모리 할당: 버디 할당자 [`__alloc_pages`](https://elixir.bootlin.com/linux/latest/A/ident/__alloc_pages), 작은 객체는 slab (`sudo slabtop`)
6. OOM: [`out_of_memory`](https://elixir.bootlin.com/linux/latest/A/ident/out_of_memory), [`oom_badness`](https://elixir.bootlin.com/linux/latest/A/ident/oom_badness)

## 실습 과제

**주소 공간**
- [ ] `vm_layout`을 두 번 실행해서 바뀌는 주소와 안 바뀌는 비율을 비교하기. `setarch -R ./vm_layout`으로 ASLR을 끄면?
- [ ] `/proc/self/maps`의 각 줄에서 권한(`r-xp`, `rw-p`)과 이름을 보고 위의 주소들이 어느 줄에 속하는지 짝 맞추기
- [ ] `malloc(16)`과 `malloc(1MiB)`의 주소가 완전히 다른 영역에 있다. `strace ./vm_layout 2>&1 | grep -E 'brk|mmap'`으로 이유 확인하기

**Lazy allocation**
- [ ] 1GiB를 `mmap`했는데 RSS가 거의 그대로다. 커널은 `mmap` 시점에 무엇만 만들었을까? (힌트: VMA)
- [ ] **읽기**만 했는데 page fault는 262144번 났는데 RSS는 그대로다. 그럼 읽은 값 0은 어디서 왔을까? (힌트: zero page)
- [ ] `huge` 옵션에서 fault 수가 512배 줄었다. 512는 어디서 나온 숫자일까? `grep -i huge /proc/meminfo`, `cat /sys/kernel/mm/transparent_hugepage/enabled`
- [ ] `MADV_DONTNEED` 후 다시 읽으면 값은 무엇일까? 코드에 추가해서 확인하기

**COW를 물리 주소로**
- [ ] `sudo ./c/pagemap`에서 fork 직후 부모와 자식의 PFN이 같고, 자식이 쓴 뒤에만 바뀐다. 2단계 `fork_cow`의 page fault 수와 연결해서 설명하기
- [ ] sudo 없이 실행하면 PFN이 0이다. 커널이 왜 일반 사용자에게 물리 주소를 숨길까? (힌트: Rowhammer)

**Go와 커널**
- [ ] GC 후 `HeapInuse`는 0.4MiB인데 RSS는 400MiB 그대로다. Go 런타임은 왜 바로 돌려주지 않을까?
- [ ] `FreeOSMemory` 후 RSS가 줄었다. `strace -f -e trace=madvise go run .`으로 어떤 `madvise` 플래그를 쓰는지 확인하기
- [ ] `GOMEMLIMIT=200MiB`에서 `NumGC`가 7 → 193으로 늘었다. 한도 근처에서 GC가 어떻게 행동하는지 `GODEBUG=gctrace=1`로 관찰하기

**cgroup 메모리 제한과 OOM (5단계 예고)**
```bash
# 100MiB 제한을 건 cgroup 안에서 1GiB를 쓰게 해 보기
sudo systemd-run --scope -p MemoryMax=100M -p MemorySwapMax=0 ./c/lazy_alloc
sudo dmesg | tail -20     # oom-kill 로그
```
- [ ] 프로그램이 어느 단계에서 죽었나? `READ every page`는 통과하고 `WRITE`에서 죽었다면 그 이유는?
- [ ] dmesg의 OOM 로그에서 `oom_score_adj`, `rss`, `memcg` 항목을 찾아 해석하기. k8s에서 Pod가 `OOMKilled`될 때 정확히 이 경로를 탄다

## 다음 단계와의 연결

`dup_mm`, `do_wp_page`처럼 커널 함수 이름을 계속 봤습니다. [4단계](../04-module)에서는 직접 커널 코드를 짜고, [6단계 eBPF](../06-ebpf)에서는 이 함수들에 훅을 걸어 page fault를 실시간으로 셉니다.
