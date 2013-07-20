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
#include <sys/task/groups.h>
#include <sys/task/thread.h>
#include <sys/task/proc.h>
#include <sys/mem/cache.h>
#include <sys/mem/vmm.h>
#include <sys/spinlock.h>
#include <sys/video.h>
#include <string.h>

bool Groups::set(pid_t pid,size_t count,USER const gid_t *groups) {
	Entries *g;
	Proc *p;
	gid_t *grpCpy = NULL;
	if(count > 0) {
		grpCpy = (gid_t*)cache_alloc(count * sizeof(gid_t));
		if(!grpCpy)
			return false;
		Thread::addHeapAlloc(grpCpy);
		memcpy(grpCpy,groups,count * sizeof(gid_t));
		Thread::remHeapAlloc(grpCpy);
	}

	g = (Entries*)cache_alloc(sizeof(Entries));
	if(!g) {
		cache_free(grpCpy);
		return false;
	}
	g->lock = 0;
	g->refCount = 1;
	g->count = count;
	g->groups = grpCpy;
	leave(pid);
	p = Proc::getByPid(pid);
	if(!p) {
		cache_free(g);
		cache_free(grpCpy);
		return false;
	}
	p->groups = g;
	return true;
}

void Groups::join(Proc *dst,Proc *src) {
	Entries *g = src->groups;
	dst->groups = g;
	if(g) {
		spinlock_aquire(&g->lock);
		g->refCount++;
		spinlock_release(&g->lock);
	}
}

size_t Groups::get(pid_t pid,USER gid_t *list,size_t count) {
	Entries *g = getByPid(pid);
	if(count == 0)
		return g ? g->count : 0;
	if(g) {
		count = MIN(g->count,count);
		memcpy(list,g->groups,count * sizeof(gid_t));
		return count;
	}
	return 0;
}

bool Groups::contains(pid_t pid,gid_t gid) {
	Entries *g = getByPid(pid);
	if(g) {
		size_t i;
		for(i = 0; i < g->count; i++) {
			if(g->groups[i] == gid)
				return true;
		}
	}
	return false;
}

void Groups::leave(pid_t pid) {
	Entries *g;
	Proc *p = Proc::getByPid(pid);
	if(!p)
		return;
	g = p->groups;
	if(g) {
		spinlock_aquire(&g->lock);
		if(--g->refCount == 0) {
			cache_free(g->groups);
			cache_free(g);
		}
		spinlock_release(&g->lock);
	}
	p->groups = NULL;
}

void Groups::print(pid_t pid) {
	Entries *g = getByPid(pid);
	if(g) {
		size_t i;
		vid_printf("[refs: %u] ",g->refCount);
		for(i = 0; i < g->count; i++)
			vid_printf("%u ",g->groups[i]);
	}
	else
		vid_printf("-");
}

Groups::Entries *Groups::getByPid(pid_t pid) {
	Proc *p = Proc::getByPid(pid);
	if(!p)
		return NULL;
	return p->groups;
}