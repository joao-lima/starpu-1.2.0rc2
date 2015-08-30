/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009, 2010-2015  Université de Bordeaux
 * Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015  CNRS
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

#include <stdlib.h>
#include <starpu_mpi.h>
#include <starpu_mpi_datatype.h>
#include <starpu_mpi_private.h>
#include <starpu_mpi_cache.h>
#include <starpu_profiling.h>
#include <starpu_mpi_stats.h>
#include <starpu_mpi_cache.h>
#include <starpu_mpi_sync_data.h>
#include <starpu_mpi_early_data.h>
#include <starpu_mpi_early_request.h>
#include <starpu_mpi_select_node.h>
#include <starpu_mpi_tag.h>
#include <starpu_mpi_comm.h>
#include <common/config.h>
#include <common/thread.h>
#include <datawizard/interfaces/data_interface.h>
#include <datawizard/coherency.h>
#include <core/simgrid.h>

static void _starpu_mpi_add_sync_point_in_fxt(void);
static void _starpu_mpi_submit_ready_request(void *arg);
static void _starpu_mpi_handle_ready_request(struct _starpu_mpi_req *req);
static void _starpu_mpi_handle_request_termination(struct _starpu_mpi_req *req);
#ifdef STARPU_VERBOSE
static char *_starpu_mpi_request_type(enum _starpu_mpi_request_type request_type);
#endif
static struct _starpu_mpi_req *_starpu_mpi_isend_common(starpu_data_handle_t data_handle,
							int dest, int data_tag, MPI_Comm comm,
							unsigned detached, unsigned sync, void (*callback)(void *), void *arg,
							int sequential_consistency);
static struct _starpu_mpi_req *_starpu_mpi_irecv_common(starpu_data_handle_t data_handle,
							int source, int data_tag, MPI_Comm comm,
							unsigned detached, unsigned sync, void (*callback)(void *), void *arg,
							int sequential_consistency, int is_internal_req,
							starpu_ssize_t count);
static void _starpu_mpi_handle_detached_request(struct _starpu_mpi_req *req);
static void _starpu_mpi_early_data_cb(void* arg);

/* The list of ready requests */
static struct _starpu_mpi_req_list *ready_requests;

/* The list of detached requests that have already been submitted to MPI */
static struct _starpu_mpi_req_list *detached_requests;
static starpu_pthread_mutex_t detached_requests_mutex;

/* Condition to wake up progression thread */
static starpu_pthread_cond_t cond_progression;
/* Condition to wake up waiting for all current MPI requests to finish */
static starpu_pthread_cond_t cond_finished;
static starpu_pthread_mutex_t mutex;
static starpu_pthread_t progress_thread;
static int running = 0;

#ifdef STARPU_SIMGRID
static int _mpi_world_size;
static int _mpi_world_rank;
#endif

/* Count requests posted by the application and not yet submitted to MPI */
static starpu_pthread_mutex_t mutex_posted_requests;
static int posted_requests = 0, newer_requests, barrier_running = 0;

#define _STARPU_MPI_INC_POSTED_REQUESTS(value) { STARPU_PTHREAD_MUTEX_LOCK(&mutex_posted_requests); posted_requests += value; STARPU_PTHREAD_MUTEX_UNLOCK(&mutex_posted_requests); }

#pragma weak smpi_simulated_main_
extern int smpi_simulated_main_(int argc, char *argv[]);

static void _starpu_mpi_request_init(struct _starpu_mpi_req **req)
{
	*req = calloc(1, sizeof(struct _starpu_mpi_req));
	STARPU_MPI_ASSERT_MSG(*req, "Invalid request");

	/* Initialize the request structure */
	(*req)->data_handle = NULL;

	(*req)->datatype = 0;
	(*req)->ptr = NULL;
	(*req)->count = -1;
	(*req)->user_datatype = -1;

	(*req)->node_tag.rank = -1;
	(*req)->node_tag.data_tag = -1;
	(*req)->node_tag.comm = NULL;

	(*req)->func = NULL;

	(*req)->status = NULL;
	(*req)->data_request = 0;
	(*req)->flag = NULL;

	(*req)->ret = -1;
	STARPU_PTHREAD_MUTEX_INIT(&((*req)->req_mutex), NULL);
	STARPU_PTHREAD_COND_INIT(&((*req)->req_cond), NULL);
	STARPU_PTHREAD_MUTEX_INIT(&((*req)->posted_mutex), NULL);
	STARPU_PTHREAD_COND_INIT(&((*req)->posted_cond), NULL);

	(*req)->request_type = UNKNOWN_REQ;

	(*req)->submitted = 0;
	(*req)->completed = 0;
	(*req)->posted = 0;

	(*req)->other_request = NULL;

	(*req)->sync = 0;
	(*req)->detached = -1;
	(*req)->callback = NULL;
	(*req)->callback_arg = NULL;

	(*req)->size_req = 0;
	(*req)->internal_req = NULL;
	(*req)->is_internal_req = 0;
	(*req)->envelope = NULL;
	(*req)->sequential_consistency = 1;
}

 /********************************************************/
 /*                                                      */
 /*  Send/Receive functionalities                        */
 /*                                                      */
 /********************************************************/

struct _starpu_mpi_early_data_cb_args
{
	starpu_data_handle_t data_handle;
	starpu_data_handle_t early_handle;
	struct _starpu_mpi_req *req;
	void *buffer;
};

static void _starpu_mpi_submit_ready_request(void *arg)
{
	_STARPU_MPI_LOG_IN();
	struct _starpu_mpi_req *req = arg;

	_STARPU_MPI_INC_POSTED_REQUESTS(-1);

	_STARPU_MPI_DEBUG(3, "new req %p srcdst %d tag %d and type %s %d\n", req, req->node_tag.rank, req->node_tag.data_tag, _starpu_mpi_request_type(req->request_type), req->is_internal_req);

	STARPU_PTHREAD_MUTEX_LOCK(&mutex);

	if (req->request_type == RECV_REQ)
	{
		/* Case : the request is the internal receive request submitted
		 * by StarPU-MPI to receive incoming data without a matching
		 * early_request from the application. We immediately allocate the
		 * pointer associated to the data_handle, and push it into the
		 * ready_requests list, so as the real MPI request can be submitted
		 * before the next submission of the envelope-catching request. */
		if (req->is_internal_req)
		{
			_starpu_mpi_handle_allocate_datatype(req->data_handle, &req->datatype, &req->user_datatype);
			if (req->user_datatype == 0)
			{
				req->count = 1;
				req->ptr = starpu_data_get_local_ptr(req->data_handle);
			}
			else
			{
				STARPU_ASSERT(req->count);
				req->ptr = malloc(req->count);
				STARPU_MPI_ASSERT_MSG(req->ptr, "cannot allocate message of size %ld\n", req->count);
			}

			_STARPU_MPI_DEBUG(3, "Pushing internal starpu_mpi_irecv request %p type %s tag %d src %d data %p ptr %p datatype '%s' count %d user_datatype %d \n",
					  req, _starpu_mpi_request_type(req->request_type), req->node_tag.data_tag, req->node_tag.rank, req->data_handle, req->ptr,
					  _starpu_mpi_datatype(req->datatype), (int)req->count, req->user_datatype);
			_starpu_mpi_req_list_push_front(ready_requests, req);

			/* inform the starpu mpi thread that the request has been pushed in the ready_requests list */
			STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
			STARPU_PTHREAD_MUTEX_LOCK(&req->posted_mutex);
			req->posted = 1;
			STARPU_PTHREAD_COND_BROADCAST(&req->posted_cond);
			STARPU_PTHREAD_MUTEX_UNLOCK(&req->posted_mutex);
			STARPU_PTHREAD_MUTEX_LOCK(&mutex);
		}
		else
		{
			/* test whether some data with the given tag and source have already been received by StarPU-MPI*/
			struct _starpu_mpi_early_data_handle *early_data_handle = _starpu_mpi_early_data_find(&req->node_tag);

			/* Case: a receive request for a data with the given tag and source has already been
			 * posted by StarPU. Asynchronously requests a Read permission over the temporary handle ,
			 * so as when the internal receive is completed, the _starpu_mpi_early_data_cb function
			 * will be called to bring the data back to the original data handle associated to the request.*/
			if (early_data_handle)
			{
				STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
				STARPU_PTHREAD_MUTEX_LOCK(&(early_data_handle->req_mutex));
				while (!(early_data_handle->req_ready))
					STARPU_PTHREAD_COND_WAIT(&(early_data_handle->req_cond), &(early_data_handle->req_mutex));
				STARPU_PTHREAD_MUTEX_UNLOCK(&(early_data_handle->req_mutex));
				STARPU_PTHREAD_MUTEX_LOCK(&mutex);

				_STARPU_MPI_DEBUG(3, "The RECV request %p with tag %d has already been received, copying previously received data into handle's pointer..\n", req, req->node_tag.data_tag);
				STARPU_ASSERT(req->data_handle != early_data_handle->handle);

				req->internal_req = early_data_handle->req;

				struct _starpu_mpi_early_data_cb_args *cb_args = malloc(sizeof(struct _starpu_mpi_early_data_cb_args));
				cb_args->data_handle = req->data_handle;
				cb_args->early_handle = early_data_handle->handle;
				cb_args->buffer = early_data_handle->buffer;
				cb_args->req = req;

				_STARPU_MPI_DEBUG(3, "Calling data_acquire_cb on starpu_mpi_copy_cb..\n");
				starpu_data_acquire_cb(early_data_handle->handle,STARPU_R,_starpu_mpi_early_data_cb,(void*) cb_args);
			}
			/* Case: no matching data has been received. Store the receive request as an early_request. */
			else
			{
				struct _starpu_mpi_req *sync_req = _starpu_mpi_sync_data_find(req->node_tag.data_tag, req->node_tag.rank, req->node_tag.comm);
				_STARPU_MPI_DEBUG(3, "----------> Looking for sync data for tag %d and src %d = %p\n", req->node_tag.data_tag, req->node_tag.rank, sync_req);
				if (sync_req)
				{
					req->sync = 1;
					_starpu_mpi_handle_allocate_datatype(req->data_handle, &req->datatype, &req->user_datatype);
					if (req->user_datatype == 0)
					{
						req->count = 1;
						req->ptr = starpu_data_get_local_ptr(req->data_handle);
					}
					else
					{
						req->count = sync_req->count;
						STARPU_ASSERT(req->count);
						req->ptr = malloc(req->count);
						STARPU_MPI_ASSERT_MSG(req->ptr, "cannot allocate message of size %ld\n", req->count);
					}
					_starpu_mpi_req_list_push_front(ready_requests, req);
					free(sync_req);
				}
				else
				{
					_STARPU_MPI_DEBUG(3, "Adding the pending receive request %p (srcdst %d tag %d) into the request hashmap\n", req, req->node_tag.rank, req->node_tag.data_tag);
					_starpu_mpi_early_request_enqueue(req);
				}
			}
		}
	}
	else
	{
		_starpu_mpi_req_list_push_front(ready_requests, req);
		_STARPU_MPI_DEBUG(3, "Pushing new request %p type %s tag %d src %d data %p ptr %p datatype '%s' count %d user_datatype %d \n",
				  req, _starpu_mpi_request_type(req->request_type), req->node_tag.data_tag, req->node_tag.rank, req->data_handle, req->ptr, _starpu_mpi_datatype(req->datatype), (int)req->count, req->user_datatype);
	}

	newer_requests = 1;
	STARPU_PTHREAD_COND_BROADCAST(&cond_progression);
	STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
	_STARPU_MPI_LOG_OUT();
}

