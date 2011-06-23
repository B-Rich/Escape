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
#include <stdio.h>
#include <stdlib.h>

int main(void) {
	size_t i,j;
	fputs("    ",stdout);
	for(i = 0; i < 16; i++)
		printf("%02zx ",i << 4);
	fputc('\n',stdout);

	for(i = 0; i < 16; i++) {
		printf("%02zx: ",i);
		for(j = 0; j < 16; j++)
			printf("\033[co;%zd;%zd]##\033[co] ",i,j);
		fputc('\n',stdout);
	}
	return EXIT_SUCCESS;
}
