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


#include <string.h>
#include <starpu.h>
#include <core/task.h>
#include <core/sched_policy.h>

#include <drivers/driver_common/driver_common.h>


#include <datawizard/coherency.h>
#include <datawizard/interfaces/data_interface.h>
#include <drivers/mp_common/mp_common.h>


/* Finalize the execution of a task by a worker*/
static int _starpu_src_common_finalize_job (struct _starpu_job *j, struct _starpu_worker *worker)
{
	int profiling = starpu_profiling_status_get();
	struct timespec codelet_end;
	_starpu_driver_end_job(worker, j, &worker->perf_arch, &codelet_end, 0,
			profiling);

	int count = worker->current_rank;

	/* If it's a combined worker, we check if it's the last one of his combined */
	if(j->task_size > 1)
	{
		struct _starpu_combined_worker * cb_worker = _starpu_get_combined_worker_struct(worker->combined_workerid);

		STARPU_PTHREAD_MUTEX_LOCK(&cb_worker->count_mutex);
		count = cb_worker->count--;
		if(count == 0)
			cb_worker->count = cb_worker->worker_size - 1;
		STARPU_PTHREAD_MUTEX_UNLOCK(&cb_worker->count_mutex);
	}

	/* Finalize the execution */
	if(count == 0)
	{

		_starpu_driver_update_job_feedback(j, worker, &worker->perf_arch,
				&j->cl_start, &codelet_end,
				profiling);

		_starpu_push_task_output (j);

		_starpu_handle_job_termination(j);
	}
	return 0;
}


/* Complete the execution of the job */
static int _starpu_src_common_process_completed_job(struct _starpu_worker_set *workerset, void * arg, int arg_size)
{
	int coreid;

	STARPU_ASSERT(sizeof(coreid) == arg_size);

	coreid = *(int *) arg;

	struct _starpu_worker *worker = &workerset->workers[coreid];
	struct _starpu_job *j = _starpu_get_job_associated_to_task(worker->current_task);

	struct _starpu_worker * old_worker = _starpu_get_local_worker_key();

	_starpu_set_local_worker_key(worker);
	_starpu_src_common_finalize_job (j, worker);
	_starpu_set_local_worker_key(old_worker);

	worker->current_task = NULL;
	return 0;
}

/* Tell the scheduler when the execution has begun */
static void _starpu_src_common_pre_exec(void * arg, int arg_size)
{
	int cb_workerid, i;
	STARPU_ASSERT(sizeof(cb_workerid) == arg_size);
	cb_workerid = *(int *) arg;
	struct _starpu_combined_worker *combined_worker = _starpu_get_combined_worker_struct(cb_workerid);
	for(i=0; i < combined_worker->worker_size; i++)
	{
		struct _starpu_worker * worker = _starpu_get_worker_struct(combined_worker->combined_workerid[i]);
		_starpu_set_local_worker_key(worker);
		_starpu_sched_pre_exec_hook(worker->current_task);
	}
}

/* recv a message and handle asynchronous message
 * return 0 if the message has not been handle (it's certainly mean that it's a synchronous message)
 * return 1 if the message has been handle
 */
static int _starpu_src_common_handle_async(const struct _starpu_mp_node *node STARPU_ATTRIBUTE_UNUSED,
		void * arg, int arg_size,
		enum _starpu_mp_command answer)
{
	struct _starpu_worker_set * worker_set=NULL;
	switch(answer)
	{
		case STARPU_EXECUTION_COMPLETED:
			worker_set = _starpu_get_worker_struct(starpu_worker_get_id())->set;
			_starpu_src_common_process_completed_job(worker_set, arg, arg_size);
			break;
		case STARPU_PRE_EXECUTION:
			_starpu_src_common_pre_exec(arg,arg_size);
			break;
		default:
			return 0;
			break;
	}
	return 1;
}

