#!/bin/sh

# StarPU --- Runtime system for heterogeneous multicore architectures.
#
# Copyright (C) 2012, 2014  CNRS
#
# StarPU is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or (at
# your option) any later version.
#
# StarPU is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# See the GNU Lesser General Public License in COPYING.LGPL for more details.

check_success()
{
    if [ $1 -ne 0 ] ; then
	echo "failure" >&2
        exit $1
    fi
}

if test ! -x ./cholesky/cholesky_tag
then
    echo "Application ./cholesky/cholesky_tag unavailable"
    exit 77
fi

SCHEDULERS=`STARPU_SCHED="help" ./basic_examples/hello_world 2>&1 | awk '/\t->/ {print $1}'`

for sched in $SCHEDULERS
do
    echo "cholesky.$sched"
    STARPU_SCHED=$sched ./cholesky/cholesky_tag
    check_success $?
done
