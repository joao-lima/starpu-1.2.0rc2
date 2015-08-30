/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009-2015  Université de Bordeaux
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

#include <datawizard/memory_manager.h>
#include <datawizard/memory_nodes.h>
#include <datawizard/memalloc.h>
#include <datawizard/footprint.h>
#include <core/disk.h>
#include <starpu.h>
#include <common/uthash.h>

/* This per-node RW-locks protect mc_list and memchunk_cache entries */
/* Note: handle header lock is always taken before this (normal add/remove case) */
static struct _starpu_spinlock mc_lock[STARPU_MAXNODES];

/* Potentially in use memory chunks. The beginning of the list is clean (home
 * node has a copy of the data, or the data is being transferred there), the
 * remainder of the list may not be clean. */
static struct _starpu_mem_chunk_list mc_list[STARPU_MAXNODES];
/* This is a shortcut inside the mc_list to the first potentially dirty MC. All
 * MC before this are clean, MC before this only *may* be clean. */
static struct _starpu_mem_chunk *mc_dirty_head[STARPU_MAXNODES];
/* Number of elements in mc_list, number of elements in the clean part of
 * mc_list plus the non-automatically allocated elements (which are thus always
 * considered as clean) */
static unsigned mc_nb[STARPU_MAXNODES], mc_clean_nb[STARPU_MAXNODES];

/* TODO: no home doesn't mean always clean, should push to larger memory nodes */
#define MC_LIST_PUSH_BACK(node, mc) do {				 \
	_starpu_mem_chunk_list_push_back(&mc_list[node], mc);		 \
	if ((mc)->clean || (mc)->home)					 \
		/* This is clean */					 \
		mc_clean_nb[node]++;					 \
	else if (!mc_dirty_head[node])					 \
		/* This is the only dirty element for now */		 \
		mc_dirty_head[node] = mc;				 \
	mc_nb[node]++;							 \
} while(0)

/* Put new clean mc at the end of the clean part of mc_list, i.e. just before mc_dirty_head (if any) */
#define MC_LIST_PUSH_CLEAN(node, mc) do {				 \
	if (mc_dirty_head[node])						 \
		_starpu_mem_chunk_list_insert_before(&mc_list[node], mc, mc_dirty_head[node]); \
	else								 \
		_starpu_mem_chunk_list_push_back(&mc_list[node], mc);	 \
	/* This is clean */						 \
	mc_clean_nb[node]++;						 \
	mc_nb[node]++;							 \
} while (0)

#define MC_LIST_ERASE(node, mc) do {					 \
	if ((mc)->clean || (mc)->home)					 \
		mc_clean_nb[node]--; /* One clean element less */	 \
	if ((mc) == mc_dirty_head[node])				 \
		/* This was the dirty head */				 \
		mc_dirty_head[node] = _starpu_mem_chunk_list_next((mc)); \
	/* One element less */						 \
	mc_nb[node]--;							 \
	/* Remove element */						 \
	_starpu_mem_chunk_list_erase(&mc_list[node], (mc));		 \
	/* Notify whoever asked for it */				 \
	if ((mc)->remove_notify)					 \
	{								 \
		*((mc)->remove_notify) = NULL;				 \
		(mc)->remove_notify = NULL;				 \
	} \
} while (0)

/* Explicitly caches memory chunks that can be reused */
struct mc_cache_entry
{
	UT_hash_handle hh;
	struct _starpu_mem_chunk_list list;
	uint32_t footprint;
};
static struct mc_cache_entry *mc_cache[STARPU_MAXNODES];
static int mc_cache_nb[STARPU_MAXNODES];
static starpu_ssize_t mc_cache_size[STARPU_MAXNODES];

/* Whether some thread is currently tidying this node */
static unsigned tidying[STARPU_MAXNODES];

/* When reclaiming memory to allocate, we reclaim MAX(what_is_to_reclaim_on_device, data_size_coefficient*data_size) */
const unsigned starpu_memstrategy_data_size_coefficient=2;

static int get_better_disk_can_accept_size(starpu_data_handle_t handle, unsigned node);
static unsigned choose_target(starpu_data_handle_t handle, unsigned node);

void _starpu_init_mem_chunk_lists(void)
{
	unsigned i;
	for (i = 0; i < STARPU_MAXNODES; i++)
	{
		_starpu_spin_init(&mc_lock[i]);
		_starpu_mem_chunk_list_init(&mc_list[i]);
		STARPU_HG_DISABLE_CHECKING(mc_cache_size[i]);
		STARPU_HG_DISABLE_CHECKING(mc_nb[i]);
		STARPU_HG_DISABLE_CHECKING(mc_clean_nb[i]);
	}
}

void _starpu_deinit_mem_chunk_lists(void)
{
	unsigned i;
	for (i = 0; i < STARPU_MAXNODES; i++)
	{
		struct mc_cache_entry *entry, *tmp;
		STARPU_ASSERT(mc_nb[i] == 0);
		STARPU_ASSERT(mc_clean_nb[i] == 0);
		STARPU_ASSERT(mc_dirty_head[i] == NULL);
		HASH_ITER(hh, mc_cache[i], entry, tmp)
		{
			HASH_DEL(mc_cache[i], entry);
			free(entry);
		}
		STARPU_ASSERT(mc_cache_nb[i] == 0);
		STARPU_ASSERT(mc_cache_size[i] == 0);
		_starpu_spin_destroy(&mc_lock[i]);
	}
}

/*
 *	Manipulate subtrees
 */

static void unlock_all_subtree(starpu_data_handle_t handle)
{
	/* lock all sub-subtrees children
	 * Note that this is done in the reverse order of the
	 * lock_all_subtree so that we avoid deadlock */
	unsigned i;
	for (i =0; i < handle->nchildren; i++)
	{
		unsigned child = handle->nchildren - 1 - i;
		starpu_data_handle_t child_handle = starpu_data_get_child(handle, child);
		unlock_all_subtree(child_handle);
	}

	_starpu_spin_unlock(&handle->header_lock);
}

static int lock_all_subtree(starpu_data_handle_t handle)
{
	int child;

	/* lock parent */
	if (_starpu_spin_trylock(&handle->header_lock))
		/* the handle is busy, abort */
		return 0;

	/* lock all sub-subtrees children */
	for (child = 0; child < (int) handle->nchildren; child++)
	{
		if (!lock_all_subtree(starpu_data_get_child(handle, child)))
		{
			/* Some child is busy, abort */
			while (--child >= 0)
				/* Unlock what we have already uselessly locked */
				unlock_all_subtree(starpu_data_get_child(handle, child));
			return 0;
		}
	}

	return 1;
}

