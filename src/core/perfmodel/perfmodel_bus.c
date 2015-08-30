/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009-2015  Université de Bordeaux
 * Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015  CNRS
 * Copyright (C) 2013 Corentin Salingue
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

#ifdef STARPU_USE_CUDA
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#endif
#include <stdlib.h>
#include <math.h>

#include <starpu.h>
#include <starpu_cuda.h>
#include <starpu_opencl.h>
#include <common/config.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <core/workers.h>
#include <core/perfmodel/perfmodel.h>
#include <core/simgrid.h>
#include <common/utils.h>

#ifdef STARPU_USE_OPENCL
#include <starpu_opencl.h>
#endif

#ifdef STARPU_HAVE_WINDOWS
#include <windows.h>
#endif

#if HAVE_DECL_HWLOC_CUDA_GET_DEVICE_OSDEV_BY_INDEX
#include <hwloc/cuda.h>
#endif

#define SIZE	(32*1024*1024*sizeof(char))
#define NITER	128

#ifndef STARPU_SIMGRID
static void _starpu_bus_force_sampling(void);
#endif

/* timing is in µs per byte (i.e. slowness, inverse of bandwidth) */
struct dev_timing
{
	int cpu_id;
	double timing_htod;
	double latency_htod;
	double timing_dtoh;
	double latency_dtoh;
};

/* TODO: measure latency */
static double bandwidth_matrix[STARPU_MAXNODES][STARPU_MAXNODES];
static double latency_matrix[STARPU_MAXNODES][STARPU_MAXNODES];
static unsigned was_benchmarked = 0;
#ifndef STARPU_SIMGRID
static unsigned ncpus = 0;
#endif
static unsigned ncuda = 0;
static unsigned nopencl = 0;
static unsigned nmic = 0;

/* Benchmarking the performance of the bus */

#ifndef STARPU_SIMGRID
static uint64_t cuda_size[STARPU_MAXCUDADEVS];
#endif
#ifdef STARPU_USE_CUDA
/* preference order of cores (logical indexes) */
static int cuda_affinity_matrix[STARPU_MAXCUDADEVS][STARPU_MAXCPUS];

#ifndef STARPU_SIMGRID
static double cudadev_timing_htod[STARPU_MAXNODES] = {0.0};
static double cudadev_latency_htod[STARPU_MAXNODES] = {0.0};
static double cudadev_timing_dtoh[STARPU_MAXNODES] = {0.0};
static double cudadev_latency_dtoh[STARPU_MAXNODES] = {0.0};
#ifdef HAVE_CUDA_MEMCPY_PEER
static double cudadev_timing_dtod[STARPU_MAXNODES][STARPU_MAXNODES] = {{0.0}};
static double cudadev_latency_dtod[STARPU_MAXNODES][STARPU_MAXNODES] = {{0.0}};
#endif
#endif
static struct dev_timing cudadev_timing_per_cpu[STARPU_MAXNODES*STARPU_MAXCPUS];
#endif

#ifndef STARPU_SIMGRID
static uint64_t opencl_size[STARPU_MAXCUDADEVS];
#endif
#ifdef STARPU_USE_OPENCL
/* preference order of cores (logical indexes) */
static int opencl_affinity_matrix[STARPU_MAXOPENCLDEVS][STARPU_MAXCPUS];
#ifndef STARPU_SIMGRID
static double opencldev_timing_htod[STARPU_MAXNODES] = {0.0};
static double opencldev_latency_htod[STARPU_MAXNODES] = {0.0};
static double opencldev_timing_dtoh[STARPU_MAXNODES] = {0.0};
static double opencldev_latency_dtoh[STARPU_MAXNODES] = {0.0};
#endif
static struct dev_timing opencldev_timing_per_cpu[STARPU_MAXNODES*STARPU_MAXCPUS];
#endif

#ifdef STARPU_USE_MIC
static double mic_time_host_to_device[STARPU_MAXNODES] = {0.0};
static double mic_time_device_to_host[STARPU_MAXNODES] = {0.0};
#endif /* STARPU_USE_MIC */

#ifdef STARPU_HAVE_HWLOC
static hwloc_topology_t hwtopology;
#endif

#if (defined(STARPU_USE_CUDA) || defined(STARPU_USE_OPENCL)) && !defined(STARPU_SIMGRID)

#ifdef STARPU_USE_CUDA

static void measure_bandwidth_between_host_and_dev_on_cpu_with_cuda(int dev, int cpu, struct dev_timing *dev_timing_per_cpu)
{
	struct _starpu_machine_config *config = _starpu_get_machine_config();
	_starpu_bind_thread_on_cpu(config, cpu);
	size_t size = SIZE;

	/* Initialize CUDA context on the device */
	/* We do not need to enable OpenGL interoperability at this point,
	 * since we cleanly shutdown CUDA before returning. */
	cudaSetDevice(dev);

	/* hack to avoid third party libs to rebind threads */
	_starpu_bind_thread_on_cpu(config, cpu);

	/* hack to force the initialization */
	cudaFree(0);

	/* hack to avoid third party libs to rebind threads */
	_starpu_bind_thread_on_cpu(config, cpu);

        /* Get the maximum size which can be allocated on the device */
	struct cudaDeviceProp prop;
	cudaError_t cures;
	cures = cudaGetDeviceProperties(&prop, dev);
	if (STARPU_UNLIKELY(cures)) STARPU_CUDA_REPORT_ERROR(cures);
	cuda_size[dev] = prop.totalGlobalMem;
        if (size > prop.totalGlobalMem/4) size = prop.totalGlobalMem/4;

	/* Allocate a buffer on the device */
	unsigned char *d_buffer;
	cures = cudaMalloc((void **)&d_buffer, size);
	STARPU_ASSERT(cures == cudaSuccess);

	/* hack to avoid third party libs to rebind threads */
	_starpu_bind_thread_on_cpu(config, cpu);

	/* Allocate a buffer on the host */
	unsigned char *h_buffer;
	cures = cudaHostAlloc((void **)&h_buffer, size, 0);
	STARPU_ASSERT(cures == cudaSuccess);

	/* hack to avoid third party libs to rebind threads */
	_starpu_bind_thread_on_cpu(config, cpu);

	/* Fill them */
	memset(h_buffer, 0, size);
	cudaMemset(d_buffer, 0, size);

	/* hack to avoid third party libs to rebind threads */
	_starpu_bind_thread_on_cpu(config, cpu);

	unsigned iter;
	double timing;
	double start;
	double end;

	/* Measure upload bandwidth */
	start = starpu_timing_now();
	for (iter = 0; iter < NITER; iter++)
	{
		cudaMemcpy(d_buffer, h_buffer, size, cudaMemcpyHostToDevice);
		cudaThreadSynchronize();
	}
	end = starpu_timing_now();
	timing = end - start;

	dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].timing_htod = timing/NITER/size;

	/* Measure download bandwidth */
	start = starpu_timing_now();
	for (iter = 0; iter < NITER; iter++)
	{
		cudaMemcpy(h_buffer, d_buffer, size, cudaMemcpyDeviceToHost);
		cudaThreadSynchronize();
	}
	end = starpu_timing_now();
	timing = end - start;

	dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].timing_dtoh = timing/NITER/size;

	/* Measure upload latency */
	start = starpu_timing_now();
	for (iter = 0; iter < NITER; iter++)
	{
		cudaMemcpy(d_buffer, h_buffer, 1, cudaMemcpyHostToDevice);
		cudaThreadSynchronize();
	}
	end = starpu_timing_now();
	timing = end - start;

	dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].latency_htod = timing/NITER;

	/* Measure download latency */
	start = starpu_timing_now();
	for (iter = 0; iter < NITER; iter++)
	{
		cudaMemcpy(h_buffer, d_buffer, 1, cudaMemcpyDeviceToHost);
		cudaThreadSynchronize();
	}
	end = starpu_timing_now();
	timing = end - start;

	dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].latency_dtoh = timing/NITER;

	/* Free buffers */
	cudaFreeHost(h_buffer);
	cudaFree(d_buffer);

	cudaThreadExit();
}

#ifdef HAVE_CUDA_MEMCPY_PEER
static void measure_bandwidth_between_dev_and_dev_cuda(int src, int dst)
{
	size_t size = SIZE;
	int can;

        /* Get the maximum size which can be allocated on the device */
	struct cudaDeviceProp prop;
	cudaError_t cures;

	cures = cudaGetDeviceProperties(&prop, src);
	if (STARPU_UNLIKELY(cures)) STARPU_CUDA_REPORT_ERROR(cures);
        if (size > prop.totalGlobalMem/4) size = prop.totalGlobalMem/4;
	cures = cudaGetDeviceProperties(&prop, dst);
	if (STARPU_UNLIKELY(cures)) STARPU_CUDA_REPORT_ERROR(cures);
        if (size > prop.totalGlobalMem/4) size = prop.totalGlobalMem/4;

	/* Initialize CUDA context on the source */
	/* We do not need to enable OpenGL interoperability at this point,
	 * since we cleanly shutdown CUDA before returning. */
	cudaSetDevice(src);

	if (starpu_get_env_number("STARPU_ENABLE_CUDA_GPU_GPU_DIRECT") != 0)
	{
		cures = cudaDeviceCanAccessPeer(&can, src, dst);
		if (!cures && can)
		{
			cures = cudaDeviceEnablePeerAccess(dst, 0);
			if (!cures)
				_STARPU_DISP("GPU-Direct %d -> %d\n", dst, src);
		}
	}

	/* Allocate a buffer on the device */
	unsigned char *s_buffer;
	cures = cudaMalloc((void **)&s_buffer, size);
	STARPU_ASSERT(cures == cudaSuccess);
	cudaMemset(s_buffer, 0, size);

	/* Initialize CUDA context on the destination */
	/* We do not need to enable OpenGL interoperability at this point,
	 * since we cleanly shutdown CUDA before returning. */
	cudaSetDevice(dst);

	if (starpu_get_env_number("STARPU_ENABLE_CUDA_GPU_GPU_DIRECT") != 0)
	{
		cures = cudaDeviceCanAccessPeer(&can, dst, src);
		if (!cures && can)
		{
			cures = cudaDeviceEnablePeerAccess(src, 0);
			if (!cures)
				_STARPU_DISP("GPU-Direct %d -> %d\n", src, dst);
		}
	}

	/* Allocate a buffer on the device */
	unsigned char *d_buffer;
	cures = cudaMalloc((void **)&d_buffer, size);
	STARPU_ASSERT(cures == cudaSuccess);
	cudaMemset(d_buffer, 0, size);

	unsigned iter;
	double timing;
	double start;
	double end;

	/* Measure upload bandwidth */
	start = starpu_timing_now();
	for (iter = 0; iter < NITER; iter++)
	{
		cudaMemcpyPeer(d_buffer, dst, s_buffer, src, size);
		cudaThreadSynchronize();
	}
	end = starpu_timing_now();
	timing = end - start;

	cudadev_timing_dtod[src+1][dst+1] = timing/NITER/size;

	/* Measure upload latency */
	start = starpu_timing_now();
	for (iter = 0; iter < NITER; iter++)
	{
		cudaMemcpyPeer(d_buffer, dst, s_buffer, src, 1);
		cudaThreadSynchronize();
	}
	end = starpu_timing_now();
	timing = end - start;

	cudadev_latency_dtod[src+1][dst+1] = timing/NITER;

	/* Free buffers */
	cudaFree(d_buffer);
	cudaSetDevice(src);
	cudaFree(s_buffer);

	cudaThreadExit();
}
#endif
#endif

