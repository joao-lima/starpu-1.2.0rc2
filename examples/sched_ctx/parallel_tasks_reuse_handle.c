/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2015 INRIA
 * Copyright (C) 2015 CNRS
 *
 * StarPU is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * StarPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#include <starpu.h>
#include <omp.h>

#ifdef STARPU_QUICK_CHECK
#define NTASKS 64
#else
#define NTASKS 100
#endif


#define SIZE 400

struct context
{
  int ncpus;
  int *cpus;
  unsigned id;
};

/* Helper for the task that will initiate everything */
void parallel_task_prologue_init_once_and_for_all(void * sched_ctx_)
{
	int sched_ctx = *(int *)sched_ctx_;
	int i;
	int *cpuids = NULL;
	int ncpuids = 0;
	starpu_sched_ctx_get_available_cpuids(sched_ctx, &cpuids, &ncpuids);
	printf("Context %d with %d threads \n", sched_ctx, ncpuids);

#pragma omp parallel num_threads(ncpuids)
	{
		starpu_sched_ctx_bind_current_thread_to_cpuid(cpuids[omp_get_thread_num()]);
	}

	omp_set_num_threads(ncpuids);
	free(cpuids);
	return;
}

void noop(void * buffers[], void * cl_arg)
{
}

static struct starpu_codelet init_parallel_worker_cl=
{
	.where = STARPU_CPU,
	.cpu_funcs = {noop, NULL},
	.nbuffers = 0,
	.name = "init_parallel_worker"
};

/* function called to initialize the parallel "workers" */
void parallel_task_init_one_context(unsigned * context_id)
{
	struct starpu_task * t;
	t = starpu_task_build(&init_parallel_worker_cl,
			      STARPU_SCHED_CTX, *context_id,
			      0);
	t->destroy = 1;
	t->prologue_callback_pop_func=parallel_task_prologue_init_once_and_for_all;
	t->prologue_callback_pop_arg=context_id;
	t->prologue_callback_pop_arg_free=0;

	int ret=starpu_task_submit(t);
}

struct context main_context;
struct context *contexts;
void parallel_task_init()
{
	/* Context creation */
	main_context.ncpus = starpu_cpu_worker_get_count();
	main_context.cpus = (int *) malloc(main_context.ncpus*sizeof(int));
	printf("ncpus : %d \n",main_context.ncpus);

	starpu_worker_get_ids_by_type(STARPU_CPU_WORKER, main_context.cpus, main_context.ncpus);

	main_context.id = starpu_sched_ctx_create(main_context.cpus,
						  main_context.ncpus,"main_ctx",
						  STARPU_SCHED_CTX_POLICY_NAME,"eager",
						  0);


	/* Initialize nested contexts */
	/* WARNING : the number of contexts must be a divisor of the number of available cpus*/

	contexts = malloc(sizeof(struct context)*2);
	int cpus_per_context = main_context.ncpus/2;
	int i;
	for(i = 0; i < 2; i++)
	{
		contexts[i].ncpus = cpus_per_context;
		contexts[i].cpus = main_context.cpus+i*cpus_per_context;
	}

	for(i = 0; i < 2; i++)
		contexts[i].id = starpu_sched_ctx_create(contexts[i].cpus,
							 contexts[i].ncpus,"nested_ctx",
							 STARPU_SCHED_CTX_NESTED,main_context.id,
							 0);

	for (i = 0; i < 2; i++)
	{
		parallel_task_init_one_context(&contexts[i].id);
	}

	starpu_task_wait_for_all();
	starpu_sched_ctx_set_context(&main_context.id);
}

void parallel_task_deinit()
{
	int i;
	for (i=0; i<2;i++)
		starpu_sched_ctx_delete(contexts[i].id);
}

/* Codelet SUM */
static void sum_cpu(void * descr[], void *cl_arg)
{
	double * v_dst = (double *) STARPU_VECTOR_GET_PTR(descr[0]);
	double * v_src0 = (double *) STARPU_VECTOR_GET_PTR(descr[1]);
	double * v_src1 = (double *) STARPU_VECTOR_GET_PTR(descr[1]);

	int size;
	starpu_codelet_unpack_args(cl_arg, &size);
	int i, k;
	for (k=0;k<10;k++)
	{
#pragma omp parallel for
		for (i=0; i<size; i++)
		{
			v_dst[i]+=v_src0[i]+v_src1[i];
		}
	}
}

static struct starpu_codelet sum_cl =
{
	.cpu_funcs = {sum_cpu, NULL},
	.nbuffers = 3,
	.modes={STARPU_RW,STARPU_R, STARPU_R}
};

int main(int argc, char **argv)
{
	int ntasks = NTASKS;
	int ret, j, k;
	unsigned ncpus = 0;

	ret = starpu_init(NULL);
	if (ret == -ENODEV)
		return 77;
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");

	parallel_task_init();

	/* Data preparation */
	double array1[SIZE];
	double array2[SIZE];

	memset(array1, 0, sizeof(double));
	int i;
	for (i=0;i<SIZE;i++)
	{
		array2[i]=i*2;
	}

	starpu_data_handle_t handle1;
	starpu_data_handle_t handle2;

	starpu_vector_data_register(&handle1, 0, (uintptr_t)array1, SIZE, sizeof(double));
	starpu_vector_data_register(&handle2, 0, (uintptr_t)array2, SIZE, sizeof(double));

	int size;
	size=SIZE;

	for (i = 0; i < ntasks; i++)
	{
		struct starpu_task * t;
		t=starpu_task_build(&sum_cl,
				    STARPU_RW,handle1,
				    STARPU_R,handle2,
				    STARPU_R,handle1,
				    STARPU_VALUE,&size,sizeof(int),
				    STARPU_SCHED_CTX, main_context.id,
				    0);
		t->cl_arg_free = 1;
		t->destroy = 1;
		ret=starpu_task_submit(t);
		if (ret == -ENODEV)
			goto out;
		STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");
	}



out:
	/* wait for all tasks at the end*/
	starpu_task_wait_for_all();

	starpu_data_unregister(handle1);
	starpu_data_unregister(handle2);
	parallel_task_deinit();

	starpu_shutdown();
	return 0;
}