static unsigned may_free_subtree(starpu_data_handle_t handle, unsigned node)
{
	/* we only free if no one refers to the leaf */
	uint32_t refcnt = _starpu_get_data_refcnt(handle, node);
	if (refcnt)
		return 0;

	if (!handle->nchildren)
		return 1;

	/* look into all sub-subtrees children */
	unsigned child;
	for (child = 0; child < handle->nchildren; child++)
	{
		unsigned res;
		starpu_data_handle_t child_handle = starpu_data_get_child(handle, child);
		res = may_free_subtree(child_handle, node);
		if (!res) return 0;
	}

	/* no problem was found */
	return 1;
}

static void transfer_subtree_to_node(starpu_data_handle_t handle, unsigned src_node,
				     unsigned dst_node)
{
	unsigned i;
	unsigned last = 0;
	unsigned cnt;
	int ret;

	STARPU_ASSERT(dst_node != src_node);

	if (handle->nchildren == 0)
	{
		struct _starpu_data_replicate *src_replicate = &handle->per_node[src_node];
		struct _starpu_data_replicate *dst_replicate = &handle->per_node[dst_node];

		/* this is a leaf */
		switch(src_replicate->state)
		{
		case STARPU_OWNER:
			/* Take temporary references on the replicates */
			_starpu_spin_checklocked(&handle->header_lock);
			src_replicate->refcnt++;
			dst_replicate->refcnt++;
			handle->busy_count+=2;

			/* Note: this may release the header lock if
			 * destination is not allocated yet */
			ret = _starpu_driver_copy_data_1_to_1(handle, src_replicate, dst_replicate, 0, NULL, 1);
			STARPU_ASSERT(ret == 0);

			src_replicate->refcnt--;
			dst_replicate->refcnt--;
			STARPU_ASSERT(handle->busy_count >= 2);
			handle->busy_count -= 2;
			ret = _starpu_data_check_not_busy(handle);
			STARPU_ASSERT(ret == 0);

			dst_replicate->state = STARPU_SHARED;

			/* NOTE: now that it's SHARED on dst, FALLTHROUGH to
			 * update src and perhaps make dst OWNER */

		case STARPU_SHARED:
			/* some other node may have the copy */
			src_replicate->state = STARPU_INVALID;

			/* count the number of copies */
			cnt = 0;
			for (i = 0; i < STARPU_MAXNODES; i++)
			{
				if (handle->per_node[i].state == STARPU_SHARED)
				{
					cnt++;
					last = i;
				}
			}
			STARPU_ASSERT(cnt > 0);

			if (cnt == 1)
				handle->per_node[last].state = STARPU_OWNER;

			break;
		case STARPU_INVALID:
			/* nothing to be done */
			break;
		default:
			STARPU_ABORT();
			break;
		}
	}
	else
	{
		/* lock all sub-subtrees children */
		unsigned child;
		for (child = 0; child < handle->nchildren; child++)
		{
			starpu_data_handle_t child_handle = starpu_data_get_child(handle, child);
			transfer_subtree_to_node(child_handle, src_node, dst_node);
		}
	}
}

static void notify_handle_children(starpu_data_handle_t handle, struct _starpu_data_replicate *replicate, unsigned node)
{
	unsigned child;

	replicate->allocated = 0;

	/* XXX why do we need that ? */
	replicate->automatically_allocated = 0;

	for (child = 0; child < handle->nchildren; child++)
	{
		/* Notify children that their buffer has been deallocated too */
		starpu_data_handle_t child_handle = starpu_data_get_child(handle, child);
		notify_handle_children(child_handle, &child_handle->per_node[node], node);
	}
}

static size_t free_memory_on_node(struct _starpu_mem_chunk *mc, unsigned node)
{
	size_t freed = 0;

	STARPU_ASSERT(mc->ops);
	STARPU_ASSERT(mc->ops->free_data_on_node);

	starpu_data_handle_t handle = mc->data;

	struct _starpu_data_replicate *replicate = mc->replicate;

	if (handle)
		_starpu_spin_checklocked(&handle->header_lock);

	if (mc->automatically_allocated &&
		(!handle || replicate->refcnt == 0))
	{
		void *data_interface;

		if (handle)
			STARPU_ASSERT(replicate->allocated);

#if defined(STARPU_USE_CUDA) && defined(HAVE_CUDA_MEMCPY_PEER) && !defined(STARPU_SIMGRID)
		if (starpu_node_get_kind(node) == STARPU_CUDA_RAM)
		{
			/* To facilitate the design of interface, we set the
			 * proper CUDA device in case it is needed. This avoids
			 * having to set it again in the free method of each
			 * interface. */
			starpu_cuda_set_device(_starpu_memory_node_get_devid(node));
		}
#endif

		if (handle)
			data_interface = replicate->data_interface;
		else
			data_interface = mc->chunk_interface;
		STARPU_ASSERT(data_interface);

		_STARPU_TRACE_START_FREE(node, mc->size);
		mc->ops->free_data_on_node(data_interface, node);
		_STARPU_TRACE_END_FREE(node);

		if (handle)
			notify_handle_children(handle, replicate, node);

		freed = mc->size;

		if (handle)
			STARPU_ASSERT(replicate->refcnt == 0);
	}

	return freed;
}



/* mc_lock is held */
static size_t do_free_mem_chunk(struct _starpu_mem_chunk *mc, unsigned node)
{
	size_t size;
	starpu_data_handle_t handle = mc->data;

	if (handle)
	{
		_starpu_spin_checklocked(&handle->header_lock);
		mc->size = _starpu_data_get_size(handle);
	}

	if (mc->replicate)
		mc->replicate->mc=NULL;

	/* free the actual buffer */
	size = free_memory_on_node(mc, node);

	/* remove the mem_chunk from the list */
	MC_LIST_ERASE(node, mc);

	_starpu_mem_chunk_delete(mc);

	return size;
}

/* This function is called for memory chunks that are possibly in used (ie. not
 * in the cache). They should therefore still be associated to a handle. */