#ifdef STARPU_USE_OPENCL
static void measure_bandwidth_between_host_and_dev_on_cpu_with_opencl(int dev, int cpu, struct dev_timing *dev_timing_per_cpu)
{
        cl_context context;
        cl_command_queue queue;
        cl_int err=0;
	size_t size = SIZE;
	int not_initialized;

        struct _starpu_machine_config *config = _starpu_get_machine_config();
	_starpu_bind_thread_on_cpu(config, cpu);

	/* Is the context already initialised ? */
        starpu_opencl_get_context(dev, &context);
	not_initialized = (context == NULL);
	if (not_initialized == 1)
	     _starpu_opencl_init_context(dev);

	/* Get context and queue */
        starpu_opencl_get_context(dev, &context);
        starpu_opencl_get_queue(dev, &queue);

        /* Get the maximum size which can be allocated on the device */
        cl_device_id device;
	cl_ulong maxMemAllocSize, totalGlobalMem;
        starpu_opencl_get_device(dev, &device);
	err = clGetDeviceInfo(device, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(maxMemAllocSize), &maxMemAllocSize, NULL);
        if (STARPU_UNLIKELY(err != CL_SUCCESS)) STARPU_OPENCL_REPORT_ERROR(err);
        if (size > (size_t)maxMemAllocSize/4) size = maxMemAllocSize/4;

	err = clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE , sizeof(totalGlobalMem), &totalGlobalMem, NULL);
        if (STARPU_UNLIKELY(err != CL_SUCCESS)) STARPU_OPENCL_REPORT_ERROR(err);
	opencl_size[dev] = totalGlobalMem;

	if (_starpu_opencl_get_device_type(dev) == CL_DEVICE_TYPE_CPU)
	{
		/* Let's not use too much RAM when running OpenCL on a CPU: it
		 * would make the OS swap like crazy. */
		size /= 2;
	}

	/* hack to avoid third party libs to rebind threads */
	_starpu_bind_thread_on_cpu(config, cpu);

	/* Allocate a buffer on the device */
	cl_mem d_buffer;
	d_buffer = clCreateBuffer(context, CL_MEM_READ_WRITE, size, NULL, &err);
	if (STARPU_UNLIKELY(err != CL_SUCCESS)) STARPU_OPENCL_REPORT_ERROR(err);

	/* hack to avoid third party libs to rebind threads */
	_starpu_bind_thread_on_cpu(config, cpu);
        /* Allocate a buffer on the host */
	unsigned char *h_buffer;
        h_buffer = (unsigned char *)malloc(size);
	STARPU_ASSERT(h_buffer);

	/* hack to avoid third party libs to rebind threads */
	_starpu_bind_thread_on_cpu(config, cpu);
        /* Fill them */
	memset(h_buffer, 0, size);
        err = clEnqueueWriteBuffer(queue, d_buffer, CL_TRUE, 0, size, h_buffer, 0, NULL, NULL);
        if (STARPU_UNLIKELY(err != CL_SUCCESS)) STARPU_OPENCL_REPORT_ERROR(err);
        clFinish(queue);
	/* hack to avoid third party libs to rebind threads */
	_starpu_bind_thread_on_cpu(config, cpu);

        unsigned iter;
	double timing;
	double start;
	double end;

	/* Measure upload bandwidth */
	start = starpu_timing_now();
	for (iter = 0; iter < NITER; iter++)
	{
                err = clEnqueueWriteBuffer(queue, d_buffer, CL_TRUE, 0, size, h_buffer, 0, NULL, NULL);
                if (STARPU_UNLIKELY(err != CL_SUCCESS)) STARPU_OPENCL_REPORT_ERROR(err);
                clFinish(queue);
	}
	end = starpu_timing_now();
	timing = end - start;

	dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].timing_htod = timing/NITER/size;

	/* Measure download bandwidth */
	start = starpu_timing_now();
	for (iter = 0; iter < NITER; iter++)
	{
                err = clEnqueueReadBuffer(queue, d_buffer, CL_TRUE, 0, size, h_buffer, 0, NULL, NULL);
                if (STARPU_UNLIKELY(err != CL_SUCCESS)) STARPU_OPENCL_REPORT_ERROR(err);
                clFinish(queue);
	}
	end = starpu_timing_now();
	timing = end - start;

	dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].timing_dtoh = timing/NITER/size;

	/* Measure upload latency */
	start = starpu_timing_now();
	for (iter = 0; iter < NITER; iter++)
	{
		err = clEnqueueWriteBuffer(queue, d_buffer, CL_TRUE, 0, 1, h_buffer, 0, NULL, NULL);
                if (STARPU_UNLIKELY(err != CL_SUCCESS)) STARPU_OPENCL_REPORT_ERROR(err);
                clFinish(queue);
	}
	end = starpu_timing_now();
	timing = end - start;

	dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].latency_htod = timing/NITER;

	/* Measure download latency */
	start = starpu_timing_now();
	for (iter = 0; iter < NITER; iter++)
	{
		err = clEnqueueReadBuffer(queue, d_buffer, CL_TRUE, 0, 1, h_buffer, 0, NULL, NULL);
                if (STARPU_UNLIKELY(err != CL_SUCCESS)) STARPU_OPENCL_REPORT_ERROR(err);
                clFinish(queue);
	}
	end = starpu_timing_now();
	timing = end - start;

	dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].latency_dtoh = timing/NITER;

	/* Free buffers */
	err = clReleaseMemObject(d_buffer);
	if (STARPU_UNLIKELY(err != CL_SUCCESS))
		STARPU_OPENCL_REPORT_ERROR(err);
	free(h_buffer);

	/* Uninitiliaze OpenCL context on the device */
	if (not_initialized == 1)
	     _starpu_opencl_deinit_context(dev);
}
#endif

/* NB: we want to sort the bandwidth by DECREASING order */
static int compar_dev_timing(const void *left_dev_timing, const void *right_dev_timing)
{
	const struct dev_timing *left = (const struct dev_timing *)left_dev_timing;
	const struct dev_timing *right = (const struct dev_timing *)right_dev_timing;

	double left_dtoh = left->timing_dtoh;
	double left_htod = left->timing_htod;
	double right_dtoh = right->timing_dtoh;
	double right_htod = right->timing_htod;

	double timing_sum2_left = left_dtoh*left_dtoh + left_htod*left_htod;
	double timing_sum2_right = right_dtoh*right_dtoh + right_htod*right_htod;

	/* it's for a decreasing sorting */
	return (timing_sum2_left > timing_sum2_right);
}

#ifdef STARPU_HAVE_HWLOC
static int find_numa_node(hwloc_obj_t obj)
{
	STARPU_ASSERT(obj);
	hwloc_obj_t current = obj;

	while (current->depth != HWLOC_OBJ_NODE)
	{
		current = current->parent;

		/* If we don't find a "node" obj before the root, this means
		 * hwloc does not know whether there are numa nodes or not, so
		 * we should not use a per-node sampling in that case. */
		STARPU_ASSERT(current);
	}

	STARPU_ASSERT(current->depth == HWLOC_OBJ_NODE);

	return current->logical_index;
}
#endif

static void measure_bandwidth_between_cpus_and_dev(int dev, struct dev_timing *dev_timing_per_cpu, char *type)
{
	/* Either we have hwloc and we measure the bandwith between each GPU
	 * and each NUMA node, or we don't have such NUMA information and we
	 * measure the bandwith for each pair of (CPU, GPU), which is slower.
	 * */
#ifdef STARPU_HAVE_HWLOC
	int cpu_depth = hwloc_get_type_depth(hwtopology, HWLOC_OBJ_PU);
	int nnuma_nodes = hwloc_get_nbobjs_by_depth(hwtopology, HWLOC_OBJ_NODE);

	/* If no NUMA node was found, we assume that we have a single memory
	 * bank. */
	const unsigned no_node_obj_was_found = (nnuma_nodes == 0);

	unsigned *is_available_per_numa_node = NULL;
	double *dev_timing_htod_per_numa_node = NULL;
	double *dev_latency_htod_per_numa_node = NULL;
	double *dev_timing_dtoh_per_numa_node = NULL;
	double *dev_latency_dtoh_per_numa_node = NULL;

	if (!no_node_obj_was_found)
	{
		is_available_per_numa_node = (unsigned *)malloc(nnuma_nodes * sizeof(unsigned));
		STARPU_ASSERT(is_available_per_numa_node);

		dev_timing_htod_per_numa_node = (double *)malloc(nnuma_nodes * sizeof(double));
		STARPU_ASSERT(dev_timing_htod_per_numa_node);
		dev_latency_htod_per_numa_node = (double *)malloc(nnuma_nodes * sizeof(double));
		STARPU_ASSERT(dev_latency_htod_per_numa_node);

		dev_timing_dtoh_per_numa_node = (double *)malloc(nnuma_nodes * sizeof(double));
		STARPU_ASSERT(dev_timing_dtoh_per_numa_node);
		dev_latency_dtoh_per_numa_node = (double *)malloc(nnuma_nodes * sizeof(double));
		STARPU_ASSERT(dev_latency_dtoh_per_numa_node);

		memset(is_available_per_numa_node, 0, nnuma_nodes*sizeof(unsigned));
	}
#endif

	unsigned cpu;
	for (cpu = 0; cpu < ncpus; cpu++)
	{
		dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].cpu_id = cpu;

#ifdef STARPU_HAVE_HWLOC
		int numa_id = 0;

		if (!no_node_obj_was_found)
		{
			hwloc_obj_t obj = hwloc_get_obj_by_depth(hwtopology, cpu_depth, cpu);

			numa_id = find_numa_node(obj);

			if (is_available_per_numa_node[numa_id])
			{
				/* We reuse the previous numbers for that NUMA node */
				dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].timing_htod =
					dev_timing_htod_per_numa_node[numa_id];
				dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].latency_htod =
					dev_latency_htod_per_numa_node[numa_id];
				dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].timing_dtoh =
					dev_timing_dtoh_per_numa_node[numa_id];
				dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].latency_dtoh =
					dev_latency_dtoh_per_numa_node[numa_id];
				continue;
			}
		}
