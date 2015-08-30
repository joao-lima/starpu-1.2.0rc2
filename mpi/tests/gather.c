/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2013, 2015  CNRS
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

#include <starpu_mpi.h>
#include "helper.h"

int main(int argc, char **argv)
{
	int ret, rank, size;
	starpu_data_handle_t handle;
	int var;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	if (size<3)
	{
		FPRINTF(stderr, "We need more than 2 processes.\n");
		MPI_Finalize();
		return STARPU_TEST_SKIPPED;
	}

	ret = starpu_init(NULL);
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");
	ret = starpu_mpi_init(NULL, NULL, 0);
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_mpi_init");

	if (rank == 0)
	{
		int n;
		for(n=1 ; n<size ; n++)
		{
			MPI_Status status;

			FPRINTF_MPI(stderr, "receiving from node %d\n", n);
			starpu_variable_data_register(&handle, STARPU_MAIN_RAM, (uintptr_t)&var, sizeof(var));
			starpu_mpi_recv(handle, n, 42, MPI_COMM_WORLD, &status);
			starpu_data_acquire(handle, STARPU_R);
			STARPU_ASSERT_MSG(var == n, "Received incorrect value <%d> from node <%d>\n", var, n);
			FPRINTF_MPI(stderr, "received <%d> from node %d\n", var, n);
			starpu_data_release(handle);
			starpu_data_unregister(handle);
		}
	}
	else
	{
		FPRINTF_MPI(stderr, "sending to node %d\n", 0);
		var = rank;
		starpu_variable_data_register(&handle, STARPU_MAIN_RAM, (uintptr_t)&var, sizeof(var));
		starpu_mpi_send(handle, 0, 42, MPI_COMM_WORLD);
		starpu_data_unregister(handle);
	}

	starpu_mpi_shutdown();
	starpu_shutdown();
	MPI_Finalize();

	return 0;
}