/* mc_lock is held and may be temporarily released! */
static size_t try_to_free_mem_chunk(struct _starpu_mem_chunk *mc, unsigned node)
{
	size_t freed = 0;

	starpu_data_handle_t handle;
	handle = mc->data;
	STARPU_ASSERT(handle);

	/* This data should be written through to this node, avoid dropping it! */
	if (handle->wt_mask & (1<<node))
		return 0;

	/* This data was registered from this node, we will not be able to drop it anyway */
	if ((int) node == handle->home_node)
		return 0;

	/* REDUX memchunk */
	if (mc->relaxed_coherency == 2)
	{
		/* TODO: reduce it back to e.g. main memory */
	}
	else
	/* Either it's a "relaxed coherency" memchunk (SCRATCH), or it's a
	 * memchunk that could be used with filters. */
	if (mc->relaxed_coherency == 1)
	{
		STARPU_ASSERT(mc->replicate);

		if (_starpu_spin_trylock(&handle->header_lock))
			/* Handle is busy, abort */
			return 0;

		if (mc->replicate->refcnt == 0)
		{
			/* Note that there is no need to transfer any data or
			 * to update the status in terms of MSI protocol
			 * because this memchunk is associated to a replicate
			 * in "relaxed coherency" mode. */
			freed = do_free_mem_chunk(mc, node);
		}

		_starpu_spin_unlock(&handle->header_lock);
	}
	/* try to lock all the subtree */
	else if (lock_all_subtree(handle))
	{
		/* check if they are all "free" */
		if (may_free_subtree(handle, node))
		{
			int target = -1;

			/* XXX Considering only owner to invalidate */

			STARPU_ASSERT(handle->per_node[node].refcnt == 0);

			/* in case there was nobody using that buffer, throw it
			 * away after writing it back to main memory */
			
			/* choose the best target */
			target = choose_target(handle, node);

			if (target != -1)
			{
				/* Should have been avoided in our caller */
				STARPU_ASSERT(!mc->remove_notify);
				mc->remove_notify = &mc;
				_starpu_spin_unlock(&mc_lock[node]);
#ifdef STARPU_MEMORY_STATS
				if (handle->per_node[node].state == STARPU_OWNER)
					_starpu_memory_handle_stats_invalidated(handle, node);
#endif
				_STARPU_TRACE_START_WRITEBACK(node);
				/* Note: this may need to allocate data etc.
				 * and thus release the header lock, take
				 * mc_lock, etc. */
				transfer_subtree_to_node(handle, node, target);
				_STARPU_TRACE_END_WRITEBACK(node);
#ifdef STARPU_MEMORY_STATS
				_starpu_memory_handle_stats_loaded_owner(handle, target);
#endif
				_starpu_spin_lock(&mc_lock[node]);

				if (mc)
				{
					STARPU_ASSERT(mc->remove_notify == &mc);
					mc->remove_notify = NULL;
					/* mc is still associated with the old
					 * handle, now free it.
					 */
					STARPU_ASSERT(handle->per_node[node].refcnt == 0);

					/* now the actual buffer may be freed */
					freed = do_free_mem_chunk(mc, node);
				}
			}
		}

		/* unlock the tree */
		unlock_all_subtree(handle);
	}
	return freed;
}

#ifdef STARPU_USE_ALLOCATION_CACHE
/* We assume that mc_lock[node] is taken. is_already_in_mc_list indicates
 * that the mc is already in the list of buffers that are possibly used, and
 * therefore not in the cache. */
static void reuse_mem_chunk(unsigned node, struct _starpu_data_replicate *new_replicate, struct _starpu_mem_chunk *mc, unsigned is_already_in_mc_list)
{
	void *data_interface;

	/* we found an appropriate mem chunk: so we get it out
	 * of the "to free" list, and reassign it to the new
	 * piece of data */

	struct _starpu_data_replicate *old_replicate = mc->replicate;
	if (old_replicate)
	{
		old_replicate->allocated = 0;
		old_replicate->automatically_allocated = 0;
		old_replicate->initialized = 0;
		data_interface = old_replicate->data_interface;
	}
	else
		data_interface = mc->chunk_interface;

	STARPU_ASSERT(new_replicate->data_interface);
	STARPU_ASSERT(data_interface);
	memcpy(new_replicate->data_interface, data_interface, mc->size_interface);

	if (!old_replicate)
	{
		/* Free the copy that we made */
		free(mc->chunk_interface);
		mc->chunk_interface = NULL;
	}

	/* XXX: We do not actually reuse the mc at the moment, only the interface */

	/* mc->data = new_replicate->handle; */
	/* mc->footprint, mc->ops, mc->size_interface, mc->automatically_allocated should be
 	 * unchanged ! */

	/* remove the mem chunk from the list of active memory chunks, register_mem_chunk will put it back later */
	if (is_already_in_mc_list)
		MC_LIST_ERASE(node, mc);

	free(mc);
}

/* mc_lock is held and may be temporarily released! */
static unsigned try_to_reuse_mem_chunk(struct _starpu_mem_chunk *mc, unsigned node, struct _starpu_data_replicate *replicate, unsigned is_already_in_mc_list)
{
	unsigned success = 0;

	starpu_data_handle_t old_data;

	old_data = mc->data;

	STARPU_ASSERT(old_data);

	/* try to lock all the subtree */
	/* and check if they are all "free" */
	if (lock_all_subtree(old_data))
	{
		if (may_free_subtree(old_data, node))
		{
			/* Should have been avoided in our caller */
			STARPU_ASSERT(!mc->remove_notify);
			mc->remove_notify = &mc;
			/* in case there was nobody using that buffer, throw it
			 * away after writing it back to main memory */
			_starpu_spin_unlock(&mc_lock[node]);
			_STARPU_TRACE_START_WRITEBACK(node);
			transfer_subtree_to_node(old_data, node, STARPU_MAIN_RAM);
			_STARPU_TRACE_END_WRITEBACK(node);
			_starpu_spin_lock(&mc_lock[node]);

			if (mc)
			{
				STARPU_ASSERT(mc->remove_notify == &mc);
				mc->remove_notify = NULL;
				/* mc is still associated with the old
				 * handle, now replace the previous data
				 */
				reuse_mem_chunk(node, replicate, mc, is_already_in_mc_list);
				success = 1;
			}
		}

		/* unlock the tree */
		unlock_all_subtree(old_data);
	}

	return success;
}

static int _starpu_data_interface_compare(void *data_interface_a, struct starpu_data_interface_ops *ops_a,
                                          void *data_interface_b, struct starpu_data_interface_ops *ops_b)
{
	if (ops_a->interfaceid != ops_b->interfaceid)
		return -1;

	int ret = ops_a->compare(data_interface_a, data_interface_b);

	return ret;
}

