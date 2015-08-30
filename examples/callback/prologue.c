/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009, 2010, 2013-2014  Université de Bordeaux
 * Copyright (C) 2010, 2011, 2012, 2013, 2015  CNRS
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

#define FPRINTF(ofile, fmt, ...) do { if (!getenv("STARPU_SSILENT")) {fprintf(ofile, fmt, ## __VA_ARGS__); }} while(0)

starpu_data_handle_t handle;

void cpu_codelet(void *descr[], STARPU_ATTRIBUTE_UNUSED void *_args)
{
	int *val = (int *)STARPU_VARIABLE_GET_PTR(descr[0]);

	*val += 1;
	printf("task executing \n");
}

struct starpu_codelet cl =
{
	.modes = { STARPU_RW },
	.cpu_funcs = {cpu_codelet},
	.nbuffers = 1,
	.name = "callback"
};

void callback_func(void *callback_arg)
{
	int ret;

	struct starpu_task *task = starpu_task_create();
	task->cl = &cl;
	task->handles[0] = handle;

	ret = starpu_task_submit(task);
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");
}

void prologue_callback_func(void *callback_arg)
{
	double *x = (double*)callback_arg;
	printf("x = %lf\n", *x);
}

void pop_prologue_callback_func(void *args)
{
	unsigned val = (uintptr_t) args;
	printf("pop_prologue_callback val %d \n", val);
}


int main(int argc, char **argv)
{
	int v=40;
	int ret;

	ret = starpu_init(NULL);
	if (ret == -ENODEV)
		return 77;
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");

	starpu_variable_data_register(&handle, STARPU_MAIN_RAM, (uintptr_t)&v, sizeof(int));
	double *x = (double*)malloc(sizeof(double));

	struct starpu_task *task = starpu_task_create();
	task->cl = &cl;
	task->prologue_callback_func = callback_func;
	task->prologue_callback_arg = NULL;

	task->prologue_callback_pop_func = pop_prologue_callback_func;
	task->prologue_callback_pop_arg = (void*) 5;

	task->handles[0] = handle;

	ret = starpu_task_submit(task);
	if (ret == -ENODEV) goto enodev;
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

	*x = -999.0;
	ret = starpu_task_insert(&cl,
				 STARPU_RW, handle,
				 STARPU_PROLOGUE_CALLBACK, prologue_callback_func,
				 STARPU_PROLOGUE_CALLBACK_ARG, x,
				 STARPU_PROLOGUE_CALLBACK_POP, pop_prologue_callback_func,
				 STARPU_PROLOGUE_CALLBACK_POP_ARG, 5,
				 0);
	if (ret == -ENODEV) goto enodev;
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_insert");

	starpu_task_wait_for_all();

enodev:
	starpu_data_unregister(handle);
	free(x);
	FPRINTF(stderr, "v -> %d\n", v);
	starpu_shutdown();
	return (ret == -ENODEV) ? 77 : 0;
}
