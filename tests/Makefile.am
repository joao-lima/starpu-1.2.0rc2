# StarPU --- Runtime system for heterogeneous multicore architectures.
#
# Copyright (C) 2009-2015  Université de Bordeaux
# Copyright (C) 2010, 2011, 2012, 2013, 2014  CNRS
# Copyright (C) 2010, 2011, 2012  INRIA
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

AM_CFLAGS = $(HWLOC_CFLAGS) $(FXT_CFLAGS) -Wall $(STARPU_CUDA_CPPFLAGS) $(STARPU_OPENCL_CPPFLAGS) $(STARPU_COI_CPPFLAGS) $(STARPU_SCIF_CPPFLAGS) $(GLOBAL_AM_CFLAGS) -Wno-unused
LIBS = $(top_builddir)/src/@LIBSTARPU_LINK@ $(HWLOC_LIBS) $(FXT_LIBS) @LIBS@
AM_CPPFLAGS = -I$(top_srcdir)/include/ -I$(top_builddir)/src -I$(top_srcdir)/src/
AM_LDFLAGS = @STARPU_EXPORT_DYNAMIC@ $(STARPU_OPENCL_LDFLAGS) $(STARPU_CUDA_LDFLAGS) $(STARPU_COI_LDFLAGS) $(STARPU_SCIF_LDFLAGS) $(FXT_LDFLAGS)

EXTRA_DIST =					\
	helper.h				\
	datawizard/scal.h			\
	datawizard/mpi_like.h			\
	microbenchs/tasks_size_overhead.sh	\
	microbenchs/tasks_size_overhead.gp	\
	datawizard/scratch_opencl_kernel.cl     \
	datawizard/sync_and_notify_data_opencl_codelet.cl\
	datawizard/opencl_codelet_unsigned_inc_kernel.cl \
	coverage/coverage.sh			\
	datawizard/acquire_release_opencl_kernel.cl \
	datawizard/interfaces/test_interfaces.h	\
	datawizard/interfaces/bcsr/bcsr_opencl_kernel.cl \
	datawizard/interfaces/coo/coo_opencl_kernel.cl \
	datawizard/interfaces/matrix/matrix_opencl_kernel.cl \
	datawizard/interfaces/variable/variable_opencl_kernel.cl \
	datawizard/interfaces/vector/test_vector_opencl_kernel.cl \
	datawizard/interfaces/multiformat/multiformat_types.h \
	datawizard/interfaces/multiformat/multiformat_opencl_kernel.cl \
	datawizard/interfaces/multiformat/multiformat_conversion_codelets_kernel.cl \
	datawizard/interfaces/multiformat/advanced/generic.h \
	datawizard/interfaces/csr/csr_opencl_kernel.cl \
	datawizard/interfaces/block/block_opencl_kernel.cl \
	perfmodels/opencl_memset_kernel.cl

CLEANFILES = 					\
	*.gcno *.gcda *.linkinfo core starpu_idle_microsec.log

BUILT_SOURCES =
SUBDIRS =

examplebindir = $(libdir)/starpu/examples

if STARPU_USE_OPENCL
nobase_STARPU_OPENCL_DATA_DATA =
endif

if STARPU_USE_CUDA

# TODO define NVCCFLAGS
NVCC ?= nvcc

NVCCFLAGS += -I$(top_srcdir)/include/ -I$(top_srcdir)/src -I$(top_builddir)/src -I$(top_builddir)/src/common -I$(top_builddir)/include $(HWLOC_CFLAGS) $(SIMGRID_CFLAGS)

.cu.cubin:
	$(MKDIR_P) `dirname $@`
	$(NVCC) -cubin $< -o $@ --compiler-options -fno-strict-aliasing  $(NVCCFLAGS)

.cu.o:
	$(MKDIR_P) `dirname $@`
	$(NVCC) $< -c -o $@ --compiler-options -fno-strict-aliasing  $(NVCCFLAGS) -I${includedir}

endif