static struct _starpu_mpi_req *_starpu_mpi_isend_irecv_common(starpu_data_handle_t data_handle,
							      int srcdst, int data_tag, MPI_Comm comm,
							      unsigned detached, unsigned sync, void (*callback)(void *), void *arg,
							      enum _starpu_mpi_request_type request_type, void (*func)(struct _starpu_mpi_req *),
							      enum starpu_data_access_mode mode,
							      int sequential_consistency,
							      int is_internal_req,
							      starpu_ssize_t count)
{
	struct _starpu_mpi_req *req;

	_STARPU_MPI_LOG_IN();
	_STARPU_MPI_INC_POSTED_REQUESTS(1);

	_starpu_mpi_comm_register(comm);

	/* Initialize the request structure */
	_starpu_mpi_request_init(&req);
	req->request_type = request_type;
	req->data_handle = data_handle;
	req->node_tag.rank = srcdst;
	req->node_tag.data_tag = data_tag;
	req->node_tag.comm = comm;
	req->detached = detached;
	req->sync = sync;
	req->callback = callback;
	req->callback_arg = arg;
	req->func = func;
	req->sequential_consistency = sequential_consistency;
	req->is_internal_req = is_internal_req;
	req->count = count;

	/* Asynchronously request StarPU to fetch the data in main memory: when
	 * it is available in main memory, _starpu_mpi_submit_ready_request(req) is called and
	 * the request is actually submitted */
	starpu_data_acquire_cb_sequential_consistency(data_handle, mode, _starpu_mpi_submit_ready_request, (void *)req, sequential_consistency);

	_STARPU_MPI_LOG_OUT();
	return req;
 }

 /********************************************************/
 /*                                                      */
 /*  Send functionalities                                */
 /*                                                      */
 /********************************************************/

static void _starpu_mpi_isend_data_func(struct _starpu_mpi_req *req)
{
	_STARPU_MPI_LOG_IN();

	_STARPU_MPI_DEBUG(20, "post MPI isend request %p type %s tag %d src %d data %p datasize %ld ptr %p datatype '%s' count %d user_datatype %d sync %d\n", req, _starpu_mpi_request_type(req->request_type), req->node_tag.data_tag, req->node_tag.rank, req->data_handle, starpu_data_get_size(req->data_handle), req->ptr, _starpu_mpi_datatype(req->datatype), (int)req->count, req->user_datatype, req->sync);

	_starpu_mpi_comm_amounts_inc(req->node_tag.comm, req->node_tag.rank, req->datatype, req->count);

	_STARPU_MPI_TRACE_ISEND_SUBMIT_BEGIN(req->node_tag.rank, req->node_tag.data_tag, 0);

	if (req->sync == 0)
	{
		_STARPU_MPI_COMM_TO_DEBUG(req->count, req->datatype, req->node_tag.rank, _STARPU_MPI_TAG_DATA, req->node_tag.data_tag, req->node_tag.comm);
		req->ret = MPI_Isend(req->ptr, req->count, req->datatype, req->node_tag.rank, _STARPU_MPI_TAG_DATA, req->node_tag.comm, &req->data_request);
		STARPU_MPI_ASSERT_MSG(req->ret == MPI_SUCCESS, "MPI_Isend returning %s", _starpu_mpi_get_mpi_code(req->ret));
	}
	else
	{
		_STARPU_MPI_COMM_TO_DEBUG(req->count, req->datatype, req->node_tag.rank, _STARPU_MPI_TAG_SYNC_DATA, req->node_tag.data_tag, req->node_tag.comm);
		req->ret = MPI_Issend(req->ptr, req->count, req->datatype, req->node_tag.rank, _STARPU_MPI_TAG_SYNC_DATA, req->node_tag.comm, &req->data_request);
		STARPU_MPI_ASSERT_MSG(req->ret == MPI_SUCCESS, "MPI_Issend returning %s", _starpu_mpi_get_mpi_code(req->ret));
	}

	_STARPU_MPI_TRACE_ISEND_SUBMIT_END(req->node_tag.rank, req->node_tag.data_tag, 0);

	/* somebody is perhaps waiting for the MPI request to be posted */
	STARPU_PTHREAD_MUTEX_LOCK(&req->req_mutex);
	req->submitted = 1;
	STARPU_PTHREAD_COND_BROADCAST(&req->req_cond);
	STARPU_PTHREAD_MUTEX_UNLOCK(&req->req_mutex);

	_starpu_mpi_handle_detached_request(req);

	_STARPU_MPI_LOG_OUT();
}

static void _starpu_mpi_isend_size_func(struct _starpu_mpi_req *req)
{
	_starpu_mpi_handle_allocate_datatype(req->data_handle, &req->datatype, &req->user_datatype);

	req->envelope = calloc(1,sizeof(struct _starpu_mpi_envelope));
	req->envelope->mode = _STARPU_MPI_ENVELOPE_DATA;
	req->envelope->data_tag = req->node_tag.data_tag;
	req->envelope->sync = req->sync;

	if (req->user_datatype == 0)
	{
		int size;
		req->count = 1;
		req->ptr = starpu_data_get_local_ptr(req->data_handle);

		MPI_Type_size(req->datatype, &size);
		req->envelope->size = (starpu_ssize_t)req->count * size;
		_STARPU_MPI_DEBUG(20, "Post MPI isend count (%ld) datatype_size %ld request to %d\n",req->count,starpu_data_get_size(req->data_handle), req->node_tag.rank);
		_STARPU_MPI_COMM_TO_DEBUG(sizeof(struct _starpu_mpi_envelope), MPI_BYTE, req->node_tag.rank, _STARPU_MPI_TAG_ENVELOPE, _STARPU_MPI_TAG_ENVELOPE, req->node_tag.comm);
		MPI_Isend(req->envelope, sizeof(struct _starpu_mpi_envelope), MPI_BYTE, req->node_tag.rank, _STARPU_MPI_TAG_ENVELOPE, req->node_tag.comm, &req->size_req);
	}
	else
	{
		int ret;

 		// Do not pack the data, just try to find out the size
		starpu_data_pack(req->data_handle, NULL, &(req->envelope->size));

		if (req->envelope->size != -1)
 		{
 			// We already know the size of the data, let's send it to overlap with the packing of the data
			_STARPU_MPI_DEBUG(20, "Sending size %ld (%ld %s) to node %d (first call to pack)\n", req->envelope->size, sizeof(req->count), _starpu_mpi_datatype(MPI_BYTE), req->node_tag.rank);
			req->count = req->envelope->size;
			_STARPU_MPI_COMM_TO_DEBUG(sizeof(struct _starpu_mpi_envelope), MPI_BYTE, req->node_tag.rank, _STARPU_MPI_TAG_ENVELOPE, _STARPU_MPI_TAG_ENVELOPE, req->node_tag.comm);
			ret = MPI_Isend(req->envelope, sizeof(struct _starpu_mpi_envelope), MPI_BYTE, req->node_tag.rank, _STARPU_MPI_TAG_ENVELOPE, req->node_tag.comm, &req->size_req);
			STARPU_MPI_ASSERT_MSG(ret == MPI_SUCCESS, "when sending size, MPI_Isend returning %s", _starpu_mpi_get_mpi_code(ret));
 		}

 		// Pack the data
 		starpu_data_pack(req->data_handle, &req->ptr, &req->count);
		if (req->envelope->size == -1)
 		{
 			// We know the size now, let's send it
			_STARPU_MPI_DEBUG(20, "Sending size %ld (%ld %s) to node %d (second call to pack)\n", req->envelope->size, sizeof(req->count), _starpu_mpi_datatype(MPI_BYTE), req->node_tag.rank);
			_STARPU_MPI_COMM_TO_DEBUG(sizeof(struct _starpu_mpi_envelope), MPI_BYTE, req->node_tag.rank, _STARPU_MPI_TAG_ENVELOPE, _STARPU_MPI_TAG_ENVELOPE, req->node_tag.comm);
			ret = MPI_Isend(req->envelope, sizeof(struct _starpu_mpi_envelope), MPI_BYTE, req->node_tag.rank, _STARPU_MPI_TAG_ENVELOPE, req->node_tag.comm, &req->size_req);
			STARPU_MPI_ASSERT_MSG(ret == MPI_SUCCESS, "when sending size, MPI_Isend returning %s", _starpu_mpi_get_mpi_code(ret));
 		}
 		else
 		{
 			// We check the size returned with the 2 calls to pack is the same
			STARPU_MPI_ASSERT_MSG(req->count == req->envelope->size, "Calls to pack_data returned different sizes %ld != %ld", req->count, req->envelope->size);
 		}
		// We can send the data now
	}

	if (req->sync)
	{
		// If the data is to be sent in synchronous mode, we need to wait for the receiver ready message
		_starpu_mpi_sync_data_add(req);
	}
	else
	{
		// Otherwise we can send the data
		_starpu_mpi_isend_data_func(req);
	}
}