#endif

#ifdef STARPU_USE_CUDA
                if (strncmp(type, "CUDA", 4) == 0)
                        measure_bandwidth_between_host_and_dev_on_cpu_with_cuda(dev, cpu, dev_timing_per_cpu);
#endif
#ifdef STARPU_USE_OPENCL
                if (strncmp(type, "OpenCL", 6) == 0)
                        measure_bandwidth_between_host_and_dev_on_cpu_with_opencl(dev, cpu, dev_timing_per_cpu);
#endif

#ifdef STARPU_HAVE_HWLOC
		if (!no_node_obj_was_found && !is_available_per_numa_node[numa_id])
		{
			/* Save the results for that NUMA node */
			dev_timing_htod_per_numa_node[numa_id] =
				dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].timing_htod;
			dev_latency_htod_per_numa_node[numa_id] =
				dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].latency_htod;
			dev_timing_dtoh_per_numa_node[numa_id] =
				dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].timing_dtoh;
			dev_latency_dtoh_per_numa_node[numa_id] =
				dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].latency_dtoh;

			is_available_per_numa_node[numa_id] = 1;
		}
#endif
        }

#ifdef STARPU_HAVE_HWLOC
	if (!no_node_obj_was_found)
	{
		free(is_available_per_numa_node);
		free(dev_timing_htod_per_numa_node);
		free(dev_latency_htod_per_numa_node);
		free(dev_timing_dtoh_per_numa_node);
		free(dev_latency_dtoh_per_numa_node);
	}
#endif /* STARPU_HAVE_HWLOC */
}

static void measure_bandwidth_between_host_and_dev(int dev, double *dev_timing_htod, double *dev_latency_htod,
                                                   double *dev_timing_dtoh, double *dev_latency_dtoh,
                                                   struct dev_timing *dev_timing_per_cpu, char *type)
{
	measure_bandwidth_between_cpus_and_dev(dev, dev_timing_per_cpu, type);

	/* sort the results */
	qsort(&(dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS]), ncpus,
              sizeof(struct dev_timing),
			compar_dev_timing);

#ifdef STARPU_VERBOSE
        unsigned cpu;
	for (cpu = 0; cpu < ncpus; cpu++)
	{
		unsigned current_cpu = dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].cpu_id;
		double bandwidth_dtoh = dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].timing_dtoh;
		double bandwidth_htod = dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+cpu].timing_htod;

		double bandwidth_sum2 = bandwidth_dtoh*bandwidth_dtoh + bandwidth_htod*bandwidth_htod;

		_STARPU_DISP("(%10s) BANDWIDTH GPU %d CPU %u - htod %f - dtoh %f - %f\n", type, dev, current_cpu, bandwidth_htod, bandwidth_dtoh, sqrt(bandwidth_sum2));
	}

	unsigned best_cpu = dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+0].cpu_id;

	_STARPU_DISP("(%10s) BANDWIDTH GPU %d BEST CPU %u\n", type, dev, best_cpu);
#endif

	/* The results are sorted in a decreasing order, so that the best
	 * measurement is currently the first entry. */
	dev_timing_dtoh[dev+1] = dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+0].timing_dtoh;
	dev_latency_dtoh[dev+1] = dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+0].latency_dtoh;
	dev_timing_htod[dev+1] = dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+0].timing_htod;
	dev_latency_htod[dev+1] = dev_timing_per_cpu[(dev+1)*STARPU_MAXCPUS+0].latency_htod;
}
#endif /* defined(STARPU_USE_CUDA) || defined(STARPU_USE_OPENCL) */

static void benchmark_all_gpu_devices(void)
{
#ifdef STARPU_SIMGRID
	_STARPU_DISP("can not measure bus in simgrid mode, please run starpu_calibrate_bus in non-simgrid mode to make sure the bus performance model was calibrated\n");
	STARPU_ABORT();
#else /* !SIMGRID */
#if defined(STARPU_USE_CUDA) || defined(STARPU_USE_OPENCL) || defined(STARPU_USE_MIC)
	unsigned i;
#endif
#ifdef HAVE_CUDA_MEMCPY_PEER
	unsigned j;
#endif

	_STARPU_DEBUG("Benchmarking the speed of the bus\n");

#ifdef STARPU_HAVE_HWLOC
	hwloc_topology_init(&hwtopology);
	hwloc_topology_load(hwtopology);
#endif

#ifdef STARPU_HAVE_HWLOC
	hwloc_bitmap_t former_cpuset = hwloc_bitmap_alloc();
	hwloc_get_cpubind(hwtopology, former_cpuset, HWLOC_CPUBIND_THREAD);
#elif __linux__
	/* Save the current cpu binding */
	cpu_set_t former_process_affinity;
	int ret;
	ret = sched_getaffinity(0, sizeof(former_process_affinity), &former_process_affinity);
	if (ret)
	{
		perror("sched_getaffinity");
		STARPU_ABORT();
	}
#else
#warning Missing binding support, StarPU will not be able to properly benchmark NUMA topology
#endif

	struct _starpu_machine_config *config = _starpu_get_machine_config();
	ncpus = _starpu_topology_get_nhwcpu(config);

#ifdef STARPU_USE_CUDA
	ncuda = _starpu_get_cuda_device_count();
	for (i = 0; i < ncuda; i++)
	{
		_STARPU_DISP("CUDA %d...\n", i);
		/* measure bandwidth between Host and Device i */
		measure_bandwidth_between_host_and_dev(i, cudadev_timing_htod, cudadev_latency_htod, cudadev_timing_dtoh, cudadev_latency_dtoh, cudadev_timing_per_cpu, "CUDA");
	}
#ifdef HAVE_CUDA_MEMCPY_PEER
	for (i = 0; i < ncuda; i++)
		for (j = 0; j < ncuda; j++)
			if (i != j)
			{
				_STARPU_DISP("CUDA %d -> %d...\n", i, j);
				/* measure bandwidth between Host and Device i */
				measure_bandwidth_between_dev_and_dev_cuda(i, j);
			}
#endif
#endif
#ifdef STARPU_USE_OPENCL
        nopencl = _starpu_opencl_get_device_count();
	for (i = 0; i < nopencl; i++)
	{
		_STARPU_DISP("OpenCL %d...\n", i);
		/* measure bandwith between Host and Device i */
		measure_bandwidth_between_host_and_dev(i, opencldev_timing_htod, opencldev_latency_htod, opencldev_timing_dtoh, opencldev_latency_dtoh, opencldev_timing_per_cpu, "OpenCL");
	}
#endif

#ifdef STARPU_USE_MIC
	/* TODO: implement real calibration ! For now we only put an arbitrary
	 * value for each device during at the declaration as a bug fix, else
	 * we get problems on heft scheduler */
        nmic = _starpu_mic_src_get_device_count();

	for (i = 0; i < STARPU_MAXNODES; i++)
	{
		mic_time_host_to_device[i] = 0.1;
		mic_time_device_to_host[i] = 0.1;
	}
#endif /* STARPU_USE_MIC */

#ifdef STARPU_HAVE_HWLOC
	hwloc_set_cpubind(hwtopology, former_cpuset, HWLOC_CPUBIND_THREAD);
	hwloc_bitmap_free(former_cpuset);
#elif __linux__
	/* Restore the former affinity */
	ret = sched_setaffinity(0, sizeof(former_process_affinity), &former_process_affinity);
	if (ret)
	{
		perror("sched_setaffinity");
		STARPU_ABORT();
	}
#endif

#ifdef STARPU_HAVE_HWLOC
	hwloc_topology_destroy(hwtopology);
#endif

	_STARPU_DEBUG("Benchmarking the speed of the bus is done.\n");

	was_benchmarked = 1;
#endif /* !SIMGRID */
}

static void get_bus_path(const char *type, char *path, size_t maxlen)
{
	char hostname[65];

	_starpu_gethostname(hostname, sizeof(hostname));
	snprintf(path, maxlen, "%s%s.%s", _starpu_get_perf_model_dir_bus(), hostname, type);
}

/*
 *	Affinity
 */

#ifndef STARPU_SIMGRID
static void get_affinity_path(char *path, size_t maxlen)
{
	get_bus_path("affinity", path, maxlen);
}

static void load_bus_affinity_file_content(void)
{
#if defined(STARPU_USE_CUDA) || defined(STARPU_USE_OPENCL)
	FILE *f;

	char path[256];
	get_affinity_path(path, sizeof(path));

	_STARPU_DEBUG("loading affinities from %s\n", path);

	f = fopen(path, "r");
	STARPU_ASSERT(f);

	_starpu_frdlock(f);

	struct _starpu_machine_config *config = _starpu_get_machine_config();
	ncpus = _starpu_topology_get_nhwcpu(config);
        unsigned gpu;

#ifdef STARPU_USE_CUDA
	ncuda = _starpu_get_cuda_device_count();
	for (gpu = 0; gpu < ncuda; gpu++)
	{
		int ret;
		unsigned dummy;

		_starpu_drop_comments(f);
		ret = fscanf(f, "%d\t", &dummy);
		STARPU_ASSERT(ret == 1);

		STARPU_ASSERT(dummy == gpu);

		unsigned cpu;
		for (cpu = 0; cpu < ncpus; cpu++)
		{
			ret = fscanf(f, "%d\t", &cuda_affinity_matrix[gpu][cpu]);
			STARPU_ASSERT(ret == 1);
		}

		ret = fscanf(f, "\n");
		STARPU_ASSERT(ret == 0);
	}
#endif /* !STARPU_USE_CUDA */
#ifdef STARPU_USE_OPENCL
        nopencl = _starpu_opencl_get_device_count();
	for (gpu = 0; gpu < nopencl; gpu++)
	{
		int ret;
		unsigned dummy;

		_starpu_drop_comments(f);
		ret = fscanf(f, "%d\t", &dummy);
		STARPU_ASSERT(ret == 1);

		STARPU_ASSERT(dummy == gpu);

		unsigned cpu;
		for (cpu = 0; cpu < ncpus; cpu++)
		{
			ret = fscanf(f, "%d\t", &opencl_affinity_matrix[gpu][cpu]);
			STARPU_ASSERT(ret == 1);
		}

		ret = fscanf(f, "\n");
		STARPU_ASSERT(ret == 0);
	}
#endif /* !STARPU_USE_OPENCL */
	_starpu_frdunlock(f);

	fclose(f);
#endif /* !(STARPU_USE_CUDA_ || STARPU_USE_OPENCL */

}