testbindir = $(libdir)/starpu/tests

#####################################
# What to install and what to check #
#####################################

if !STARPU_HAVE_WINDOWS
## test loader program
if !STARPU_CROSS_COMPILING
LOADER			=	loader
LOADER_BIN		=	$(abs_top_builddir)/tests/$(LOADER)
else
LOADER			=	
LOADER_BIN		=	$(top_builddir)/tests/loader-cross.sh
endif

if STARPU_HAVE_AM111
TESTS_ENVIRONMENT	=	top_builddir="$(abs_top_builddir)" top_srcdir="$(abs_top_srcdir)"
LOG_COMPILER		=	$(LOADER_BIN)
else
TESTS_ENVIRONMENT	=	top_builddir="$(abs_top_builddir)" top_srcdir="$(abs_top_srcdir)" $(LOADER_BIN)
endif
endif

TESTS = $(noinst_PROGRAMS)

if STARPU_COVERAGE_ENABLED
TESTS	+=	coverage/coverage.sh
endif

XFAIL_TESTS=	errorcheck/invalid_blocking_calls

noinst_PROGRAMS =				\
	main/deprecated_func			\
	main/driver_api/init_run_deinit         \
	main/driver_api/run_driver              \
	main/deploop                            \
	main/restart				\
	main/execute_on_a_specific_worker	\
	main/execute_schedule			\
	main/insert_task			\
	main/insert_task_dyn_handles		\
	main/insert_task_nullcodelet		\
	main/insert_task_array			\
	main/multithreaded			\
	main/multithreaded_init			\
	main/starpu_task_bundle			\
	main/starpu_task_wait_for_all		\
	main/starpu_task_wait			\
	main/static_restartable			\
	main/static_restartable_using_initializer\
	main/static_restartable_tag		\
	main/regenerate				\
	main/regenerate_pipeline		\
	main/wait_all_regenerable_tasks		\
	main/subgraph_repeat			\
	main/subgraph_repeat_tag		\
	main/subgraph_repeat_regenerate		\
	main/subgraph_repeat_regenerate_tag	\
	main/empty_task				\
	main/empty_task_sync_point		\
	main/empty_task_sync_point_tasks	\
	main/empty_task_chain			\
	main/tag_wait_api			\
	main/task_wait_api			\
	main/declare_deps_in_callback		\
	main/declare_deps_after_submission	\
	main/declare_deps_after_submission_synchronous	\
	main/get_current_task			\
	main/starpu_init			\
	main/starpu_worker_exists		\
	main/submit				\
	main/pause_resume			\
	main/codelet_null_callback		\
	main/pack				\
	datawizard/allocate			\
	datawizard/acquire_cb			\
	datawizard/acquire_cb_insert		\
	datawizard/acquire_release		\
	datawizard/acquire_release2		\
	datawizard/cache			\
	datawizard/commute			\
	datawizard/commute2			\
	datawizard/copy				\
	datawizard/data_implicit_deps		\
	datawizard/data_lookup			\
	datawizard/scratch			\
	datawizard/sync_and_notify_data		\
	datawizard/sync_and_notify_data_implicit\
	datawizard/dsm_stress			\
	datawizard/double_parameter		\
	datawizard/write_only_tmp_buffer	\
	datawizard/data_invalidation		\
	datawizard/dining_philosophers		\
	datawizard/manual_reduction		\
	datawizard/readers_and_writers		\
	datawizard/unpartition			\
	datawizard/user_interaction_implicit	\
	datawizard/sync_with_data_with_mem	\
	datawizard/sync_with_data_with_mem_non_blocking\
	datawizard/sync_with_data_with_mem_non_blocking_implicit\
	datawizard/mpi_like			\
	datawizard/mpi_like_async		\
	datawizard/critical_section_with_void_interface\
	datawizard/increment_init		\
	datawizard/increment_redux		\
	datawizard/increment_redux_v2		\
	datawizard/increment_redux_lazy		\
	datawizard/handle_to_pointer		\
	datawizard/lazy_allocation		\
	datawizard/lazy_unregister		\
	datawizard/no_unregister		\
	datawizard/noreclaim			\
	datawizard/interfaces/copy_interfaces	\
	datawizard/interfaces/block/block_interface \
	datawizard/interfaces/bcsr/bcsr_interface \
	datawizard/interfaces/coo/coo_interface \
	datawizard/interfaces/csr/csr_interface \
	datawizard/interfaces/matrix/matrix_interface \
	datawizard/interfaces/multiformat/multiformat_interface \
	datawizard/interfaces/multiformat/advanced/multiformat_cuda_opencl \
	datawizard/interfaces/multiformat/advanced/multiformat_data_release \
	datawizard/interfaces/multiformat/advanced/multiformat_worker \
	datawizard/interfaces/multiformat/advanced/multiformat_handle_conversion \
	datawizard/interfaces/multiformat/advanced/same_handle \
	datawizard/interfaces/variable/variable_interface    \
	datawizard/interfaces/vector/test_vector_interface   \
	datawizard/interfaces/void/void_interface \
	datawizard/in_place_partition   	\
	datawizard/partition_lazy		\
	datawizard/gpu_register   		\
	datawizard/gpu_ptr_register   		\
	datawizard/variable_parameters		\
	datawizard/wt_host			\
	datawizard/wt_broadcast			\
	datawizard/readonly			\
	datawizard/specific_node		\
	datawizard/task_with_multiple_time_the_same_handle	\
	datawizard/test_arbiter			\
	disk/disk_copy				\
	disk/disk_compute			\
	disk/disk_pack				\
	disk/mem_reclaim			\
	errorcheck/starpu_init_noworker		\
	errorcheck/invalid_blocking_calls	\
	errorcheck/invalid_tasks		\
	helper/cublas_init			\
	helper/starpu_data_cpy			\
	helper/pinned_memory			\
	helper/execute_on_all			\
	helper/starpu_create_sync_task		\
	microbenchs/async_tasks_overhead	\
	microbenchs/sync_tasks_overhead		\
	microbenchs/tasks_overhead		\
	microbenchs/tasks_size_overhead		\
	microbenchs/prefetch_data_on_node 	\
	microbenchs/redundant_buffer		\
	microbenchs/local_pingpong		\
	microbenchs/matrix_as_vector		\
	openmp/init_exit_01			\
	openmp/init_exit_02			\
	openmp/environment			\
	openmp/api_01				\
	openmp/parallel_01			\
	openmp/parallel_02			\
	openmp/parallel_03			\
	openmp/parallel_barrier_01		\
	openmp/parallel_master_01		\
	openmp/parallel_master_inline_01	\
	openmp/parallel_single_wait_01		\
	openmp/parallel_single_nowait_01	\
	openmp/parallel_single_inline_01	\
	openmp/parallel_single_copyprivate_01	\
	openmp/parallel_single_copyprivate_inline_01	\
	openmp/parallel_critical_01		\
	openmp/parallel_critical_inline_01	\
	openmp/parallel_critical_named_01	\
	openmp/parallel_critical_named_inline_01\
	openmp/parallel_simple_lock_01		\
	openmp/parallel_nested_lock_01		\
	openmp/parallel_for_01			\
	openmp/parallel_for_02			\
	openmp/parallel_for_ordered_01		\
	openmp/parallel_sections_01		\
	openmp/parallel_sections_combined_01	\
	openmp/task_01				\
	openmp/task_02				\
	openmp/taskwait_01			\
	openmp/taskgroup_01			\
	openmp/taskgroup_02			\
	openmp/array_slice_01			\
	openmp/cuda_task_01			\
	overlap/overlap				\
	overlap/gpu_concurrency			\
	parallel_tasks/explicit_combined_worker	\
	parallel_tasks/parallel_kernels		\
	parallel_tasks/parallel_kernels_spmd	\
	parallel_tasks/spmd_peager		\
	parallel_tasks/cuda_only		\
	perfmodels/regression_based		\
	perfmodels/non_linear_regression_based	\
	perfmodels/feed				\
	perfmodels/user_base			\
	perfmodels/valid_model			\
	perfmodels/value_nan			\
	perfmodels/memory			\
	sched_policies/data_locality            \
	sched_policies/execute_all_tasks        \
	sched_policies/prio        		\
	sched_policies/simple_deps              \
	sched_policies/simple_cpu_gpu_sched