static struct _starpu_mpi_req *_starpu_mpi_isend_common(starpu_data_handle_t data_handle,
							int dest, int data_tag, MPI_Comm comm,
							unsigned detached, unsigned sync, void (*callback)(void *), void *arg,
							int sequential_consistency)
{
	return _starpu_mpi_isend_irecv_common(data_handle, dest, data_tag, comm, detached, sync, callback, arg, SEND_REQ, _starpu_mpi_isend_size_func, STARPU_R, sequential_consistency, 0, 0);
}

int starpu_mpi_isend(starpu_data_handle_t data_handle, starpu_mpi_req *public_req, int dest, int data_tag, MPI_Comm comm)
{
	_STARPU_MPI_LOG_IN();
	STARPU_MPI_ASSERT_MSG(public_req, "starpu_mpi_isend needs a valid starpu_mpi_req");

	struct _starpu_mpi_req *req;
	_STARPU_MPI_TRACE_ISEND_COMPLETE_BEGIN(dest, data_tag, 0);
	req = _starpu_mpi_isend_common(data_handle, dest, data_tag, comm, 0, 0, NULL, NULL, 1);
	_STARPU_MPI_TRACE_ISEND_COMPLETE_END(dest, data_tag, 0);

	STARPU_MPI_ASSERT_MSG(req, "Invalid return for _starpu_mpi_isend_common");
	*public_req = req;

	_STARPU_MPI_LOG_OUT();
	return 0;
}

int starpu_mpi_isend_detached(starpu_data_handle_t data_handle,
			      int dest, int data_tag, MPI_Comm comm, void (*callback)(void *), void *arg)
{
	_STARPU_MPI_LOG_IN();
	_starpu_mpi_isend_common(data_handle, dest, data_tag, comm, 1, 0, callback, arg, 1);
	_STARPU_MPI_LOG_OUT();
	return 0;
}

int starpu_mpi_send(starpu_data_handle_t data_handle, int dest, int data_tag, MPI_Comm comm)
{
	starpu_mpi_req req;
	MPI_Status status;

	_STARPU_MPI_LOG_IN();
	memset(&status, 0, sizeof(MPI_Status));

	starpu_mpi_isend(data_handle, &req, dest, data_tag, comm);
	starpu_mpi_wait(&req, &status);

	_STARPU_MPI_LOG_OUT();
	return 0;
}

int starpu_mpi_issend(starpu_data_handle_t data_handle, starpu_mpi_req *public_req, int dest, int data_tag, MPI_Comm comm)
{
	_STARPU_MPI_LOG_IN();
	STARPU_MPI_ASSERT_MSG(public_req, "starpu_mpi_issend needs a valid starpu_mpi_req");

	struct _starpu_mpi_req *req;
	req = _starpu_mpi_isend_common(data_handle, dest, data_tag, comm, 0, 1, NULL, NULL, 1);

	STARPU_MPI_ASSERT_MSG(req, "Invalid return for _starpu_mpi_isend_common");
	*public_req = req;

	_STARPU_MPI_LOG_OUT();
	return 0;
}

int starpu_mpi_issend_detached(starpu_data_handle_t data_handle, int dest, int data_tag, MPI_Comm comm, void (*callback)(void *), void *arg)
{
	_STARPU_MPI_LOG_IN();

	_starpu_mpi_isend_common(data_handle, dest, data_tag, comm, 1, 1, callback, arg, 1);

	_STARPU_MPI_LOG_OUT();
	return 0;
}

/********************************************************/
/*                                                      */
/*  receive functionalities                             */
/*                                                      */
/********************************************************/

static void _starpu_mpi_irecv_data_func(struct _starpu_mpi_req *req)
{
	_STARPU_MPI_LOG_IN();

	_STARPU_MPI_DEBUG(20, "post MPI irecv request %p type %s tag %d src %d data %p ptr %p datatype '%s' count %d user_datatype %d \n", req, _starpu_mpi_request_type(req->request_type), req->node_tag.data_tag, req->node_tag.rank, req->data_handle, req->ptr, _starpu_mpi_datatype(req->datatype), (int)req->count, req->user_datatype);

	_STARPU_MPI_TRACE_IRECV_SUBMIT_BEGIN(req->node_tag.rank, req->node_tag.data_tag);

	if (req->sync)
	{
		struct _starpu_mpi_envelope *_envelope = calloc(1,sizeof(struct _starpu_mpi_envelope));
		_envelope->mode = _STARPU_MPI_ENVELOPE_SYNC_READY;
		_envelope->data_tag = req->node_tag.data_tag;
		_STARPU_MPI_DEBUG(20, "Telling node %d it can send the data and waiting for the data back ...\n", req->node_tag.rank);
		_STARPU_MPI_COMM_TO_DEBUG(sizeof(struct _starpu_mpi_envelope), MPI_BYTE, req->node_tag.rank, _STARPU_MPI_TAG_ENVELOPE, _STARPU_MPI_TAG_ENVELOPE, req->node_tag.comm);
		req->ret = MPI_Send(_envelope, sizeof(struct _starpu_mpi_envelope), MPI_BYTE, req->node_tag.rank, _STARPU_MPI_TAG_ENVELOPE, req->node_tag.comm);
		STARPU_MPI_ASSERT_MSG(req->ret == MPI_SUCCESS, "MPI_Send returning %s", _starpu_mpi_get_mpi_code(req->ret));
		free(_envelope);
	}

	if (req->sync)
	{
		_STARPU_MPI_COMM_FROM_DEBUG(req->count, req->datatype, req->node_tag.rank, _STARPU_MPI_TAG_SYNC_DATA, req->node_tag.data_tag, req->node_tag.comm);
		req->ret = MPI_Irecv(req->ptr, req->count, req->datatype, req->node_tag.rank, _STARPU_MPI_TAG_SYNC_DATA, req->node_tag.comm, &req->data_request);
	}
	else
	{
		_STARPU_MPI_COMM_FROM_DEBUG(req->count, req->datatype, req->node_tag.rank, _STARPU_MPI_TAG_DATA, req->node_tag.data_tag, req->node_tag.comm);
		req->ret = MPI_Irecv(req->ptr, req->count, req->datatype, req->node_tag.rank, _STARPU_MPI_TAG_DATA, req->node_tag.comm, &req->data_request);
	}
	STARPU_MPI_ASSERT_MSG(req->ret == MPI_SUCCESS, "MPI_IRecv returning %s", _starpu_mpi_get_mpi_code(req->ret));

	_STARPU_MPI_TRACE_IRECV_SUBMIT_END(req->node_tag.rank, req->node_tag.data_tag);

	/* somebody is perhaps waiting for the MPI request to be posted */
	STARPU_PTHREAD_MUTEX_LOCK(&req->req_mutex);
	req->submitted = 1;
	STARPU_PTHREAD_COND_BROADCAST(&req->req_cond);
	STARPU_PTHREAD_MUTEX_UNLOCK(&req->req_mutex);

	_starpu_mpi_handle_detached_request(req);

	_STARPU_MPI_LOG_OUT();
}

static struct _starpu_mpi_req *_starpu_mpi_irecv_common(starpu_data_handle_t data_handle, int source, int data_tag, MPI_Comm comm, unsigned detached, unsigned sync, void (*callback)(void *), void *arg, int sequential_consistency, int is_internal_req, starpu_ssize_t count)
{
	return _starpu_mpi_isend_irecv_common(data_handle, source, data_tag, comm, detached, sync, callback, arg, RECV_REQ, _starpu_mpi_irecv_data_func, STARPU_W, sequential_consistency, is_internal_req, count);
}

