/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2015  CNRS
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

#ifndef __STARPU_MPI_TAG_H__
#define __STARPU_MPI_TAG_H__

#include <starpu.h>
#include <stdlib.h>
#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

void _starpu_mpi_tag_init(void);
void _starpu_mpi_tag_free(void);
void _starpu_mpi_data_register_tag(starpu_data_handle_t handle, int tag);
int _starpu_mpi_data_release_tag(starpu_data_handle_t handle);
starpu_data_handle_t _starpu_mpi_data_get_data_handle_from_tag(int tag);

#ifdef __cplusplus
}
#endif

#endif // __STARPU_MPI_TAG_H__