if STARPU_LONG_CHECK
noinst_PROGRAMS +=				\
	main/tag_task_data_deps			\
	datawizard/reclaim
endif

examplebin_PROGRAMS = \
	microbenchs/tasks_size_overhead		\
	microbenchs/local_pingpong
examplebin_SCRIPTS = \
	microbenchs/tasks_size_overhead.gp \
	microbenchs/tasks_size_overhead.sh

if STARPU_HAVE_WINDOWS
check_PROGRAMS	=	$(noinst_PROGRAMS)
else
check_PROGRAMS	=	$(LOADER) $(noinst_PROGRAMS)
endif

#######################
# Source files        #
#######################

datawizard_acquire_release_SOURCES =		\
	datawizard/acquire_release.c
if STARPU_USE_CUDA
datawizard_acquire_release_SOURCES +=		\
	datawizard/acquire_release_cuda.cu
endif
if STARPU_USE_OPENCL
datawizard_acquire_release_SOURCES +=		\
	datawizard/acquire_release_opencl.c
nobase_STARPU_OPENCL_DATA_DATA += \
	datawizard/acquire_release_opencl_kernel.cl
endif

datawizard_acquire_release2_SOURCES =		\
	datawizard/acquire_release2.c
if STARPU_USE_CUDA
datawizard_acquire_release2_SOURCES +=		\
	datawizard/acquire_release_cuda.cu