#ifndef STARPU_SIMGRID
static void write_bus_affinity_file_content(void)
{
	STARPU_ASSERT(was_benchmarked);

#if defined(STARPU_USE_CUDA) || defined(STARPU_USE_OPENCL)
	FILE *f;
	char path[256];
	get_affinity_path(path, sizeof(path));

	_STARPU_DEBUG("writing affinities to %s\n", path);

	f = fopen(path, "w+");
	if (!f)
	{
		perror("fopen write_buf_affinity_file_content");
		_STARPU_DISP("path '%s'\n", path);
		fflush(stderr);
		STARPU_ABORT();
	}

	_starpu_frdlock(f);
	unsigned cpu;
        unsigned gpu;

        fprintf(f, "# GPU\t");
	for (cpu = 0; cpu < ncpus; cpu++)
		fprintf(f, "CPU%u\t", cpu);
	fprintf(f, "\n");

#ifdef STARPU_USE_CUDA
	for (gpu = 0; gpu < ncuda; gpu++)
	{
		fprintf(f, "%d\t", gpu);

		for (cpu = 0; cpu < ncpus; cpu++)
		{
			fprintf(f, "%d\t", cudadev_timing_per_cpu[(gpu+1)*STARPU_MAXCPUS+cpu].cpu_id);
		}

		fprintf(f, "\n");
	}
#endif
#ifdef STARPU_USE_OPENCL
	for (gpu = 0; gpu < nopencl; gpu++)
	{
		fprintf(f, "%d\t", gpu);

		for (cpu = 0; cpu < ncpus; cpu++)
		{
                        fprintf(f, "%d\t", opencldev_timing_per_cpu[(gpu+1)*STARPU_MAXCPUS+cpu].cpu_id);
		}

		fprintf(f, "\n");
	}
#endif

	_starpu_frdunlock(f);
	fclose(f);
#endif
}
#endif /* STARPU_SIMGRID */

static void generate_bus_affinity_file(void)
{
	if (!was_benchmarked)
		benchmark_all_gpu_devices();

	write_bus_affinity_file_content();
}

static void load_bus_affinity_file(void)
{
	int res;

	char path[256];
	get_affinity_path(path, sizeof(path));

	res = access(path, F_OK);
	if (res)
	{
		/* File does not exist yet */
		generate_bus_affinity_file();
	}

	load_bus_affinity_file_content();
}

#ifdef STARPU_USE_CUDA
int *_starpu_get_cuda_affinity_vector(unsigned gpuid)
{
        return cuda_affinity_matrix[gpuid];
}
#endif /* STARPU_USE_CUDA */

#ifdef STARPU_USE_OPENCL
int *_starpu_get_opencl_affinity_vector(unsigned gpuid)
{
        return opencl_affinity_matrix[gpuid];
}
#endif /* STARPU_USE_OPENCL */

void starpu_bus_print_affinity(FILE *f)
{
#if defined(STARPU_USE_CUDA) || defined(STARPU_USE_OPENCL)
	unsigned cpu;
	unsigned gpu;
#endif

	fprintf(f, "# GPU\tCPU in preference order (logical index)\n");

#ifdef STARPU_USE_CUDA
	fprintf(f, "# CUDA\n");
	for(gpu = 0 ; gpu<ncuda ; gpu++)
	{
		fprintf(f, "%d\t", gpu);
		for (cpu = 0; cpu < ncpus; cpu++)
		{
			fprintf(f, "%d\t", cuda_affinity_matrix[gpu][cpu]);
		}
		fprintf(f, "\n");
	}
#endif
#ifdef STARPU_USE_OPENCL
	fprintf(f, "# OpenCL\n");
	for(gpu = 0 ; gpu<nopencl ; gpu++)
	{
		fprintf(f, "%d\t", gpu);
		for (cpu = 0; cpu < ncpus; cpu++)
		{
			fprintf(f, "%d\t", opencl_affinity_matrix[gpu][cpu]);
		}
		fprintf(f, "\n");
	}
#endif
}
#endif /* STARPU_SIMGRID */

/*
 *	Latency
 */

static void get_latency_path(char *path, size_t maxlen)
{
	get_bus_path("latency", path, maxlen);
}

static int load_bus_latency_file_content(void)
{
	int n;
	unsigned src, dst;
	FILE *f;
	double latency;

	char path[256];
	get_latency_path(path, sizeof(path));

	_STARPU_DEBUG("loading latencies from %s\n", path);

	f = fopen(path, "r");
	if (!f)
	{
		perror("fopen load_bus_latency_file_content");
		_STARPU_DISP("path '%s'\n", path);
		fflush(stderr);
		STARPU_ABORT();
	}
	_starpu_frdlock(f);

	for (src = 0; src < STARPU_MAXNODES; src++)
	{
		_starpu_drop_comments(f);
		for (dst = 0; dst < STARPU_MAXNODES; dst++)
		{
			n = _starpu_read_double(f, "%lf", &latency);
			if (n != 1)
			{
				_STARPU_DISP("Error while reading latency file <%s>. Expected a number\n", path);
				fclose(f);
				return 0;
			}
			n = getc(f);
			if (n == '\n')
				break;
			if (n != '\t')
			{
				_STARPU_DISP("bogus character %c in latency file %s\n", n, path);
				fclose(f);
				return 0;
			}

			latency_matrix[src][dst] = latency;

			/* Look out for \t\n */
			n = getc(f);
			if (n == '\n')
				break;
			ungetc(n, f);
			n = '\t';
		}

		/* No more values, take NAN */
		for ( ; dst < STARPU_MAXNODES; dst++)
			latency_matrix[src][dst] = NAN;

		while (n == '\t')
		{
			/* Look out for \t\n */
			n = getc(f);
			if (n == '\n')
				break;
			ungetc(n, f);

			n = _starpu_read_double(f, "%lf", &latency);
			if (n && !isnan(latency))
			{
				_STARPU_DISP("Too many nodes in latency file %s for this configuration (%d)\n", path, STARPU_MAXNODES);
				fclose(f);
				return 0;
			}
			n = getc(f);
		}
		if (n != '\n')
		{
			_STARPU_DISP("Bogus character %c in latency file %s\n", n, path);
			fclose(f);
			return 0;
		}

		/* Look out for EOF */
		n = getc(f);
		if (n == EOF)
			break;
		ungetc(n, f);
	}
	_starpu_frdunlock(f);
	fclose(f);

	/* No more values, take NAN */
	for ( ; src < STARPU_MAXNODES; src++)
		for (dst = 0; dst < STARPU_MAXNODES; dst++)
			latency_matrix[src][dst] = NAN;

	return 1;
}

#ifndef STARPU_SIMGRID
static void write_bus_latency_file_content(void)
{
        unsigned src, dst, maxnode;
	FILE *f;

	STARPU_ASSERT(was_benchmarked);

	char path[256];
	get_latency_path(path, sizeof(path));

	_STARPU_DEBUG("writing latencies to %s\n", path);

	f = fopen(path, "w+");
	if (!f)
	{
		perror("fopen write_bus_latency_file_content");
		_STARPU_DISP("path '%s'\n", path);
		fflush(stderr);
		STARPU_ABORT();
	}
	_starpu_fwrlock(f);
	_starpu_ftruncate(f);

	fprintf(f, "# ");
	for (dst = 0; dst < STARPU_MAXNODES; dst++)
		fprintf(f, "to %d\t\t", dst);
	fprintf(f, "\n");

        maxnode = ncuda;
#ifdef STARPU_USE_OPENCL
        maxnode += nopencl;
#endif
#ifdef STARPU_USE_MIC
        maxnode += nmic;
#endif
        for (src = 0; src < STARPU_MAXNODES; src++)
	{
		for (dst = 0; dst < STARPU_MAXNODES; dst++)
		{
			double latency = 0.0;

			if ((src > maxnode) || (dst > maxnode))
			{
				/* convention */
				latency = NAN;
			}
			else if (src == dst)
			{
				latency = 0.0;
			}
			else
			{
				/* µs */
#ifdef STARPU_USE_CUDA
#ifdef HAVE_CUDA_MEMCPY_PEER
				if (src && src < ncuda && dst && dst <= ncuda)
					latency = cudadev_latency_dtod[src][dst];
				else
#endif
				{
					if (src && src <= ncuda)
						latency += cudadev_latency_dtoh[src];
					if (dst && dst <= ncuda)
						latency += cudadev_latency_htod[dst];
				}
#endif
#ifdef STARPU_USE_OPENCL
				if (src > ncuda)
					latency += opencldev_latency_dtoh[src-ncuda];
				if (dst > ncuda)
					latency += opencldev_latency_htod[dst-ncuda];
#endif
			}

			if (dst)
				fputc('\t', f);
			fprintf(f, "%lf", latency);
		}

		fprintf(f, "\n");
	}
	_starpu_fwrunlock(f);

	fclose(f);
}
#endif

static void generate_bus_latency_file(void)
{
	if (!was_benchmarked)
		benchmark_all_gpu_devices();

#ifndef STARPU_SIMGRID
	write_bus_latency_file_content();
#endif
}

static void load_bus_latency_file(void)
{
	int res;

	char path[256];
	get_latency_path(path, sizeof(path));

	res = access(path, F_OK);
	if (res || !load_bus_latency_file_content())
	{
		/* File does not exist yet or is bogus */
		generate_bus_latency_file();
	}

}


/*
 *	Bandwidth
 */
static void get_bandwidth_path(char *path, size_t maxlen)
{
	get_bus_path("bandwidth", path, maxlen);
}

static int load_bus_bandwidth_file_content(void)
{
	int n;
	unsigned src, dst;
	FILE *f;
	double bandwidth;

	char path[256];
	get_bandwidth_path(path, sizeof(path));

	_STARPU_DEBUG("loading bandwidth from %s\n", path);

	f = fopen(path, "r");
	if (!f)
	{
		perror("fopen load_bus_bandwidth_file_content");
		_STARPU_DISP("path '%s'\n", path);
		fflush(stderr);
		STARPU_ABORT();
	}
	_starpu_frdlock(f);

	for (src = 0; src < STARPU_MAXNODES; src++)
	{
		_starpu_drop_comments(f);
		for (dst = 0; dst < STARPU_MAXNODES; dst++)
		{
			n = _starpu_read_double(f, "%lf", &bandwidth);
			if (n != 1)
			{
				_STARPU_DISP("Error while reading bandwidth file <%s>. Expected a number\n", path);
				fclose(f);
				return 0;
			}
			n = getc(f);
			if (n == '\n')
				break;
			if (n != '\t')
			{
				_STARPU_DISP("bogus character %c in bandwidth file %s\n", n, path);
				fclose(f);
				return 0;
			}

			bandwidth_matrix[src][dst] = bandwidth;

			/* Look out for \t\n */
			n = getc(f);
			if (n == '\n')
				break;
			ungetc(n, f);
			n = '\t';
		}

		/* No more values, take NAN */
		for ( ; dst < STARPU_MAXNODES; dst++)
			bandwidth_matrix[src][dst] = NAN;

		while (n == '\t')
		{
			/* Look out for \t\n */
			n = getc(f);
			if (n == '\n')
				break;
			ungetc(n, f);

			n = _starpu_read_double(f, "%lf", &bandwidth);
			if (n && !isnan(bandwidth))
			{
				_STARPU_DISP("Too many nodes in bandwidth file %s for this configuration (%d)\n", path, STARPU_MAXNODES);
				fclose(f);
				return 0;
			}
			n = getc(f);
		}
		if (n != '\n')
		{
			_STARPU_DISP("Bogus character %c in bandwidth file %s\n", n, path);
			fclose(f);
			return 0;
		}

		/* Look out for EOF */
		n = getc(f);
		if (n == EOF)
			break;
		ungetc(n, f);
	}
	_starpu_frdunlock(f);
	fclose(f);

	/* No more values, take NAN */
	for ( ; src < STARPU_MAXNODES; src++)
		for (dst = 0; dst < STARPU_MAXNODES; dst++)
			latency_matrix[src][dst] = NAN;

	return 1;
}

