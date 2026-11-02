// SPDX-License-Identifier: GPL-2.0
/*
 * 커널 안에서 실행되는 eBPF 프로그램.
 * 누군가 execve로 새 프로그램을 실행할 때마다(sched_process_exec 트레이스포인트)
 * pid, 부모 pid, cgroup id, 프로그램 이름, 파일 경로를 ring buffer로 유저 공간에 보낸다.
 *
 * 커널 모듈과 다른 점: 로드 전에 커널의 "verifier"가 이 코드를 검사한다.
 * 무한 루프, 범위 밖 메모리 접근, NULL 역참조가 있으면 로드 자체가 거부된다.
 */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

#define TASK_COMM_LEN 16
#define MAX_FILENAME 128

/*
 * CO-RE (Compile Once, Run Everywhere):
 * task_struct 전체를 정의하지 않고 필요한 필드만 적어 둔다.
 * preserve_access_index 덕분에 로드 시점에 libbpf/cilium-ebpf가
 * 실행 중인 커널의 BTF를 보고 필드 오프셋을 맞춰 준다.
 */
struct task_struct {
	int tgid;
	struct task_struct *real_parent;
} __attribute__((preserve_access_index));

/* /sys/kernel/tracing/events/sched/sched_process_exec/format 과 같은 배치 */
struct sched_process_exec_args {
	unsigned long long common; /* common_type, flags, preempt_count, common_pid */
	unsigned int filename_loc; /* __data_loc: 하위 16비트 = 오프셋, 상위 16비트 = 길이 */
	int pid;
	int old_pid;
};

struct event {
	__u32 pid;
	__u32 ppid;
	__u64 cgroup_id;
	char comm[TASK_COMM_LEN];
	char filename[MAX_FILENAME];
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} events SEC(".maps");

SEC("tracepoint/sched/sched_process_exec")
int handle_exec(struct sched_process_exec_args *ctx)
{
	struct task_struct *task = (struct task_struct *)bpf_get_current_task();
	struct event *e;

	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (!e) /* verifier: 이 NULL 체크를 지우면 로드가 거부된다. 직접 해 보기 */
		return 0;

	e->pid = bpf_get_current_pid_tgid() >> 32;
	e->ppid = BPF_CORE_READ(task, real_parent, tgid);
	e->cgroup_id = bpf_get_current_cgroup_id();
	bpf_get_current_comm(&e->comm, sizeof(e->comm));
	bpf_probe_read_kernel_str(&e->filename, sizeof(e->filename),
				  (void *)ctx + (ctx->filename_loc & 0xFFFF));

	bpf_ringbuf_submit(e, 0);
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
