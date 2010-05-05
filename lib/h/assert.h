/**
 * $Id$
 * Copyright (C) 2008 - 2009 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef ASSERT_H_
#define ASSERT_H_

#if IN_KERNEL
#	include <util.h>
#else
#	include <esc/proc.h>
#	include <stdio.h>
#endif

#ifndef NDEBUG

#	define assert(cond) vassert(cond,"")

#	if IN_KERNEL
#		define vassert(cond,errorMsg,...) do { if(!(cond)) { \
			util_panic("Assert '" #cond "' failed at %s, %s() line %d: " errorMsg,__FILE__,__FUNCTION__,\
				__LINE__,## __VA_ARGS__); \
		} } while(0);
#	else
#		define vassert(cond,errorMsg,...) do { if(!(cond)) { \
			printe("Assert '" #cond "' failed at %s, %s() line %d: " errorMsg,__FILE__,__FUNCTION__,\
				__LINE__,## __VA_ARGS__); \
			printStackTrace(); \
			exit(1); \
		} } while(0);
#	endif

#else

#	define assert(cond) (void)(cond)
#	define vassert(cond,errorMsg,...) (void)(cond)

#endif

#endif /* ASSERT_H_ */