/* This function must be called with mc_lock[node] taken */
static struct _starpu_mem_chunk *_starpu_memchunk_cache_lookup_locked(unsigned node, starpu_data_handle_t handle, uint32_t footprint)
{
	/* go through all buffers in the cache */
	struct mc_cache_entry *entry;

	HASH_FIND(hh, mc_cache[node], &footprint, sizeof(footprint), entry);
	if (!entry)
		/* No data with that footprint */
		return NULL;

	struct _starpu_mem_chunk *mc;
	for (mc = _starpu_mem_chunk_list_begin(&entry->list);
	     mc != _starpu_mem_chunk_list_end(&entry->list);
	     mc = _starpu_mem_chunk_list_next(mc))
	{
		/* Is that a false hit ? (this is _very_ unlikely) */
		if (_starpu_data_interface_compare(handle->per_node[node].data_interface, handle->ops, mc->chunk_interface, mc->ops) != 1)
			continue;

		/* Cache hit */

		/* Remove from the cache */
		_starpu_mem_chunk_list_erase(&entry->list, mc);
		mc_cache_nb[node]--;
		STARPU_ASSERT(mc_cache_nb[node] >= 0);
		mc_cache_size[node] -= mc->size;
		STARPU_ASSERT(mc_cache_size[node] >= 0);
		return mc;
	}

	/* This is a cache miss */
	return NULL;
}

/* this function looks for a memory chunk that matches a given footprint in the
 * list of mem chunk that need to be freed. */
static unsigned try_to_find_reusable_mem_chunk(unsigned node, starpu_data_handle_t data, struct _starpu_data_replicate *replicate, uint32_t footprint)
{
	struct _starpu_mem_chunk *mc, *orig_next_mc, *next_mc;
	int success = 0;

	_starpu_spin_lock(&mc_lock[node]);
	/* go through all buffers in the cache */
	mc = _starpu_memchunk_cache_lookup_locked(node, data, footprint);
	if (mc)
	{
		/* We found an entry in the cache so we can reuse it */
		reuse_mem_chunk(node, replicate, mc, 0);
		_starpu_spin_unlock(&mc_lock[node]);
		return 1;
	}

	if (!_starpu_has_not_important_data)
	{
		_starpu_spin_unlock(&mc_lock[node]);
		return 0;
	}

restart:
	/* now look for some non essential data in the active list */
	for (mc = _starpu_mem_chunk_list_begin(&mc_list[node]);
	     mc != _starpu_mem_chunk_list_end(&mc_list[node]) && !success;
	     mc = next_mc)
	{
		/* there is a risk that the memory chunk is freed before next
		 * iteration starts: so we compute the next element of the list
		 * now */
		orig_next_mc = next_mc = _starpu_mem_chunk_list_next(mc);
		if (mc->remove_notify)
			/* Somebody already working here, skip */
			continue;
		if (next_mc)
		{
			if (next_mc->remove_notify)
				/* Somebody already working here, skip */
				continue;
			next_mc->remove_notify = &next_mc;
		}

		if (mc->data->is_not_important && (mc->footprint == footprint))
		{
//			fprintf(stderr, "found a candidate ...\n");
			/* Note: this may unlock mc_list! */
			success = try_to_reuse_mem_chunk(mc, node, replicate, 1);
		}

		if (orig_next_mc)
		{
			if (!next_mc)
				/* Oops, somebody dropped the next item while we were
				 * not keeping the mc_lock. Restart from the beginning
				 * of the list */
				goto restart;
			else
			{
				STARPU_ASSERT(next_mc->remove_notify == &next_mc);
				next_mc->remove_notify = NULL;
			}
		}
	}
	_starpu_spin_unlock(&mc_lock[node]);

	return success;
}
#endif

/*
 * Free the memory chuncks that are explicitely tagged to be freed.
 */
static size_t flush_memchunk_cache(unsigned node, size_t reclaim)
{
	struct _starpu_mem_chunk *mc;
	struct mc_cache_entry *entry, *tmp;

	size_t freed = 0;

	_starpu_spin_lock(&mc_lock[node]);
	HASH_ITER(hh, mc_cache[node], entry, tmp)
	{
		while (!_starpu_mem_chunk_list_empty(&entry->list))
		{
			mc = _starpu_mem_chunk_list_pop_front(&entry->list);
			STARPU_ASSERT(!mc->data);
			STARPU_ASSERT(!mc->replicate);

			mc_cache_nb[node]--;
			STARPU_ASSERT(mc_cache_nb[node] >= 0);
			mc_cache_size[node] -= mc->size;
			STARPU_ASSERT(mc_cache_size[node] >= 0);
			freed += free_memory_on_node(mc, node);

			free(mc->chunk_interface);
			_starpu_mem_chunk_delete(mc);

			if (reclaim && freed >= reclaim)
				break;
		}

		if (reclaim && freed >= reclaim)
			break;
	}
	_starpu_spin_unlock(&mc_lock[node]);
	return freed;
}

/*
 * Try to free the buffers currently in use on the memory node. If the force
 * flag is set, the memory is freed regardless of coherency concerns (this
 * should only be used at the termination of StarPU for instance). The
 * mc_lock[node] should be taken prior to calling this function.
 */