#ifndef STARPU_SIMGRID
static void write_bus_bandwidth_file_content(void)
{
	unsigned src, dst, maxnode;
	FILE *f;

	STARPU_ASSERT(was_benchmarked);

	char path[256];
	get_bandwidth_path(path, sizeof(path));

	_STARPU_DEBUG("writing bandwidth to %s\n", path);

	f = fopen(path, "w+");
	STARPU_ASSERT(f);

	_starpu_fwrlock(f);
	_starpu_ftruncate(f);

	fprintf(f, "# ");
	for (dst = 0; dst < STARPU_MAXNODES; dst++)
		fprintf(f, "to %d\t\t", dst);
	fprintf(f, "\n");

        maxnode = ncuda;
#ifdef STARPU_USE_OPENCL
        maxnode += nopencl;
#endif
#ifdef STARPU_USE_MIC
        maxnode += nmic;
#endif
	for (src = 0; src < STARPU_MAXNODES; src++)
	{
		for (dst = 0; dst < STARPU_MAXNODES; dst++)
		{
			double bandwidth;

			if ((src > maxnode) || (dst > maxnode))
			{
				bandwidth = NAN;
			}
#if defined(STARPU_USE_CUDA) || defined(STARPU_USE_OPENCL) || defined(STARPU_USE_MIC)
			else if (src != dst)
			{
				double slowness = 0.0;
				/* Total bandwidth is the harmonic mean of bandwidths */
#ifdef STARPU_USE_CUDA
#ifdef HAVE_CUDA_MEMCPY_PEER
				if (src && src <= ncuda && dst && dst <= ncuda)
					/* Direct GPU-GPU transfert */
					slowness = cudadev_timing_dtod[src][dst];
				else
#endif
				{
					if (src && src <= ncuda)
						slowness += cudadev_timing_dtoh[src];
					if (dst && dst <= ncuda)
						slowness += cudadev_timing_htod[dst];
				}
#endif
				/* TODO: generalize computation */
#ifdef STARPU_USE_OPENCL
				if (src > ncuda && src <= ncuda + nopencl)
					slowness += opencldev_timing_dtoh[src-ncuda];
				if (dst > ncuda && dst <= ncuda + nopencl)
					slowness += opencldev_timing_htod[dst-ncuda];
#endif
#ifdef STARPU_USE_MIC
				if (src > ncuda + nopencl)
					slowness += mic_time_device_to_host[src - (ncuda + nopencl)];
				if (dst > ncuda + nopencl)
					slowness += mic_time_host_to_device[dst - (ncuda + nopencl)];
#endif
				bandwidth = 1.0/slowness;
			}
#endif
			else
			{
			        /* convention */
			        bandwidth = 0.0;
			}

			if (dst)
				fputc('\t', f);
			fprintf(f, "%f", bandwidth);
		}

		fprintf(f, "\n");
	}

	_starpu_fwrunlock(f);
	fclose(f);
}
#endif /* STARPU_SIMGRID */

void starpu_bus_print_bandwidth(FILE *f)
{
	unsigned src, dst, maxnode;

        maxnode = ncuda;
#ifdef STARPU_USE_OPENCL
        maxnode += nopencl;
#endif
#ifdef STARPU_USE_MIC
        maxnode += nmic;
#endif

	fprintf(f, "from/to\t");
	fprintf(f, "RAM\t");
	for (dst = 0; dst < ncuda; dst++)
		fprintf(f, "CUDA %d\t", dst);
	for (dst = 0; dst < nopencl; dst++)
		fprintf(f, "OpenCL%d\t", dst);
	for (dst = 0; dst < nmic; dst++)
		fprintf(f, "MIC%d\t", dst);
	fprintf(f, "\n");

	for (src = 0; src <= maxnode; src++)
	{
		if (!src)
			fprintf(f, "RAM\t");
		else if (src <= ncuda)
			fprintf(f, "CUDA %d\t", src-1);
		else if (src <= ncuda + nopencl)
			fprintf(f, "OpenCL%d\t", src-ncuda-1);
		else
			fprintf(f, "MIC%d\t", src-ncuda-nopencl-1);
		for (dst = 0; dst <= maxnode; dst++)
			fprintf(f, "%.0f\t", bandwidth_matrix[src][dst]);

		fprintf(f, "\n");
	}
	fprintf(f, "\n");

	for (src = 0; src <= maxnode; src++)
	{
		if (!src)
			fprintf(f, "RAM\t");
		else if (src <= ncuda)
			fprintf(f, "CUDA %d\t", src-1);
		else if (src <= ncuda + nopencl)
			fprintf(f, "OpenCL%d\t", src-ncuda-1);
		else
			fprintf(f, "MIC%d\t", src-ncuda-nopencl-1);
		for (dst = 0; dst <= maxnode; dst++)
			fprintf(f, "%.0f\t", latency_matrix[src][dst]);

		fprintf(f, "\n");
	}

#if defined(STARPU_USE_CUDA) || defined(STARPU_USE_OPENCL)
	if (ncuda != 0 || nopencl != 0)
		fprintf(f, "\nGPU\tCPU in preference order (logical index), host-to-device, device-to-host\n");
	for (src = 1; src <= ncuda + nopencl; src++)
	{
		struct dev_timing *timing;
		struct _starpu_machine_config *config = _starpu_get_machine_config();
		unsigned config_ncpus = _starpu_topology_get_nhwcpu(config);
		unsigned cpu;

#ifdef STARPU_USE_CUDA
		if (src <= ncuda)
		{
			fprintf(f, "CUDA %d\t", src-1);
			for (cpu = 0; cpu < config_ncpus; cpu++)
			{
				timing = &cudadev_timing_per_cpu[src*STARPU_MAXCPUS+cpu];
				if (timing->timing_htod)
					fprintf(f, "%2d %.0f %.0f\t", timing->cpu_id, 1/timing->timing_htod, 1/timing->timing_dtoh);
				else
					fprintf(f, "%2d\t", cuda_affinity_matrix[src-1][cpu]);
			}
		}
#ifdef STARPU_USE_OPENCL
		else
#endif
#endif
#ifdef STARPU_USE_OPENCL
		{
			fprintf(f, "OpenCL%d\t", src-ncuda-1);
			for (cpu = 0; cpu < config_ncpus; cpu++)
			{
				timing = &opencldev_timing_per_cpu[(src-ncuda)*STARPU_MAXCPUS+cpu];
				if (timing->timing_htod)
					fprintf(f, "%2d %.0f %.0f\t", timing->cpu_id, 1/timing->timing_htod, 1/timing->timing_dtoh);
				else
					fprintf(f, "%2d\t", opencl_affinity_matrix[src-1][cpu]);
			}
		}
#endif
		fprintf(f, "\n");
	}
#endif
}

static void generate_bus_bandwidth_file(void)
{
	if (!was_benchmarked)
		benchmark_all_gpu_devices();

#ifndef STARPU_SIMGRID
	write_bus_bandwidth_file_content();
#endif
}

static void load_bus_bandwidth_file(void)
{
	int res;

	char path[256];
	get_bandwidth_path(path, sizeof(path));

	res = access(path, F_OK);
	if (res || !load_bus_bandwidth_file_content())
	{
		/* File does not exist yet or is bogus */
		generate_bus_bandwidth_file();
	}
}

#ifndef STARPU_SIMGRID
/*
 *	Config
 */
static void get_config_path(char *path, size_t maxlen)
{
	get_bus_path("config", path, maxlen);
}

static void check_bus_config_file(void)
{
        int res;
        char path[256];
        struct _starpu_machine_config *config = _starpu_get_machine_config();

        get_config_path(path, sizeof(path));
        res = access(path, F_OK);
	if (res || config->conf->bus_calibrate > 0)
	{
		if (res)
			_STARPU_DISP("No performance model for the bus, calibrating...\n");
		_starpu_bus_force_sampling();
		if (res)
			_STARPU_DISP("... done\n");
        }
        else
	{
                FILE *f;
                int ret;
		unsigned read_cuda = -1, read_opencl = -1, read_mic = -1;
                unsigned read_cpus = -1;

                // Loading configuration from file
                f = fopen(path, "r");
                STARPU_ASSERT(f);
		_starpu_frdlock(f);
                _starpu_drop_comments(f);
                ret = fscanf(f, "%u\t", &read_cpus);
		STARPU_ASSERT(ret == 1);
                _starpu_drop_comments(f);
		ret = fscanf(f, "%d\t", &read_cuda);
		STARPU_ASSERT(ret == 1);
                _starpu_drop_comments(f);
		ret = fscanf(f, "%d\t", &read_opencl);
		STARPU_ASSERT(ret == 1);
                _starpu_drop_comments(f);
		ret = fscanf(f, "%d\t", &read_mic);
		if (ret == 0)
			read_mic = 0;
                _starpu_drop_comments(f);
		_starpu_frdunlock(f);
                fclose(f);

                // Loading current configuration
                ncpus = _starpu_topology_get_nhwcpu(config);
#ifdef STARPU_USE_CUDA
		ncuda = _starpu_get_cuda_device_count();
#endif
#ifdef STARPU_USE_OPENCL
                nopencl = _starpu_opencl_get_device_count();
#endif
#ifdef STARPU_USE_MIC
                nmic = _starpu_mic_src_get_device_count();
#endif /* STARPU_USE_MIC */

                // Checking if both configurations match
                if (read_cpus != ncpus)
		{
			_STARPU_DISP("Current configuration does not match the bus performance model (CPUS: (stored) %u != (current) %u), recalibrating...\n", read_cpus, ncpus);
                        _starpu_bus_force_sampling();
			_STARPU_DISP("... done\n");
                }
                else if (read_cuda != ncuda)
		{
                        _STARPU_DISP("Current configuration does not match the bus performance model (CUDA: (stored) %d != (current) %d), recalibrating...\n", read_cuda, ncuda);
                        _starpu_bus_force_sampling();
			_STARPU_DISP("... done\n");
                }
                else if (read_opencl != nopencl)
		{
                        _STARPU_DISP("Current configuration does not match the bus performance model (OpenCL: (stored) %d != (current) %d), recalibrating...\n", read_opencl, nopencl);
                        _starpu_bus_force_sampling();
			_STARPU_DISP("... done\n");
                }
                else if (read_mic != nmic)
		{
                        _STARPU_DISP("Current configuration does not match the bus performance model (MIC: (stored) %d != (current) %d), recalibrating...\n", read_mic, nmic);
                        _starpu_bus_force_sampling();
			_STARPU_DISP("... done\n");
                }
        }
}

