# StarPU --- Runtime system for heterogeneous multicore architectures.
#
# Copyright (C) 2009-2012  Université de Bordeaux
# Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015  CNRS
#
# StarPU is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or (at
# your option) any later version.
#
# StarPU is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# See the GNU Lesser General Public License in COPYING.LGPL for more details.

CC=$(MPICC)
CCLD=$(MPICC)

BUILT_SOURCES =

CLEANFILES = *.gcno *.gcda *.linkinfo

AM_CFLAGS = -Wall $(STARPU_CUDA_CPPFLAGS) $(STARPU_OPENCL_CPPFLAGS) $(FXT_CFLAGS) $(MAGMA_CFLAGS) $(HWLOC_CFLAGS) $(GLOBAL_AM_CFLAGS)
LIBS = $(top_builddir)/src/@LIBSTARPU_LINK@ @LIBS@ $(FXT_LIBS) $(MAGMA_LIBS)
AM_CPPFLAGS = -I$(top_srcdir)/include/ -I$(top_srcdir)/src/ -I$(top_builddir)/src -I$(top_builddir)/include -I$(top_srcdir)/mpi/include -I$(top_srcdir)/mpi/src
AM_LDFLAGS = $(STARPU_OPENCL_LDFLAGS) $(STARPU_CUDA_LDFLAGS) $(STARPU_COI_LDFLAGS) $(STARPU_SCIF_LDFLAGS)

lib_LTLIBRARIES = libstarpumpi-@STARPU_EFFECTIVE_VERSION@.la

libstarpumpi_@STARPU_EFFECTIVE_VERSION@_la_LIBADD = $(top_builddir)/src/libstarpu-@STARPU_EFFECTIVE_VERSION@.la
libstarpumpi_@STARPU_EFFECTIVE_VERSION@_la_LDFLAGS = $(ldflags) -no-undefined					\
  -version-info $(LIBSTARPUMPI_INTERFACE_CURRENT):$(LIBSTARPUMPI_INTERFACE_REVISION):$(LIBSTARPUMPI_INTERFACE_AGE) \
  $(MPICC_LDFLAGS) $(FXT_LDFLAGS)
noinst_HEADERS =					\
	starpu_mpi_private.h				\
	starpu_mpi_fxt.h				\
	starpu_mpi_stats.h				\
	starpu_mpi_datatype.h				\
	starpu_mpi_cache.h				\
	starpu_mpi_select_node.h			\
	starpu_mpi_cache_stats.h			\
	starpu_mpi_early_data.h				\
	starpu_mpi_early_request.h			\
	starpu_mpi_sync_data.h				\
	starpu_mpi_comm.h				\
	starpu_mpi_tag.h

libstarpumpi_@STARPU_EFFECTIVE_VERSION@_la_SOURCES =	\
	starpu_mpi.c					\
	starpu_mpi_helper.c				\
	starpu_mpi_datatype.c				\
	starpu_mpi_task_insert.c			\
	starpu_mpi_collective.c				\
	starpu_mpi_stats.c				\
	starpu_mpi_private.c				\
	starpu_mpi_cache.c				\
	starpu_mpi_select_node.c			\
	starpu_mpi_cache_stats.c			\
	starpu_mpi_early_data.c				\
	starpu_mpi_early_request.c			\
	starpu_mpi_sync_data.c				\
	starpu_mpi_comm.c				\
	starpu_mpi_tag.c

showcheck:
	-cat /dev/null
