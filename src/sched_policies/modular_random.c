/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2013  INRIA
 * Copyright (C) 2013  Simon Archipoff
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

#include <starpu_sched_component.h>
#include <starpu_scheduler.h>

/* Random scheduler with a fifo queue for its scheduling window */

static void initialize_random_fifo_center_policy(unsigned sched_ctx_id)
{
	struct starpu_sched_tree *t;
	struct starpu_sched_component * random_component;

	starpu_sched_ctx_create_worker_collection(sched_ctx_id, STARPU_WORKER_LIST);

	t = starpu_sched_tree_create(sched_ctx_id);
 	t->root = starpu_sched_component_fifo_create(t, NULL);
	random_component = starpu_sched_component_random_create(t, NULL);

	starpu_sched_component_connect(t->root, random_component);

	unsigned i;
	for(i = 0; i < starpu_worker_get_count() + starpu_combined_worker_get_count(); i++)
		starpu_sched_component_connect(random_component, starpu_sched_component_worker_get(sched_ctx_id, i));

	starpu_sched_tree_update_workers(t);
	starpu_sched_ctx_set_policy_data(sched_ctx_id, (void*)t);
}

static void deinitialize_random_fifo_center_policy(unsigned sched_ctx_id)
{
	struct starpu_sched_tree *tree = (struct starpu_sched_tree*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	starpu_sched_tree_destroy(tree);
	starpu_sched_ctx_delete_worker_collection(sched_ctx_id);
}

struct starpu_sched_policy _starpu_sched_modular_random_policy =
{
	.init_sched = initialize_random_fifo_center_policy,
	.deinit_sched = deinitialize_random_fifo_center_policy,
	.add_workers = starpu_sched_tree_add_workers,
	.remove_workers = starpu_sched_tree_remove_workers,
	.push_task = starpu_sched_tree_push_task,
	.pop_task = starpu_sched_tree_pop_task,
	.pre_exec_hook = NULL,
	.post_exec_hook = NULL,
	.pop_every_task = NULL,
	.policy_name = "modular-random",
	.policy_description = "random modular policy"
};

/* Random scheduler with a priority queue for its scheduling window */

static void initialize_random_prio_center_policy(unsigned sched_ctx_id)
{
	struct starpu_sched_tree *t;
	struct starpu_sched_component * random_component;

	starpu_sched_ctx_create_worker_collection(sched_ctx_id, STARPU_WORKER_LIST);

	t = starpu_sched_tree_create(sched_ctx_id);
 	t->root = starpu_sched_component_prio_create(t, NULL);
	random_component = starpu_sched_component_random_create(t, NULL);

	starpu_sched_component_connect(t->root, random_component);

	unsigned i;
	for(i = 0; i < starpu_worker_get_count() + starpu_combined_worker_get_count(); i++)
		starpu_sched_component_connect(random_component, starpu_sched_component_worker_get(sched_ctx_id, i));

	starpu_sched_tree_update_workers(t);
	starpu_sched_ctx_set_policy_data(sched_ctx_id, (void*)t);
}

static void deinitialize_random_prio_center_policy(unsigned sched_ctx_id)
{
	struct starpu_sched_tree *tree = (struct starpu_sched_tree*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	starpu_sched_tree_destroy(tree);
	starpu_sched_ctx_delete_worker_collection(sched_ctx_id);
}

struct starpu_sched_policy _starpu_sched_modular_random_prio_policy =
{
	.init_sched = initialize_random_prio_center_policy,
	.deinit_sched = deinitialize_random_prio_center_policy,
	.add_workers = starpu_sched_tree_add_workers,
	.remove_workers = starpu_sched_tree_remove_workers,
	.push_task = starpu_sched_tree_push_task,
	.pop_task = starpu_sched_tree_pop_task,
	.pre_exec_hook = NULL,
	.post_exec_hook = NULL,
	.pop_every_task = NULL,
	.policy_name = "modular-random-prio",
	.policy_description = "random-prio modular policy"
};