/* Handle all message which have been stored in the message_queue */
static void _starpu_src_common_handle_stored_async(struct _starpu_mp_node *node)
{
	STARPU_PTHREAD_MUTEX_LOCK(&node->message_queue_mutex);
	/* while the list is not empty */
	while(!mp_message_list_empty(&node->message_queue))
	{
		/* We pop a message and handle it */
		struct mp_message * message = mp_message_list_pop_back(&node->message_queue);
		_starpu_src_common_handle_async(node, message->buffer,
				message->size, message->type);
		mp_message_delete(message);
	}
	STARPU_PTHREAD_MUTEX_UNLOCK(&node->message_queue_mutex);
}

/* Store a message if is asynchronous
 * return 1 if the message has been stored
 * return 0 if the message is unknown or synchrone */
int _starpu_src_common_store_message(struct _starpu_mp_node *node,
		void * arg, int arg_size, enum _starpu_mp_command answer)
{
	struct mp_message * message = NULL;
	switch(answer)
	{
		case STARPU_EXECUTION_COMPLETED:
		case STARPU_PRE_EXECUTION:
			message = mp_message_new();
			message->type = answer;
			memcpy(message->buffer, arg, arg_size);
			message->size = arg_size;

			STARPU_PTHREAD_MUTEX_LOCK(&node->message_queue_mutex);
			mp_message_list_push_front(&node->message_queue,message);
			STARPU_PTHREAD_MUTEX_UNLOCK(&node->message_queue_mutex);
			return 1;
			break;
		default:
			return 0;
			break;
	}
}

/* Store all asynchronous messages and return when a synchronous message is received */
static enum _starpu_mp_command _starpu_src_common_wait_command_sync(struct _starpu_mp_node *node,
		void ** arg, int* arg_size)
{
	enum _starpu_mp_command answer;
	int is_sync = 0;
	while(!is_sync)
	{
		answer = _starpu_mp_common_recv_command(node, arg, arg_size);
		if(!_starpu_src_common_store_message(node,*arg,*arg_size,answer))
			is_sync=1;
	}
	return answer;
}

/* Handle a asynchrone message and return a error if a synchronous message is received */
static void _starpu_src_common_recv_async(struct _starpu_mp_node * node)
{
	enum _starpu_mp_command answer;
	void *arg;
	int arg_size;
	answer = _starpu_mp_common_recv_command(node, &arg, &arg_size);
	if(!_starpu_src_common_handle_async(node,arg,arg_size,answer))
	{
		printf("incorrect commande: unknown command or sync command");
		STARPU_ASSERT(0);
	}
}

/* Handle all asynchrone message while a completed execution message from a specific worker has been receive */
 enum _starpu_mp_command _starpu_src_common_wait_completed_execution(struct _starpu_mp_node *node, int devid, void **arg, int * arg_size)
{
	enum _starpu_mp_command answer;

	int completed = 0;
	/*While the waited completed execution message has not been receive*/
	while(!completed)
	{
		answer = _starpu_mp_common_recv_command (node, arg, arg_size);

		if(answer == STARPU_EXECUTION_COMPLETED)
		{
			int coreid;
			STARPU_ASSERT(sizeof(coreid) == *arg_size);
			coreid = *(int *) *arg;
			if(devid == coreid)
				completed = 1;
			else
				if(!_starpu_src_common_store_message(node, *arg, *arg_size, answer))
					/* We receive a unknown or asynchronous message  */
					STARPU_ASSERT(0);
		}
		else
		{
			if(!_starpu_src_common_store_message(node, *arg, *arg_size, answer))
				/* We receive a unknown or asynchronous message  */
				STARPU_ASSERT(0);
		}
	}
	return answer;
}


/* Send a request to the sink NODE for the number of cores on it. */
int _starpu_src_common_sink_nbcores (const struct _starpu_mp_node *node, int *buf)
{

	enum _starpu_mp_command answer;
	void *arg;
	int arg_size = sizeof (int);

	_starpu_mp_common_send_command (node, STARPU_SINK_NBCORES, NULL, 0);

	answer = _starpu_mp_common_recv_command (node, &arg, &arg_size);

	STARPU_ASSERT (answer == STARPU_ANSWER_SINK_NBCORES && arg_size == sizeof (int));

	memcpy (buf, arg, arg_size);

	return 0;
}

