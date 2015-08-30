/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2011, 2013  INRIA
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

#ifndef __SCHED_CONTEXT_H__
#define __SCHED_CONTEXT_H__

#include <starpu.h>
#include <starpu_sched_ctx.h>
#include <starpu_sched_ctx_hypervisor.h>
#include <starpu_scheduler.h>
#include <common/config.h>
#include <common/barrier_counter.h>
#include <profiling/profiling.h>
#include <semaphore.h>
#include "sched_ctx_list.h"

#ifdef STARPU_HAVE_HWLOC
#include <hwloc.h>
#endif

#define NO_RESIZE -1
#define REQ_RESIZE 0
#define DO_RESIZE 1

#define STARPU_GLOBAL_SCHED_CTX 0

struct _starpu_sched_ctx
{
	/* id of the context used in user mode*/
	unsigned id;

	/* name of context */
	const char *name;

	/* policy of the context */
	struct starpu_sched_policy *sched_policy;

	/* data necessary for the policy */
	void *policy_data;

	struct starpu_worker_collection *workers;

	/* we keep an initial sched which we never delete */
	unsigned is_initial_sched;

	/* wait for the tasks submitted to the context to be executed */
	struct _starpu_barrier_counter tasks_barrier;

	/* wait for the tasks ready of the context to be executed */
	struct _starpu_barrier_counter ready_tasks_barrier;

	/* amount of ready flops in a context */
	double ready_flops;

	/* cond to block push when there are no workers in the ctx */
	starpu_pthread_cond_t no_workers_cond;

	/* mutex to block push when there are no workers in the ctx */
	starpu_pthread_mutex_t no_workers_mutex;

	/*ready tasks that couldn't be pushed because the ctx has no workers*/
	struct starpu_task_list empty_ctx_tasks;

	/* mutext protecting empty_ctx_tasks list */
	starpu_pthread_mutex_t empty_ctx_mutex;

	/*ready tasks that couldn't be pushed because the the window of tasks was already full*/
	struct starpu_task_list waiting_tasks;

	/* mutext protecting waiting_tasks list */
	starpu_pthread_mutex_t waiting_tasks_mutex;

	/* min CPUs to execute*/
	int min_ncpus;

	/* max CPUs to execute*/
	int max_ncpus;

	/* min GPUs to execute*/
	int min_ngpus;

	/* max GPUs to execute*/
	int max_ngpus;

	/* in case we delete the context leave resources to the inheritor*/
	unsigned inheritor;

	/* indicates whether the application finished submitting tasks
	   to this context*/
	unsigned finished_submit;

        /* By default we have a binary type of priority: either a task is a priority
         * task (level 1) or it is not (level 0). */
     	int min_priority;
	int max_priority;
     	int min_priority_is_set;
	int max_priority_is_set;

	/* hwloc tree structure of workers */
#ifdef STARPU_HAVE_HWLOC
	hwloc_bitmap_t hwloc_workers_set;
#endif

#ifdef STARPU_USE_SC_HYPERVISOR
	/* a structure containing a series of performance counters determining the resize procedure */
	struct starpu_sched_ctx_performance_counters *perf_counters;
#endif //STARPU_USE_SC_HYPERVISOR

	/* callback called when the context finished executed its submitted tasks */
	void (*close_callback)(unsigned sched_ctx_id, void* args);
	void *close_args;
	
	/* value placing the contexts in their hierarchy */
	unsigned hierarchy_level;

	/* if we execute non-StarPU code inside the context 
	   we have a single master worker that stays awake, 
	   if not master is -1 */
	int main_master;

	/* conditions variables used when parallel sections are executed in contexts */
	starpu_pthread_cond_t parallel_sect_cond[STARPU_NMAXWORKERS];
	starpu_pthread_mutex_t parallel_sect_mutex[STARPU_NMAXWORKERS];
	starpu_pthread_cond_t parallel_sect_cond_busy[STARPU_NMAXWORKERS];
	int busy[STARPU_NMAXWORKERS];

	/* boolean indicating that workers should block in order to allow
	   parallel sections to be executed on their allocated resources */
	unsigned parallel_sect[STARPU_NMAXWORKERS];

	/* id of the master worker */
	int master[STARPU_NMAXWORKERS];

	/* semaphore that block appl thread until starpu threads are 
	   all blocked and ready to exec the parallel code */
	sem_t fall_asleep_sem[STARPU_NMAXWORKERS];

	/* semaphore that block appl thread until starpu threads are 
	   all woke up and ready continue appl */
	sem_t wake_up_sem[STARPU_NMAXWORKERS];
       
	/* bool indicating if the workers is sleeping in this ctx */
	unsigned sleeping[STARPU_NMAXWORKERS];

	/* ctx nesting the current ctx */
	unsigned nesting_sched_ctx;

	/* perf model for the device comb of the ctx */
	struct starpu_perfmodel_arch perf_arch;

	/* for ctxs without policy: flag to indicate that we want to get
	   the threads to sleep in order to replace them with other threads or leave
	   them awake & use them in the parallel code*/
	unsigned awake_workers;
};

