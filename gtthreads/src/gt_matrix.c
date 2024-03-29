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
bool yield=false;
int tid_yield = 24;
int yield_cnt = 2;
int yield_interval = 10;

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

	int i,j;
	mat->rows = SIZE;
	mat->cols = SIZE;
	for(i = 0; i < mat->rows;i++)
		for( j = 0; j < mat->cols; j++ )
		{	
			mat->m[i][j] = val;
		}
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

extern int uthread_create(uthread_t *, void *, void *, uthread_group_t, int, int);

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
			for(k = 0; k < end_row; k++)

				if(yield && yield_cnt>0)
				{	
					
					if(i==end_row/3 && ptr->tid % yield_interval == 0 && yield_cnt >0 ){
						yield_cnt --;
						gt_yield();
					}
				}
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
			else if(!strcmp(&argv[inx][1], "yd"))
			{
				//TODO: add option of load balancing mechanism
				yield = true;
				printf("enable yielding for a thread\n");
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


	gtthread_app_init(sched_mode, load_balance);

	int credit_groups[] = {100,75,50,25};
	int matrix_sizes[] = {256,128,64,32};

	// printf("sched_mode and load_balance:%d, %d\n", sched_mode, load_balance);
	init_matrices();

	gettimeofday(&tv1,NULL);

	for(int c=0;c<4;c++)
	{
		for(int matrix_ind=0;matrix_ind<4;matrix_ind++)
		{
			
			int grp_sz = NUM_THREADS/(4*4);
			for(int inx=0; inx<grp_sz; inx++)
			{	
				
				int tid = inx+grp_sz*matrix_ind+grp_sz*4*c;
				// printf("Thread created %d\n",tid);

				uarg = &uargs[tid];
				uarg->_A = &A;
				uarg->_B = &B;
				uarg->_C = &C;

				uarg->tid = tid;

				uarg->gid = ((tid) % num_cpus);

				// uarg->start_row = (inx * PER_THREAD_ROWS);
				uarg->start_row = 0;
				uarg->num_rows = matrix_sizes[matrix_ind];
			

				#ifdef GT_GROUP_SPLIT
								/* Wanted to split the columns by groups !!! */
								uarg->start_col = (uarg->gid * PER_GROUP_COLS);
				#endif

				uthread_create(&utids[tid], uthread_mulmat, uarg, uarg->gid, credit_groups[c], matrix_sizes[matrix_ind]);
			}
		}
	}

	gtthread_app_exit();

	// log data
	float running_cpu_time[16] = {0};
	float running_wait_time[16] = {0.0};
	float running_exec_time[16] = {0.0};

	FILE *filePtr;
    char filename_stats1[] = "output1.txt";

    // Open the file in write mode
    filePtr = fopen(filename_stats1, "w");

    // Check if file opened successfully
    if (filePtr == NULL) {
        printf("Unable to open file %s\n", filename_stats1);
        return 1;
    }

    // Write output to the file
	fprintf(filePtr, "group_name,thread_number,cpu_time(us),wait_time(us),exec_time(us)\n");
	for(int i=0;i<NUM_THREADS;i++)
	{	
		int cpu_time = (uthread_timing_log[i].total_cpu_time.tv_sec*1000000  + uthread_timing_log[i].total_cpu_time.tv_usec);
		int exec_time = (uthread_timing_log[i].total_exec_time.tv_sec*1000000  + uthread_timing_log[i].total_exec_time.tv_usec);

		running_cpu_time[(int) i/8] += cpu_time;
		running_exec_time[(int) i/8] += exec_time;

		fprintf(filePtr, "c_%d_m_%d,%d,%d,%d,%d\n", uthread_timing_log[i].alloted_credits, uthread_timing_log[i].matrix_size, i, cpu_time, exec_time - cpu_time, exec_time);
	}
    // Close the file
    fclose(filePtr);


	// stats 2
	char filename_stats2[] = "output2.txt";

    // Open the file in write mode
    filePtr = fopen(filename_stats2, "w");

    // Check if file opened successfully
    if (filePtr == NULL) {
        printf("Unable to open file %s\n", filename_stats2);
        return 1;
    }

    // Write output to the file
	fprintf(filePtr, "group_name,mean_cpu_time(us),mean_wait_time(us),mean_exec_time(us),matrix_size\n");
	for(int i=0;i<16;i++)
	{	

		fprintf(filePtr, "c_%d_m_%d,%f,%f,%f,%d\n", credit_groups[(int)i/4], matrix_sizes[i%4], running_cpu_time[i]/(float) 8, (running_exec_time[i]-running_cpu_time[i])/(float) 8, running_exec_time[i]/(float) 8, matrix_sizes[i%4]);
	}
    // Close the file
    fclose(filePtr);

	

	// else
	// {
	// 	uthread_arg_t *uarg;
	// 	int inx;

	// 	gtthread_app_init(sched_mode, load_balance);

	// 	init_matrices();

	// 	gettimeofday(&tv1,NULL);

	// 	for(inx=0; inx<NUM_THREADS; inx++)
	// 	{
	// 		uarg = &uargs[inx];
	// 		uarg->_A = &A;
	// 		uarg->_B = &B;
	// 		uarg->_C = &C;

	// 		uarg->tid = inx;

	// 		uarg->gid = (inx % NUM_GROUPS);

	// 		uarg->start_row = (inx * PER_THREAD_ROWS);
	// 	#ifdef GT_GROUP_SPLIT
	// 		/* Wanted to split the columns by groups !!! */
	// 		uarg->start_col = (uarg->gid * PER_GROUP_COLS);
	// 	#endif

	// 		uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, 0);
	// 	}

	// 	gtthread_app_exit();
	// }
	

	// print_matrix(&C);
// 	fprintf(stderr, "********************************");
	return(0);
}
