# StarPU --- Runtime system for heterogeneous multicore architectures.
#
# Copyright (C) 2011, 2012 INRIA
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

SUBDIRS = src tests
if BUILD_EXAMPLES
SUBDIRS += examples
endif

EXTRA_DIST =					\
  COPYING					\
  README					\
  ChangeLog

noinst_HEADERS =				\
  include/starpu-gcc/opencl.h			\
  include/starpu-gcc/tasks.h			\
  include/starpu-gcc/utils.h			\
  include/starpu-gcc/warn-unregistered.h


showcheck:
	RET=0 ; \
	for i in $(SUBDIRS) ; do \
		make -C $$i showcheck || RET=1 ; \
	done ; \
	exit $$RET

# Generate a GNU-style ChangeLog for inclusion in the tarball.
# It requires network access and may be slow.
gen-ChangeLog:
	if test "x$$CHANGELOG" = "xno"; then					\
	   echo "ChangeLog not built, per user request" >&2;			\
	elif ! xsltproc --version > /dev/null 2>&1; then			\
	   echo "xsltproc not found, ChangeLog not generated" >&2;		\
	elif ! test -d "$(srcdir)/.svn"; then					\
	   echo "Subversion meta-data not found, ChangeLog not generated" >&2;	\
	elif ! svn --version > /dev/null 2>&1; then				\
	   echo "Subversion not found, ChangeLog not generated" >&2;		\
	else									\
	   ( cd "$(srcdir)";							\
	     svn log --xml --verbose ) |					\
	   xsltproc "$(top_srcdir)/build-aux/svn2cl.xsl" - > "ChangeLog.tmp";	\
	   mv "ChangeLog.tmp" "$(distdir)/ChangeLog";				\
	fi

dist-hook: gen-ChangeLog

.PHONY: showcheck dist-hook gen-ChangeLog
