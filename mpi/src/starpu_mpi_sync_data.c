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

#include <stdlib.h>
#include <starpu_mpi.h>
#include <starpu_mpi_sync_data.h>
#include <starpu_mpi_private.h>
#include <common/uthash.h>

struct _starpu_mpi_sync_data_handle_hashlist
{
	struct _starpu_mpi_req_list *list;
	UT_hash_handle hh;
	struct _starpu_mpi_node_tag node_tag;
};

/** stores data which have been received by MPI but have not been requested by the application */
static starpu_pthread_mutex_t _starpu_mpi_sync_data_handle_mutex;
static struct _starpu_mpi_sync_data_handle_hashlist *_starpu_mpi_sync_data_handle_hashmap = NULL;
static int _starpu_mpi_sync_data_handle_hashmap_count = 0;

void _starpu_mpi_sync_data_init(void)
{
	_starpu_mpi_sync_data_handle_hashmap = NULL;
	STARPU_PTHREAD_MUTEX_INIT(&_starpu_mpi_sync_data_handle_mutex, NULL);
	_starpu_mpi_sync_data_handle_hashmap_count = 0;
}

void _starpu_mpi_sync_data_free(void)
{
	struct _starpu_mpi_sync_data_handle_hashlist *current, *tmp;
	HASH_ITER(hh, _starpu_mpi_sync_data_handle_hashmap, current, tmp)
	{
		_starpu_mpi_req_list_delete(current->list);
		HASH_DEL(_starpu_mpi_sync_data_handle_hashmap, current);
		free(current);
	}
	STARPU_PTHREAD_MUTEX_DESTROY(&_starpu_mpi_sync_data_handle_mutex);
}

#ifdef STARPU_VERBOSE
static
void _starpu_mpi_sync_data_handle_display_hash(struct _starpu_mpi_node_tag *node_tag)
{
	struct _starpu_mpi_sync_data_handle_hashlist *hashlist;
	HASH_FIND(hh, _starpu_mpi_sync_data_handle_hashmap, node_tag, sizeof(struct _starpu_mpi_node_tag), hashlist);

	if (hashlist == NULL)
	{
		_STARPU_MPI_DEBUG(60, "Hashlist for comm %p source %d and tag %d does not exist\n", node_tag->comm, node_tag->rank, node_tag->data_tag);
	}
	else if (_starpu_mpi_req_list_empty(hashlist->list))
	{
		_STARPU_MPI_DEBUG(60, "Hashlist for comm %p source %d and tag %d is empty\n", node_tag->comm, node_tag->rank, node_tag->data_tag);
	}
	else
	{
		struct _starpu_mpi_req *cur;
		for (cur = _starpu_mpi_req_list_begin(hashlist->list) ;
		     cur != _starpu_mpi_req_list_end(hashlist->list);
		     cur = _starpu_mpi_req_list_next(cur))
		{
			_STARPU_MPI_DEBUG(60, "Element for comm %p source %d and tag %d: %p\n", node_tag->comm, node_tag->rank, node_tag->data_tag, cur);
		}
	}
}
#endif

void _starpu_mpi_sync_data_check_termination(void)
{
	STARPU_ASSERT_MSG(_starpu_mpi_sync_data_handle_hashmap_count == 0, "Number of sync received messages left is not zero, did you forget to post a receive corresponding to a send?");
}

int _starpu_mpi_sync_data_count(void)
{
	return _starpu_mpi_sync_data_handle_hashmap_count;
}

struct _starpu_mpi_req *_starpu_mpi_sync_data_find(int data_tag, int source, MPI_Comm comm)
{
	struct _starpu_mpi_req *req;
	struct _starpu_mpi_node_tag node_tag;
	struct _starpu_mpi_sync_data_handle_hashlist *found;

	memset(&node_tag, 0, sizeof(struct _starpu_mpi_node_tag));
	node_tag.comm = comm;
	node_tag.rank = source;
	node_tag.data_tag = data_tag;

	_STARPU_MPI_DEBUG(60, "Looking for sync_data_handle with comm %p source %d tag %d in the hashmap\n", comm, source, data_tag);

	STARPU_PTHREAD_MUTEX_LOCK(&_starpu_mpi_sync_data_handle_mutex);
	HASH_FIND(hh, _starpu_mpi_sync_data_handle_hashmap, &node_tag, sizeof(struct _starpu_mpi_node_tag), found);
	if (found == NULL)
	{
		req = NULL;
	}
	else
	{
		if (_starpu_mpi_req_list_empty(found->list))
		{
			req = NULL;
		}
		else
		{
			req = _starpu_mpi_req_list_pop_front(found->list);
			_starpu_mpi_sync_data_handle_hashmap_count --;
		}
	}
	STARPU_PTHREAD_MUTEX_UNLOCK(&_starpu_mpi_sync_data_handle_mutex);
	_STARPU_MPI_DEBUG(60, "Found sync_data_handle %p with comm %p source %d tag %d in the hashmap\n", req, comm, source, data_tag);
	return req;
}

void _starpu_mpi_sync_data_add(struct _starpu_mpi_req *sync_req)
{
	struct _starpu_mpi_sync_data_handle_hashlist *hashlist;

	_STARPU_MPI_DEBUG(2000, "Adding sync_req %p with comm %p source %d tag %d in the hashmap\n", sync_req, sync_req->node_tag.comm, sync_req->node_tag.rank, sync_req->node_tag.data_tag);

	STARPU_PTHREAD_MUTEX_LOCK(&_starpu_mpi_sync_data_handle_mutex);
	HASH_FIND(hh, _starpu_mpi_sync_data_handle_hashmap, &sync_req->node_tag, sizeof(struct _starpu_mpi_node_tag), hashlist);
	if (hashlist == NULL)
	{
		hashlist = malloc(sizeof(struct _starpu_mpi_sync_data_handle_hashlist));
		hashlist->list = _starpu_mpi_req_list_new();
		hashlist->node_tag = sync_req->node_tag;
		HASH_ADD(hh, _starpu_mpi_sync_data_handle_hashmap, node_tag, sizeof(hashlist->node_tag), hashlist);
	}
	_starpu_mpi_req_list_push_back(hashlist->list, sync_req);
	_starpu_mpi_sync_data_handle_hashmap_count ++;
	STARPU_PTHREAD_MUTEX_UNLOCK(&_starpu_mpi_sync_data_handle_mutex);
#ifdef STARPU_VERBOSE
	_starpu_mpi_sync_data_handle_display_hash(&sync_req->node_tag);
#endif
}