/* Send a request to the sink linked to NODE for the pointer to the
 * function defined by FUNC_NAME.
 * In case of success, it returns 0 and FUNC_PTR contains the pointer ;
 * else it returns -ESPIPE if the function was not found.
 */
int _starpu_src_common_lookup(struct _starpu_mp_node *node,
		void (**func_ptr)(void), const char *func_name)
{
	enum _starpu_mp_command answer;
	void *arg;
	int arg_size;

	/* strlen ignore the terminating '\0' */
	arg_size = (strlen(func_name) + 1) * sizeof(char);

	//_STARPU_DEBUG("Looking up %s\n", func_name);
	_starpu_mp_common_send_command(node, STARPU_LOOKUP, (void *) func_name,
			arg_size);

	answer = _starpu_src_common_wait_command_sync(node, (void **) &arg,
			&arg_size);

	if (answer == STARPU_ERROR_LOOKUP)
	{
		_STARPU_DISP("Error looking up symbol %s\n", func_name);
		return -ESPIPE;
	}

	/* We have to be sure the device answered the right question and the
	 * answer has the right size */
	STARPU_ASSERT(answer == STARPU_ANSWER_LOOKUP);
	STARPU_ASSERT(arg_size == sizeof(*func_ptr));

	memcpy(func_ptr, arg, arg_size);

	//_STARPU_DEBUG("got %p\n", *func_ptr);

	return 0;
}

/* Send a message to the sink to execute a kernel.
 * The message sent has the form below :
 * [Function pointer on sink, number of interfaces, interfaces
 * (union _starpu_interface), cl_arg]
 */
/* Launch the execution of the function KERNEL points to on the sink linked
 * to NODE. Returns 0 in case of success, -EINVAL if kernel is an invalid
 * pointer.
 * Data interfaces in task are send to the sink.
 */
int _starpu_src_common_execute_kernel(struct _starpu_mp_node *node,
		void (*kernel)(void), unsigned coreid,
		enum starpu_codelet_type type,
		int is_parallel_task, int cb_workerid,
		starpu_data_handle_t *handles,
		void **interfaces,
		unsigned nb_interfaces,
		void *cl_arg, size_t cl_arg_size)
{

	void *buffer, *buffer_ptr, *arg =NULL;
	int buffer_size = 0, arg_size =0;
	unsigned i;

	buffer_size = sizeof(kernel) + sizeof(coreid) + sizeof(type)
		+ sizeof(nb_interfaces) + nb_interfaces * sizeof(union _starpu_interface) + sizeof(is_parallel_task);

	/*if the task is parallel*/
	if(is_parallel_task)
	{
		buffer_size += sizeof(cb_workerid);
	}

	/* If the user didn't give any cl_arg, there is no need to send it */
	if (cl_arg)
	{
		STARPU_ASSERT(cl_arg_size);
		buffer_size += cl_arg_size;
	}

	/* We give to send_command a buffer we just allocated, which contains
	 * a pointer to the function (sink-side), core on which execute this
	 * function (sink-side), number of interfaces we send,
	 * an array of generic (union) interfaces and the value of cl_arg */
	buffer_ptr = buffer = (void *) malloc(buffer_size);

	*(void(**)(void)) buffer = kernel;
	buffer_ptr += sizeof(kernel);

	*(enum starpu_codelet_type *) buffer_ptr = type;
	buffer_ptr += sizeof(type);

	*(int *) buffer_ptr = is_parallel_task;
	buffer_ptr += sizeof(is_parallel_task);

	if(is_parallel_task)
	{
		*(int *) buffer_ptr = cb_workerid ;
		buffer_ptr += sizeof(cb_workerid);
	}

	*(unsigned *) buffer_ptr = coreid;
	buffer_ptr += sizeof(coreid);

	*(unsigned *) buffer_ptr = nb_interfaces;
	buffer_ptr += sizeof(nb_interfaces);

	/* Message-passing execution is a particular case as the codelet is
	 * executed on a sink with a different memory, whereas a codelet is
	 * executed on the host part for the other accelerators.
	 * Thus we need to send a copy of each interface on the MP device */

	for (i = 0; i < nb_interfaces; i++)
	{
		starpu_data_handle_t handle = handles[i];

		memcpy (buffer_ptr, interfaces[i],
				handle->ops->interface_size);
		/* The sink side has no mean to get the type of each
		 * interface, we use a union to make it generic and permit the
		 * sink to go through the array */
		buffer_ptr += sizeof(union _starpu_interface);
	}

	if (cl_arg)
		memcpy(buffer_ptr, cl_arg, cl_arg_size);

	_starpu_mp_common_send_command(node, STARPU_EXECUTE, buffer, buffer_size);
	enum _starpu_mp_command answer = _starpu_src_common_wait_command_sync(node, &arg, &arg_size);

	if (answer == STARPU_ERROR_EXECUTE)
		return -EINVAL;

	STARPU_ASSERT(answer == STARPU_EXECUTION_SUBMITTED);

	free(buffer);

	return 0;
}


