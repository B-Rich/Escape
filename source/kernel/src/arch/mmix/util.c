/**
 * $Id$
 * Copyright (C) 2008 - 2011 Nils Asmussen
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

#include <sys/common.h>
#include <sys/dbg/console.h>
#include <sys/dbg/kb.h>
#include <sys/mem/vmm.h>
#include <sys/mem/cache.h>
#include <sys/debug.h>
#include <sys/intrpt.h>
#include <sys/util.h>
#include <sys/cpu.h>
#include <sys/video.h>
#include <stdarg.h>
#include <string.h>

static sFuncCall frames[1] = {
	{0,0,""}
};

void util_panic_arch(void) {
}

void util_printUserState(void) {
	const sThread *t = thread_getRunning();
	sKSpecRegs *sregs = thread_getSpecRegs();
	vid_printf("User state:\n");
	intrpt_printStackFrame(thread_getIntrptStack(t));
	vid_printf("\trBB : #%016lx rWW : #%016lx rXX : #%016lx\n",sregs->rbb,sregs->rww,sregs->rxx);
	vid_printf("\trYY : #%016lx rZZ : #%016lx\n",sregs->ryy,sregs->rzz);
}

sFuncCall *util_getUserStackTrace(void) {
	/* TODO */
	/* the MMIX-toolchain doesn't use a frame-pointer when enabling optimization, as it seems.
	 * therefore without information about the stackframe-sizes or similar, there is no way to
	 * determine the stacktrace */
	return frames;
}

sFuncCall *util_getKernelStackTrace(void) {
	return frames;
}

sFuncCall *util_getUserStackTraceOf(A_UNUSED sThread *t) {
	return frames;
}

sFuncCall *util_getKernelStackTraceOf(A_UNUSED const sThread *t) {
	return frames;
}
