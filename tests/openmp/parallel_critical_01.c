/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2014  INRIA
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

#include <pthread.h>
#include <starpu.h>
#include "../helper.h"
#include <stdio.h>

#if !defined(STARPU_OPENMP)
int main(int argc, char **argv)
{
	return STARPU_TEST_SKIPPED;
}
#else
__attribute__((constructor))
static void omp_constructor(void)
{
	int ret = starpu_omp_init();
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_omp_init");
}

__attribute__((destructor))
static void omp_destructor(void)
{
	starpu_omp_shutdown();
}

void critical_g(void *arg)
{
	(void) arg;
	int worker_id;
	pthread_t tid;
	tid = pthread_self();
	worker_id = starpu_worker_get_id();
	printf("[tid %p] task thread = %d -- critical\n", (void *)tid, worker_id);
}

void parallel_region_f(void *buffers[], void *args)
{
	(void) buffers;
	(void) args;
	int worker_id;
	pthread_t tid;
	tid = pthread_self();
	worker_id = starpu_worker_get_id();
	printf("[tid %p] task thread = %d -- parallel -->\n", (void *)tid, worker_id);
	starpu_omp_critical(critical_g, NULL, NULL);
	starpu_omp_critical(critical_g, NULL, NULL);
	starpu_omp_critical(critical_g, NULL, NULL);
	starpu_omp_critical(critical_g, NULL, NULL);
	printf("[tid %p] task thread = %d -- parallel <--\n", (void *)tid, worker_id);
}

int
main (int argc, char *argv[])
{
	struct starpu_omp_parallel_region_attr attr;
	pthread_t tid;
	tid = pthread_self();
	printf("<main>\n");
	memset(&attr, 0, sizeof(attr));
	attr.cl.cpu_funcs[0] = parallel_region_f;
	attr.cl.where        = STARPU_CPU;
	attr.if_clause       = 1;
	starpu_omp_parallel_region(&attr);
	printf("<main>\n");
	starpu_omp_parallel_region(&attr);
	printf("<main>\n");
	return 0;
}
#endif