int starpu_mpi_irecv(starpu_data_handle_t data_handle, starpu_mpi_req *public_req, int source, int data_tag, MPI_Comm comm)
{
	_STARPU_MPI_LOG_IN();
	STARPU_MPI_ASSERT_MSG(public_req, "starpu_mpi_irecv needs a valid starpu_mpi_req");

//	// We check if a tag is defined for the data handle, if not,
//	// we define the one given for the communication.
//	// A tag is necessary for the internal mpi engine.
//	int tag = starpu_data_get_tag(data_handle);
//	if (tag == -1)
//		starpu_data_set_tag(data_handle, data_tag);

	struct _starpu_mpi_req *req;
	_STARPU_MPI_TRACE_IRECV_COMPLETE_BEGIN(source, data_tag);
	req = _starpu_mpi_irecv_common(data_handle, source, data_tag, comm, 0, 0, NULL, NULL, 1, 0, 0);
	_STARPU_MPI_TRACE_IRECV_COMPLETE_END(source, data_tag);
	STARPU_MPI_ASSERT_MSG(req, "Invalid return for _starpu_mpi_irecv_common");
	*public_req = req;

	_STARPU_MPI_LOG_OUT();
	return 0;
}

int starpu_mpi_irecv_detached(starpu_data_handle_t data_handle, int source, int data_tag, MPI_Comm comm, void (*callback)(void *), void *arg)
{
	_STARPU_MPI_LOG_IN();

//	// We check if a tag is defined for the data handle, if not,
//	// we define the one given for the communication.
//	// A tag is necessary for the internal mpi engine.
//	int tag = starpu_data_get_tag(data_handle);
//	if (tag == -1)
//		starpu_data_set_tag(data_handle, data_tag);

	_starpu_mpi_irecv_common(data_handle, source, data_tag, comm, 1, 0, callback, arg, 1, 0, 0);
	_STARPU_MPI_LOG_OUT();
	return 0;
}

int starpu_mpi_irecv_detached_sequential_consistency(starpu_data_handle_t data_handle, int source, int data_tag, MPI_Comm comm, void (*callback)(void *), void *arg, int sequential_consistency)
{
	_STARPU_MPI_LOG_IN();

//	// We check if a tag is defined for the data handle, if not,
//	// we define the one given for the communication.
//	// A tag is necessary for the internal mpi engine.
//	int tag = starpu_data_get_tag(data_handle);
//	if (tag == -1)
//		starpu_data_set_tag(data_handle, data_tag);

	_starpu_mpi_irecv_common(data_handle, source, data_tag, comm, 1, 0, callback, arg, sequential_consistency, 0, 0);

	_STARPU_MPI_LOG_OUT();
	return 0;
}

int starpu_mpi_recv(starpu_data_handle_t data_handle, int source, int data_tag, MPI_Comm comm, MPI_Status *status)
{
	starpu_mpi_req req;
	_STARPU_MPI_LOG_IN();

//	// We check if a tag is defined for the data handle, if not,
//	// we define the one given for the communication.
//	// A tag is necessary for the internal mpi engine.
//	int tag = starpu_data_get_tag(data_handle);
//	if (tag == -1)
//		starpu_data_set_tag(data_handle, data_tag);

	starpu_mpi_irecv(data_handle, &req, source, data_tag, comm);
	starpu_mpi_wait(&req, status);

	_STARPU_MPI_LOG_OUT();
	return 0;
}

/********************************************************/
/*                                                      */
/*  Wait functionalities                                */
/*                                                      */
/********************************************************/

static void _starpu_mpi_wait_func(struct _starpu_mpi_req *waiting_req)
{
	_STARPU_MPI_LOG_IN();
	/* Which is the mpi request we are waiting for ? */
	struct _starpu_mpi_req *req = waiting_req->other_request;

	_STARPU_MPI_TRACE_UWAIT_BEGIN(req->node_tag.rank, req->node_tag.data_tag);

	req->ret = MPI_Wait(&req->data_request, waiting_req->status);
	STARPU_MPI_ASSERT_MSG(req->ret == MPI_SUCCESS, "MPI_Wait returning %s", _starpu_mpi_get_mpi_code(req->ret));

	_STARPU_MPI_TRACE_UWAIT_END(req->node_tag.rank, req->node_tag.data_tag);

	_starpu_mpi_handle_request_termination(req);
	_STARPU_MPI_LOG_OUT();
}

int starpu_mpi_wait(starpu_mpi_req *public_req, MPI_Status *status)
{
	int ret;
	struct _starpu_mpi_req *req = *public_req;
	struct _starpu_mpi_req *waiting_req;

	_STARPU_MPI_LOG_IN();
	_STARPU_MPI_INC_POSTED_REQUESTS(1);

	/* We cannot try to complete a MPI request that was not actually posted
	 * to MPI yet. */
	STARPU_PTHREAD_MUTEX_LOCK(&(req->req_mutex));
	while (!(req->submitted))
		STARPU_PTHREAD_COND_WAIT(&(req->req_cond), &(req->req_mutex));
	STARPU_PTHREAD_MUTEX_UNLOCK(&(req->req_mutex));

	/* Initialize the request structure */
	 _starpu_mpi_request_init(&waiting_req);
	waiting_req->status = status;
	waiting_req->other_request = req;
	waiting_req->func = _starpu_mpi_wait_func;
	waiting_req->request_type = WAIT_REQ;

	_starpu_mpi_submit_ready_request(waiting_req);

	/* We wait for the MPI request to finish */
	STARPU_PTHREAD_MUTEX_LOCK(&req->req_mutex);
	while (!req->completed)
		STARPU_PTHREAD_COND_WAIT(&req->req_cond, &req->req_mutex);
	STARPU_PTHREAD_MUTEX_UNLOCK(&req->req_mutex);

	ret = req->ret;

	/* The internal request structure was automatically allocated */
	*public_req = NULL;
	if (req->internal_req)
	{
		free(req->internal_req); req->internal_req = NULL;
	}
	free(req);

	free(waiting_req);
	_STARPU_MPI_LOG_OUT();
	return ret;
}

/********************************************************/
/*                                                      */
/*  Test functionalities                                */
/*                                                      */
/********************************************************/

static void _starpu_mpi_test_func(struct _starpu_mpi_req *testing_req)
{
	_STARPU_MPI_LOG_IN();
	/* Which is the mpi request we are testing for ? */
	struct _starpu_mpi_req *req = testing_req->other_request;

	_STARPU_MPI_DEBUG(2, "Test request %p type %s tag %d src %d data %p ptr %p datatype '%s' count %d user_datatype %d \n",
			  req, _starpu_mpi_request_type(req->request_type), req->node_tag.data_tag, req->node_tag.rank, req->data_handle, req->ptr, _starpu_mpi_datatype(req->datatype), (int)req->count, req->user_datatype);

	_STARPU_MPI_TRACE_UTESTING_BEGIN(req->node_tag.rank, req->node_tag.data_tag);

	req->ret = MPI_Test(&req->data_request, testing_req->flag, testing_req->status);
	STARPU_MPI_ASSERT_MSG(req->ret == MPI_SUCCESS, "MPI_Test returning %s", _starpu_mpi_get_mpi_code(req->ret));

	_STARPU_MPI_TRACE_UTESTING_END(req->node_tag.rank, req->node_tag.data_tag);

	if (*testing_req->flag)
	{
		testing_req->ret = req->ret;
		_starpu_mpi_handle_request_termination(req);
	}

	STARPU_PTHREAD_MUTEX_LOCK(&testing_req->req_mutex);
	testing_req->completed = 1;
	STARPU_PTHREAD_COND_SIGNAL(&testing_req->req_cond);
	STARPU_PTHREAD_MUTEX_UNLOCK(&testing_req->req_mutex);
	_STARPU_MPI_LOG_OUT();
}

int starpu_mpi_test(starpu_mpi_req *public_req, int *flag, MPI_Status *status)
{
	_STARPU_MPI_LOG_IN();
	int ret = 0;

	STARPU_MPI_ASSERT_MSG(public_req, "starpu_mpi_test needs a valid starpu_mpi_req");

	struct _starpu_mpi_req *req = *public_req;

	STARPU_MPI_ASSERT_MSG(!req->detached, "MPI_Test cannot be called on a detached request");

	STARPU_PTHREAD_MUTEX_LOCK(&req->req_mutex);
	unsigned submitted = req->submitted;
	STARPU_PTHREAD_MUTEX_UNLOCK(&req->req_mutex);

	if (submitted)
	{
		struct _starpu_mpi_req *testing_req;
		_starpu_mpi_request_init(&testing_req);

		/* Initialize the request structure */
		STARPU_PTHREAD_MUTEX_INIT(&(testing_req->req_mutex), NULL);
		STARPU_PTHREAD_COND_INIT(&(testing_req->req_cond), NULL);
		testing_req->flag = flag;
		testing_req->status = status;
		testing_req->other_request = req;
		testing_req->func = _starpu_mpi_test_func;
		testing_req->completed = 0;
		testing_req->request_type = TEST_REQ;

		_STARPU_MPI_INC_POSTED_REQUESTS(1);
		_starpu_mpi_submit_ready_request(testing_req);

		/* We wait for the test request to finish */
		STARPU_PTHREAD_MUTEX_LOCK(&(testing_req->req_mutex));
		while (!(testing_req->completed))
			STARPU_PTHREAD_COND_WAIT(&(testing_req->req_cond), &(testing_req->req_mutex));
		STARPU_PTHREAD_MUTEX_UNLOCK(&(testing_req->req_mutex));

		ret = testing_req->ret;

		if (*(testing_req->flag))
		{
			/* The request was completed so we free the internal
			 * request structure which was automatically allocated
			 * */
			*public_req = NULL;
			if (req->internal_req)
			{
				free(req->internal_req); req->internal_req = NULL;
			}
			free(req);
		}

		free(testing_req);
	}
	else
	{
		*flag = 0;
	}

	_STARPU_MPI_LOG_OUT();
	return ret;
}

/********************************************************/
/*                                                      */
/*  Barrier functionalities                             */
/*                                                      */
/********************************************************/