static void write_bus_config_file_content(void)
{
	FILE *f;
	char path[256];

	STARPU_ASSERT(was_benchmarked);
        get_config_path(path, sizeof(path));

	_STARPU_DEBUG("writing config to %s\n", path);

        f = fopen(path, "w+");
	STARPU_ASSERT(f);
	_starpu_fwrlock(f);
	_starpu_ftruncate(f);

        fprintf(f, "# Current configuration\n");
        fprintf(f, "%u # Number of CPUs\n", ncpus);
        fprintf(f, "%d # Number of CUDA devices\n", ncuda);
        fprintf(f, "%d # Number of OpenCL devices\n", nopencl);
        fprintf(f, "%d # Number of MIC devices\n", nmic);

	_starpu_fwrunlock(f);
        fclose(f);
}

static void generate_bus_config_file(void)
{
	if (!was_benchmarked)
		benchmark_all_gpu_devices();

	write_bus_config_file_content();
}
#endif /* !SIMGRID */

void _starpu_simgrid_get_platform_path(char *path, size_t maxlen)
{
	get_bus_path("platform.xml", path, maxlen);
}

#ifndef STARPU_SIMGRID
/*
 * Compute the precise PCI tree bandwidth and link shares
 *
 * We only have measurements from one leaf to another. We assume that the
 * available bandwidth is greater at lower levels, and thus measurements from
 * increasingly far GPUs provide the PCI bridges bandwidths at each level.
 *
 * The bandwidth of a PCI bridge is thus computed as the maximum of the speed
 * of the various transfers that we have achieved through it.  We thus browse
 * the PCI tree three times:
 *
 * - first through all CUDA-CUDA possible transfers to compute the maximum
 *   measured bandwidth on each PCI link and hub used for that.
 * - then through the whole tree to emit links for each PCI link and hub.
 * - then through all CUDA-CUDA possible transfers again to emit routes.
 */

#if defined(STARPU_USE_CUDA) && HAVE_DECL_HWLOC_CUDA_GET_DEVICE_OSDEV_BY_INDEX && defined(HAVE_CUDA_MEMCPY_PEER)

/* Records, for each PCI link and hub, the maximum bandwidth seen through it */
struct pci_userdata
{
	/* Uplink max measurement */
	double bw_up;
	double bw_down;

	/* Hub max measurement */
	double bw;
};

/* Allocate a pci_userdata structure for the given object */
static void allocate_userdata(hwloc_obj_t obj)
{
	struct pci_userdata *data;

	if (obj->userdata)
		return;

	data = obj->userdata = malloc(sizeof(*data));
	data->bw_up = 0.0;
	data->bw_down = 0.0;
	data->bw = 0.0;
}

/* Update the maximum bandwidth seen going to upstream */
static void update_bandwidth_up(hwloc_obj_t obj, double bandwidth)
{
	struct pci_userdata *data;
	if (obj->type != HWLOC_OBJ_BRIDGE && obj->type != HWLOC_OBJ_PCI_DEVICE)
		return;
	allocate_userdata(obj);

	data = obj->userdata;
	if (data->bw_up < bandwidth)
		data->bw_up = bandwidth;
}

/* Update the maximum bandwidth seen going from upstream */
static void update_bandwidth_down(hwloc_obj_t obj, double bandwidth)
{
	struct pci_userdata *data;
	if (obj->type != HWLOC_OBJ_BRIDGE && obj->type != HWLOC_OBJ_PCI_DEVICE)
		return;
	allocate_userdata(obj);

	data = obj->userdata;
	if (data->bw_down < bandwidth)
		data->bw_down = bandwidth;
}

/* Update the maximum bandwidth seen going through this Hub */
static void update_bandwidth_through(hwloc_obj_t obj, double bandwidth)
{
	struct pci_userdata *data;
	allocate_userdata(obj);

	data = obj->userdata;
	if (data->bw < bandwidth)
		data->bw = bandwidth;
}

/* find_* functions perform the first step: computing maximum bandwidths */

/* Our trafic had to go through the host, go back from target up to the host,
 * updating uplink downstream bandwidth along the way */
static void find_platform_backward_path(hwloc_obj_t obj, double bandwidth)
{
	if (!obj)
		/* Oops, we should have seen a host bridge. Well, too bad. */
		return;

	/* Update uplink bandwidth of PCI Hub */
	update_bandwidth_down(obj, bandwidth);
	/* Update internal bandwidth of PCI Hub */
	update_bandwidth_through(obj, bandwidth);

	if (obj->type == HWLOC_OBJ_BRIDGE && obj->attr->bridge.upstream_type == HWLOC_OBJ_BRIDGE_HOST)
		/* Finished */
		return;

	/* Continue up */
	find_platform_backward_path(obj->parent, bandwidth);
}
/* Same, but update uplink upstream bandwidth */
static void find_platform_forward_path(hwloc_obj_t obj, double bandwidth)
{
	if (!obj)
		/* Oops, we should have seen a host bridge. Well, too bad. */
		return;

	/* Update uplink bandwidth of PCI Hub */
	update_bandwidth_up(obj, bandwidth);
	/* Update internal bandwidth of PCI Hub */
	update_bandwidth_through(obj, bandwidth);

	if (obj->type == HWLOC_OBJ_BRIDGE && obj->attr->bridge.upstream_type == HWLOC_OBJ_BRIDGE_HOST)
		/* Finished */
		return;

	/* Continue up */
	find_platform_forward_path(obj->parent, bandwidth);
}

/* Find the path from obj1 through parent down to obj2 (without ever going up),
 * and update the maximum bandwidth along the path */
static int find_platform_path_down(hwloc_obj_t parent, hwloc_obj_t obj1, hwloc_obj_t obj2, double bandwidth)
{
	unsigned i;

	/* Base case, path is empty */
	if (parent == obj2)
		return 1;

	/* Try to go down from parent */
	for (i = 0; i < parent->arity; i++)
		if (parent->children[i] != obj1 && find_platform_path_down(parent->children[i], NULL, obj2, bandwidth))
		{
			/* Found it down there, update bandwidth of parent */
			update_bandwidth_down(parent->children[i], bandwidth);
			update_bandwidth_through(parent, bandwidth);
			return 1;
		}
	return 0;
}

/* Find the path from obj1 to obj2, and update the maximum bandwidth along the
 * path */
static int find_platform_path_up(hwloc_obj_t obj1, hwloc_obj_t obj2, double bandwidth)
{
	int ret;
	hwloc_obj_t parent = obj1->parent;

	if (!parent)
	{
		/* Oops, we should have seen a host bridge. Act as if we had seen it.  */
		find_platform_backward_path(obj2, bandwidth);
		return 1;
	}

	if (find_platform_path_down(parent, obj1, obj2, bandwidth))
		/* obj2 was a mere (sub)child of our parent */
		return 1;

	/* obj2 is not a (sub)child of our parent, we have to go up through the parent */
	if (parent->type == HWLOC_OBJ_BRIDGE && parent->attr->bridge.upstream_type == HWLOC_OBJ_BRIDGE_HOST)
	{
		/* We have to go up to the Host, so obj2 is not in the same PCI
		 * tree, so we're for for obj1 to Host, and just find the path
		 * from obj2 to Host too.
		 */
		find_platform_backward_path(obj2, bandwidth);

		update_bandwidth_up(parent, bandwidth);
		update_bandwidth_through(parent, bandwidth);

		return 1;
	}

	/* Not at host yet, just go up */
	ret = find_platform_path_up(parent, obj2, bandwidth);
	update_bandwidth_up(parent, bandwidth);
	update_bandwidth_through(parent, bandwidth);
	return ret;
}

/* find the path between cuda i and cuda j, and update the maximum bandwidth along the path */
static int find_platform_cuda_path(hwloc_topology_t topology, unsigned i, unsigned j, double bandwidth)
{
	hwloc_obj_t cudai, cudaj;
	cudai = hwloc_cuda_get_device_osdev_by_index(topology, i);
	cudaj = hwloc_cuda_get_device_osdev_by_index(topology, j);

	if (!cudai || !cudaj)
		return 0;

	return find_platform_path_up(cudai, cudaj, bandwidth);
}

/* emit_topology_bandwidths performs the second step: emitting link names */

/* Emit the link name of the object */
static void emit_pci_hub(FILE *f, hwloc_obj_t obj)
{
	STARPU_ASSERT(obj->type == HWLOC_OBJ_BRIDGE);
	fprintf(f, "PCI:%04x:[%02x-%02x]", obj->attr->bridge.downstream.pci.domain, obj->attr->bridge.downstream.pci.secondary_bus, obj->attr->bridge.downstream.pci.subordinate_bus);
}

static void emit_pci_dev(FILE *f, struct hwloc_pcidev_attr_s *pcidev)
{
	fprintf(f, "PCI:%04x:%02x:%02x.%1x", pcidev->domain, pcidev->bus, pcidev->dev, pcidev->func);
}

/* Emit the links of the object */
static void emit_topology_bandwidths(FILE *f, hwloc_obj_t obj)
{
	unsigned i;
	if (obj->userdata)
	{
		struct pci_userdata *data = obj->userdata;

		if (obj->type == HWLOC_OBJ_BRIDGE)
		{
			/* Uplink */
			fprintf(f, "   <link id='");
			emit_pci_hub(f, obj);
			fprintf(f, " up' bandwidth='%f' latency='0.000000'/>\n", data->bw_up);
			fprintf(f, "   <link id='");
			emit_pci_hub(f, obj);
			fprintf(f, " down' bandwidth='%f' latency='0.000000'/>\n", data->bw_down);

			/* PCI Switches are assumed to have infinite internal bandwidth */
			if (!obj->name || !strstr(obj->name, "Switch"))
			{
				/* We assume that PCI Hubs have double bandwidth in
				 * order to support full duplex but not more */
				fprintf(f, "   <link id='");
				emit_pci_hub(f, obj);
				fprintf(f, " through' bandwidth='%f' latency='0.000000'/>\n", data->bw * 2);
			}
		}
		else if (obj->type == HWLOC_OBJ_PCI_DEVICE)
		{
			fprintf(f, "   <link id='");
			emit_pci_dev(f, &obj->attr->pcidev);
			fprintf(f, " up' bandwidth='%f' latency='0.000000'/>\n", data->bw_up);
			fprintf(f, "   <link id='");
			emit_pci_dev(f, &obj->attr->pcidev);
			fprintf(f, " down' bandwidth='%f' latency='0.000000'/>\n", data->bw_down);
		}
	}

	for (i = 0; i < obj->arity; i++)
		emit_topology_bandwidths(f, obj->children[i]);
}