/* Get the information and call the function to send to the sink a message to execute the task*/
static int _starpu_src_common_execute(struct _starpu_job *j,
		struct _starpu_worker *worker,
		struct _starpu_mp_node * node)
{
	int ret;

	STARPU_ASSERT(j);
	struct starpu_task *task = j->task;

	int profiling = starpu_profiling_status_get();

	STARPU_ASSERT(task);
	if (worker->current_rank == 0)
	{
		ret = _starpu_fetch_task_input(j);
		if (ret != 0)
		{
			/* there was not enough memory, so the input of
			 * the codelet cannot be fetched ... put the
			 * codelet back, and try it later */
			return -EAGAIN;
		}
	}

	void (*kernel)(void)  = node->get_kernel_from_job(node,j);

	_starpu_driver_start_job(worker, j, &worker->perf_arch, &j->cl_start, 0, profiling);


	//_STARPU_DEBUG("\nworkerid:%d, rank:%d, type:%d,	cb_workerid:%d, task_size:%d\n\n",worker->devid,worker->current_rank,task->cl->type,j->combined_workerid,j->task_size);

	_starpu_src_common_execute_kernel(node, kernel, worker->devid, task->cl->type,
			(j->task_size > 1),
			j->combined_workerid, task->handles,
			task->interfaces, STARPU_TASK_GET_NBUFFERS(task),
			task->cl_arg, task->cl_arg_size);


	return 0;
}


/* Send a request to the sink linked to the MP_NODE to allocate SIZE bytes on
 * the sink.
 * In case of success, it returns 0 and *ADDR contains the address of the
 * allocated area ;
 * else it returns 1 if the allocation fail.
 */
int _starpu_src_common_allocate(struct _starpu_mp_node *mp_node,
		void **addr, size_t size)
{
	enum _starpu_mp_command answer;
	void *arg;
	int arg_size;

	_starpu_mp_common_send_command(mp_node, STARPU_ALLOCATE, &size,
			sizeof(size));

	answer = _starpu_src_common_wait_command_sync(mp_node, &arg, &arg_size);

	if (answer == STARPU_ERROR_ALLOCATE)
		return 1;

	STARPU_ASSERT(answer == STARPU_ANSWER_ALLOCATE &&
			arg_size == sizeof(*addr));

	memcpy(addr, arg, arg_size);

	return 0;
}

/* Send a request to the sink linked to the MP_NODE to deallocate the memory
 * area pointed by ADDR.
 */
void _starpu_src_common_free(const struct _starpu_mp_node *mp_node,
		void *addr)
{
	_starpu_mp_common_send_command(mp_node, STARPU_FREE, &addr, sizeof(addr));
}

/* Send SIZE bytes pointed by SRC to DST on the sink linked to the MP_NODE.
*/
int _starpu_src_common_copy_host_to_sink(const struct _starpu_mp_node *mp_node,
		void *src, void *dst, size_t size)
{
	struct _starpu_mp_transfer_command cmd = {size, dst};

	_starpu_mp_common_send_command(mp_node, STARPU_RECV_FROM_HOST, &cmd, sizeof(cmd));
	mp_node->dt_send(mp_node, src, size);

	return 0;
}