static void _starpu_mpi_barrier_func(struct _starpu_mpi_req *barrier_req)
{
	_STARPU_MPI_LOG_IN();

	barrier_req->ret = MPI_Barrier(barrier_req->node_tag.comm);
	STARPU_MPI_ASSERT_MSG(barrier_req->ret == MPI_SUCCESS, "MPI_Barrier returning %s", _starpu_mpi_get_mpi_code(barrier_req->ret));

	_starpu_mpi_handle_request_termination(barrier_req);
	_STARPU_MPI_LOG_OUT();
}

int starpu_mpi_barrier(MPI_Comm comm)
{
	int ret;
	struct _starpu_mpi_req *barrier_req;

	_STARPU_MPI_LOG_IN();
	_starpu_mpi_request_init(&barrier_req);

	/* First wait for *both* all tasks and MPI requests to finish, in case
	 * some tasks generate MPI requests, MPI requests generate tasks, etc.
	 */
	STARPU_PTHREAD_MUTEX_LOCK(&mutex);
	STARPU_MPI_ASSERT_MSG(!barrier_running, "Concurrent starpu_mpi_barrier is not implemented, even on different communicators");
	barrier_running = 1;
	do
	{
		while (posted_requests)
			/* Wait for all current MPI requests to finish */
			STARPU_PTHREAD_COND_WAIT(&cond_finished, &mutex);
		/* No current request, clear flag */
		newer_requests = 0;
		STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
		/* Now wait for all tasks */
		starpu_task_wait_for_all();
		STARPU_PTHREAD_MUTEX_LOCK(&mutex);
		/* Check newer_requests again, in case some MPI requests
		 * triggered by tasks completed and triggered tasks between
		 * wait_for_all finished and we take the lock */
	} while (posted_requests || newer_requests);
	barrier_running = 0;
	STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);

	/* Initialize the request structure */
	STARPU_PTHREAD_MUTEX_INIT(&(barrier_req->req_mutex), NULL);
	STARPU_PTHREAD_COND_INIT(&(barrier_req->req_cond), NULL);
	barrier_req->func = _starpu_mpi_barrier_func;
	barrier_req->request_type = BARRIER_REQ;
	barrier_req->node_tag.comm = comm;

	_STARPU_MPI_INC_POSTED_REQUESTS(1);
	_starpu_mpi_submit_ready_request(barrier_req);

	/* We wait for the MPI request to finish */
	STARPU_PTHREAD_MUTEX_LOCK(&barrier_req->req_mutex);
	while (!barrier_req->completed)
		STARPU_PTHREAD_COND_WAIT(&barrier_req->req_cond, &barrier_req->req_mutex);
	STARPU_PTHREAD_MUTEX_UNLOCK(&barrier_req->req_mutex);

	ret = barrier_req->ret;

	free(barrier_req);
	_STARPU_MPI_LOG_OUT();
	return ret;
}

/********************************************************/
/*                                                      */
/*  Progression                                         */
/*                                                      */
/********************************************************/

#ifdef STARPU_VERBOSE
static char *_starpu_mpi_request_type(enum _starpu_mpi_request_type request_type)
{
	switch (request_type)
		{
		case SEND_REQ: return "SEND_REQ";
		case RECV_REQ: return "RECV_REQ";
		case WAIT_REQ: return "WAIT_REQ";
		case TEST_REQ: return "TEST_REQ";
		case BARRIER_REQ: return "BARRIER_REQ";
		case UNKNOWN_REQ: return "UNSET_REQ";
		default: return "unknown request type";
		}
}
#endif

static void _starpu_mpi_handle_request_termination(struct _starpu_mpi_req *req)
{
	_STARPU_MPI_LOG_IN();

	_STARPU_MPI_DEBUG(2, "complete MPI request %p type %s tag %d src %d data %p ptr %p datatype '%s' count %d user_datatype %d internal_req %p\n",
			  req, _starpu_mpi_request_type(req->request_type), req->node_tag.data_tag, req->node_tag.rank, req->data_handle, req->ptr,
			  _starpu_mpi_datatype(req->datatype), (int)req->count, req->user_datatype, req->internal_req);

	if (req->internal_req)
	{
		struct _starpu_mpi_early_data_handle *early_data_handle = _starpu_mpi_early_data_find(&req->node_tag);
		STARPU_MPI_ASSERT_MSG(early_data_handle, "Could not find a copy data handle with the tag %d and the node %d\n", req->node_tag.data_tag, req->node_tag.rank);
		_STARPU_MPI_DEBUG(3, "Handling deleting of early_data structure from the hashmap..\n");
		_starpu_mpi_early_data_delete(early_data_handle);
		free(early_data_handle);
	}
	else
	{
		if (req->request_type == RECV_REQ || req->request_type == SEND_REQ)
		{
			if (req->user_datatype == 1)
			{
				if (req->request_type == SEND_REQ)
				{
					// We need to make sure the communication for sending the size
					// has completed, as MPI can re-order messages, let's call
					// MPI_Wait to make sure data have been sent
					int ret;
					ret = MPI_Wait(&req->size_req, MPI_STATUS_IGNORE);
					STARPU_MPI_ASSERT_MSG(ret == MPI_SUCCESS, "MPI_Wait returning %s", _starpu_mpi_get_mpi_code(ret));
					free(req->ptr);
				}
				else if (req->request_type == RECV_REQ)
				{
					// req->ptr is freed by starpu_data_unpack
					starpu_data_unpack(req->data_handle, req->ptr, req->count);
				}
			}
			else
			{
				_starpu_mpi_handle_free_datatype(req->data_handle, &req->datatype);
			}
		}
	}

	if (req->data_handle)
		starpu_data_release(req->data_handle);

	if (req->envelope)
	{
		free(req->envelope);
		req->envelope = NULL;
	}

	/* Execute the specified callback, if any */
	if (req->callback)
		req->callback(req->callback_arg);

	/* tell anyone potentially waiting on the request that it is
	 * terminated now */
	STARPU_PTHREAD_MUTEX_LOCK(&req->req_mutex);
	req->completed = 1;
	STARPU_PTHREAD_COND_BROADCAST(&req->req_cond);
	STARPU_PTHREAD_MUTEX_UNLOCK(&req->req_mutex);
	_STARPU_MPI_LOG_OUT();
}

static void _starpu_mpi_early_data_cb(void* arg)
{
	struct _starpu_mpi_early_data_cb_args *args = arg;

	// We store in the application request the internal MPI
	// request so that it can be used by starpu_mpi_wait
	args->req->data_request = args->req->internal_req->data_request;
	args->req->submitted = 1;

	if (args->buffer)
	{
		/* Data has been received as a raw memory, it has to be unpacked */
		struct starpu_data_interface_ops *itf_src = starpu_data_get_interface_ops(args->early_handle);
		struct starpu_data_interface_ops *itf_dst = starpu_data_get_interface_ops(args->data_handle);
		STARPU_MPI_ASSERT_MSG(itf_dst->unpack_data, "The data interface does not define an unpack function\n");
		itf_dst->unpack_data(args->data_handle, STARPU_MAIN_RAM, args->buffer, itf_src->get_size(args->early_handle));
		free(args->buffer);
	}
	else
	{
		struct starpu_data_interface_ops *itf = starpu_data_get_interface_ops(args->early_handle);
		void* itf_src = starpu_data_get_interface_on_node(args->early_handle, STARPU_MAIN_RAM);
		void* itf_dst = starpu_data_get_interface_on_node(args->data_handle, STARPU_MAIN_RAM);

		if (!itf->copy_methods->ram_to_ram)
		{
			_STARPU_MPI_DEBUG(3, "Initiating any_to_any copy..\n");
			itf->copy_methods->any_to_any(itf_src, STARPU_MAIN_RAM, itf_dst, STARPU_MAIN_RAM, NULL);
		}
		else
		{
			_STARPU_MPI_DEBUG(3, "Initiating ram_to_ram copy..\n");
			itf->copy_methods->ram_to_ram(itf_src, STARPU_MAIN_RAM, itf_dst, STARPU_MAIN_RAM);
		}
	}

	_STARPU_MPI_DEBUG(3, "Done, handling release of early_handle..\n");
	starpu_data_release(args->early_handle);

	_STARPU_MPI_DEBUG(3, "Done, handling unregister of early_handle..\n");
	starpu_data_unregister_submit(args->early_handle);

	_STARPU_MPI_DEBUG(3, "Done, handling request %p termination of the already received request\n",args->req);
	// If the request is detached, we need to call _starpu_mpi_handle_request_termination
	// as it will not be called automatically as the request is not in the list detached_requests
	if (args->req->detached)
		_starpu_mpi_handle_request_termination(args->req);
	// else: If the request is not detached its termination will
	// be handled when calling starpu_mpi_wait

	free(args);
}

#ifdef STARPU_MPI_ACTIVITY
static unsigned _starpu_mpi_progression_hook_func(void *arg STARPU_ATTRIBUTE_UNUSED)
{
	unsigned may_block = 1;

	STARPU_PTHREAD_MUTEX_LOCK(&detached_requests_mutex);
	if (!_starpu_mpi_req_list_empty(detached_requests))
	{
		STARPU_PTHREAD_MUTEX_UNLOCK(&detached_requests_mutex);
		STARPU_PTHREAD_MUTEX_LOCK(&mutex);
		STARPU_PTHREAD_COND_SIGNAL(&cond_progression);
		STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
		may_block = 0;
	}
	else
		STARPU_PTHREAD_MUTEX_UNLOCK(&detached_requests_mutex);


	return may_block;
}
#endif /* STARPU_MPI_ACTIVITY */