endif
if STARPU_USE_OPENCL
datawizard_acquire_release2_SOURCES +=		\
	datawizard/acquire_release_opencl.c
endif

datawizard_scratch_SOURCES =			\
	datawizard/scratch.c
if STARPU_USE_CUDA
datawizard_scratch_SOURCES +=		\
	datawizard/scratch_cuda.cu
endif
if STARPU_USE_OPENCL
datawizard_scratch_SOURCES += \
	datawizard/scratch_opencl.c
nobase_STARPU_OPENCL_DATA_DATA += \
	datawizard/scratch_opencl_kernel.cl
endif

datawizard_mpi_like_SOURCES =		\
	datawizard/mpi_like.c
if STARPU_USE_CUDA
datawizard_mpi_like_SOURCES +=			\
	datawizard/cuda_codelet_unsigned_inc.cu
endif
if STARPU_USE_OPENCL
datawizard_mpi_like_SOURCES +=			\
	datawizard/opencl_codelet_unsigned_inc.c
nobase_STARPU_OPENCL_DATA_DATA+= \
	datawizard/opencl_codelet_unsigned_inc_kernel.cl
endif

datawizard_mpi_like_async_SOURCES =		\
	datawizard/mpi_like_async.c
if STARPU_USE_CUDA
datawizard_mpi_like_async_SOURCES +=		\
	datawizard/cuda_codelet_unsigned_inc.cu
endif
if STARPU_USE_OPENCL
datawizard_mpi_like_async_SOURCES +=			\
	datawizard/opencl_codelet_unsigned_inc.c
endif

datawizard_sync_and_notify_data_SOURCES =	\
	datawizard/sync_and_notify_data.c
if STARPU_USE_CUDA
datawizard_sync_and_notify_data_SOURCES +=	\
	datawizard/sync_and_notify_data_kernels.cu
endif
if STARPU_USE_OPENCL
datawizard_sync_and_notify_data_SOURCES +=	\
	datawizard/sync_and_notify_data_opencl.c
