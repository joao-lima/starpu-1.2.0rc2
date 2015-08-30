/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2013 Corentin Salingue
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

/* Try to write into disk memory
 * Use mechanism to push datas from main ram to disk ram
 */

#include <fcntl.h>
#include <starpu.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <common/config.h>
#include "../helper.h"

#ifdef STARPU_HAVE_MEMCHECK_H
#include <valgrind/memcheck.h>
#else
#define VALGRIND_MAKE_MEM_DEFINED(addr, size) (void)0
#endif

#ifdef STARPU_HAVE_WINDOWS
#  include <io.h>
#  if defined(_WIN32) && !defined(__CYGWIN__)
#    define mkdir(path, mode) mkdir(path)
#  endif
#endif

#ifdef STARPU_QUICK_CHECK
#  define NDATA 4
#  define NITER 16
#else
#  define NDATA 128
#  define NITER 1024
#endif
#  define MEMSIZE 1
#  define MEMSIZE_STR "1"

#if !defined(STARPU_HAVE_SETENV)
#warning setenv is not defined. Skipping test
int main(int argc, char **argv)
{
	return STARPU_TEST_SKIPPED;
}
#else

const struct starpu_data_copy_methods my_vector_copy_data_methods_s;
struct starpu_data_interface_ops starpu_interface_my_vector_ops;

void starpu_my_vector_data_register(starpu_data_handle_t *handleptr, unsigned home_node,
                        uintptr_t ptr, uint32_t nx, size_t elemsize)
{
	struct starpu_vector_interface vector =
	{
		.id = STARPU_VECTOR_INTERFACE_ID,
		.ptr = ptr,
		.nx = nx,
		.elemsize = elemsize,
                .dev_handle = ptr,
		.slice_base = 0,
                .offset = 0
	};

	starpu_data_register(handleptr, home_node, &vector, &starpu_interface_my_vector_ops);
}

static void zero(void *buffers[], void *args)
{
	struct starpu_vector_interface *vector = (struct starpu_vector_interface *) buffers[0];
	char *val = (char*) STARPU_VECTOR_GET_PTR(vector);
	*val = 0;
	VALGRIND_MAKE_MEM_DEFINED(val, STARPU_VECTOR_GET_NX(vector) * STARPU_VECTOR_GET_ELEMSIZE(vector));
}

static void inc(void *buffers[], void *args)
{
	struct starpu_vector_interface *vector = (struct starpu_vector_interface *) buffers[0];
	char *val = (char*) STARPU_VECTOR_GET_PTR(vector);
	*val++;
}

static struct starpu_codelet zero_cl =
{
	.cpu_funcs = { zero },
	.nbuffers = 1,
	.modes = { STARPU_W },
};

static struct starpu_codelet inc_cl =
{
	.cpu_funcs = { inc },
	.nbuffers = 1,
	.modes = { STARPU_RW },
};

int dotest(struct starpu_disk_ops *ops, char *base, void (*vector_data_register)(starpu_data_handle_t *handleptr, unsigned home_node, uintptr_t ptr, uint32_t nx, size_t elemsize))
{
	int *A, *C;
	starpu_data_handle_t handles[NDATA];

	/* Initialize StarPU without GPU devices to make sure the memory of the GPU devices will not be used */
	struct starpu_conf conf;
	int ret = starpu_conf_init(&conf);
	if (ret == -EINVAL)
		return EXIT_FAILURE;
	conf.ncuda = 0;
	conf.nopencl = 0;
	ret = starpu_init(&conf);
	if (ret == -ENODEV) goto enodev;

	/* Initialize path and name */
	/* register swap disk */
	int new_dd = starpu_disk_register(ops, (void *) base, 1024*1024*16);
	/* can't write on /tmp/ */
	if (new_dd == -ENOENT) goto enoent;

	unsigned int i;

	/* Initialize twice as much data as available memory */
	for (i = 0; i < NDATA; i++)
	{
		vector_data_register(&handles[i], -1, 0, (MEMSIZE*1024*1024*2) / NDATA, sizeof(char));
		starpu_task_insert(&zero_cl, STARPU_W, handles[i], 0);
	}

	for (i = 0; i < NITER; i++)
		starpu_task_insert(&inc_cl, STARPU_RW, handles[rand()%NDATA], 0);

	/* Free data */
	for (i = 0; i < NDATA; i++)
		starpu_data_unregister(handles[i]);

	/* terminate StarPU, no task can be submitted after */
	starpu_shutdown();

	return EXIT_SUCCESS;

enoent:
	FPRINTF(stderr, "Couldn't write data: ENOENT\n");
	starpu_shutdown();
enodev:
	return STARPU_TEST_SKIPPED;
}

static int merge_result(int old, int new)
{
	if (new == EXIT_FAILURE)
		return EXIT_FAILURE;
	if (old == 0)
		return 0;
	return new;
}

int main(void)
{
	int ret = 0;
	char s[128];
	snprintf(s, sizeof(s), "/tmp/%s-disk-%d", getenv("USER"), getpid());
	mkdir(s, 0777);

	setenv("STARPU_LIMIT_CPU_MEM", MEMSIZE_STR, 1);

	/* Build an vector-like interface which doesn't have the any_to_any helper, to force making use of pack/unpack */
	memcpy(&starpu_interface_my_vector_ops, &starpu_interface_vector_ops, sizeof(starpu_interface_my_vector_ops));
	starpu_interface_my_vector_ops.copy_methods = &my_vector_copy_data_methods_s;

	ret = merge_result(ret, dotest(&starpu_disk_stdio_ops, s, starpu_vector_data_register));
	ret = merge_result(ret, dotest(&starpu_disk_stdio_ops, s, starpu_my_vector_data_register));
	ret = merge_result(ret, dotest(&starpu_disk_unistd_ops, s, starpu_vector_data_register));
	ret = merge_result(ret, dotest(&starpu_disk_unistd_ops, s, starpu_my_vector_data_register));
#ifdef STARPU_LINUX_SYS
	ret = merge_result(ret, dotest(&starpu_disk_unistd_o_direct_ops, s, starpu_vector_data_register));
	ret = merge_result(ret, dotest(&starpu_disk_unistd_o_direct_ops, s, starpu_my_vector_data_register));
#endif
	rmdir(s);
	return ret;
}
#endif