static void _starpu_mpi_test_detached_requests(void)
{
	_STARPU_MPI_LOG_IN();
	int flag;
	MPI_Status status;
	struct _starpu_mpi_req *req, *next_req;

	STARPU_PTHREAD_MUTEX_LOCK(&detached_requests_mutex);

	for (req = _starpu_mpi_req_list_begin(detached_requests);
		req != _starpu_mpi_req_list_end(detached_requests);
		req = next_req)
	{
		next_req = _starpu_mpi_req_list_next(req);

		STARPU_PTHREAD_MUTEX_UNLOCK(&detached_requests_mutex);

		//_STARPU_MPI_DEBUG(3, "Test detached request %p - mpitag %d - TYPE %s %d\n", &req->data_request, req->node_tag.data_tag, _starpu_mpi_request_type(req->request_type), req->node_tag.rank);
		req->ret = MPI_Test(&req->data_request, &flag, &status);

		STARPU_MPI_ASSERT_MSG(req->ret == MPI_SUCCESS, "MPI_Test returning %s", _starpu_mpi_get_mpi_code(req->ret));

		if (flag)
		{
			if (req->request_type == RECV_REQ)
			{
				_STARPU_MPI_TRACE_IRECV_COMPLETE_BEGIN(req->node_tag.rank, req->node_tag.data_tag);
			}
			else if (req->request_type == SEND_REQ)
			{
				_STARPU_MPI_TRACE_ISEND_COMPLETE_BEGIN(req->node_tag.rank, req->node_tag.data_tag, 0);
			}

			_starpu_mpi_handle_request_termination(req);

			if (req->request_type == RECV_REQ)
			{
				_STARPU_MPI_TRACE_IRECV_COMPLETE_END(req->node_tag.rank, req->node_tag.data_tag);
			}
			else if (req->request_type == SEND_REQ)
			{
				_STARPU_MPI_TRACE_ISEND_COMPLETE_END(req->node_tag.rank, req->node_tag.data_tag, 0);
			}
		}

		STARPU_PTHREAD_MUTEX_LOCK(&detached_requests_mutex);

		if (flag)
		{
			_starpu_mpi_req_list_erase(detached_requests, req);
#ifdef STARPU_DEVEL
#warning FIXME: when do we free internal requests
#endif
			if (!req->is_internal_req)
				free(req);
		}

	}

	STARPU_PTHREAD_MUTEX_UNLOCK(&detached_requests_mutex);
	_STARPU_MPI_LOG_OUT();
}

static void _starpu_mpi_handle_detached_request(struct _starpu_mpi_req *req)
{
	if (req->detached)
	{
		/* put the submitted request into the list of pending requests
		 * so that it can be handled by the progression mechanisms */
		STARPU_PTHREAD_MUTEX_LOCK(&detached_requests_mutex);
		_starpu_mpi_req_list_push_front(detached_requests, req);
		STARPU_PTHREAD_MUTEX_UNLOCK(&detached_requests_mutex);

		starpu_wake_all_blocked_workers();

		STARPU_PTHREAD_MUTEX_LOCK(&mutex);
		STARPU_PTHREAD_COND_SIGNAL(&cond_progression);
		STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
	}
}

static void _starpu_mpi_handle_ready_request(struct _starpu_mpi_req *req)
{
	_STARPU_MPI_LOG_IN();
	STARPU_MPI_ASSERT_MSG(req, "Invalid request");

	/* submit the request to MPI */
	_STARPU_MPI_DEBUG(2, "Handling new request %p type %s tag %d src %d data %p ptr %p datatype '%s' count %d user_datatype %d \n",
			  req, _starpu_mpi_request_type(req->request_type), req->node_tag.data_tag, req->node_tag.rank, req->data_handle, req->ptr, _starpu_mpi_datatype(req->datatype), (int)req->count, req->user_datatype);
	req->func(req);

	_STARPU_MPI_LOG_OUT();
}

struct _starpu_mpi_argc_argv
{
	int initialize_mpi;
	int *argc;
	char ***argv;
	MPI_Comm comm;
};

static void _starpu_mpi_print_thread_level_support(int thread_level, char *msg)
{
	switch (thread_level)
	{
	case MPI_THREAD_SERIALIZED:
	{
		_STARPU_DISP("MPI%s MPI_THREAD_SERIALIZED; Multiple threads may make MPI calls, but only one at a time.\n", msg);
		break;
	}
	case MPI_THREAD_FUNNELED:
	{
		_STARPU_DISP("MPI%s MPI_THREAD_FUNNELED; The application can safely make calls to StarPU-MPI functions, but should not call directly MPI communication functions.\n", msg);
		break;
	}
	case MPI_THREAD_SINGLE:
	{
		_STARPU_DISP("MPI%s MPI_THREAD_SINGLE; MPI does not have multi-thread support, this might cause problems. The application can make calls to StarPU-MPI functions, but not call directly MPI Communication functions.\n", msg);
		break;
	}
	}
}

static void _starpu_mpi_receive_early_data(struct _starpu_mpi_envelope *envelope, MPI_Status status, MPI_Comm comm)
{
	_STARPU_MPI_DEBUG(20, "Request with tag %d and source %d not found, creating a early_handle to receive incoming data..\n", envelope->data_tag, status.MPI_SOURCE);
	_STARPU_MPI_DEBUG(20, "Request sync %d\n", envelope->sync);

	struct _starpu_mpi_early_data_handle* early_data_handle = _starpu_mpi_early_data_create(envelope, status.MPI_SOURCE, comm);

	starpu_data_handle_t data_handle = NULL;
	STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
	data_handle = _starpu_mpi_data_get_data_handle_from_tag(envelope->data_tag);
	STARPU_PTHREAD_MUTEX_LOCK(&mutex);

	if (data_handle && starpu_data_get_interface_id(data_handle) < STARPU_MAX_INTERFACE_ID)
	{
		/* We know which data will receive it and we won't have to unpack, use just the same kind of data.  */
		early_data_handle->buffer = NULL;
		starpu_data_register_same(&early_data_handle->handle, data_handle);
		_starpu_mpi_early_data_add(early_data_handle);
	}
	else
	{
		/* The application has not registered yet a data with the tag,
		 * we are going to receive the data as a raw memory, and give it
		 * to the application when it post a receive for this tag
		 */
		_STARPU_MPI_DEBUG(3, "Posting a receive for a data of size %d which has not yet been registered\n", (int)early_data_handle->env->size);
		early_data_handle->buffer = malloc(early_data_handle->env->size);
		starpu_variable_data_register(&early_data_handle->handle, STARPU_MAIN_RAM, (uintptr_t) early_data_handle->buffer, early_data_handle->env->size);
		_starpu_mpi_early_data_add(early_data_handle);
	}

	_STARPU_MPI_DEBUG(20, "Posting internal detached irecv on early_handle with tag %d from comm %p src %d ..\n", early_data_handle->node_tag.data_tag, comm, status.MPI_SOURCE);
	STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
	early_data_handle->req = _starpu_mpi_irecv_common(early_data_handle->handle, status.MPI_SOURCE,
							  early_data_handle->node_tag.data_tag, comm, 1, 0,
							  NULL, NULL, 1, 1, envelope->size);
	STARPU_PTHREAD_MUTEX_LOCK(&mutex);

	// We wait until the request is pushed in the
	// ready_request list, that ensures that the next loop
	// will call _starpu_mpi_handle_ready_request
	// on the request and post the corresponding mpi_irecv,
	// otherwise, it may lead to read data as envelop
	STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
	STARPU_PTHREAD_MUTEX_LOCK(&(early_data_handle->req->posted_mutex));
	while (!(early_data_handle->req->posted))
		STARPU_PTHREAD_COND_WAIT(&(early_data_handle->req->posted_cond), &(early_data_handle->req->posted_mutex));
	STARPU_PTHREAD_MUTEX_UNLOCK(&(early_data_handle->req->posted_mutex));

	STARPU_PTHREAD_MUTEX_LOCK(&early_data_handle->req_mutex);
	early_data_handle->req_ready = 1;
	STARPU_PTHREAD_COND_BROADCAST(&early_data_handle->req_cond);
	STARPU_PTHREAD_MUTEX_UNLOCK(&early_data_handle->req_mutex);
	STARPU_PTHREAD_MUTEX_LOCK(&mutex);
}