nobase_STARPU_OPENCL_DATA_DATA += \
	datawizard/sync_and_notify_data_opencl_codelet.cl
endif

datawizard_sync_and_notify_data_implicit_SOURCES =	\
	datawizard/sync_and_notify_data_implicit.c
if STARPU_USE_CUDA
datawizard_sync_and_notify_data_implicit_SOURCES +=	\
	datawizard/sync_and_notify_data_kernels.cu
endif
if STARPU_USE_OPENCL
datawizard_sync_and_notify_data_implicit_SOURCES +=	\
	datawizard/sync_and_notify_data_opencl.c
endif

datawizard_in_place_partition_SOURCES =	\
	datawizard/in_place_partition.c	\
	datawizard/scal.c
if STARPU_USE_CUDA
datawizard_in_place_partition_SOURCES +=	\
	datawizard/scal_cuda.cu
endif
if STARPU_USE_OPENCL
datawizard_in_place_partition_SOURCES +=	\
	datawizard/scal_opencl.cl
endif

datawizard_partition_lazy_SOURCES =	\
	datawizard/partition_lazy.c	\
	datawizard/scal.c
if STARPU_USE_CUDA
datawizard_partition_lazy_SOURCES +=	\
	datawizard/scal_cuda.cu
endif
if STARPU_USE_OPENCL
datawizard_partition_lazy_SOURCES +=	\
	datawizard/scal_opencl.cl
endif

datawizard_gpu_register_SOURCES =	\
	datawizard/gpu_register.c	\
	datawizard/scal.c
if STARPU_USE_CUDA
datawizard_gpu_register_SOURCES +=	\
	datawizard/scal_cuda.cu
endif
if STARPU_USE_OPENCL
datawizard_gpu_register_SOURCES +=	\
	datawizard/scal_opencl.cl
endif

datawizard_gpu_ptr_register_SOURCES =	\
	datawizard/gpu_ptr_register.c	\
	datawizard/scal.c
if STARPU_USE_CUDA
datawizard_gpu_ptr_register_SOURCES +=	\
	datawizard/scal_cuda.cu
endif
if STARPU_USE_OPENCL
datawizard_gpu_ptr_register_SOURCES +=	\
	datawizard/scal_opencl.cl
endif

datawizard_wt_host_SOURCES =			\
	datawizard/wt_host.c
datawizard_wt_broadcast_SOURCES =		\
	datawizard/wt_broadcast.c

datawizard_specific_node_SOURCES =		\
	datawizard/specific_node.c
if STARPU_USE_CUDA
datawizard_specific_node_SOURCES +=		\
	datawizard/cuda_codelet_unsigned_inc.cu
endif
if STARPU_USE_OPENCL
datawizard_specific_node_SOURCES +=			\
	datawizard/opencl_codelet_unsigned_inc.c
endif

datawizard_test_arbiter_SOURCES =	\
	datawizard/test_arbiter.cpp

main_deprecated_func_CFLAGS = $(AM_CFLAGS) -Wno-deprecated-declarations

main_subgraph_repeat_SOURCES =		\
	main/subgraph_repeat.c
if STARPU_USE_CUDA
main_subgraph_repeat_SOURCES +=		\
	main/increment.cu
endif

main_subgraph_repeat_tag_SOURCES =		\
	main/subgraph_repeat_tag.c
if STARPU_USE_CUDA
main_subgraph_repeat_tag_SOURCES +=		\
	main/increment.cu
endif

main_subgraph_repeat_regenerate_SOURCES =		\
	main/subgraph_repeat_regenerate.c
if STARPU_USE_CUDA
main_subgraph_repeat_regenerate_SOURCES +=		\
	main/increment.cu
endif

main_subgraph_repeat_regenerate_tag_SOURCES =		\
	main/subgraph_repeat_regenerate_tag.c
if STARPU_USE_CUDA
main_subgraph_repeat_regenerate_tag_SOURCES +=		\
	main/increment.cu
endif