/* emit_pci_link_* functions perform the third step: emitting the routes */

static void emit_pci_link(FILE *f, hwloc_obj_t obj, const char *suffix)
{
	if (obj->type == HWLOC_OBJ_BRIDGE)
	{
		fprintf(f, "    <link_ctn id='");
		emit_pci_hub(f, obj);
		fprintf(f, " %s'/>\n", suffix);
	}
	else if (obj->type == HWLOC_OBJ_PCI_DEVICE)
	{
		fprintf(f, "    <link_ctn id='");
		emit_pci_dev(f, &obj->attr->pcidev);
		fprintf(f, " %s'/>\n", suffix);
	}
}

/* Go to upstream */
static void emit_pci_link_up(FILE *f, hwloc_obj_t obj)
{
	emit_pci_link(f, obj, "up");
}

/* Go from upstream */
static void emit_pci_link_down(FILE *f, hwloc_obj_t obj)
{
	emit_pci_link(f, obj, "down");
}

/* Go through PCI hub */
static void emit_pci_link_through(FILE *f, hwloc_obj_t obj)
{
	/* We don't care about trafic going through PCI switches */
	if (obj->type == HWLOC_OBJ_BRIDGE)
	{
		if (!obj->name || !strstr(obj->name, "Switch"))
			emit_pci_link(f, obj, "through");
		else
		{
			fprintf(f, "    <!--   Switch ");
			emit_pci_hub(f, obj);
			fprintf(f, " through -->\n");
		}
	}
}

/* Our trafic has to go through the host, go back from target up to the host,
 * using uplink downstream along the way */
static void emit_platform_backward_path(FILE *f, hwloc_obj_t obj)
{
	if (!obj)
		/* Oops, we should have seen a host bridge. Well, too bad. */
		return;

	/* Go through PCI Hub */
	emit_pci_link_through(f, obj);
	/* Go through uplink */
	emit_pci_link_down(f, obj);

	if (obj->type == HWLOC_OBJ_BRIDGE && obj->attr->bridge.upstream_type == HWLOC_OBJ_BRIDGE_HOST)
	{
		/* Finished, go through host */
		fprintf(f, "    <link_ctn id='Host'/>\n");
		return;
	}

	/* Continue up */
	emit_platform_backward_path(f, obj->parent);
}
/* Same, but use upstream link */
static void emit_platform_forward_path(FILE *f, hwloc_obj_t obj)
{
	if (!obj)
		/* Oops, we should have seen a host bridge. Well, too bad. */
		return;

	/* Go through PCI Hub */
	emit_pci_link_through(f, obj);
	/* Go through uplink */
	emit_pci_link_up(f, obj);

	if (obj->type == HWLOC_OBJ_BRIDGE && obj->attr->bridge.upstream_type == HWLOC_OBJ_BRIDGE_HOST)
	{
		/* Finished, go through host */
		fprintf(f, "    <link_ctn id='Host'/>\n");
		return;
	}

	/* Continue up */
	emit_platform_forward_path(f, obj->parent);
}

/* Find the path from obj1 through parent down to obj2 (without ever going up),
 * and use the links along the path */
static int emit_platform_path_down(FILE *f, hwloc_obj_t parent, hwloc_obj_t obj1, hwloc_obj_t obj2)
{
	unsigned i;

	/* Base case, path is empty */
	if (parent == obj2)
		return 1;

	/* Try to go down from parent */
	for (i = 0; i < parent->arity; i++)
		if (parent->children[i] != obj1 && emit_platform_path_down(f, parent->children[i], NULL, obj2))
		{
			/* Found it down there, path goes through this hub */
			emit_pci_link_down(f, parent->children[i]);
			emit_pci_link_through(f, parent);
			return 1;
		}
	return 0;
}

/* Find the path from obj1 to obj2, and use the links along the path */
static int emit_platform_path_up(FILE *f, hwloc_obj_t obj1, hwloc_obj_t obj2)
{
	int ret;
	hwloc_obj_t parent = obj1->parent;

	if (!parent)
	{
		/* Oops, we should have seen a host bridge. Act as if we had seen it.  */
		emit_platform_backward_path(f, obj2);
		return 1;
	}

	if (emit_platform_path_down(f, parent, obj1, obj2))
		/* obj2 was a mere (sub)child of our parent */
		return 1;

	/* obj2 is not a (sub)child of our parent, we have to go up through the parent */
	if (parent->type == HWLOC_OBJ_BRIDGE && parent->attr->bridge.upstream_type == HWLOC_OBJ_BRIDGE_HOST)
	{
		/* We have to go up to the Host, so obj2 is not in the same PCI
		 * tree, so we're for for obj1 to Host, and just find the path
		 * from obj2 to Host too.
		 */
		emit_platform_backward_path(f, obj2);
		fprintf(f, "    <link_ctn id='Host'/>\n");

		emit_pci_link_up(f, parent);
		emit_pci_link_through(f, parent);

		return 1;
	}

	/* Not at host yet, just go up */
	ret = emit_platform_path_up(f, parent, obj2);
	emit_pci_link_up(f, parent);
	emit_pci_link_through(f, parent);
	return ret;
}

/* Clean our mess in the topology before destroying it */
static void clean_topology(hwloc_obj_t obj)
{
	unsigned i;
	if (obj->userdata)
		free(obj->userdata);
	for (i = 0; i < obj->arity; i++)
		clean_topology(obj->children[i]);
}
#endif