static void *_starpu_mpi_progress_thread_func(void *arg)
{
	struct _starpu_mpi_argc_argv *argc_argv = (struct _starpu_mpi_argc_argv *) arg;
	int rank, worldsize;

	if (argc_argv->initialize_mpi)
	{
		int thread_support;
		_STARPU_DEBUG("Calling MPI_Init_thread\n");
		if (MPI_Init_thread(argc_argv->argc, argc_argv->argv, MPI_THREAD_SERIALIZED, &thread_support) != MPI_SUCCESS)
		{
			_STARPU_ERROR("MPI_Init_thread failed\n");
		}
		_starpu_mpi_print_thread_level_support(thread_support, "_Init_thread level =");
	}
	else
	{
		int provided;
		MPI_Query_thread(&provided);
		_starpu_mpi_print_thread_level_support(provided, " has been initialized with");
	}

	MPI_Comm_rank(argc_argv->comm, &rank);
	MPI_Comm_size(argc_argv->comm, &worldsize);
	MPI_Comm_set_errhandler(argc_argv->comm, MPI_ERRORS_RETURN);

#ifdef STARPU_SIMGRID
	_mpi_world_size = worldsize;
	_mpi_world_rank = rank;
	/* Now that MPI is set up, let the rest of simgrid get initialized */
	MSG_process_create_with_arguments("main", smpi_simulated_main_, NULL, _starpu_simgrid_get_host_by_name("MAIN"), *(argc_argv->argc), *(argc_argv->argv));
#endif

	{
		_STARPU_MPI_TRACE_START(rank, worldsize);
#ifdef STARPU_USE_FXT
		starpu_profiling_set_id(rank);
#endif //STARPU_USE_FXT
	}

	_starpu_mpi_add_sync_point_in_fxt();
	_starpu_mpi_comm_amounts_init(argc_argv->comm);
	_starpu_mpi_cache_init(argc_argv->comm);
	_starpu_mpi_select_node_init();
	_starpu_mpi_tag_init();
	_starpu_mpi_comm_init(argc_argv->comm);

	_starpu_mpi_early_request_init();
	_starpu_mpi_early_data_init();
	_starpu_mpi_sync_data_init();

	/* notify the main thread that the progression thread is ready */
	STARPU_PTHREAD_MUTEX_LOCK(&mutex);
	running = 1;
	STARPU_PTHREAD_COND_SIGNAL(&cond_progression);
	STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);

	STARPU_PTHREAD_MUTEX_LOCK(&mutex);

 	int envelope_request_submitted = 0;

	while (running || posted_requests || !(_starpu_mpi_req_list_empty(ready_requests)) || !(_starpu_mpi_req_list_empty(detached_requests)))// || !(_starpu_mpi_early_request_count()) || !(_starpu_mpi_sync_data_count()))
	{
		/* shall we block ? */
		unsigned block = _starpu_mpi_req_list_empty(ready_requests) && _starpu_mpi_early_request_count() == 0 && _starpu_mpi_sync_data_count() == 0;

#ifndef STARPU_MPI_ACTIVITY
		STARPU_PTHREAD_MUTEX_LOCK(&detached_requests_mutex);
		block = block && _starpu_mpi_req_list_empty(detached_requests);
		STARPU_PTHREAD_MUTEX_UNLOCK(&detached_requests_mutex);
#endif /* STARPU_MPI_ACTIVITY */

		if (block)
		{
			_STARPU_MPI_DEBUG(3, "NO MORE REQUESTS TO HANDLE\n");

			_STARPU_MPI_TRACE_SLEEP_BEGIN();

			if (barrier_running)
				/* Tell mpi_barrier */
				STARPU_PTHREAD_COND_SIGNAL(&cond_finished);
			STARPU_PTHREAD_COND_WAIT(&cond_progression, &mutex);

			_STARPU_MPI_TRACE_SLEEP_END();
		}

		/* get one request */
		struct _starpu_mpi_req *req;
		while (!_starpu_mpi_req_list_empty(ready_requests))
		{
			req = _starpu_mpi_req_list_pop_back(ready_requests);

			/* handling a request is likely to block for a while
			 * (on a sync_data_with_mem call), we want to let the
			 * application submit requests in the meantime, so we
			 * release the lock. */
			STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
			_starpu_mpi_handle_ready_request(req);
			STARPU_PTHREAD_MUTEX_LOCK(&mutex);
		}

		/* If there is no currently submitted envelope_request submitted to
                 * catch envelopes from senders, and there is some pending
                 * receive requests on our side, we resubmit a header request. */
		if (((_starpu_mpi_early_request_count() > 0) || (_starpu_mpi_sync_data_count() > 0)) && (envelope_request_submitted == 0))// && (HASH_COUNT(_starpu_mpi_early_data_handle_hashmap) == 0))
		{
			_starpu_mpi_comm_post_recv();
			envelope_request_submitted = 1;
		}

		/* test whether there are some terminated "detached request" */
		STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
		_starpu_mpi_test_detached_requests();
		STARPU_PTHREAD_MUTEX_LOCK(&mutex);

		if (envelope_request_submitted == 1)
		{
			int flag;
			struct _starpu_mpi_envelope *envelope;
			MPI_Status envelope_status;
			MPI_Comm envelope_comm;

			/* test whether an envelope has arrived. */
			flag = _starpu_mpi_comm_test_recv(&envelope_status, &envelope, &envelope_comm);

			if (flag)
			{
				_STARPU_MPI_DEBUG(4, "Envelope received with mode %d\n", envelope->mode);
				if (envelope->mode == _STARPU_MPI_ENVELOPE_SYNC_READY)
				{
					struct _starpu_mpi_req *_sync_req = _starpu_mpi_sync_data_find(envelope->data_tag, envelope_status.MPI_SOURCE, envelope_comm);
					_STARPU_MPI_DEBUG(20, "Sending data with tag %d to node %d\n", _sync_req->node_tag.data_tag, envelope_status.MPI_SOURCE);
					STARPU_MPI_ASSERT_MSG(envelope->data_tag == _sync_req->node_tag.data_tag, "Tag mismatch (envelope %d != req %d)\n", envelope->data_tag, _sync_req->node_tag.data_tag);
					STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
					_starpu_mpi_isend_data_func(_sync_req);
					STARPU_PTHREAD_MUTEX_LOCK(&mutex);
				}
				else
				{
					_STARPU_MPI_DEBUG(3, "Searching for application request with tag %d and source %d (size %ld)\n", envelope->data_tag, envelope_status.MPI_SOURCE, envelope->size);

					struct _starpu_mpi_req *early_request = _starpu_mpi_early_request_dequeue(envelope->data_tag, envelope_status.MPI_SOURCE, envelope_comm);

					/* Case: a data will arrive before a matching receive is
					 * posted by the application. Create a temporary handle to
					 * store the incoming data, submit a starpu_mpi_irecv_detached
					 * on this handle, and store it as an early_data
					 */
					if (early_request == NULL)
					{
						if (envelope->sync)
						{
							_STARPU_MPI_DEBUG(2000, "-------------------------> adding request for tag %d\n", envelope->data_tag);
							struct _starpu_mpi_req *new_req;
#ifdef STARPU_DEVEL
#warning creating a request is not really useful.
#endif
							/* Initialize the request structure */
							_starpu_mpi_request_init(&new_req);
							new_req->request_type = RECV_REQ;
							new_req->data_handle = NULL;
							new_req->node_tag.rank = envelope_status.MPI_SOURCE;
							new_req->node_tag.data_tag = envelope->data_tag;
							new_req->node_tag.comm = envelope_comm;
							new_req->detached = 1;
							new_req->sync = 1;
							new_req->callback = NULL;
							new_req->callback_arg = NULL;
							new_req->func = _starpu_mpi_irecv_data_func;
							new_req->sequential_consistency = 1;
							new_req->is_internal_req = 0; // ????
							new_req->count = envelope->size;
							_starpu_mpi_sync_data_add(new_req);
						}
						else
						{
							_starpu_mpi_receive_early_data(envelope, envelope_status, envelope_comm);
						}
					}
					/* Case: a matching application request has been found for
					 * the incoming data, we handle the correct allocation
					 * of the pointer associated to the data handle, then
					 * submit the corresponding receive with
					 * _starpu_mpi_handle_ready_request. */
					else
					{
						_STARPU_MPI_DEBUG(2000, "A matching application request has been found for the incoming data with tag %d\n", envelope->data_tag);
						_STARPU_MPI_DEBUG(2000, "Request sync %d\n", envelope->sync);

						early_request->sync = envelope->sync;
						_starpu_mpi_handle_allocate_datatype(early_request->data_handle, &early_request->datatype, &early_request->user_datatype);
						if (early_request->user_datatype == 0)
						{
							early_request->count = 1;
							early_request->ptr = starpu_data_get_local_ptr(early_request->data_handle);
						}
						else
						{
							early_request->count = envelope->size;
							early_request->ptr = malloc(early_request->count);

							STARPU_MPI_ASSERT_MSG(early_request->ptr, "cannot allocate message of size %ld\n", early_request->count);
						}

						_STARPU_MPI_DEBUG(3, "Handling new request... \n");
						/* handling a request is likely to block for a while
						 * (on a sync_data_with_mem call), we want to let the
						 * application submit requests in the meantime, so we
						 * release the lock. */
						STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);
						_starpu_mpi_handle_ready_request(early_request);
						STARPU_PTHREAD_MUTEX_LOCK(&mutex);
					}
				}
				envelope_request_submitted = 0;
			}
			else
			{
				//_STARPU_MPI_DEBUG(4, "Nothing received, continue ..\n");
			}
		}
	}

	if (envelope_request_submitted)
	{
		_starpu_mpi_comm_cancel_recv();
		envelope_request_submitted = 0;
	}

	STARPU_MPI_ASSERT_MSG(_starpu_mpi_req_list_empty(detached_requests), "List of detached requests not empty");
	STARPU_MPI_ASSERT_MSG(_starpu_mpi_req_list_empty(ready_requests), "List of ready requests not empty");
	STARPU_MPI_ASSERT_MSG(posted_requests == 0, "Number of posted request is not zero");
	_starpu_mpi_early_request_check_termination();
	_starpu_mpi_early_data_check_termination();
	_starpu_mpi_sync_data_check_termination();

	if (argc_argv->initialize_mpi)
	{
		_STARPU_MPI_DEBUG(3, "Calling MPI_Finalize()\n");
		MPI_Finalize();
	}

	STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);

	_starpu_mpi_sync_data_free();
	_starpu_mpi_early_data_free();
	_starpu_mpi_early_request_free();
	free(argc_argv);

	return NULL;
}