openmp_init_exit_01_SOURCES = 	\
	openmp/init_exit_01.c

openmp_init_exit_02_SOURCES = 	\
	openmp/init_exit_02.c

openmp_environment_SOURCES = 	\
	openmp/environment.c

openmp_api_01_SOURCES = 	\
	openmp/api_01.c

openmp_parallel_01_SOURCES = 	\
	openmp/parallel_01.c

openmp_parallel_02_SOURCES = 	\
	openmp/parallel_02.c

openmp_parallel_03_SOURCES = 	\
	openmp/parallel_03.c

openmp_parallel_barrier_01_SOURCES = 	\
	openmp/parallel_barrier_01.c

openmp_parallel_master_01_SOURCES = 	\
	openmp/parallel_master_01.c

openmp_parallel_master_inline_01_SOURCES = 	\
	openmp/parallel_master_inline_01.c

openmp_parallel_single_wait_01_SOURCES = 	\
	openmp/parallel_single_wait_01.c

openmp_parallel_single_nowait_01_SOURCES = 	\
	openmp/parallel_single_nowait_01.c

openmp_parallel_single_inline_01_SOURCES = 	\
	openmp/parallel_single_inline_01.c

openmp_parallel_single_copyprivate_01_SOURCES = 	\
	openmp/parallel_single_copyprivate_01.c

openmp_parallel_single_copyprivate_inline_01_SOURCES = 	\
	openmp/parallel_single_copyprivate_inline_01.c

openmp_parallel_critical_01_SOURCES = 	\
	openmp/parallel_critical_01.c

openmp_parallel_critical_inline_01_SOURCES = 	\
	openmp/parallel_critical_inline_01.c

openmp_parallel_critical_named_01_SOURCES = 	\
	openmp/parallel_critical_named_01.c

openmp_parallel_critical_named_inline_01_SOURCES = 	\
	openmp/parallel_critical_named_inline_01.c

openmp_parallel_simple_lock_01_SOURCES = 	\
	openmp/parallel_simple_lock_01.c

openmp_parallel_nested_lock_01_SOURCES = 	\
	openmp/parallel_nested_lock_01.c

openmp_parallel_for_01_SOURCES = 	\
	openmp/parallel_for_01.c

openmp_parallel_for_02_SOURCES = 	\
	openmp/parallel_for_02.c

openmp_parallel_for_ordered_01_SOURCES = 	\
	openmp/parallel_for_ordered_01.c

openmp_parallel_sections_01_SOURCES = 	\
	openmp/parallel_sections_01.c

openmp_parallel_sections_combined_01_SOURCES = 	\
	openmp/parallel_sections_combined_01.c

openmp_task_01_SOURCES = 	\
	openmp/task_01.c

openmp_task_02_SOURCES = 	\
	openmp/task_02.c

openmp_taskwait_01_SOURCES = 	\
	openmp/taskwait_01.c

openmp_taskgroup_01_SOURCES = 	\
	openmp/taskgroup_01.c

openmp_taskgroup_02_SOURCES = 	\
	openmp/taskgroup_02.c

openmp_array_slice_01_SOURCES = 	\
	openmp/array_slice_01.c

openmp_cuda_task_01_SOURCES = 	\
	openmp/cuda_task_01.c

###################
# Block interface #
###################
datawizard_interfaces_block_block_interface_SOURCES= \
	datawizard/interfaces/test_interfaces.c  \
	datawizard/interfaces/block/block_interface.c

if STARPU_USE_CUDA
datawizard_interfaces_block_block_interface_SOURCES+= \
	datawizard/interfaces/block/block_cuda.cu
endif

if STARPU_USE_OPENCL
datawizard_interfaces_block_block_interface_SOURCES+= \
	datawizard/interfaces/block/block_opencl.c
nobase_STARPU_OPENCL_DATA_DATA += \
	datawizard/interfaces/block/block_opencl_kernel.cl
endif

