/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2013-2014  Université de Bordeaux
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
#include <float.h>
#include <limits.h>

/* The two thresolds concerns the prio components, which contains queues
 * who can handle the priority of StarPU tasks. You can tune your
 * scheduling by benching those values and choose which one is the
 * best for your current application. 
 * The current value of the ntasks_threshold is the best we found
 * so far across several types of applications (cholesky, LU, stencil).
 */
#define _STARPU_SCHED_NTASKS_THRESHOLD_DEFAULT 30
#define _STARPU_SCHED_EXP_LEN_THRESHOLD_DEFAULT 1000000000.0

static void initialize_heft2_center_policy(unsigned sched_ctx_id)
{
	starpu_sched_ctx_create_worker_collection(sched_ctx_id, STARPU_WORKER_LIST);

	/* The application may use any integer */
	if (starpu_sched_ctx_min_priority_is_set(sched_ctx_id) == 0)
		starpu_sched_ctx_set_min_priority(sched_ctx_id, INT_MIN);
	if (starpu_sched_ctx_max_priority_is_set(sched_ctx_id) == 0)
		starpu_sched_ctx_set_max_priority(sched_ctx_id, INT_MAX);

	struct starpu_sched_tree * t = starpu_sched_tree_create(sched_ctx_id);

	struct starpu_sched_component * window_component = starpu_sched_component_prio_create(t, NULL);

	struct starpu_sched_component * perfmodel_component = starpu_sched_component_heft_create(t, NULL);
	struct starpu_sched_component * no_perfmodel_component = starpu_sched_component_eager_create(t, NULL);
	struct starpu_sched_component * calibrator_component = starpu_sched_component_eager_calibration_create(t, NULL);
	
	struct starpu_sched_component_perfmodel_select_data perfmodel_select_data =
		{
			.calibrator_component = calibrator_component,
			.no_perfmodel_component = no_perfmodel_component,
			.perfmodel_component = perfmodel_component,
		};

	struct starpu_sched_component * perfmodel_select_component = starpu_sched_component_perfmodel_select_create(t, &perfmodel_select_data);

	t->root = window_component;
	starpu_sched_component_connect(window_component, perfmodel_select_component);

	starpu_sched_component_connect(perfmodel_select_component, calibrator_component);
	starpu_sched_component_connect(perfmodel_select_component, perfmodel_component);
	starpu_sched_component_connect(perfmodel_select_component, no_perfmodel_component);

	struct starpu_sched_component_prio_data prio_data =
		{
			.ntasks_threshold = starpu_get_env_number_default("STARPU_NTASKS_THRESHOLD", _STARPU_SCHED_NTASKS_THRESHOLD_DEFAULT),
			.exp_len_threshold = starpu_get_env_float_default("STARPU_EXP_LEN_THRESHOLD", _STARPU_SCHED_EXP_LEN_THRESHOLD_DEFAULT),
		};

	unsigned i;
	for(i = 0; i < starpu_worker_get_count() + starpu_combined_worker_get_count(); i++)
	{
		struct starpu_sched_component * worker_component = starpu_sched_component_worker_get(sched_ctx_id, i);
		struct starpu_sched_component * prio_component = starpu_sched_component_prio_create(t, &prio_data);
		struct starpu_sched_component * impl_component = starpu_sched_component_best_implementation_create(t, NULL);

		starpu_sched_component_connect(prio_component, worker_component);
		starpu_sched_component_connect(impl_component, prio_component);

		starpu_sched_component_connect(perfmodel_component, impl_component);
		starpu_sched_component_connect(no_perfmodel_component, impl_component);
		starpu_sched_component_connect(calibrator_component, impl_component);
	}

	starpu_sched_tree_update_workers(t);
	starpu_sched_ctx_set_policy_data(sched_ctx_id, (void*)t);
}

static void deinitialize_heft2_center_policy(unsigned sched_ctx_id)
{
	struct starpu_sched_tree *t = (struct starpu_sched_tree*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	starpu_sched_tree_destroy(t);
	starpu_sched_ctx_delete_worker_collection(sched_ctx_id);
}

struct starpu_sched_policy _starpu_sched_modular_heft2_policy =
{
	.init_sched = initialize_heft2_center_policy,
	.deinit_sched = deinitialize_heft2_center_policy,
	.add_workers = starpu_sched_tree_add_workers,
	.remove_workers = starpu_sched_tree_remove_workers,
	.push_task = starpu_sched_tree_push_task,
	.pop_task = starpu_sched_tree_pop_task,
	.pre_exec_hook = starpu_sched_component_worker_pre_exec_hook,
	.post_exec_hook = starpu_sched_component_worker_post_exec_hook,
	.pop_every_task = NULL,
	.policy_name = "modular-heft2",
	.policy_description = "heft modular2 policy"
};
