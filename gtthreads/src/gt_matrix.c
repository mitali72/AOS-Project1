#include <stdio.h>
#include <unistd.h>
// #include <linux/unistd.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "gt_include.h"


#define ROWS 256
#define COLS ROWS
#define SIZE COLS

#define NUM_CPUS 4
#define NUM_GROUPS NUM_CPUS
#define PER_GROUP_COLS (SIZE/NUM_GROUPS)

#define NUM_THREADS 128
#define PER_THREAD_ROWS (SIZE/NUM_THREADS)

int sched_mode;
bool load_balance=false;

uthread_log_t uthread_timing_log[NUM_THREADS];

/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

typedef struct matrix
{
	int m[SIZE][SIZE];

	int rows;
	int cols;
	unsigned int reserved[2];
} matrix_t;


typedef struct __uthread_arg
{
	matrix_t *_A, *_B, *_C;
	unsigned int reserved0;

	unsigned int tid;
	unsigned int gid;
	int start_row; /* start_row -> (start_row + PER_THREAD_ROWS) */
	int start_col; /* start_col -> (start_col + PER_GROUP_COLS) */
	int num_rows;
}uthread_arg_t;

struct timeval tv1;

static void generate_matrix(matrix_t *mat, int val)
{

	printf("inside gen\n");
	int i,j;
	mat->rows = SIZE;
	mat->cols = SIZE;
	for(i = 0; i < mat->rows;i++)
		for( j = 0; j < mat->cols; j++ )
		{	
			// printf("i, j: %d, %d\n", i,j);
			mat->m[i][j] = val;
		}

	// printf("inside gen, got completed\n");
	return;
}

static void print_matrix(matrix_t *mat)
{
	int i, j;

	for(i=0;i<SIZE;i++)
	{
		for(j=0;j<SIZE;j++)
			printf(" %d ",mat->m[i][j]);
		printf("\n");
	}

	return;
}

extern int uthread_create(uthread_t *, void *, void *, uthread_group_t, int);

static void * uthread_mulmat(void *p)
{
	int i, j, k;
	int start_row, end_row;
	int start_col, end_col;
	unsigned int cpuid;
	struct timeval tv2;

#define ptr ((uthread_arg_t *)p)

	i=0; j= 0; k=0;

	start_row = ptr->start_row;
	// end_row = (ptr->start_row + PER_THREAD_ROWS);
	end_row = ptr->num_rows;

#ifdef GT_GROUP_SPLIT
	start_col = ptr->start_col;
	end_col = (ptr->start_col + PER_THREAD_ROWS);
#else
	start_col = 0;
	end_col = ptr->num_rows;
#endif

#ifdef GT_THREADS
	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started",ptr->tid, ptr->gid, cpuid);
#else
	fprintf(stderr, "\nThread(id:%d, group:%d) started",ptr->tid, ptr->gid);
#endif

	for(i = start_row; i < end_row; i++)
		for(j = start_col; j < end_col; j++)
			for(k = 0; k < SIZE; k++)
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];

#ifdef GT_THREADS
	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#else
	gettimeofday(&tv2,NULL);
	fprintf(stderr, "\nThread(id:%d, group:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#endif

#undef ptr
	return 0;
}

matrix_t A, B, C;

static void init_matrices()
{	
	// printf("entered init matrices\n");
	generate_matrix(&A, 1);
	generate_matrix(&B, 1);
	generate_matrix(&C, 0);

	return;
}


void parse_args(int argc, char* argv[])
{
	int inx;

	for(inx=0; inx<argc; inx++)
	{
		if(argv[inx][0]=='-')
		{
			if(!strcmp(&argv[inx][1], "lb"))
			{
				//TODO: add option of load balancing mechanism
				load_balance = true;
				printf("enable load balancing\n");
			}
			else if(!strcmp(&argv[inx][1], "s"))
			{
				//TODO: add different types of scheduler
				inx++;
				if(!strcmp(&argv[inx][0], "0"))
				{	
					sched_mode = 0;
					printf("use priority scheduler\n");
				}
				else if(!strcmp(&argv[inx][0], "1"))
				{	
					sched_mode = 1;
					printf("use credit scheduler\n");
				}
			}
		}
	}

	return;
}


uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];

int main(int argc, char* argv[])
{	
	if (argc < 2)
	{
        fprintf(stderr, "Invalid arguments" );
        return 1;
	}
	// int num_cpus = (int)sysconf(_SC_NPROCESSORS_CONF);
	int num_cpus = 4;
	uthread_arg_t *uarg;
	parse_args(argc, argv);

	if(sched_mode==1)
	{	

		gtthread_app_init(sched_mode, load_balance);

		int credit_groups[] = {25,50,75,100};
		int matrix_sizes[] = {32,64,128,256};

		// printf("sched_mode and load_balance:%d, %d\n", sched_mode, load_balance);
		init_matrices();

		gettimeofday(&tv1,NULL);

		for(int c=0;c<4;c++)
		{
			for(int mind=0;mind<4;mind++)
			{
				
				int grp_sz = NUM_THREADS/(4*4);
				for(int inx=0; inx<grp_sz; inx++)
				{	
					
					int tid = inx+grp_sz*mind+grp_sz*4*c;
					printf("Thread created %d\n",tid);
					printf("matrices init?\n");

					uarg = &uargs[tid];
					uarg->_A = &A;
					uarg->_B = &B;
					uarg->_C = &C;

					uarg->tid = tid;

					uarg->gid = ((tid) % num_cpus);

					// uarg->start_row = (inx * PER_THREAD_ROWS);
					uarg->start_row = 0;
					uarg->num_rows = matrix_sizes[mind];
				

					#ifdef GT_GROUP_SPLIT
									/* Wanted to split the columns by groups !!! */
									uarg->start_col = (uarg->gid * PER_GROUP_COLS);
					#endif

					printf("Thread meta data set %d\n",tid);

					uthread_create(&utids[tid], uthread_mulmat, uarg, uarg->gid, credit_groups[c]);
				}
			}
		}

		gtthread_app_exit();
	}
	else
	{
		uthread_arg_t *uarg;
		int inx;

		gtthread_app_init(sched_mode, load_balance);

		init_matrices();

		gettimeofday(&tv1,NULL);

		for(inx=0; inx<NUM_THREADS; inx++)
		{
			uarg = &uargs[inx];
			uarg->_A = &A;
			uarg->_B = &B;
			uarg->_C = &C;

			uarg->tid = inx;

			uarg->gid = (inx % NUM_GROUPS);

			uarg->start_row = (inx * PER_THREAD_ROWS);
		#ifdef GT_GROUP_SPLIT
			/* Wanted to split the columns by groups !!! */
			uarg->start_col = (uarg->gid * PER_GROUP_COLS);
		#endif

			uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, 0);
		}

		gtthread_app_exit();
	}
	

	// print_matrix(&C);
// 	fprintf(stderr, "********************************");
	return(0);
}