##################
# BSCR interface #
##################
datawizard_interfaces_bcsr_bcsr_interface_SOURCES= \
	datawizard/interfaces/test_interfaces.c \
	datawizard/interfaces/bcsr/bcsr_interface.c 

if STARPU_USE_CUDA
datawizard_interfaces_bcsr_bcsr_interface_SOURCES+= \
	datawizard/interfaces/bcsr/bcsr_cuda.cu
endif

if STARPU_USE_OPENCL
datawizard_interfaces_bcsr_bcsr_interface_SOURCES+= \
	datawizard/interfaces/bcsr/bcsr_opencl.c
nobase_STARPU_OPENCL_DATA_DATA += \
	datawizard/interfaces/bcsr/bcsr_opencl_kernel.cl
endif

#################
# COO interface #
#################
datawizard_interfaces_coo_coo_interface_SOURCES= \
	datawizard/interfaces/test_interfaces.c \
	datawizard/interfaces/coo/coo_interface.c 

if STARPU_USE_CUDA
datawizard_interfaces_coo_coo_interface_SOURCES+= \
	datawizard/interfaces/coo/coo_cuda.cu
endif

if STARPU_USE_OPENCL
datawizard_interfaces_coo_coo_interface_SOURCES+= \
	datawizard/interfaces/coo/coo_opencl.c
nobase_STARPU_OPENCL_DATA_DATA += \
	datawizard/interfaces/coo/coo_opencl_kernel.cl
endif

#################
# CSR interface #
#################
datawizard_interfaces_csr_csr_interface_SOURCES= \
	datawizard/interfaces/test_interfaces.c  \
	datawizard/interfaces/csr/csr_interface.c

if STARPU_USE_CUDA
datawizard_interfaces_csr_csr_interface_SOURCES+= \
	datawizard/interfaces/csr/csr_cuda.cu
endif

if STARPU_USE_OPENCL
datawizard_interfaces_csr_csr_interface_SOURCES+= \
	datawizard/interfaces/csr/csr_opencl.c
nobase_STARPU_OPENCL_DATA_DATA += \
	datawizard/interfaces/csr/csr_opencl_kernel.cl
endif


datawizard_interfaces_vector_test_vector_interface_SOURCES =               \
	datawizard/interfaces/vector/test_vector_interface.c               \
	datawizard/interfaces/test_interfaces.c

if STARPU_USE_CUDA
datawizard_interfaces_vector_test_vector_interface_SOURCES +=               \
	datawizard/interfaces/vector/test_vector_cuda.cu
endif

if STARPU_USE_OPENCL
datawizard_interfaces_vector_test_vector_interface_SOURCES +=               \
	datawizard/interfaces/vector/test_vector_opencl.c 
nobase_STARPU_OPENCL_DATA_DATA += \
	datawizard/interfaces/vector/test_vector_opencl_kernel.cl
endif

####################
# Matrix interface #
####################
datawizard_interfaces_matrix_matrix_interface_SOURCES= \
	datawizard/interfaces/test_interfaces.c        \
	datawizard/interfaces/matrix/matrix_interface.c

if STARPU_USE_CUDA
datawizard_interfaces_matrix_matrix_interface_SOURCES+= \
	datawizard/interfaces/matrix/matrix_cuda.cu
endif

if STARPU_USE_OPENCL
datawizard_interfaces_matrix_matrix_interface_SOURCES+= \
	datawizard/interfaces/matrix/matrix_opencl.c
nobase_STARPU_OPENCL_DATA_DATA+= \
	datawizard/interfaces/matrix/matrix_opencl_kernel.cl
endif


#########################
# Multiformat interface #
#########################
datawizard_interfaces_multiformat_multiformat_interface_SOURCES =           \
	datawizard/interfaces/test_interfaces.c                             \
	datawizard/interfaces/multiformat/multiformat_interface.c           \
	datawizard/interfaces/multiformat/multiformat_conversion_codelets.c

