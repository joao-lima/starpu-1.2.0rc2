/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2013  INRIA
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

static int eager_push_task(struct starpu_sched_component * component, struct starpu_task * task)
{
	STARPU_ASSERT(component && task && starpu_sched_component_is_eager(component));
	STARPU_ASSERT(starpu_sched_component_can_execute_task(component,task));
	
	int workerid;
	for(workerid = starpu_bitmap_first(component->workers_in_ctx);
	    workerid != -1;
	    workerid = starpu_bitmap_next(component->workers_in_ctx, workerid))
	{
		int nimpl;
		for(nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++)
		{
			if(starpu_worker_can_execute_task(workerid,task,nimpl)
			   || starpu_combined_worker_can_execute_task(workerid, task, nimpl))
			{
				int i;
				for (i = 0; i < component->nchildren; i++)
				{
					int idworker;
					for(idworker = starpu_bitmap_first(component->children[i]->workers);
						idworker != -1;
						idworker = starpu_bitmap_next(component->children[i]->workers, idworker))
					{
						if (idworker == workerid)
						{
							if(starpu_sched_component_is_worker(component->children[i]))
							{
								component->children[i]->can_pull(component->children[i]);
								return 1;
							}
							else
								return component->children[i]->push_task(component->children[i],task);
						}
					}
				}
			}
		}
	}
	return 1;
}

int starpu_sched_component_is_eager(struct starpu_sched_component * component)
{
	return component->push_task == eager_push_task;
}

struct starpu_sched_component * starpu_sched_component_eager_create(struct starpu_sched_tree *tree, void * arg STARPU_ATTRIBUTE_UNUSED)
{
	struct starpu_sched_component * component = starpu_sched_component_create(tree);
	component->push_task = eager_push_task;

	return component;
}
