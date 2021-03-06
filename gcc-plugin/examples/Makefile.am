# StarPU --- Runtime system for heterogeneous multicore architectures.
#
# Copyright (C) 2011, 2012 INRIA
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

TESTS	=

examplebindir = $(libdir)/starpu/plugin

if STARPU_USE_CPU

if STARPU_HAVE_WINDOWS
check_PROGRAMS	=	$(TESTS)
else
check_PROGRAMS	=	$(LOADER) $(TESTS)
endif

if !STARPU_HAVE_WINDOWS
## test loader program
LOADER			=	loader
loader_CPPFLAGS		=	$(AM_CFLAGS) $(AM_CPPFLAGS) -I$(top_builddir)/src/
LOADER_BIN		=	$(abs_top_builddir)/gcc-plugin/examples/$(LOADER)
loader_SOURCES		=	../../tests/loader.c

if STARPU_HAVE_AM111
TESTS_ENVIRONMENT	=	top_builddir="$(abs_top_builddir)" top_srcdir="$(abs_top_srcdir)"
LOG_COMPILER		=	$(LOADER_BIN)
else
TESTS_ENVIRONMENT	=	top_builddir="$(abs_top_builddir)" top_srcdir="$(abs_top_srcdir)" $(LOADER_BIN)
endif

endif

CLEANFILES = starpu_idle_microsec.log
examplebin_PROGRAMS =			\
	hello-starpu 			\
	matrix-mult			\
	stencil5			\
	vector_scal/vector_scal

#if !NO_BLAS_LIB
#examplebin_PROGRAMS +=				\
#  cholesky/cholesky
#endif !NO_BLAS_LIB
endif STARPU_USE_CPU

AM_LDFLAGS = $(top_builddir)/src/@LIBSTARPU_LINK@
AM_LDFLAGS += $(STARPU_OPENCL_LDFLAGS) $(STARPU_CUDA_LDFLAGS)

AM_CPPFLAGS =						\
  -I$(top_srcdir)/include				\
  -I$(top_srcdir)/examples				\
  $(STARPU_OPENCL_CPPFLAGS) $(STARPU_CUDA_CPPFLAGS)

AM_CFLAGS =							\
  -fplugin="$(builddir)/../src/.libs/starpu.so"			\
  -fplugin-arg-starpu-include-dir="$(top_srcdir)/include"	\
  -fplugin-arg-starpu-verbose					\
  -Wall $(HWLOC_CFLAGS)

#noinst_HEADERS =				\
#  cholesky/cholesky.h				\
#  cholesky/cholesky_kernels.h

#if !NO_BLAS_LIB
#cholesky_cholesky_SOURCES	=		\
#	cholesky/cholesky.c		\
#	cholesky/cholesky_models.c	\
#	cholesky/cholesky_kernels.c	\
#	$(top_srcdir)/examples/common/blas.c
#
#cholesky_cholesky_LDADD	=	\
#	$(STARPU_BLAS_LDFLAGS)
#endif

vector_scal_vector_scal_SOURCES = vector_scal/vector_scal.c

if STARPU_USE_CUDA

vector_scal_vector_scal_SOURCES += vector_scal/vector_scal_cuda.cu

.cu.o:
	$(NVCC) $< -c -o $@ $(NVCCFLAGS)			\
	  -I$(top_builddir)/include -I$(top_srcdir)/include

else !STARPU_USE_CUDA

EXTRA_DIST = vector_scal/vector_scal_cuda.cu

endif

TESTS += $(examplebin_PROGRAMS)


showcheck:
	-cat $(TEST_LOGS) /dev/null
	! grep -q " runtime error: " $(TEST_LOGS) /dev/null
	RET=0 ; \
	for i in $(SUBDIRS) ; do \
		make -C $$i showcheck || RET=1 ; \
	done ; \
	exit $$RET