if STARPU_USE_CUDA
datawizard_interfaces_multiformat_multiformat_interface_SOURCES+=                  \
	datawizard/interfaces/multiformat/multiformat_cuda.cu                      \
	datawizard/interfaces/multiformat/multiformat_conversion_codelets_cuda.cu
endif

if STARPU_USE_OPENCL
datawizard_interfaces_multiformat_multiformat_interface_SOURCES+=                  \
	datawizard/interfaces/multiformat/multiformat_opencl.c                     \
	datawizard/interfaces/multiformat/multiformat_conversion_codelets_opencl.c
nobase_STARPU_OPENCL_DATA_DATA +=                                                          \
	datawizard/interfaces/multiformat/multiformat_opencl_kernel.cl                     \
	datawizard/interfaces/multiformat/multiformat_conversion_codelets_kernel.cl
endif

datawizard_interfaces_multiformat_advanced_multiformat_cuda_opencl_SOURCES=\
	datawizard/interfaces/multiformat/advanced/generic.c               \
	datawizard/interfaces/multiformat/advanced/multiformat_cuda_opencl.c

datawizard_interfaces_multiformat_advanced_multiformat_data_release_SOURCES = \
	datawizard/interfaces/multiformat/advanced/generic.c                  \
	datawizard/interfaces/multiformat/advanced/multiformat_data_release.c

datawizard_interfaces_multiformat_advanced_multiformat_worker_SOURCES=\
	datawizard/interfaces/multiformat/advanced/generic.c               \
	datawizard/interfaces/multiformat/advanced/multiformat_worker.c

datawizard_interfaces_multiformat_advanced_multiformat_handle_conversion_SOURCES = \
	datawizard/interfaces/multiformat/advanced/generic.c \
	datawizard/interfaces/multiformat/advanced/multiformat_handle_conversion.c

datawizard_interfaces_multiformat_advanced_same_handle_SOURCES= \
	datawizard/interfaces/multiformat/advanced/generic.c               \
	datawizard/interfaces/multiformat/advanced/same_handle.c


datawizard_interfaces_variable_variable_interface_SOURCES=   \
	datawizard/interfaces/test_interfaces.c              \
	datawizard/interfaces/variable/variable_interface.c

if STARPU_USE_CUDA
datawizard_interfaces_variable_variable_interface_SOURCES+= \
	datawizard/interfaces/variable/variable_cuda.cu
endif

if STARPU_USE_OPENCL
datawizard_interfaces_variable_variable_interface_SOURCES+= \
	datawizard/interfaces/variable/variable_opencl.c
nobase_STARPU_OPENCL_DATA_DATA += \
	datawizard/interfaces/variable/variable_opencl_kernel.cl
endif

##################
# Void interface #
##################
datawizard_interfaces_void_void_interface_SOURCES=\
	datawizard/interfaces/test_interfaces.c        \
	datawizard/interfaces/void/void_interface.c


overlap_gpu_concurrency_SOURCES=\
	overlap/gpu_concurrency.c
if STARPU_USE_CUDA
overlap_gpu_concurrency_SOURCES+=\
	overlap/long_kernel.cu
endif

perfmodels_regression_based_SOURCES=\
	perfmodels/regression_based.c

if STARPU_USE_OPENCL
perfmodels_regression_based_SOURCES+=\
	perfmodels/opencl_memset.c
nobase_STARPU_OPENCL_DATA_DATA += \
	perfmodels/opencl_memset_kernel.cl
endif

perfmodels_non_linear_regression_based_SOURCES=\
	perfmodels/non_linear_regression_based.c

if STARPU_USE_OPENCL
perfmodels_non_linear_regression_based_SOURCES+=\
	perfmodels/opencl_memset.c
endif

perfmodels_feed_SOURCES=\
	perfmodels/feed.c

sched_policies_execute_all_tasks_LDFLAGS = $(AM_LDFLAGS) -lm

showcheck:
	-cat $(TEST_LOGS) /dev/null
	! grep -q " runtime error: " $(TEST_LOGS) /dev/null
