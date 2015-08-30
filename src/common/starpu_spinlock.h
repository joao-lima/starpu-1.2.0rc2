/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010-2014  Université de Bordeaux
 * Copyright (C) 2010, 2011, 2013, 2014, 2015  CNRS
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
#ifndef __STARPU_SPINLOCK_H__
#define __STARPU_SPINLOCK_H__

#include <errno.h>
#include <stdint.h>
#include <common/config.h>
#include <common/fxt.h>
#include <starpu.h>

struct _starpu_spinlock
{
#if defined(STARPU_SPINLOCK_CHECK)
	starpu_pthread_mutex_t errcheck_lock;
	const char *last_taker;
#else
	starpu_pthread_spinlock_t lock;
#endif
};

#ifdef STARPU_SPINLOCK_CHECK 
#define STARPU_RECORD_LOCK(lock) do { 	\
	(lock)->last_taker = __starpu_func__; \
} while(0) 
#else // !STARPU_SPINLOCK_CHECK
#define STARPU_RECORD_LOCK(lock) do {} while(0)
#endif // STARPU_SPINLOCK_CHECK

int _starpu_spin_init(struct _starpu_spinlock *lock);
int _starpu_spin_destroy(struct _starpu_spinlock *lock);

int _starpu_spin_lock(struct _starpu_spinlock *lock);
#define _starpu_spin_lock(lock) ({ \
	_STARPU_TRACE_LOCKING_SPINLOCK(); \
	_starpu_spin_lock(lock); \
	_STARPU_TRACE_SPINLOCK_LOCKED(); \
	STARPU_RECORD_LOCK(lock); \
	0; \
}) 

int _starpu_spin_trylock(struct _starpu_spinlock *lock);
#define _starpu_spin_trylock(lock) ({ \
	_STARPU_TRACE_TRYLOCK_SPINLOCK(); \
	int err = _starpu_spin_trylock(lock); \
	if (!err) { \
		STARPU_RECORD_LOCK(lock); \
		_STARPU_TRACE_SPINLOCK_LOCKED(); \
	} \
	err; \
})
int _starpu_spin_checklocked(struct _starpu_spinlock *lock);
int _starpu_spin_unlock(struct _starpu_spinlock *lock);
#define _starpu_spin_unlock(lock) ({ \
	_STARPU_TRACE_UNLOCKING_SPINLOCK(); \
	_starpu_spin_unlock(lock); \
	_STARPU_TRACE_SPINLOCK_UNLOCKED(); \
	0; \
}) 


#define STARPU_SPIN_MAXTRY 10 

#endif // __STARPU_SPINLOCK_H__