struct _starpu_machine_config;

/* init sched_ctx_id of all contextes*/
void _starpu_init_all_sched_ctxs(struct _starpu_machine_config *config);

/* allocate all structures belonging to a context */
struct _starpu_sched_ctx*  _starpu_create_sched_ctx(struct starpu_sched_policy *policy, int *workerid, int nworkerids, unsigned is_init_sched, const char *sched_name,
						    int min_prio_set, int min_prio,
						    int max_prio_set, int max_prio, unsigned awake_workers);

/* delete all sched_ctx */
void _starpu_delete_all_sched_ctxs();

/* This function waits until all the tasks that were already submitted to a specific
 * context have been executed. */
int _starpu_wait_for_all_tasks_of_sched_ctx(unsigned sched_ctx_id);

/* This function waits until at most n tasks are still submitted. */
int _starpu_wait_for_n_submitted_tasks_of_sched_ctx(unsigned sched_ctx_id, unsigned n);

/* In order to implement starpu_wait_for_all_tasks_of_ctx, we keep track of the number of
 * task currently submitted to the context */
void _starpu_decrement_nsubmitted_tasks_of_sched_ctx(unsigned sched_ctx_id);
void _starpu_increment_nsubmitted_tasks_of_sched_ctx(unsigned sched_ctx_id);
int _starpu_get_nsubmitted_tasks_of_sched_ctx(unsigned sched_ctx_id);
int _starpu_check_nsubmitted_tasks_of_sched_ctx(unsigned sched_ctx_id);

void _starpu_decrement_nready_tasks_of_sched_ctx(unsigned sched_ctx_id, double ready_flops);
unsigned _starpu_increment_nready_tasks_of_sched_ctx(unsigned sched_ctx_id, double ready_flops, struct starpu_task *task);
int _starpu_wait_for_no_ready_of_sched_ctx(unsigned sched_ctx_id);

/* Return the corresponding index of the workerid in the ctx table */
int _starpu_get_index_in_ctx_of_workerid(unsigned sched_ctx, unsigned workerid);

/* Get the total number of sched_ctxs created till now */
unsigned _starpu_get_nsched_ctxs();

/* Get the mutex corresponding to the global workerid */
starpu_pthread_mutex_t *_starpu_get_sched_mutex(struct _starpu_sched_ctx *sched_ctx, int worker);

/* Get workers belonging to a certain context, it returns the number of workers
 take care: no mutex taken, the list of workers might not be updated */
int _starpu_get_workers_of_sched_ctx(unsigned sched_ctx_id, int *pus, enum starpu_worker_archtype arch);

/* Let the worker know it does not belong to the context and that
   it should stop poping from it */
void _starpu_worker_gets_out_of_ctx(unsigned sched_ctx_id, struct _starpu_worker *worker);

/* Check if the worker belongs to another sched_ctx */
unsigned _starpu_worker_belongs_to_a_sched_ctx(int workerid, unsigned sched_ctx_id);

/* mutex synchronising several simultaneous modifications of a context */
starpu_pthread_rwlock_t* _starpu_sched_ctx_get_changing_ctx_mutex(unsigned sched_ctx_id);

/* indicates wheather this worker should go to sleep or not 
   (if it is the last one awake in a context he should better keep awake) */
unsigned _starpu_sched_ctx_last_worker_awake(struct _starpu_worker *worker);

/* let the appl know that the worker blocked to execute parallel code */
void _starpu_sched_ctx_signal_worker_blocked(unsigned sched_ctx_id, int workerid);

/* let the appl know that the worker woke up */
void _starpu_sched_ctx_signal_worker_woke_up(unsigned sched_ctx_id, int workerid);

/* If starpu_sched_ctx_set_context() has been called, returns the context
 * id set by its last call, or the id of the initial context */
unsigned _starpu_sched_ctx_get_current_context();

/* verify how many workers can execute a certain task */
int _starpu_nworkers_able_to_execute_task(struct starpu_task *task, struct _starpu_sched_ctx *sched_ctx);

void _starpu_fetch_tasks_from_empty_ctx_list(struct _starpu_sched_ctx *sched_ctx);

unsigned _starpu_sched_ctx_allow_hypervisor(unsigned sched_ctx_id);

struct starpu_perfmodel_arch * _starpu_sched_ctx_get_perf_archtype(unsigned sched_ctx);
#ifdef STARPU_USE_SC_HYPERVISOR
/* Notifies the hypervisor that a tasks was poped from the workers' list */
void _starpu_sched_ctx_post_exec_task_cb(int workerid, struct starpu_task *task, size_t data_size, uint32_t footprint);

#endif //STARPU_USE_SC_HYPERVISOR

/* if the worker is the master of a parallel context, and the job is meant to be executed on this parallel context, return a pointer to the context */
struct _starpu_sched_ctx *_starpu_sched_ctx_get_sched_ctx_for_worker_and_job(struct _starpu_worker *worker, struct _starpu_job *j);

#endif // __SCHED_CONTEXT_H__