static size_t free_potentially_in_use_mc(unsigned node, unsigned force, size_t reclaim)
{
	size_t freed = 0;

	struct _starpu_mem_chunk *mc, *next_mc;

	/*
	 * We have to unlock mc_lock before locking header_lock, so we have
	 * to be careful with the list.  We try to do just one pass, by
	 * remembering the next mc to be tried. If it gets dropped, we restart
	 * from zero. So we continue until we go through the whole list without
	 * finding anything to free.
	 */

restart:
	_starpu_spin_lock(&mc_lock[node]);

restart2:
	for (mc = _starpu_mem_chunk_list_begin(&mc_list[node]);
	     mc != _starpu_mem_chunk_list_end(&mc_list[node]) && (!reclaim || freed < reclaim);
	     mc = next_mc)
	{
		/* mc hopefully gets out of the list, we thus need to prefetch
		 * the next element */
		next_mc = _starpu_mem_chunk_list_next(mc);

		if (!force)
		{
			struct _starpu_mem_chunk *orig_next_mc = next_mc;
			if (mc->remove_notify)
				/* Somebody already working here, skip */
				continue;
			if (next_mc)
			{
				if (next_mc->remove_notify)
					/* Somebody already working here, skip */
					continue;
				next_mc->remove_notify = &next_mc;
			}
			/* Note: this may unlock mc_list! */
			freed += try_to_free_mem_chunk(mc, node);

			if (orig_next_mc)
			{
				if (!next_mc)
					/* Oops, somebody dropped the next item while we were
					 * not keeping the mc_lock. Restart from the beginning
					 * of the list */
					goto restart2;
				else
				{
					STARPU_ASSERT(next_mc->remove_notify == &next_mc);
					next_mc->remove_notify = NULL;
				}
			}
		}
		else
		{
			/* Shutting down, really free */
			starpu_data_handle_t handle = mc->data;

			if (_starpu_spin_trylock(&handle->header_lock))
			{
				/* Ergl. We are shutting down, but somebody is
				 * still locking the handle. That's not
				 * supposed to happen, but better be safe by
				 * letting it go through. */
				_starpu_spin_unlock(&mc_lock[node]);
				goto restart;
			}

			/* We must free the memory now, because we are
			 * terminating the drivers: note that data coherency is
			 * not maintained in that case ! */
			freed += do_free_mem_chunk(mc, node);

			_starpu_spin_unlock(&handle->header_lock);
		}
	}
	_starpu_spin_unlock(&mc_lock[node]);

	return freed;
}

size_t _starpu_memory_reclaim_generic(unsigned node, unsigned force, size_t reclaim)
{
	size_t freed = 0;

	if (reclaim && !force)
	{
		static unsigned warned;
		if (!warned)
		{
			if (STARPU_ATOMIC_ADD(&warned, 1) == 1)
			{
				char name[32];
				_starpu_memory_node_get_name(node, name, sizeof(name));
				_STARPU_DISP("Not enough memory left on node %s. Your application data set seems too huge to fit on the device, StarPU will cope by trying to purge %lu MiB out. This message will not be printed again for further purges\n", name, (unsigned long) (reclaim / 1048576));
			}
		}
	}

	/* remove all buffers for which there was a removal request */
	freed += flush_memchunk_cache(node, reclaim);

	/* try to free all allocated data potentially in use */
	if (force || (reclaim && freed<reclaim))
		freed += free_potentially_in_use_mc(node, force, reclaim);

	return freed;

}

/*
 * This function frees all the memory that was implicitely allocated by StarPU
 * (for the data replicates). This is not ensuring data coherency, and should
 * only be called while StarPU is getting shut down.
 */
size_t _starpu_free_all_automatically_allocated_buffers(unsigned node)
{
	return _starpu_memory_reclaim_generic(node, 1, 0);
}

/* Periodic tidy of available memory  */
void starpu_memchunk_tidy(unsigned node)
{
	starpu_ssize_t total = starpu_memory_get_total(node);
	starpu_ssize_t available = starpu_memory_get_available(node);
	size_t target, amount;
	unsigned minimum_p = starpu_get_env_number_default("STARPU_MINIMUM_AVAILABLE_MEM", 5);
	unsigned target_p = starpu_get_env_number_default("STARPU_TARGET_AVAILABLE_MEM", 10);
	unsigned minimum_clean_p = starpu_get_env_number_default("STARPU_MINIMUM_CLEAN_BUFFERS", 0);
	unsigned target_clean_p = starpu_get_env_number_default("STARPU_TARGET_CLEAN_BUFFERS", 0);

	if (mc_clean_nb[node] < (mc_nb[node] * minimum_clean_p) / 100)
	{
		struct _starpu_mem_chunk *mc, *orig_next_mc, *next_mc;
		int skipped = 0;	/* Whether we skipped a dirty MC, and we should thus stop updating mc_dirty_head. */

		_STARPU_DEBUG("%d not clean: %d %d\n", node, mc_clean_nb[node], mc_nb[node]);

		_STARPU_TRACE_START_WRITEBACK_ASYNC(node);
		_starpu_spin_lock(&mc_lock[node]);

		for (mc = mc_dirty_head[node];
			mc && mc_clean_nb[node] < (mc_nb[node] * target_clean_p) / 100;
			mc = next_mc, mc && skipped ? 0 : (mc_dirty_head[node] = mc))
		{
			starpu_data_handle_t handle;

			/* mc may get out of the list, we thus need to prefetch
			 * the next element */
			next_mc = _starpu_mem_chunk_list_next(mc);

			if (mc->home)
				/* Home node, it's always clean */
				continue;
			if (mc->clean)
				/* already clean */
				continue;
			if (next_mc && next_mc->remove_notify)
			{
				/* Somebody already working here, skip */
				skipped = 1;
				continue;
			}

			handle = mc->data;
			STARPU_ASSERT(handle);
			STARPU_ASSERT(handle->home_node != -1);

			if (_starpu_spin_trylock(&handle->header_lock))
			{
				/* the handle is busy, abort */
				skipped = 1;
				continue;
			}

			if (
				/* This data should be written through to this node, avoid
				 * dropping it! */
				handle->wt_mask & (1<<node)
				/* This is partitioned, don't care about the
				 * whole data, we'll work on the subdatas.  */
			     || handle->nchildren
			        /* Somebody is still writing to it */
			     || (_starpu_get_data_refcnt(handle, node) && handle->current_mode == STARPU_W)
				/* REDUX, can't do anything with it, skip it */
			     || mc->relaxed_coherency == 2
			)
			{
				_starpu_spin_unlock(&handle->header_lock);
				continue;
			}

			/* This should have been marked as clean already */
			if (handle->per_node[handle->home_node].state != STARPU_INVALID || mc->relaxed_coherency == 1)
			{
				/* it's actually clean */
				mc->clean = 1;
				mc_clean_nb[node]++;
			}
			else
			{
				/* MC is dirty, submit writeback */

				/* MC will be clean, consider it as such */
				mc->clean = 1;
				mc_clean_nb[node]++;

				orig_next_mc = next_mc;
				if (next_mc)
				{
					STARPU_ASSERT(!next_mc->remove_notify);
					next_mc->remove_notify = &next_mc;
				}

				_starpu_spin_unlock(&mc_lock[node]);
				if (!_starpu_create_request_to_fetch_data(handle, &handle->per_node[handle->home_node], STARPU_R, 2, 1, NULL, NULL))
				{
					/* No request was actually needed??
					 * Odd, but cope with it.  */
					handle = NULL;
				}
				_starpu_spin_lock(&mc_lock[node]);

				if (orig_next_mc)
				{
					if (!next_mc)
						/* Oops, somebody dropped the next item while we were
						 * not keeping the mc_lock. Give up for now, and we'll
						 * see the rest later */
						;
					else
					{
						STARPU_ASSERT(next_mc->remove_notify == &next_mc);
						next_mc->remove_notify = NULL;
					}
				}
			}

			if (handle)
				_starpu_spin_unlock(&handle->header_lock);
		}
		_starpu_spin_unlock(&mc_lock[node]);
		_STARPU_TRACE_END_WRITEBACK_ASYNC(node);
	}

	if (total <= 0)
		return;

	/* Count cached allocation as being available */
	available += mc_cache_size[node];

	if (available >= (total * minimum_p) / 100)
		/* Enough available space, do not trigger reclaiming */
		return;

	/* Not enough available space, reclaim until we reach the target.  */
	target = (total * target_p) / 100;
	amount = target - available;

	if (!STARPU_RUNNING_ON_VALGRIND && tidying[node])
		/* Some thread is already tidying this node, let it do it */
		return;

	if (STARPU_ATOMIC_ADD(&tidying[node], 1) > 1)
		/* Some thread got it before us, let it do it */
		goto out;

	static unsigned warned;
	if (!warned)
	{
		if (STARPU_ATOMIC_ADD(&warned, 1) == 1)
		{
			char name[32];
			_starpu_memory_node_get_name(node, name, sizeof(name));
			_STARPU_DISP("Low memory left on node %s (%luMiB over %luMiB). Your application data set seems too huge to fit on the device, StarPU will cope by trying to purge %lu MiB out. This message will not be printed again for further purges. The thresholds can be tuned using the STARPU_MINIMUM_AVAILABLE_MEM and STARPU_TARGET_AVAILABLE_MEM environment variables.\n", name, (unsigned long) (available / 1048576), (unsigned long) (total / 1048576), (unsigned long) (amount / 1048576));
		}
	}

	_STARPU_TRACE_START_MEMRECLAIM(node,2);
	_STARPU_TRACE_START_MEMRECLAIM(node,2);
	free_potentially_in_use_mc(node, 0, amount);
	_STARPU_TRACE_END_MEMRECLAIM(node,2);
out:
	(void) STARPU_ATOMIC_ADD(&tidying[node], -1);
}

