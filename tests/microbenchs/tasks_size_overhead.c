/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010-2014  Université de Bordeaux
 * Copyright (C) 2010, 2011, 2012, 2013  CNRS
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

/* This benchmark creates a thousand tasks of the same (small) duration, with
 * various number of cpus and various durations.
 *
 * Use ./tasks_size_overhead.sh to generate a plot of the result.
 *
 * Thanks Martin Tillenius for the idea.
 */

#include <stdio.h>
#include <unistd.h>

#include <starpu.h>
#include "../helper.h"

#define START 4
#define STOP 4096
#ifdef STARPU_QUICK_CHECK
#define FACTOR 8
#else
#define FACTOR 2
#endif

starpu_data_handle_t data_handles[8];
float *buffers[8];

#ifdef STARPU_QUICK_CHECK
static unsigned ntasks = 10;
#else
static unsigned ntasks = 1000;
#endif
static unsigned nbuffers = 0;

struct starpu_task *tasks;

void func(void *descr[] STARPU_ATTRIBUTE_UNUSED, void *arg)
{
	double tv1, tv2;
	unsigned n = (uintptr_t)arg;
	long usec = 0;
	tv1 = starpu_timing_now();
	do
	{
		tv2 = starpu_timing_now();
		usec = tv2 - tv1;
	}
	while (usec < n);
}

static struct starpu_codelet codelet =
{
	.cpu_funcs = {func},
	.nbuffers = 0,
	.modes = {STARPU_R, STARPU_R, STARPU_R, STARPU_R, STARPU_R, STARPU_R, STARPU_R, STARPU_R}
};

static void parse_args(int argc, char **argv)
{
	int c;
	while ((c = getopt(argc, argv, "i:b:h")) != -1)
	switch(c)
	{
		case 'i':
			ntasks = atoi(optarg);
			break;
		case 'b':
			nbuffers = atoi(optarg);
			codelet.nbuffers = nbuffers;
			break;
		case 'h':
			fprintf(stderr, "Usage: %s [-i ntasks] [-b nbuffers] [-h]\n", argv[0]);
			break;
	}
}

int main(int argc, char **argv)
{
	int ret;
	unsigned i;
	unsigned size;
	unsigned totcpus, ncpus;

	double timing;
	double start;
	double end;

	struct starpu_conf conf;

	unsigned buffer;

	parse_args(argc, argv);

	/* Get number of CPUs */
	starpu_conf_init(&conf);
	conf.ncuda = 0;
	conf.nopencl = 0;
	ret = starpu_init(&conf);
	if (ret == -ENODEV) return STARPU_TEST_SKIPPED;
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");

	totcpus = starpu_worker_get_count_by_type(STARPU_CPU_WORKER);

	starpu_shutdown();

	/* Allocate data */
	for (buffer = 0; buffer < nbuffers; buffer++)
		buffers[buffer] = (float *) malloc(16*sizeof(float));

	tasks = (struct starpu_task *) calloc(1, ntasks*sizeof(struct starpu_task));

	/* Emit headers and compute raw tasks speed */
	FPRINTF(stdout, "# tasks : %u buffers : %u\n", ntasks, nbuffers);
	FPRINTF(stdout, "# ncpus\t");
	for (size = START; size <= STOP; size *= FACTOR)
		FPRINTF(stdout, "%u iters(us)\ttotal(s)\t", size);
	FPRINTF(stdout, "\n");
	FPRINTF(stdout, "\"seq\"\t");
	for (size = START; size <= STOP; size *= FACTOR)
	{
		double dstart, dend;
		dstart = starpu_timing_now();
		for (i = 0; i < ntasks; i++)
			func(NULL, (void*) (uintptr_t) size);
		dend = starpu_timing_now();
		FPRINTF(stdout, "%.0f       \t%f\t", (dend-dstart)/ntasks, (dend-dstart)/1000000);
	}
	FPRINTF(stdout, "\n");
	fflush(stdout);

	/* For each number of cpus, benchmark */
	for (ncpus= 1; ncpus <= totcpus; ncpus++)
	{
		FPRINTF(stdout, "%u\t", ncpus);
		fflush(stdout);

		conf.ncpus = ncpus;
		ret = starpu_init(&conf);
		if (ret == -ENODEV) return STARPU_TEST_SKIPPED;
		STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");

		for (buffer = 0; buffer < nbuffers; buffer++)
			starpu_vector_data_register(&data_handles[buffer], STARPU_MAIN_RAM, (uintptr_t)buffers[buffer], 16, sizeof(float));

		for (size = START; size <= STOP; size *= FACTOR)
		{
			/* submit tasks */
			start = starpu_timing_now();
			for (i = 0; i < ntasks; i++)
			{
				starpu_task_init(&tasks[i]);
				tasks[i].callback_func = NULL;
				tasks[i].cl = &codelet;
				tasks[i].cl_arg = (void*) (uintptr_t) size;
				tasks[i].synchronous = 0;

				/* we have 8 buffers at most */
				for (buffer = 0; buffer < nbuffers; buffer++)
				{
					tasks[i].handles[buffer] = data_handles[buffer];
				}

				ret = starpu_task_submit(&tasks[i]);
				if (ret == -ENODEV) goto enodev;
				STARPU_CHECK_RETURN_VALUE(ret, "starpu_task");
			}
			ret = starpu_task_wait_for_all();
			STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_wait_for_all");
			end = starpu_timing_now();

			for (i = 0; i < ntasks; i++)
				starpu_task_clean(&tasks[i]);

			timing = end - start;

			FPRINTF(stdout, "%u\t%f\t", size, timing/1000000);
			fflush(stdout);

			{
				char *output_dir = getenv("STARPU_BENCH_DIR");
				char *bench_id = getenv("STARPU_BENCH_ID");

				if (output_dir && bench_id)
				{
					char file[1024];
					FILE *f;

					sprintf(file, "%s/tasks_size_overhead_total.dat", output_dir);
					f = fopen(file, "a");
					fprintf(f, "%s\t%f\n", bench_id, timing/1000000);
					fclose(f);
				}
			}
		}

		for (buffer = 0; buffer < nbuffers; buffer++)
		{
			starpu_data_unregister(data_handles[buffer]);
		}

		starpu_shutdown();

		FPRINTF(stdout, "\n");
		fflush(stdout);
	}

	free(tasks);
	return EXIT_SUCCESS;

enodev:
	fprintf(stderr, "WARNING: No one can execute this task\n");
	/* yes, we do not perform the computation but we did detect that no one
 	 * could perform the kernel, so this is not an error from StarPU */
	starpu_shutdown();
	free(tasks);
	return STARPU_TEST_SKIPPED;
}
