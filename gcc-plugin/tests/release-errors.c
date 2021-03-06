/* GCC-StarPU
   Copyright (C) 2011, 2012 INRIA

   GCC-StarPU is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   GCC-StarPU is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC-StarPU.  If not, see <http://www.gnu.org/licenses/>.  */

extern float *y;

static const double a[123];

int
main (int argc, char *argv[])
{
#pragma starpu initialize

  int x[123] __attribute__ ((unused));
  static char z[345] __attribute__ ((unused));

#pragma starpu register x

#pragma starpu release /* (error "parse error") */
#pragma starpu release 123 /* (error "neither a pointer nor an array") */
#pragma starpu release does_not_exit /* (error "unbound variable") */

#pragma starpu release argc /* (error "neither a pointer nor an array") */
#pragma starpu release y
#pragma starpu release x

#pragma starpu release x z			  /* (error "junk after") */

  return 1;
}