static struct _starpu_mem_chunk *_starpu_memchunk_init(struct _starpu_data_replicate *replicate, size_t interface_size, unsigned home, unsigned automatically_allocated)
{
	struct _starpu_mem_chunk *mc = _starpu_mem_chunk_new();
	starpu_data_handle_t handle = replicate->handle;

	STARPU_ASSERT(handle);
	STARPU_ASSERT(handle->ops);

	mc->data = handle;
	mc->footprint = _starpu_compute_data_footprint(handle);
	mc->ops = handle->ops;
	mc->automatically_allocated = automatically_allocated;
	mc->relaxed_coherency = replicate->relaxed_coherency;
	mc->home = home;
	mc->clean = 0;
	if (replicate->relaxed_coherency == 1)
		/* SCRATCH is always easy to drop, thus clean */
		mc->clean = 1;
	else if (replicate->relaxed_coherency == 0 && handle->home_node != -1 && handle->per_node[replicate->memory_node].state != STARPU_INVALID)
		/* This is a normal data and the home node has the value */
		mc->clean = 1;
	mc->replicate = replicate;
	mc->replicate->mc = mc;
	mc->chunk_interface = NULL;
	mc->size_interface = interface_size;
	mc->remove_notify = NULL;

	return mc;
}

static void register_mem_chunk(starpu_data_handle_t handle, struct _starpu_data_replicate *replicate, unsigned automatically_allocated)
{
	unsigned dst_node = replicate->memory_node;

	struct _starpu_mem_chunk *mc;

	/* the interface was already filled by ops->allocate_data_on_node */
	size_t interface_size = replicate->handle->ops->interface_size;

	/* Put this memchunk in the list of memchunk in use */
	mc = _starpu_memchunk_init(replicate, interface_size, handle->home_node == -1 || (int) dst_node == handle->home_node, automatically_allocated);

	_starpu_spin_lock(&mc_lock[dst_node]);
	MC_LIST_PUSH_BACK(dst_node, mc);
	_starpu_spin_unlock(&mc_lock[dst_node]);
}

/* This function is called when the handle is destroyed (eg. when calling
 * unregister or unpartition). It puts all the memchunks that refer to the
 * specified handle into the cache.
 */
void _starpu_request_mem_chunk_removal(starpu_data_handle_t handle, struct _starpu_data_replicate *replicate, unsigned node, size_t size)
{
	struct _starpu_mem_chunk *mc = replicate->mc;

	STARPU_ASSERT(mc->data == handle);
	_starpu_spin_checklocked(&handle->header_lock);

	/* Record the allocated size, so that later in memory
	 * reclaiming we can estimate how much memory we free
	 * by freeing this.  */
	mc->size = size;

	/* Also keep the interface parameters and pointers, for later reuse
	 * while detached, or freed */
	mc->chunk_interface = malloc(mc->size_interface);
	memcpy(mc->chunk_interface, replicate->data_interface, mc->size_interface);

	/* This memchunk doesn't have to do with the data any more. */
	replicate->mc = NULL;
	mc->replicate = NULL;
	replicate->allocated = 0;
	replicate->automatically_allocated = 0;
	replicate->initialized = 0;

	_starpu_spin_lock(&mc_lock[node]);

	mc->data = NULL;
	/* remove it from the main list */
	MC_LIST_ERASE(node, mc);

	_starpu_spin_unlock(&mc_lock[node]);

	/*
	 * Unless the user has provided a main RAM limitation, we would fill
	 * memory with cached data and then eventually swap.
	 */
	/*
	 * This is particularly important when
	 * STARPU_USE_ALLOCATION_CACHE is not enabled, as we
	 * wouldn't even re-use these allocations!
	 */
	if (handle->ops->dontcache || (starpu_node_get_kind(node) == STARPU_CPU_RAM && starpu_get_env_number("STARPU_LIMIT_CPU_MEM") < 0))
	{
		/* Free data immediately */
		free_memory_on_node(mc, node);

		free(mc->chunk_interface);
		_starpu_mem_chunk_delete(mc);
	}
	else
	{
		/* put it in the list of buffers to be removed */
		uint32_t footprint = mc->footprint;
		struct mc_cache_entry *entry;
		_starpu_spin_lock(&mc_lock[node]);
		HASH_FIND(hh, mc_cache[node], &footprint, sizeof(footprint), entry);
		if (!entry)
		{
			entry = malloc(sizeof(*entry));
			_starpu_mem_chunk_list_init(&entry->list);
			entry->footprint = footprint;
			HASH_ADD(hh, mc_cache[node], footprint, sizeof(entry->footprint), entry);
		}
		mc_cache_nb[node]++;
		mc_cache_size[node] += mc->size;
		_starpu_mem_chunk_list_push_front(&entry->list, mc);
		_starpu_spin_unlock(&mc_lock[node]);
	}
}