/* Receive SIZE bytes pointed by SRC on the sink linked to the MP_NODE and store them in DST.
*/
int _starpu_src_common_copy_sink_to_host(const struct _starpu_mp_node *mp_node,
		void *src, void *dst, size_t size)
{
	struct _starpu_mp_transfer_command cmd = {size, src};

	_starpu_mp_common_send_command(mp_node, STARPU_SEND_TO_HOST, &cmd, sizeof(cmd));
	mp_node->dt_recv(mp_node, dst, size);

	return 0;
}

/* Tell the sink linked to SRC_NODE to send SIZE bytes of data pointed by SRC
 * to the sink linked to DST_NODE. The latter store them in DST.
 */
int _starpu_src_common_copy_sink_to_sink(const struct _starpu_mp_node *src_node,
		const struct _starpu_mp_node *dst_node, void *src, void *dst, size_t size)
{
	enum _starpu_mp_command answer;
	void *arg;
	int arg_size;

	struct _starpu_mp_transfer_command_to_device cmd = {dst_node->peer_id, size, src};

	/* Tell source to send data to dest. */
	_starpu_mp_common_send_command(src_node, STARPU_SEND_TO_SINK, &cmd, sizeof(cmd));

	cmd.devid = src_node->peer_id;
	cmd.size = size;
	cmd.addr = dst;

	/* Tell dest to receive data from source. */
	_starpu_mp_common_send_command(dst_node, STARPU_RECV_FROM_SINK, &cmd, sizeof(cmd));

	/* Wait for answer from dest to know wether transfer is finished. */
	answer = _starpu_mp_common_recv_command(dst_node, &arg, &arg_size);

	STARPU_ASSERT(answer == STARPU_TRANSFER_COMPLETE);

	return 0;
}

/* 5 functions to determine the executable to run on the device (MIC, SCC,
 * MPI).
 */
static void _starpu_src_common_cat_3(char *final, const char *first,
		const char *second, const char *third)
{
	strcpy(final, first);
	strcat(final, second);
	strcat(final, third);
}

static void _starpu_src_common_cat_2(char *final, const char *first, const char *second)
{
	_starpu_src_common_cat_3(final, first, second, "");
}

static void _starpu_src_common_dir_cat(char *final, const char *dir, const char *file)
{
	if (file[0] == '/')
		++file;

	size_t size = strlen(dir);
	if (dir[size - 1] == '/')
		_starpu_src_common_cat_2(final, dir, file);
	else
		_starpu_src_common_cat_3(final, dir, "/", file);
}

static int _starpu_src_common_test_suffixes(char *located_file_name, const char *base, const char **suffixes)
{
	unsigned int i;
	for (i = 0; suffixes[i] != NULL; ++i)
	{
		_starpu_src_common_cat_2(located_file_name, base, suffixes[i]);
		if (access(located_file_name, R_OK) == 0)
			return 0;
	}

	return 1;
}

int _starpu_src_common_locate_file(char *located_file_name,
		const char *env_file_name, const char *env_mic_path,
		const char *config_file_name, const char *actual_file_name,
		const char **suffixes)
{
	if (env_file_name != NULL)
	{
		if (access(env_file_name, R_OK) == 0)
		{
			strcpy(located_file_name, env_file_name);
			return 0;
		}
		else if(env_mic_path != NULL)
		{
			_starpu_src_common_dir_cat(located_file_name, env_mic_path, env_file_name);

			return access(located_file_name, R_OK);
		}
	}
	else if (config_file_name != NULL)
	{
		if (access(config_file_name, R_OK) == 0)
		{
			strcpy(located_file_name, config_file_name);
			return 0;
		}
		else if (env_mic_path != NULL)
		{
			_starpu_src_common_dir_cat(located_file_name, env_mic_path, config_file_name);

			return access(located_file_name, R_OK);
		}
	}
	else if (actual_file_name != NULL)
	{
		if (_starpu_src_common_test_suffixes(located_file_name, actual_file_name, suffixes) == 0)
			return 0;

		if (env_mic_path != NULL)
		{
			char actual_cpy[1024];
			strcpy(actual_cpy, actual_file_name);

			char *last =  strrchr(actual_cpy, '/');
			while (last != NULL)
			{
				char tmp[1024];

				_starpu_src_common_dir_cat(tmp, env_mic_path, last);

				if (access(tmp, R_OK) == 0)
				{
					strcpy(located_file_name, tmp);
					return 0;
				}

				if (_starpu_src_common_test_suffixes(located_file_name, tmp, suffixes) == 0)
					return 0;

				*last = '\0';
				char *last_tmp = strrchr(actual_cpy, '/');
				*last = '/';
				last = last_tmp;
			}
		}
	}

	return 1;
}

