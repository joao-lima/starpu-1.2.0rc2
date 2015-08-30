/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2012  INRIA
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


#ifndef __STARPU_SCC_H__
#define __STARPU_SCC_H__

#include <starpu_config.h>


#ifdef STARPU_USE_SCC

typedef void *starpu_scc_func_symbol_t;

int starpu_scc_register_kernel(starpu_scc_func_symbol_t *symbol, const char *func_name);

starpu_scc_kernel_t starpu_scc_get_kernel(starpu_scc_func_symbol_t symbol);

#endif /* STARPU_USE_SCC */


#endif /* __STARPU_SCC_H__ */
