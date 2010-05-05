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

#include <esc/common.h>
#include <esc/exceptions/io.h>
#include <esc/exceptions/outofmemory.h>
#include <esc/io/ostringstream.h>
#include <stdio.h>

int vsnprintf(char *str,size_t n,const char *fmt,va_list ap) {
	int res = 0;
	sOStream *s = NULL;
	TRY {
		s = osstream_open(str,n);
		res = s->vwritef(s,fmt,ap);
	}
	CATCH(IOException,e) {
		res = EOF;
	}
	CATCH(OutOfMemoryException,e) {
		res = EOF;
	}
	ENDCATCH
	if(s)
		s->close(s);
	return res;
}