/* Send workers to the sink node
 */
static void _starpu_src_common_send_workers(struct _starpu_mp_node * node, int baseworkerid, int nworkers)
{
	struct _starpu_machine_config *config = _starpu_get_machine_config();
	int worker_size = sizeof(struct _starpu_worker)*nworkers;
	int combined_worker_size = STARPU_NMAX_COMBINEDWORKERS*sizeof(struct _starpu_combined_worker);
	int msg[5];
	msg[0] = nworkers;
	msg[1] = worker_size;
	msg[2] = combined_worker_size;
	msg[3] = baseworkerid;
	msg[4] = starpu_worker_get_count();

	/* tell the sink node that we will send him all workers */
	_starpu_mp_common_send_command(node, STARPU_SYNC_WORKERS,
			&msg, sizeof(msg));

	/* Send all worker to the sink node */
	node->dt_send(node,&config->workers[baseworkerid],worker_size);

	/* Send all combined workers to the sink node */
	node->dt_send(node, &config->combined_workers,combined_worker_size);
}

/* Function looping on the source node */
void _starpu_src_common_worker(struct _starpu_worker_set * worker_set,
		unsigned baseworkerid,
		struct _starpu_mp_node * mp_node)
{
	unsigned memnode = worker_set->workers[0].memory_node;
	struct starpu_task **tasks = malloc(sizeof(struct starpu_task *)*worker_set->nworkers);

	_starpu_src_common_send_workers(mp_node, baseworkerid, worker_set->nworkers);

	/*main loop*/
	while (_starpu_machine_is_running())
	{
		int res;
		struct _starpu_job * j;

		_starpu_may_pause();

		_STARPU_TRACE_START_PROGRESS(memnode);
		_starpu_datawizard_progress(memnode, 1);
		_STARPU_TRACE_END_PROGRESS(memnode);

		/* Handle message which have been store */
		_starpu_src_common_handle_stored_async(mp_node);

		/* poll the device for completed jobs.*/
		while(mp_node->mp_recv_is_ready(mp_node))
			_starpu_src_common_recv_async(mp_node);

		/* get task for each worker*/
		res = _starpu_get_multi_worker_task(worker_set->workers, tasks, worker_set->nworkers, memnode);

		/*if at least one worker have pop a task*/
		if(res != 0)
		{
			unsigned i;
			for(i=0; i<worker_set->nworkers; i++)
			{
				if(tasks[i] != NULL)
				{
					j = _starpu_get_job_associated_to_task(tasks[i]);
					_starpu_set_local_worker_key(&worker_set->workers[i]);
					res =  _starpu_src_common_execute(j, &worker_set->workers[i], mp_node);
					switch (res)
					{
						case 0:
							/* The task task has been launched with no error */
							break;
						case -EAGAIN:
							_STARPU_DISP("ouch, this MP worker could not actually run task %p, putting it back...\n", tasks[i]);
							_starpu_push_task_to_workers(tasks[i]);
							STARPU_ABORT();
							continue;
							break;
						default:
							STARPU_ASSERT(0);
					}
				}
			}
		}
	}
	free(tasks);

	_starpu_handle_all_pending_node_data_requests(memnode);

	/* In case there remains some memory that was automatically
	 * allocated by StarPU, we release it now. Note that data
	 * coherency is not maintained anymore at that point ! */
	_starpu_free_all_automatically_allocated_buffers(memnode);

}