static void write_bus_platform_file_content(void)
{
	FILE *f;
	char path[256];
	unsigned i;

	STARPU_ASSERT(was_benchmarked);

	_starpu_simgrid_get_platform_path(path, sizeof(path));

	_STARPU_DEBUG("writing platform to %s\n", path);

	f = fopen(path, "w+");
	if (!f)
	{
		perror("fopen write_bus_platform_file_content");
		_STARPU_DISP("path '%s'\n", path);
		fflush(stderr);
		STARPU_ABORT();
	}
	_starpu_fwrlock(f);
	_starpu_ftruncate(f);

	fprintf(f,
"<?xml version='1.0'?>\n"
" <!DOCTYPE platform SYSTEM 'http://simgrid.gforge.inria.fr/simgrid.dtd'>\n"
" <platform version='3'>\n"
" <config id='General'>\n"
"   <prop id='network/TCP_gamma' value='-1'></prop>\n"
"   <prop id='network/latency_factor' value='1'></prop>\n"
"   <prop id='network/bandwidth_factor' value='1'></prop>\n"
" </config>\n"
" <AS  id='AS0'  routing='Full'>\n"
"   <host id='MAIN' power='1'/>\n"
		);

	for (i = 0; i < ncpus; i++)
		/* TODO: host memory for out-of-core simulation */
		fprintf(f, "   <host id='CPU%d' power='2000000000'/>\n", i);

	for (i = 0; i < ncuda; i++)
	{
		fprintf(f, "   <host id='CUDA%d' power='2000000000'>\n", i);
		fprintf(f, "     <prop id='memsize' value='%llu'/>\n", (unsigned long long) cuda_size[i]);
#ifdef HAVE_CUDA_MEMCPY_PEER
		fprintf(f, "     <prop id='memcpy_peer' value='1'/>\n");
#endif
		fprintf(f, "   </host>\n");
	}

	for (i = 0; i < nopencl; i++)
	{
		fprintf(f, "   <host id='OpenCL%d' power='2000000000'>\n", i);
		fprintf(f, "     <prop id='memsize' value='%llu'/>\n", (unsigned long long) opencl_size[i]);
		fprintf(f, "   </host>\n");
	}

	fprintf(f, "\n   <host id='RAM' power='1'/>\n");

	/*
	 * Compute maximum bandwidth, taken as host bandwidth
	 */
	double max_bandwidth = 0;
#ifdef STARPU_USE_CUDA
	for (i = 0; i < ncuda; i++)
	{
		double down_bw = 1.0 / cudadev_timing_dtoh[1+i];
		double up_bw = 1.0 / cudadev_timing_htod[1+i];
		if (max_bandwidth < down_bw)
			max_bandwidth = down_bw;
		if (max_bandwidth < up_bw)
			max_bandwidth = up_bw;
	}
#endif
#ifdef STARPU_USE_OPENCL
	for (i = 0; i < nopencl; i++)
	{
		double down_bw = 1.0 / opencldev_timing_dtoh[1+i];
		double up_bw = 1.0 / opencldev_timing_htod[1+i];
		if (max_bandwidth < down_bw)
			max_bandwidth = down_bw;
		if (max_bandwidth < up_bw)
			max_bandwidth = up_bw;
	}
#endif
	fprintf(f, "\n   <link id='Host' bandwidth='%f' latency='0.000000'/>\n\n", max_bandwidth*1000000);

	/*
	 * OpenCL links
	 */

#ifdef STARPU_USE_OPENCL
	for (i = 0; i < nopencl; i++)
	{
		char i_name[16];
		snprintf(i_name, sizeof(i_name), "OpenCL%d", i);
		fprintf(f, "   <link id='RAM-%s' bandwidth='%f' latency='%f'/>\n",
			i_name,
			1000000 / opencldev_timing_htod[1+i],
			opencldev_latency_htod[1+i]/1000000.);
		fprintf(f, "   <link id='%s-RAM' bandwidth='%f' latency='%f'/>\n",
			i_name,
			1000000 / opencldev_timing_dtoh[1+i],
			opencldev_latency_dtoh[1+i]/1000000.);
	}
	fprintf(f, "\n");
#endif

	/*
	 * CUDA links and routes
	 */

#ifdef STARPU_USE_CUDA
	/* Write RAM/CUDA bandwidths and latencies */
	for (i = 0; i < ncuda; i++)
	{
		char i_name[16];
		snprintf(i_name, sizeof(i_name), "CUDA%d", i);
		fprintf(f, "   <link id='RAM-%s' bandwidth='%f' latency='%f'/>\n",
			i_name,
			1000000. / cudadev_timing_htod[1+i],
			cudadev_latency_htod[1+i]/1000000.);
		fprintf(f, "   <link id='%s-RAM' bandwidth='%f' latency='%f'/>\n",
			i_name,
			1000000. / cudadev_timing_dtoh[1+i],
			cudadev_latency_dtoh[1+i]/1000000.);
	}
	fprintf(f, "\n");
#ifdef HAVE_CUDA_MEMCPY_PEER
	/* Write CUDA/CUDA bandwidths and latencies */
	for (i = 0; i < ncuda; i++)
	{
		unsigned j;
		char i_name[16];
		snprintf(i_name, sizeof(i_name), "CUDA%d", i);
		for (j = 0; j < ncuda; j++)
		{
			char j_name[16];
			if (j == i)
				continue;
			snprintf(j_name, sizeof(j_name), "CUDA%d", j);
			fprintf(f, "   <link id='%s-%s' bandwidth='%f' latency='%f'/>\n",
				i_name, j_name,
				1000000. / cudadev_timing_dtod[1+i][1+j],
				cudadev_latency_dtod[1+i][1+j]/1000000.);
		}
	}
#endif

#if HAVE_DECL_HWLOC_CUDA_GET_DEVICE_OSDEV_BY_INDEX && defined(HAVE_CUDA_MEMCPY_PEER)
	/* If we have enough hwloc information, write PCI bandwidths and routes */
	if (!starpu_get_env_number_default("STARPU_PCI_FLAT", 0))
	{
		hwloc_topology_t topology;
		hwloc_topology_init(&topology);
		hwloc_topology_set_flags(topology, HWLOC_TOPOLOGY_FLAG_IO_DEVICES | HWLOC_TOPOLOGY_FLAG_IO_BRIDGES);
		hwloc_topology_load(topology);

		/* First find paths and record measured bandwidth along the path */
		for (i = 0; i < ncuda; i++)
		{
			unsigned j;
			for (j = 0; j < ncuda; j++)
				if (i != j)
					if (!find_platform_cuda_path(topology, i, j, 1000000. / cudadev_timing_dtod[1+i][1+j]))
					{
						clean_topology(hwloc_get_root_obj(topology));
						hwloc_topology_destroy(topology);
						goto flat_cuda;
					}
			/* Record RAM/CUDA bandwidths */
			find_platform_forward_path(hwloc_cuda_get_device_osdev_by_index(topology, i), 1000000. / cudadev_timing_dtoh[1+i]);
			find_platform_backward_path(hwloc_cuda_get_device_osdev_by_index(topology, i), 1000000. / cudadev_timing_htod[1+i]);
		}

		/* Ok, found path in all cases, can emit advanced platform routes */
		fprintf(f, "\n");
		emit_topology_bandwidths(f, hwloc_get_root_obj(topology));
		fprintf(f, "\n");
		for (i = 0; i < ncuda; i++)
		{
			unsigned j;
			for (j = 0; j < ncuda; j++)
				if (i != j)
				{
					fprintf(f, "   <route src='CUDA%u' dst='CUDA%u' symmetrical='NO'>\n", i, j);
					fprintf(f, "    <link_ctn id='CUDA%d-CUDA%d'/>\n", i, j);
					emit_platform_path_up(f,
						hwloc_cuda_get_device_osdev_by_index(topology, i),
						hwloc_cuda_get_device_osdev_by_index(topology, j));
					fprintf(f, "   </route>\n");
				}

			fprintf(f, "   <route src='CUDA%d' dst='RAM' symmetrical='NO'>\n", i);
			fprintf(f, "    <link_ctn id='CUDA%d-RAM'/>\n", i);
			emit_platform_forward_path(f, hwloc_cuda_get_device_osdev_by_index(topology, i));
			fprintf(f, "   </route>\n");

			fprintf(f, "   <route src='RAM' dst='CUDA%d' symmetrical='NO'>\n", i);
			fprintf(f, "    <link_ctn id='RAM-CUDA%d'/>\n", i);
			emit_platform_backward_path(f, hwloc_cuda_get_device_osdev_by_index(topology, i));
			fprintf(f, "   </route>\n");
		}

		clean_topology(hwloc_get_root_obj(topology));
		hwloc_topology_destroy(topology);
	}
	else
	{
flat_cuda:
#else
	{
#endif
	/* If we don't have enough hwloc information, write trivial routes always through host */
	for (i = 0; i < ncuda; i++)
	{
		char i_name[16];
		snprintf(i_name, sizeof(i_name), "CUDA%d", i);
		fprintf(f, "   <route src='RAM' dst='%s' symmetrical='NO'><link_ctn id='RAM-%s'/><link_ctn id='Host'/></route>\n", i_name, i_name);
		fprintf(f, "   <route src='%s' dst='RAM' symmetrical='NO'><link_ctn id='%s-RAM'/><link_ctn id='Host'/></route>\n", i_name, i_name);
	}
#ifdef HAVE_CUDA_MEMCPY_PEER
	for (i = 0; i < ncuda; i++)
	{
		unsigned j;
		char i_name[16];
		snprintf(i_name, sizeof(i_name), "CUDA%d", i);
		for (j = 0; j < ncuda; j++)
		{
			char j_name[16];
			if (j == i)
				continue;
			snprintf(j_name, sizeof(j_name), "CUDA%d", j);
			fprintf(f, "   <route src='%s' dst='%s' symmetrical='NO'><link_ctn id='%s-%s'/><link_ctn id='Host'/></route>\n", i_name, j_name, i_name, j_name);
		}
	}
#endif
	} /* defined(STARPU_HAVE_HWLOC) && defined(HAVE_CUDA_MEMCPY_PEER) */
	fprintf(f, "\n");
#endif /* STARPU_USE_CUDA */

	/*
	 * OpenCL routes
	 */

#ifdef STARPU_USE_OPENCL
	for (i = 0; i < nopencl; i++)
	{
		char i_name[16];
		snprintf(i_name, sizeof(i_name), "OpenCL%d", i);
		fprintf(f, "   <route src='RAM' dst='%s' symmetrical='NO'><link_ctn id='RAM-%s'/><link_ctn id='Host'/></route>\n", i_name, i_name);
		fprintf(f, "   <route src='%s' dst='RAM' symmetrical='NO'><link_ctn id='%s-RAM'/><link_ctn id='Host'/></route>\n", i_name, i_name);
	}
#endif

	fprintf(f,
" </AS>\n"
" </platform>\n"
		);

	_starpu_fwrunlock(f);
	fclose(f);
}

static void generate_bus_platform_file(void)
{
	if (!was_benchmarked)
		benchmark_all_gpu_devices();

	write_bus_platform_file_content();
}

static void check_bus_platform_file(void)
{
	int res;

	char path[256];
	_starpu_simgrid_get_platform_path(path, sizeof(path));

	res = access(path, F_OK);
	if (res)
	{
		/* File does not exist yet */
		generate_bus_platform_file();
	}
}

/*
 *	Generic
 */

static void _starpu_bus_force_sampling(void)
{
	_STARPU_DEBUG("Force bus sampling ...\n");
	_starpu_create_sampling_directory_if_needed();

	generate_bus_affinity_file();
	generate_bus_latency_file();
	generate_bus_bandwidth_file();
	generate_bus_config_file();
	generate_bus_platform_file();
}
#endif /* !SIMGRID */

void _starpu_load_bus_performance_files(void)
{
	_starpu_create_sampling_directory_if_needed();

#if defined(STARPU_USE_CUDA) || defined(STARPU_USE_SIMGRID)
	ncuda = _starpu_get_cuda_device_count();
#endif
#if defined(STARPU_USE_OPENCL) || defined(STARPU_USE_SIMGRID)
	nopencl = _starpu_opencl_get_device_count();
#endif

#ifndef STARPU_SIMGRID
        check_bus_config_file();
	load_bus_affinity_file();
#endif
	load_bus_latency_file();
	load_bus_bandwidth_file();
#ifndef STARPU_SIMGRID
	check_bus_platform_file();
#endif
}

/* (in MB/s) */
double starpu_transfer_bandwidth(unsigned src_node, unsigned dst_node)
{
	return bandwidth_matrix[src_node][dst_node];
}

/* (in µs) */
double starpu_transfer_latency(unsigned src_node, unsigned dst_node)
{
	return latency_matrix[src_node][dst_node];
}

/* (in µs) */
double starpu_transfer_predict(unsigned src_node, unsigned dst_node, size_t size)
{
	double bandwidth = bandwidth_matrix[src_node][dst_node];
	double latency = latency_matrix[src_node][dst_node];
	struct _starpu_machine_topology *topology = &_starpu_get_machine_config()->topology;

	return latency + (size/bandwidth)*2*(topology->ncudagpus+topology->nopenclgpus);
}


/* calculate save bandwidth and latency */
/* bandwidth in MB/s - latency in µs */
void _starpu_save_bandwidth_and_latency_disk(double bandwidth_write, double bandwidth_read, double latency_write, double latency_read, unsigned node)
{
	unsigned int i, j;
	double slowness_disk_between_main_ram, slowness_main_ram_between_node;

	/* save bandwith */
	for(i = 0; i < STARPU_MAXNODES; ++i)
	{
		for(j = 0; j < STARPU_MAXNODES; ++j)
		{
			if (i == j && j == node) /* source == destination == node */
			{
				bandwidth_matrix[i][j] = 0;
			}
			else if (i == node) /* source == disk */
			{
				/* convert in slowness */
				if(bandwidth_read != 0)
					slowness_disk_between_main_ram = 1/bandwidth_read;
				else
					slowness_disk_between_main_ram = 0;

				if(bandwidth_matrix[STARPU_MAIN_RAM][j] != 0)
					slowness_main_ram_between_node = 1/bandwidth_matrix[STARPU_MAIN_RAM][j];
				else
					slowness_main_ram_between_node = 0;
				
				bandwidth_matrix[i][j] = 1/(slowness_disk_between_main_ram+slowness_main_ram_between_node);
			}
			else if (j == node) /* destination == disk */
			{
				/* convert in slowness */
				if(bandwidth_write != 0)
					slowness_disk_between_main_ram = 1/bandwidth_write;
				else
					slowness_disk_between_main_ram = 0;

				if(bandwidth_matrix[i][STARPU_MAIN_RAM] != 0)
					slowness_main_ram_between_node = 1/bandwidth_matrix[i][STARPU_MAIN_RAM];
				else
					slowness_main_ram_between_node = 0;

				bandwidth_matrix[i][j] = 1/(slowness_disk_between_main_ram+slowness_main_ram_between_node);
			}
			else if (j > node || i > node) /* not affected by the node */
			{
				bandwidth_matrix[i][j] = NAN;
			}
		}
	}

	/* save latency */
	for(i = 0; i < STARPU_MAXNODES; ++i)
	{
		for(j = 0; j < STARPU_MAXNODES; ++j)
		{
			if (i == j && j == node) /* source == destination == node */
			{
				latency_matrix[i][j] = 0;
			}
			else if (i == node) /* source == disk */
			{
				latency_matrix[i][j] = (latency_write+latency_matrix[STARPU_MAIN_RAM][j]);
			}
			else if (j == node) /* destination == disk */
			{
				latency_matrix[i][j] = (latency_read+latency_matrix[i][STARPU_MAIN_RAM]);
			}
			else if (j > node || i > node) /* not affected by the node */
			{
				latency_matrix[i][j] = NAN;
			}
		}
	}
}
