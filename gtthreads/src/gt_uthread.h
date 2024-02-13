#ifndef __GT_UTHREAD_H
#define __GT_UTHREAD_H

/* User-level thread implementation (using alternate signal stacks) */

typedef unsigned int uthread_t;
typedef unsigned int uthread_group_t;

/* uthread states */
#define UTHREAD_INIT 0x01
#define UTHREAD_RUNNABLE 0x02
#define UTHREAD_RUNNING 0x04
#define UTHREAD_CANCELLED 0x08
#define UTHREAD_DONE 0x10

#include <stdbool.h>

/* uthread struct : has all the uthread context info */
typedef struct uthread_struct
{

	int uthread_state; /* UTHREAD_INIT, UTHREAD_RUNNABLE, UTHREAD_RUNNING, UTHREAD_CANCELLED, UTHREAD_DONE */
	int uthread_priority; /* uthread running priority */
	int cpu_id; /* cpu it is currently executing on */
	int last_cpu_id; /* last cpu it was executing on */
	int alloted_credits; /* uthread total alloted credits */
	int remaining_credits; /* uthread remaining credits */
	int matrix_size;
	bool yielded;

	struct timeval curr_start_time, curr_end_time, total_cpu_time, total_exec_time, start_time, end_time;

	uthread_t uthread_tid; /* thread id */
	uthread_group_t uthread_gid; /* thread group id  */
	int (*uthread_func)(void*);
	void *uthread_arg;

	void *exit_status; /* exit status */
	int reserved1;
	int reserved2;
	int reserved3;

	sigjmp_buf uthread_env; /* 156 bytes : save user-level thread context*/
	stack_t uthread_stack; /* 12 bytes : user-level thread stack */
	TAILQ_ENTRY(uthread_struct) uthread_runq;
} uthread_struct_t;

typedef struct uthread_log
{
	struct timeval total_cpu_time, total_exec_time, total_wait_time;
	int alloted_credits;
	int matrix_size;

} uthread_log_t;

struct __kthread_runqueue;
extern void uthread_schedule(uthread_struct_t * (*kthread_best_sched_uthread)(struct __kthread_runqueue *));
extern void gt_yield();
#endif