/*
 * In order to allocate a piece of data, we try to reuse existing buffers if
 * its possible.
 *	1 - we try to reuse a memchunk that is explicitely unused.
 *	2 - we go through the list of memory chunks and find one that is not
 *	referenced and that has the same footprint to reuse it.
 *	3 - we call the usual driver's alloc method
 *	4 - we go through the list of memory chunks and release those that are
 *	not referenced (or part of those).
 *
 */

static starpu_ssize_t _starpu_allocate_interface(starpu_data_handle_t handle, struct _starpu_data_replicate *replicate, unsigned dst_node, unsigned is_prefetch)
{
	unsigned attempts = 0;
	starpu_ssize_t allocated_memory;
	int ret;
	starpu_ssize_t data_size = _starpu_data_get_size(handle);

	_starpu_spin_checklocked(&handle->header_lock);

	_starpu_data_allocation_inc_stats(dst_node);

#ifdef STARPU_USE_ALLOCATION_CACHE
	/* perhaps we can directly reuse a buffer in the free-list */
	uint32_t footprint = _starpu_compute_data_footprint(handle);

	_STARPU_TRACE_START_ALLOC_REUSE(dst_node, data_size);
	if (try_to_find_reusable_mem_chunk(dst_node, handle, replicate, footprint))
	{
		_starpu_allocation_cache_hit(dst_node);
		return data_size;
	}
	_STARPU_TRACE_END_ALLOC_REUSE(dst_node);
#endif
	STARPU_ASSERT(handle->ops);
	STARPU_ASSERT(handle->ops->allocate_data_on_node);
	STARPU_ASSERT(replicate->data_interface);

	size_t size = handle->ops->interface_size;
	if (!size)
		/* nul-size VLA is undefined... */
		size = 1;
	char data_interface[size];

	memcpy(data_interface, replicate->data_interface, handle->ops->interface_size);

	/* Take temporary reference on the replicate */
	replicate->refcnt++;
	handle->busy_count++;
	_starpu_spin_unlock(&handle->header_lock);

	do
	{
		_STARPU_TRACE_START_ALLOC(dst_node, data_size);

#if defined(STARPU_USE_CUDA) && defined(HAVE_CUDA_MEMCPY_PEER) && !defined(STARPU_SIMGRID)
		if (starpu_node_get_kind(dst_node) == STARPU_CUDA_RAM)
		{
			/* To facilitate the design of interface, we set the
			 * proper CUDA device in case it is needed. This avoids
			 * having to set it again in the malloc method of each
			 * interface. */
			starpu_cuda_set_device(_starpu_memory_node_get_devid(dst_node));
		}
#endif

		allocated_memory = handle->ops->allocate_data_on_node(data_interface, dst_node);
		_STARPU_TRACE_END_ALLOC(dst_node);

		if (allocated_memory == -ENOMEM)
		{
			size_t reclaim = 0.25*_starpu_memory_manager_get_global_memory_size(dst_node);
			size_t handle_size = handle->ops->get_size(handle);
			if (starpu_memstrategy_data_size_coefficient*handle_size > reclaim)
				reclaim = starpu_memstrategy_data_size_coefficient*handle_size;

			_STARPU_TRACE_START_MEMRECLAIM(dst_node,is_prefetch);
			if (is_prefetch)
			{
				flush_memchunk_cache(dst_node, reclaim);
			}
			else
				_starpu_memory_reclaim_generic(dst_node, 0, reclaim);
			_STARPU_TRACE_END_MEMRECLAIM(dst_node,is_prefetch);
		}
	}
	while((allocated_memory == -ENOMEM) && attempts++ < 2);

	int cpt = 0;
	while (cpt < STARPU_SPIN_MAXTRY && _starpu_spin_trylock(&handle->header_lock))
	{
		cpt++;
		_starpu_datawizard_progress(_starpu_memory_node_get_local_key(), 0);
	}
	if (cpt == STARPU_SPIN_MAXTRY)
		_starpu_spin_lock(&handle->header_lock);

	replicate->refcnt--;
	STARPU_ASSERT(replicate->refcnt >= 0);
	STARPU_ASSERT(handle->busy_count > 0);
	handle->busy_count--;
	ret = _starpu_data_check_not_busy(handle);
	STARPU_ASSERT(ret == 0);

	if (allocated_memory == -ENOMEM)
	{
		if (replicate->allocated)
			/* Didn't manage to allocate, but somebody else did */
			allocated_memory = 0;
		goto out;
	}

	if (replicate->allocated)
	{
		/* Argl, somebody allocated it in between already, drop this one */
		_STARPU_TRACE_START_FREE(dst_node, data_size);
		handle->ops->free_data_on_node(data_interface, dst_node);
		_STARPU_TRACE_END_FREE(dst_node);
		allocated_memory = 0;
	}
	else
		/* Install allocated interface */
		memcpy(replicate->data_interface, data_interface, handle->ops->interface_size);

out:
	return allocated_memory;
}

int _starpu_allocate_memory_on_node(starpu_data_handle_t handle, struct _starpu_data_replicate *replicate, unsigned is_prefetch)
{
	starpu_ssize_t allocated_memory;

	unsigned dst_node = replicate->memory_node;

	STARPU_ASSERT(handle);
	_starpu_spin_checklocked(&handle->header_lock);

	/* A buffer is already allocated on the node */
	if (replicate->allocated)
		return 0;

	STARPU_ASSERT(replicate->data_interface);
	allocated_memory = _starpu_allocate_interface(handle, replicate, dst_node, is_prefetch);

	/* perhaps we could really not handle that capacity misses */
	if (allocated_memory == -ENOMEM)
		return -ENOMEM;

	if (replicate->allocated)
		/* Somebody allocated it in between already */
		return 0;

	register_mem_chunk(handle, replicate, 1);

	replicate->allocated = 1;
	replicate->automatically_allocated = 1;

	if (replicate->relaxed_coherency == 0 && dst_node == STARPU_MAIN_RAM)
	{
		/* We are allocating the buffer in main memory, also register it
		 * for the gcc plugin.  */
		void *ptr = starpu_data_handle_to_pointer(handle, STARPU_MAIN_RAM);
		if (ptr != NULL)
		{
			_starpu_data_register_ram_pointer(handle, ptr);
		}
	}

	return 0;
}