/********************************************************/
/*                                                      */
/*  (De)Initialization methods                          */
/*                                                      */
/********************************************************/

#ifdef STARPU_MPI_ACTIVITY
static int hookid = - 1;
#endif /* STARPU_MPI_ACTIVITY */

static void _starpu_mpi_add_sync_point_in_fxt(void)
{
#ifdef STARPU_USE_FXT
	int rank;
	int worldsize;
	int ret;

	starpu_mpi_comm_rank(MPI_COMM_WORLD, &rank);
	starpu_mpi_comm_size(MPI_COMM_WORLD, &worldsize);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	STARPU_MPI_ASSERT_MSG(ret == MPI_SUCCESS, "MPI_Barrier returning %s", _starpu_mpi_get_mpi_code(ret));

	/* We generate a "unique" key so that we can make sure that different
	 * FxT traces come from the same MPI run. */
	int random_number;

	/* XXX perhaps we don't want to generate a new seed if the application
	 * specified some reproductible behaviour ? */
	if (rank == 0)
	{
		srand(time(NULL));
		random_number = rand();
	}

	ret = MPI_Bcast(&random_number, 1, MPI_INT, 0, MPI_COMM_WORLD);
	STARPU_MPI_ASSERT_MSG(ret == MPI_SUCCESS, "MPI_Bcast returning %s", _starpu_mpi_get_mpi_code(ret));

	_STARPU_MPI_TRACE_BARRIER(rank, worldsize, random_number);

	_STARPU_MPI_DEBUG(3, "unique key %x\n", random_number);
#endif
}

static
int _starpu_mpi_initialize(int *argc, char ***argv, int initialize_mpi, MPI_Comm comm)
{
	STARPU_PTHREAD_MUTEX_INIT(&mutex, NULL);
	STARPU_PTHREAD_COND_INIT(&cond_progression, NULL);
	STARPU_PTHREAD_COND_INIT(&cond_finished, NULL);
	ready_requests = _starpu_mpi_req_list_new();

	STARPU_PTHREAD_MUTEX_INIT(&detached_requests_mutex, NULL);
	detached_requests = _starpu_mpi_req_list_new();

	STARPU_PTHREAD_MUTEX_INIT(&mutex_posted_requests, NULL);

	struct _starpu_mpi_argc_argv *argc_argv = malloc(sizeof(struct _starpu_mpi_argc_argv));
	argc_argv->initialize_mpi = initialize_mpi;
	argc_argv->argc = argc;
	argc_argv->argv = argv;
	argc_argv->comm = comm;

#ifdef STARPU_MPI_ACTIVITY
	hookid = starpu_progression_hook_register(_starpu_mpi_progression_hook_func, NULL);
	STARPU_MPI_ASSERT_MSG(hookid >= 0, "starpu_progression_hook_register failed");
#endif /* STARPU_MPI_ACTIVITY */

#ifdef STARPU_SIMGRID
	_starpu_mpi_progress_thread_func(argc_argv);
	return 0;
#else
	STARPU_PTHREAD_CREATE(&progress_thread, NULL, _starpu_mpi_progress_thread_func, argc_argv);

	STARPU_PTHREAD_MUTEX_LOCK(&mutex);
	while (!running)
		STARPU_PTHREAD_COND_WAIT(&cond_progression, &mutex);
	STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);

	return 0;
#endif
}

#ifdef STARPU_SIMGRID
/* This is called before application's main, to initialize SMPI before we can
 * create MSG processes to run application's main */
int _starpu_mpi_simgrid_init(int argc, char *argv[])
{
	return _starpu_mpi_initialize(&argc, &argv, 1, MPI_COMM_WORLD);
}
#endif

int starpu_mpi_init_comm(int *argc, char ***argv, int initialize_mpi, MPI_Comm comm)
{
#ifdef STARPU_SIMGRID
	STARPU_MPI_ASSERT_MSG(initialize_mpi, "application has to let StarPU initialize MPI");
	return 0;
#else
	return _starpu_mpi_initialize(argc, argv, initialize_mpi, comm);
#endif
}

int starpu_mpi_init(int *argc, char ***argv, int initialize_mpi)
{
	return starpu_mpi_init_comm(argc, argv, initialize_mpi, MPI_COMM_WORLD);
}

int starpu_mpi_initialize(void)
{
#ifdef STARPU_SIMGRID
	STARPU_MPI_ASSERT_MSG(0, "application has to let StarPU initialize MPI");
	return 0;
#else
	return _starpu_mpi_initialize(NULL, NULL, 0, MPI_COMM_WORLD);
#endif
}

int starpu_mpi_initialize_extended(int *rank, int *world_size)
{
#ifdef STARPU_SIMGRID
	*world_size = _mpi_world_size;
	*rank = _mpi_world_rank;
	return 0;
#else
	int ret;

	ret = _starpu_mpi_initialize(NULL, NULL, 1, MPI_COMM_WORLD);
	if (ret == 0)
	{
		_STARPU_DEBUG("Calling MPI_Comm_rank\n");
		MPI_Comm_rank(MPI_COMM_WORLD, rank);
		MPI_Comm_size(MPI_COMM_WORLD, world_size);
	}
	return ret;
#endif
}

int starpu_mpi_shutdown(void)
{
	void *value;
	int rank, world_size;

	/* We need to get the rank before calling MPI_Finalize to pass to _starpu_mpi_comm_amounts_display() */
	starpu_mpi_comm_rank(MPI_COMM_WORLD, &rank);
	starpu_mpi_comm_size(MPI_COMM_WORLD, &world_size);

	/* kill the progression thread */
	STARPU_PTHREAD_MUTEX_LOCK(&mutex);
	running = 0;
	STARPU_PTHREAD_COND_BROADCAST(&cond_progression);
	STARPU_PTHREAD_MUTEX_UNLOCK(&mutex);

	starpu_pthread_join(progress_thread, &value);

#ifdef STARPU_MPI_ACTIVITY
	starpu_progression_hook_deregister(hookid);
#endif /* STARPU_MPI_ACTIVITY */

	_STARPU_MPI_TRACE_STOP(rank, world_size);

	/* free the request queues */
	_starpu_mpi_req_list_delete(detached_requests);
	_starpu_mpi_req_list_delete(ready_requests);

	_starpu_mpi_comm_amounts_display(rank);
	_starpu_mpi_comm_amounts_free();
	_starpu_mpi_cache_free(world_size);
	_starpu_mpi_tag_free();
	_starpu_mpi_comm_free();

	return 0;
}

void _starpu_mpi_clear_cache(starpu_data_handle_t data_handle)
{
	_starpu_mpi_data_release_tag(data_handle);
	struct _starpu_mpi_node_tag *mpi_data = data_handle->mpi_data;
	_starpu_mpi_cache_flush(mpi_data->comm, data_handle);
	free(data_handle->mpi_data);
}

void starpu_mpi_data_register_comm(starpu_data_handle_t data_handle, int tag, int rank, MPI_Comm comm)
{
	struct _starpu_mpi_node_tag *mpi_data;
	if (data_handle->mpi_data)
	{
		mpi_data = data_handle->mpi_data;
	}
	else
	{
		mpi_data = calloc(1, sizeof(struct _starpu_mpi_node_tag));
		data_handle->mpi_data = mpi_data;
		_starpu_mpi_data_register_tag(data_handle, tag);
		_starpu_data_set_unregister_hook(data_handle, _starpu_mpi_clear_cache);
	}

	if (tag != -1)
	{
		mpi_data->data_tag = tag;
	}
	if (rank != -1)
	{
		mpi_data->rank = rank;
		mpi_data->comm = comm;
		_starpu_mpi_comm_register(comm);
	}
}

void starpu_mpi_data_set_rank_comm(starpu_data_handle_t handle, int rank, MPI_Comm comm)
{
	starpu_mpi_data_register_comm(handle, -1, rank, comm);
}

void starpu_mpi_data_set_tag(starpu_data_handle_t handle, int tag)
{
	starpu_mpi_data_register_comm(handle, tag, -1, MPI_COMM_WORLD);
}

int starpu_mpi_data_get_rank(starpu_data_handle_t data)
{
	STARPU_ASSERT_MSG(data->mpi_data, "starpu_mpi_data_register MUST be called for data %p\n", data);
	return ((struct _starpu_mpi_node_tag *)(data->mpi_data))->rank;
}

int starpu_mpi_data_get_tag(starpu_data_handle_t data)
{
	STARPU_ASSERT_MSG(data->mpi_data, "starpu_mpi_data_register MUST be called for data %p\n", data);
	return ((struct _starpu_mpi_node_tag *)(data->mpi_data))->data_tag;
}

int starpu_mpi_comm_size(MPI_Comm comm, int *size)
{
#ifdef STARPU_SIMGRID
	STARPU_MPI_ASSERT_MSG(comm == MPI_COMM_WORLD, "StarPU-SMPI only works with MPI_COMM_WORLD for now");
	*size = _mpi_world_size;
	return 0;
#else
	return MPI_Comm_size(comm, size);
#endif
}

int starpu_mpi_comm_rank(MPI_Comm comm, int *rank)
{
#ifdef STARPU_SIMGRID
	STARPU_MPI_ASSERT_MSG(comm == MPI_COMM_WORLD, "StarPU-SMPI only works with MPI_COMM_WORLD for now");
	*rank = _mpi_world_rank;
	return 0;
#else
	return MPI_Comm_rank(comm, rank);
#endif
}

int starpu_mpi_world_rank(void)
{
	int rank;
	starpu_mpi_comm_rank(MPI_COMM_WORLD, &rank);
	return rank;
}