unsigned starpu_data_test_if_allocated_on_node(starpu_data_handle_t handle, unsigned memory_node)
{
	return handle->per_node[memory_node].allocated;
}

/* This memchunk has been recently used, put it last on the mc_list, so we will
 * try to evict it as late as possible */
void _starpu_memchunk_recently_used(struct _starpu_mem_chunk *mc, unsigned node)
{
	if (!mc)
		/* user-allocated memory */
		return;
	_starpu_spin_lock(&mc_lock[node]);
	MC_LIST_ERASE(node, mc);
	MC_LIST_PUSH_BACK(node, mc);
	_starpu_spin_unlock(&mc_lock[node]);
}

/* This memchunk will not be used in the close future, put it on the clean
 * list, so we will to evict it first */
void _starpu_memchunk_wont_use(struct _starpu_mem_chunk *mc, unsigned node)
{
	if (!mc)
		/* user-allocated memory */
		return;
	_starpu_spin_lock(&mc_lock[node]);
	MC_LIST_ERASE(node, mc);
	/* Caller will schedule a clean transfer */
	mc->clean = 1;
	MC_LIST_PUSH_CLEAN(node, mc);
	_starpu_spin_unlock(&mc_lock[node]);
}

/* This memchunk is being written to, and thus becomes dirty */
void _starpu_memchunk_dirty(struct _starpu_mem_chunk *mc, unsigned node)
{
	if (!mc)
		/* user-allocated memory */
		return;
	if (mc->home)
		/* Home is always clean */
		return;
	_starpu_spin_lock(&mc_lock[node]);
	if (mc->relaxed_coherency == 1)
	{
		/* SCRATCH, make it clean if not already*/
		if (!mc->clean)
		{
			mc_clean_nb[node]++;
			mc->clean = 1;
		}
	}
	else
	{
		if (mc->clean)
		{
			mc_clean_nb[node]--;
			mc->clean = 0;
		}
	}
	_starpu_spin_unlock(&mc_lock[node]);
}

#ifdef STARPU_MEMORY_STATS
void _starpu_memory_display_stats_by_node(int node)
{
	_starpu_spin_lock(&mc_lock[node]);

	if (!_starpu_mem_chunk_list_empty(&mc_list[node]))
	{
		struct _starpu_mem_chunk *mc;

		fprintf(stderr, "#-------\n");
		fprintf(stderr, "Data on Node #%d\n",node);

		for (mc = _starpu_mem_chunk_list_begin(&mc_list[node]);
		     mc != _starpu_mem_chunk_list_end(&mc_list[node]);
		     mc = _starpu_mem_chunk_list_next(mc))
		{
			if (mc->automatically_allocated == 0)
				_starpu_memory_display_handle_stats(mc->data);
		}

	}

	_starpu_spin_unlock(&mc_lock[node]);
}
#endif

void starpu_data_display_memory_stats(void)
{
#ifdef STARPU_MEMORY_STATS
	unsigned node;

	fprintf(stderr, "\n#---------------------\n");
	fprintf(stderr, "Memory stats :\n");
	for (node = 0; node < STARPU_MAXNODES; node++)
	{
	     _starpu_memory_display_stats_by_node(node);
	}
	fprintf(stderr, "\n#---------------------\n");
#endif
}


static int
get_better_disk_can_accept_size(starpu_data_handle_t handle, unsigned node)
{
	int target = -1;
	unsigned nnodes = starpu_memory_nodes_get_count();
	unsigned int i;
	double time_disk = 0;
				
	for (i = 0; i < nnodes; i++)
	{
		if (starpu_node_get_kind(i) == STARPU_DISK_RAM && i != node &&
		    (_starpu_memory_manager_test_allocate_size(i, _starpu_data_get_size(handle)) == 1 ||
		     handle->per_node[i].allocated))
		{
			/* if we can write on the disk */
			if (_starpu_get_disk_flag(i) != STARPU_DISK_NO_RECLAIM)
			{
				/* only time can change between disk <-> main_ram 
				 * and not between main_ram <-> worker if we compare diks*/
				double time_tmp = starpu_transfer_predict(i, STARPU_MAIN_RAM, _starpu_data_get_size(handle));
				if (target == -1 || time_disk > time_tmp)
				{
					target = i;
					time_disk = time_tmp;
				}
			}
		}
	}
	return target;
}


static unsigned
choose_target(starpu_data_handle_t handle, unsigned node)
{
	int target = -1;
	size_t size_handle = _starpu_data_get_size(handle);
	if (handle->home_node != -1)
		/* try to push on RAM if we can before to push on disk */
		if(starpu_node_get_kind(handle->home_node) == STARPU_DISK_RAM && node != STARPU_MAIN_RAM)
		{
			if (handle->per_node[STARPU_MAIN_RAM].allocated || 
			    _starpu_memory_manager_test_allocate_size(STARPU_MAIN_RAM, size_handle) == 1)
			{
				target = STARPU_MAIN_RAM;
			}
			else
			{
				target = get_better_disk_can_accept_size(handle, node);
			}

		}
          	/* others memory nodes */
		else 
		{
			target = handle->home_node;
		}
	else
	{
		/* handle->home_node == -1 */
		/* no place for datas in RAM, we push on disk */
		if (node == STARPU_MAIN_RAM)
		{
			target = get_better_disk_can_accept_size(handle, node);
		}
		/* node != 0 */
		/* try to push data to RAM if we can before to push on disk*/
		else if (handle->per_node[STARPU_MAIN_RAM].allocated || 
			 _starpu_memory_manager_test_allocate_size(STARPU_MAIN_RAM, size_handle) == 1)
		{
			target = STARPU_MAIN_RAM;
		}
		/* no place in RAM */
		else
		{
			target = get_better_disk_can_accept_size(handle, node);
		}
	}
	/* we haven't the right to write on the disk */
	if (target != -1 && starpu_node_get_kind(target) == STARPU_DISK_RAM && _starpu_get_disk_flag(target) == STARPU_DISK_NO_RECLAIM)
		target = -1;

	return target;
}
