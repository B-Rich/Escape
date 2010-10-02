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

#include <sys/common.h>
#include <sys/mem/region.h>
#include <sys/mem/paging.h>
#include <sys/mem/vmm.h>
#include <sys/mem/cow.h>
#include <sys/mem/kheap.h>
#include <sys/mem/swap.h>
#include <sys/task/event.h>
#include <sys/vfs/vfs.h>
#include <sys/vfs/real.h>
#include <sys/video.h>
#include <sys/util.h>
#include <errors.h>
#include <limits.h>
#include <string.h>
#include <assert.h>

/**
 * The vmm-module manages the user-part of a process's virtual addressspace. That means it
 * manages the regions that are used by the process (as an instance of sVMRegion) and decides
 * where the regions are placed. So it bounds a region to a virtual address via sVMRegion.
 */

#define FREE_AREA_BEGIN		0xA0000000
#define FREE_AREA_END		KERNEL_AREA_V_ADDR

#define REG(p,i)			(((sVMRegion**)(p)->regions)[(i)])
#define ROUNDUP(bytes)		(((bytes) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

static bool vmm_demandLoad(sVMRegion *vm,u32 *flags,u32 addr);
static bool vmm_loadFromFile(sThread *t,sVMRegion *vm,u32 addr,u32 loadCount);
static u32 *vmm_getPageFlag(sVMRegion *reg,u32 addr);
static s32 vmm_findRegIndex(sProc *p,bool text);
static u32 vmm_findFreeStack(sProc *p,u32 byteCount);
static sVMRegion *vmm_isOccupied(sProc *p,u32 start,u32 end);
static u32 vmm_getFirstUsableAddr(sProc *p,bool textNData);
static u32 vmm_findFreeAddr(sProc *p,u32 byteCount);
static bool vmm_extendRegions(sProc *p,u32 i);
static sVMRegion *vmm_alloc(void);
static void vmm_free(sVMRegion *vm);
static s32 vmm_getAttr(sProc *p,u8 type,u32 bCount,u32 *pgFlags,u32 *flags,u32 *virt,s32 *rno);

void vmm_init(void) {
	/* nothing to do */
}

u32 vmm_addPhys(sProc *p,u32 *phys,u32 bCount,u32 align) {
	tVMRegNo reg;
	sVMRegion *vm;
	sAllocStats stats;
	u32 i,pages = BYTES_2_PAGES(bCount);
	u32 *frames = (u32*)kheap_alloc(sizeof(u32) * pages);
	if(frames == NULL)
		return 0;

	/* if *phys is not set yet, we should allocate physical contiguous memory */
	if(*phys == 0) {
		s32 first = mm_allocateContiguous(pages,align / PAGE_SIZE);
		if(first < 0)
			return 0;
		for(i = 0; i < pages; i++)
			frames[i] = first + i;
	}
	/* otherwise use the specified one */
	else {
		for(i = 0; i < pages; i++) {
			frames[i] = *phys / PAGE_SIZE;
			*phys += PAGE_SIZE;
		}
	}

	/* create region */
	reg = vmm_add(p,NULL,0,bCount,bCount,*phys ? REG_DEVICE : REG_PHYS);
	if(reg < 0) {
		if(!*phys)
			mm_freeContiguous(frames[0],pages);
		kheap_free(frames);
		return 0;
	}

	/* map memory */
	vm = REG(p,reg);
	/* TODO we have to check somehow wether we have enough mem for the page-tables */
	stats = paging_mapTo(p->pagedir,vm->virt,frames,pages,PG_PRESENT | PG_WRITABLE);
	if(*phys) {
		/* the page-tables are ours, the pages may be used by others, too */
		p->ownFrames += stats.ptables;
		p->sharedFrames += pages;
	}
	else {
		/* its our own mem; store physical address for the caller */
		p->ownFrames += stats.ptables + stats.frames;
		*phys = frames[0] * PAGE_SIZE;
	}

	kheap_free(frames);
	return vm->virt;
}

tVMRegNo vmm_add(sProc *p,sBinDesc *bin,u32 binOffset,u32 bCount,u32 lCount,u8 type) {
	sRegion *reg;
	sVMRegion *vm;
	tVMRegNo rno;
	s32 res;
	u32 virt,pgFlags,flags;

	/* for text and shared-library-text: try to find another process with that text */
	if(bin && (type == REG_TEXT || type == REG_SHLIBTEXT)) {
		tVMRegNo prno;
		sProc *binowner = proc_getProcWithBin(bin,&prno);
		if(binowner)
			return vmm_join(binowner,prno,p);
	}

	/* get the attributes of the region (depending on type) */
	if((res = vmm_getAttr(p,type,bCount,&pgFlags,&flags,&virt,&rno)) < 0)
		return res;
	/* no demand-loading if the binary isn't present */
	if(bin == NULL)
		pgFlags &= ~PF_DEMANDLOAD;

	/* create region */
	reg = reg_create(bin,binOffset,bCount,lCount,pgFlags,flags);
	if(!reg)
		return ERR_NOT_ENOUGH_MEM;
	if(!reg_addTo(reg,p))
		goto errReg;
	vm = vmm_alloc();
	if(vm == NULL)
		goto errAdd;
	vm->reg = reg;
	vm->virt = virt;
	REG(p,rno) = vm;

	/* map into process */
	if(type != REG_DEVICE && type != REG_PHYS) {
		sAllocStats stats;
		u8 mapFlags = 0;
		u32 pageCount = BYTES_2_PAGES(vm->reg->byteCount);
		if(!(pgFlags & PF_DEMANDLOAD))
			mapFlags |= PG_PRESENT;
		if(flags & (RF_WRITABLE))
			mapFlags |= PG_WRITABLE;
		stats = paging_mapTo(p->pagedir,virt,NULL,pageCount,mapFlags);
		if(flags & RF_SHAREABLE)
			p->sharedFrames += stats.frames;
		else
			p->ownFrames += stats.frames;
		p->ownFrames += stats.ptables;
#if DISABLE_DEMLOAD
		u32 i;
		for(i = 0; i < vm->reg->pfSize; i++)
			vmm_pagefault(vm->virt + i * PAGE_SIZE);
#endif
	}
	return rno;

errAdd:
	reg_remFrom(reg,p);
errReg:
	reg_destroy(reg);
	return ERR_NOT_ENOUGH_MEM;
}

s32 vmm_setRegProt(sProc *p,tVMRegNo rno,u8 flags) {
	u32 i,pgcount;
	sSLNode *n;
	sVMRegion *vmreg = REG(p,rno);
	if(!(flags & RF_WRITABLE) && flags != 0)
		return ERR_INVALID_ARGS;
	if(!vmreg || (vmreg->reg->flags & (RF_NOFREE | RF_STACK | RF_TLS)))
		return ERR_SETPROT_IMPOSSIBLE;
	/* check if COW is enabled for a page */
	pgcount = BYTES_2_PAGES(vmreg->reg->byteCount);
	for(i = 0; i < pgcount; i++) {
		if(vmreg->reg->pageFlags[i] & PF_COPYONWRITE)
			return ERR_SETPROT_IMPOSSIBLE;
	}
	/* change reg flags */
	vmreg->reg->flags &= ~RF_WRITABLE;
	vmreg->reg->flags |= flags;
	/* change mapping */
	for(n = sll_begin(vmreg->reg->procs); n != NULL; n = n->next) {
		sProc *mp = (sProc*)n->data;
		/* the region may be mapped to a different virtual address */
		tVMRegNo mprno = vmm_getRNoByRegion(mp,vmreg->reg);
		sVMRegion *mpreg = REG(mp,mprno);
		assert(mprno != -1);
		for(i = 0; i < pgcount; i++) {
			/* determine flags; we can't always mark it present.. */
			u8 mapFlags = PG_KEEPFRM;
			if(!(vmreg->reg->pageFlags[i] & (PF_DEMANDLOAD | PF_LOADINPROGRESS | PF_SWAPPED)))
				mapFlags |= PG_PRESENT;
			if(flags & RF_WRITABLE)
				mapFlags |= PG_WRITABLE;
			paging_mapTo(mp->pagedir,mpreg->virt + i * PAGE_SIZE,NULL,1,mapFlags);
		}
	}
	return 0;
}

void vmm_swapOut(sRegion *reg,u32 index) {
	sSLNode *n;
	u32 offset = index * PAGE_SIZE;
	reg->pageFlags[index] |= PF_SWAPPED;
	for(n = sll_begin(reg->procs); n != NULL; n = n->next) {
		sProc *mp = (sProc*)n->data;
		/* the region may be mapped to a different virtual address */
		tVMRegNo mprno = vmm_getRNoByRegion(mp,reg);
		sVMRegion *mpreg = REG(mp,mprno);
		assert(mprno != -1);
		paging_mapTo(mp->pagedir,mpreg->virt + offset,NULL,1,0);
		if(reg->flags & RF_SHAREABLE)
			mp->sharedFrames--;
		else
			mp->ownFrames--;
		mp->swapped++;
	}
}

void vmm_swapIn(sRegion *reg,u32 index,u32 frameNo) {
	sSLNode *n;
	u32 offset = index * PAGE_SIZE;
	u8 flags = PG_PRESENT;
	if(reg->flags & (RF_WRITABLE))
		flags |= PG_WRITABLE;
	reg->pageFlags[index] &= ~PF_SWAPPED;
	for(n = sll_begin(reg->procs); n != NULL; n = n->next) {
		sProc *mp = (sProc*)n->data;
		/* the region may be mapped to a different virtual address */
		tVMRegNo mprno = vmm_getRNoByRegion(mp,reg);
		sVMRegion *mpreg = REG(mp,mprno);
		assert(mprno != -1);
		paging_mapTo(mp->pagedir,mpreg->virt + offset,&frameNo,1,flags);
		if(reg->flags & RF_SHAREABLE)
			mp->sharedFrames++;
		else
			mp->ownFrames++;
		mp->swapped--;
	}
}

void vmm_setTimestamp(sThread *t,u32 timestamp) {
	u32 i;
	for(i = 0; i < t->proc->regSize; i++) {
		sVMRegion *vm = REG(t->proc,i);
		if(vm) {
			/* for stack and tls: just mark the ones of the given thread used */
			if((!(vm->reg->flags & RF_TLS) || i == (u32)t->tlsRegion) &&
				(!(vm->reg->flags & RF_STACK) || i == (u32)t->stackRegion)) {
				vm->reg->timestamp = timestamp;
			}
		}
	}
}

sRegion *vmm_getLRURegion(sProc *p) {
	sRegion *lru = NULL;
	u32 i,ts = ULONG_MAX;
	for(i = 0; i < p->regSize; i++) {
		sVMRegion *vm = REG(p,i);
		if(vm && !(vm->reg->flags & RF_NOFREE) && vm->reg->timestamp < ts) {
			u32 j,count = 0,pages = BYTES_2_PAGES(vm->reg->byteCount);
			for(j = 0; j < pages; j++) {
				if(!(vm->reg->pageFlags[j] & (PF_SWAPPED | PF_COPYONWRITE | PF_DEMANDLOAD))) {
					count++;
					break;
				}
			}
			if(count > 0) {
				ts = vm->reg->timestamp;
				lru = vm->reg;
			}
		}
	}
	return lru;
}

u32 vmm_getPgIdxForSwap(sRegion *reg) {
	u32 i,pages = BYTES_2_PAGES(reg->byteCount);
	u32 index,count = 0;
	for(i = 0; i < pages; i++) {
		if(!(reg->pageFlags[i] & (PF_SWAPPED | PF_COPYONWRITE | PF_DEMANDLOAD)))
			count++;
	}
	index = util_rand() % count;
	for(i = 0; i < pages; i++) {
		if(!(reg->pageFlags[i] & (PF_SWAPPED | PF_COPYONWRITE | PF_DEMANDLOAD))) {
			if(index-- == 0)
				return i;
		}
	}
	/* should never happen */
	assert(false);
	return 0;
}

bool vmm_exists(sProc *p,tVMRegNo reg) {
	assert(p);
	return reg >= 0 && reg < (s32)p->regSize && REG(p,reg);
}

tVMRegNo vmm_getDLDataReg(sProc *p) {
	u32 i;
	assert(p);
	for(i = 0; i < p->regSize; i++) {
		sVMRegion *vm = REG(p,i);
		if(vm && vm->reg->flags == (RF_WRITABLE | RF_GROWABLE) &&
				vm->virt >= FREE_AREA_BEGIN)
			return i;
	}
	return -1;
}

float vmm_getMemUsage(sProc *p,u32 *pages) {
	u32 i;
	float rpages = 0;
	*pages = 0;
	for(i = 0; i < p->regSize; i++) {
		sVMRegion *vm = REG(p,i);
		if(vm) {
			u32 j,count = 0;
			u32 pageCount = BYTES_2_PAGES(vm->reg->byteCount);
			for(j = 0; j < pageCount; j++) {
				if(!(vm->reg->pageFlags[j] & (PF_SWAPPED | PF_DEMANDLOAD | PF_COPYONWRITE)))
					count++;
			}
			*pages += pageCount;
			rpages += (float)count / reg_refCount(vm->reg);
		}
	}
	return rpages;
}

sSLList *vmm_getUsersOf(sProc *p,tVMRegNo rno) {
	return REG(p,rno)->reg->procs;
}

sVMRegion *vmm_getRegion(sProc *p,tVMRegNo rno) {
	return REG(p,rno);
}

tVMRegNo vmm_getRegionOf(sProc *p,u32 addr) {
	u32 i;
	for(i = 0; i < p->regSize; i++) {
		sVMRegion *vm = REG(p,i);
		if(vm && addr >= vm->virt && addr < vm->virt + ROUNDUP(vm->reg->byteCount))
			return i;
	}
	return -1;
}

tVMRegNo vmm_getRNoByRegion(sProc *p,sRegion *reg) {
	u32 i;
	for(i = 0; i < p->regSize; i++) {
		sVMRegion *vm = REG(p,i);
		if(vm && vm->reg == reg)
			return i;
	}
	return -1;
}

void vmm_getRegRange(sProc *p,tVMRegNo reg,u32 *start,u32 *end) {
	sVMRegion *vm = REG(p,reg);
	assert(p && reg >= 0 && reg < (s32)p->regSize && vm);
	if(start)
		*start = vm->virt;
	if(end)
		*end = vm->virt + vm->reg->byteCount;
}

tVMRegNo vmm_hasBinary(sProc *p,sBinDesc *bin) {
	sVMRegion *vm;
	u32 i;
	if(p->regSize == 0 || p->regions == NULL)
		return -1;
	for(i = 0; i < p->regSize; i++) {
		vm = REG(p,i);
		/* if its shareable and the binary fits, return region-number */
		if(vm && (vm->reg->flags & RF_SHAREABLE) &&
				vm->reg->binary.modifytime == bin->modifytime &&
				vm->reg->binary.ino == bin->ino && vm->reg->binary.dev == bin->dev) {
			return i;
		}
	}
	return -1;
}

bool vmm_pagefault(u32 addr) {
	sProc *p = proc_getRunning();
	tVMRegNo rno = vmm_getRegionOf(p,addr);
	sVMRegion *vm;
	u32 *flags;
	if(rno < 0)
		return false;
	vm = REG(p,rno);
	flags = vmm_getPageFlag(vm,addr);
	addr &= ~(PAGE_SIZE - 1);
	if(*flags & PF_DEMANDLOAD) {
		if(vmm_demandLoad(vm,flags,addr)) {
			*flags &= ~PF_DEMANDLOAD;
			return true;
		}
	}
	else if(*flags & PF_SWAPPED)
		return swap_in(p,addr);
	else if(*flags & PF_COPYONWRITE) {
		u32 frmCount = cow_pagefault(addr);
		p->ownFrames += frmCount;
		p->sharedFrames -= frmCount;
		*flags &= ~PF_COPYONWRITE;
		return true;
	}
	return false;
}

void vmm_remove(sProc *p,tVMRegNo reg) {
	u32 i,c = 0;
	sVMRegion *vm = REG(p,reg);
	u32 pcount = BYTES_2_PAGES(vm->reg->byteCount);
	assert(p && reg < (s32)p->regSize && vm != NULL);
	assert(reg_remFrom(vm->reg,p));
	if(reg_refCount(vm->reg) == 0) {
		u32 virt = vm->virt;
		/* remove us from cow and unmap the pages (and free frames, if necessary) */
		for(i = 0; i < pcount; i++) {
			sAllocStats stats;
			bool freeFrame = !(vm->reg->flags & RF_NOFREE);
			if(vm->reg->pageFlags[i] & PF_COPYONWRITE) {
				u32 foundOther;
				/* we can free the frame if there is no other user */
				p->sharedFrames -= cow_remove(p,paging_getFrameNo(p->pagedir,virt),&foundOther);
				freeFrame = !foundOther;
				/* if we'll free the frame with unmap we will substract 1 too much because
				 * we don't own the frame */
				if(freeFrame)
					p->ownFrames++;
			}
			stats = paging_unmapFrom(p->pagedir,virt,1,freeFrame);
			if(vm->reg->flags & RF_SHAREABLE)
				p->sharedFrames -= stats.frames;
			else
				p->ownFrames -= stats.frames;
			p->ownFrames -= stats.ptables;
			virt += PAGE_SIZE;
		}
		/* now destroy region */
		reg_destroy(vm->reg);
	}
	else {
		/* no free here, just unmap */
		sAllocStats stats = paging_unmapFrom(p->pagedir,vm->virt,pcount,false);
		/* in this case its always a shared region because otherwise there wouldn't be other users */
		/* so we have to substract the present content-frames from the shared ones,
		 * and the ptables from ours */
		p->sharedFrames -= reg_presentPageCount(vm->reg);
		p->ownFrames -= stats.ptables;
	}
	REG(p,reg) = NULL;

	/* check wether all regions are NULL */
	for(i = 0; i < p->regSize; i++) {
		vm = REG(p,i);
		if(vm) {
			c++;
			break;
		}
	}
	/* free regions, if all have been removed */
	if(c == 0) {
		kheap_free(p->regions);
		p->regSize = 0;
		p->regions = NULL;
	}
}

tVMRegNo vmm_join(sProc *src,tVMRegNo rno,sProc *dst) {
	sVMRegion *vm = REG(src,rno),*nvm;
	tVMRegNo nrno;
	sAllocStats stats;
	u32 pageCount;
	assert(src && dst && src != dst);
	assert(rno >= 0 && rno < (s32)src->regSize && vm != NULL);
	assert(vm->reg->flags & RF_SHAREABLE);
	nvm = vmm_alloc();
	if(nvm == NULL)
		return ERR_NOT_ENOUGH_MEM;
	nvm->reg = vm->reg;
	if(rno == RNO_TEXT)
		nvm->virt = TEXT_BEGIN;
	else
		nvm->virt = vmm_findFreeAddr(dst,vm->reg->byteCount);
	if(nvm->virt == 0)
		goto errVmm;
	if(!reg_addTo(vm->reg,dst))
		goto errVmm;
	nrno = vmm_findRegIndex(dst,rno == RNO_TEXT);
	if(nrno < 0)
		goto errRem;
	REG(dst,nrno) = nvm;
	/* shared, so content-frames to shared, ptables to own */
	pageCount = BYTES_2_PAGES(vm->reg->byteCount);
	stats = paging_clonePages(src->pagedir,dst->pagedir,vm->virt,nvm->virt,pageCount,true);
	dst->sharedFrames += stats.frames;
	dst->ownFrames += stats.ptables;
	return nrno;

errRem:
	reg_remFrom(vm->reg,dst);
errVmm:
	vmm_free(nvm);
	return ERR_NOT_ENOUGH_MEM;
}

s32 vmm_cloneAll(sProc *dst) {
	u32 i,j;
	sVMRegion **regs;
	sThread *t = thread_getRunning();
	sProc *src = proc_getRunning();
	assert(dst && dst->regions == NULL && dst->regSize == 0);
	/* nothing to do? */
	if(src->regSize == 0)
		return 0;
	/* create array */
	regs = (sVMRegion **)kheap_calloc(src->regSize,sizeof(sVMRegion*));
	if(regs == NULL)
		return ERR_NOT_ENOUGH_MEM;

	for(i = 0; i < src->regSize; i++) {
		sVMRegion *vm = REG(src,i);
		/* just clone the stack-region of the current thread */
		if(vm && (!(vm->reg->flags & RF_STACK) || (tVMRegNo)i == t->stackRegion)) {
			sAllocStats stats;
			sVMRegion *nvm = vmm_alloc();
			u32 pageCount;
			if(nvm == NULL)
				goto error;
			nvm->virt = vm->virt;
			if(vm->reg->flags & RF_SHAREABLE) {
				reg_addTo(vm->reg,dst);
				nvm->reg = vm->reg;
			}
			else {
				nvm->reg = reg_clone(dst,vm->reg);
				if(nvm->reg == NULL)
					goto error;
			}
			regs[i] = nvm;
			/* now copy the pages */
			pageCount = BYTES_2_PAGES(nvm->reg->byteCount);
			stats = paging_clonePages(src->pagedir,dst->pagedir,vm->virt,nvm->virt,pageCount,
					vm->reg->flags & RF_SHAREABLE);
			dst->ownFrames += stats.ptables;
			if(vm->reg->flags & RF_SHAREABLE)
				dst->sharedFrames += stats.frames;
			/* add frames to copy-on-write, if not shared */
			else {
				u32 virt = nvm->virt;
				for(j = 0; j < pageCount; j++) {
					/* not when demand-load or swapping is outstanding since we've not loaded it
					 * from disk yet */
					if(!(vm->reg->pageFlags[j] & (PF_DEMANDLOAD | PF_SWAPPED))) {
						u32 frameNo = paging_getFrameNo(src->pagedir,virt);
						/* if not already done, mark as cow for parent */
						if(!(vm->reg->pageFlags[j] & PF_COPYONWRITE)) {
							vm->reg->pageFlags[j] |= PF_COPYONWRITE;
							if(!cow_add(src,frameNo))
								goto error;
							src->sharedFrames++;
							src->ownFrames--;
						}
						/* do it always for the child */
						nvm->reg->pageFlags[j] |= PF_COPYONWRITE;
						if(!cow_add(dst,frameNo))
							goto error;
						dst->sharedFrames++;
					}
					virt += PAGE_SIZE;
				}
			}
		}
	}
	dst->regions = regs;
	dst->regSize = src->regSize;
	return 0;

error:
	/* remove the regions that have already been added */
	/* set them first, because vmm_remove() uses them */
	dst->regions = regs;
	dst->regSize = src->regSize;
	/* Note that vmm_remove() will undo the cow just for the child; but thats ok since
	 * the parent will get its frame back as soon as it writes to it */
	/* Note also that we don't restore the old frame-counts; for dst it makes no sense because
	 * we'll destroy the process anyway. for src we can't do it because the cow-entries still
	 * exists and therefore its correct. */
	proc_removeRegions(dst,true);
	/* no need to free regs, since vmm_remove has already done it */
	return ERR_NOT_ENOUGH_MEM;
}

s32 vmm_growStackTo(sThread *t,u32 addr) {
	sVMRegion *vm = REG(t->proc,t->stackRegion);
	addr &= ~(PAGE_SIZE - 1);
	/* report failure if its outside (upper) of the region */
	if(addr >= vm->virt + ROUNDUP(vm->reg->byteCount))
		return ERR_NOT_ENOUGH_MEM;
	if(addr < vm->virt) {
		s32 newPages = (vm->virt - addr) / PAGE_SIZE;
		if(newPages > 0)
			return vmm_grow(t->proc,t->stackRegion,newPages);
	}
	return 0;
}

s32 vmm_grow(sProc *p,tVMRegNo reg,s32 amount) {
	u32 oldSize,oldVirt,virt;
	sVMRegion *vm;

	vm = REG(p,reg);
	assert(p && reg >= 0 && reg < (s32)p->regSize && vm != NULL);
	assert((vm->reg->flags & RF_GROWABLE) && !(vm->reg->flags & RF_SHAREABLE));

	/* check wether we've reached the max stack-pages */
	if(vm->reg->flags & RF_STACK && BYTES_2_PAGES(vm->reg->byteCount) + amount >= MAX_STACK_PAGES - 1)
		return ERR_NOT_ENOUGH_MEM;

	/* swap out, if necessary */
	if(amount > 0 && !swap_outUntil(amount))
		return ERR_NOT_ENOUGH_MEM;

	/* check again to be sure */
	vm = REG(p,reg);
	if(vm->reg->flags & RF_STACK && BYTES_2_PAGES(vm->reg->byteCount) + amount >= MAX_STACK_PAGES - 1)
		return ERR_NOT_ENOUGH_MEM;

	/* resize region */
	oldVirt = vm->virt;
	oldSize = vm->reg->byteCount;
	if(amount != 0) {
		sAllocStats stats;
		/* check wether the space is free */
		if(amount > 0) {
			if(vm->reg->flags & RF_STACK) {
				if(vmm_isOccupied(p,oldVirt - amount * PAGE_SIZE,oldVirt))
					return ERR_NOT_ENOUGH_MEM;
			}
			else {
				u32 end = oldVirt + ROUNDUP(vm->reg->byteCount);
				if(vmm_isOccupied(p,end,end + amount * PAGE_SIZE))
					return ERR_NOT_ENOUGH_MEM;
			}
		}
		if(!reg_grow(vm->reg,amount))
			return ERR_NOT_ENOUGH_MEM;

		/* map / unmap pages */
		if(amount > 0) {
			u32 mapFlags = PG_PRESENT;
			if(vm->reg->flags & RF_WRITABLE)
				mapFlags |= PG_WRITABLE;
			/* stack-pages are added before the existing */
			if(vm->reg->flags & RF_STACK) {
				vm->virt -= amount * PAGE_SIZE;
				virt = vm->virt;
			}
			else
				virt = vm->virt + ROUNDUP(oldSize);
			stats = paging_mapTo(p->pagedir,virt,NULL,amount,mapFlags);
			p->ownFrames += stats.frames + stats.ptables;
			/* now clear the memory */
			memclear((void*)virt,PAGE_SIZE * amount);
		}
		else {
			if(vm->reg->flags & RF_STACK) {
				virt = vm->virt;
				vm->virt += -amount * PAGE_SIZE;
			}
			else
				virt = vm->virt + ROUNDUP(vm->reg->byteCount);
			stats = paging_unmapFrom(p->pagedir,virt,-amount,true);
			p->ownFrames -= stats.frames + stats.ptables;
		}
	}
	return ((vm->reg->flags & RF_STACK) ? oldVirt : oldVirt + ROUNDUP(oldSize)) / PAGE_SIZE;
}

static bool vmm_demandLoad(sVMRegion *vm,u32 *flags,u32 addr) {
	bool res = false;
	sThread *t = thread_getRunning();

	/* calculate the number of bytes to load and zero */
	u32 loadCount = 0, zeroCount;
	if(addr - vm->virt < vm->reg->loadCount)
		loadCount = MIN(PAGE_SIZE,vm->reg->loadCount - (addr - vm->virt));
	else
		res = true;
	zeroCount = MIN(PAGE_SIZE,vm->reg->byteCount - (addr - vm->virt)) - loadCount;

	/* if another thread already loads it, wait here until he's done */
	if(*flags & PF_LOADINPROGRESS) {
		do {
			ev_wait(t->tid,EVI_VMM_DONE,(tEvObj)vm->reg);
			thread_switchNoSigs();
		}
		while(*flags & PF_LOADINPROGRESS);
		return (*flags & PF_DEMANDLOAD) == 0;
	}

	/* load from file */
	*flags |= PF_LOADINPROGRESS;
	if(loadCount)
		res = vmm_loadFromFile(t,vm,addr,loadCount);

	/* zero the rest, if necessary */
	if(res && zeroCount) {
		assert(!(vm->reg->flags & RF_SHAREABLE));
		if(!loadCount) {
			sAllocStats stats = paging_map(addr,NULL,1,PG_PRESENT | PG_WRITABLE);
			t->proc->ownFrames += stats.frames;
		}
		memclear((void*)(addr + loadCount),zeroCount);
	}

	/* wakeup all waiting processes */
	*flags &= ~PF_LOADINPROGRESS;
	ev_wakeup(EVI_VMM_DONE,(tEvObj)vm->reg);
	return res;
}

static bool vmm_loadFromFile(sThread *t,sVMRegion *vm,u32 addr,u32 loadCount) {
	s32 err;
	tPid pid = t->proc->pid;
	sFileInfo info;
	u8 *tempBuf;
	tFileNo file;
	sSLNode *n;
	u32 temp,frame;
	u8 mapFlags;

	file = vfs_real_openInode(pid,VFS_READ,vm->reg->binary.ino,vm->reg->binary.dev);
	if(file < 0) {
		err = file;
		goto error;
	}

	/* file not present or modified? */
	if((err = vfs_fstat(pid,file,&info)) < 0 || info.modifytime != vm->reg->binary.modifytime)
		goto errorClose;
	if((err = vfs_seek(pid,file,vm->reg->binOffset + (addr - vm->virt),SEEK_SET)) < 0)
		goto errorClose;

	/* first read into a temp-buffer because we can't mark the page as present until
	 * its read from disk. and we can't use a temporary mapping when switching
	 * threads. */
	tempBuf = (u8*)kheap_alloc(loadCount);
	if(tempBuf == NULL) {
		err = ERR_NOT_ENOUGH_MEM;
		goto errorClose;
	}
	if((err = vfs_readFile(pid,file,(u8*)tempBuf,loadCount)) < 0)
		goto errorFree;

	/* ensure that a frame is available; note that its easy here since no one else can demand load
	 * this page while we do it */
	if(!swap_outUntil(1))
		goto errorFree;
	/* after swapping: page not present anymore or demand-load off? there must be something wrong */
	if(((addr - vm->virt) / PAGE_SIZE) >= BYTES_2_PAGES(vm->reg->byteCount) ||
			!(vm->reg->pageFlags[(addr - vm->virt) / PAGE_SIZE] & PF_DEMANDLOAD))
		goto errorFree;

	frame = mm_allocate();
	mapFlags = PG_PRESENT;
	if(vm->reg->flags & RF_WRITABLE)
		mapFlags |= PG_WRITABLE;
	/* copy into frame */
	temp = paging_mapToTemp(&frame,1);
	memcpy((void*)temp,tempBuf,loadCount);
	paging_unmapFromTemp(1);

	/* free resources not needed anymore */
	kheap_free(tempBuf);
	vfs_closeFile(pid,file);

	/* map into all pagedirs */
	for(n = sll_begin(vm->reg->procs); n != NULL; n = n->next) {
		sProc *mp = (sProc*)n->data;
		/* the region may be mapped to a different virtual address */
		tVMRegNo mprno = vmm_getRNoByRegion(mp,vm->reg);
		sVMRegion *mpreg = REG(mp,mprno);
		assert(mprno != -1);
		paging_mapTo(mp->pagedir,mpreg->virt + (addr - vm->virt),&frame,1,mapFlags);
		if(vm->reg->flags & RF_SHAREABLE)
			mp->sharedFrames++;
		else
			mp->ownFrames++;
	}
	return true;

errorFree:
	kheap_free(tempBuf);
errorClose:
	vfs_closeFile(pid,file);
error:
#if DEBUGGING
	vid_printf("Demandload for proc %s: %s (%d)\n",t->proc->command,strerror(err),err);
#endif
	return false;
}

static u32 *vmm_getPageFlag(sVMRegion *reg,u32 addr) {
	return reg->reg->pageFlags + (addr - reg->virt) / PAGE_SIZE;
}

static tVMRegNo vmm_findRegIndex(sProc *p,bool text) {
	u32 j;
	/* start behind the fix regions (text, rodata, bss and data) */
	tVMRegNo i = text ? 0 : RNO_DATA + 1;
	for(j = 0; j < 2; j++) {
		for(; i < (s32)p->regSize; i++) {
			if(REG(p,i) == NULL)
				return i;
		}
		/* if the first pass failed, increase regions and try again; start with the current i */
		assert(j == 0);
		if(!vmm_extendRegions(p,i))
			break;
	}
	return -1;
}

static u32 vmm_findFreeStack(sProc *p,u32 byteCount) {
	u32 addr,end;
	if(byteCount > (MAX_STACK_PAGES - 1) * PAGE_SIZE)
		return 0;
	end = vmm_getFirstUsableAddr(p,true);
	for(addr = FREE_AREA_BEGIN; addr > end; addr -= MAX_STACK_PAGES * PAGE_SIZE) {
		/* leave a gap between the stacks as a guard */
		if(vmm_isOccupied(p,addr - (MAX_STACK_PAGES - 1) * PAGE_SIZE,addr) == NULL)
			return addr - ROUNDUP(byteCount);
	}
	return 0;
}

static sVMRegion *vmm_isOccupied(sProc *p,u32 start,u32 end) {
	u32 i;
	for(i = 0; i < p->regSize; i++) {
		sVMRegion *reg = REG(p,i);
		if(reg != NULL) {
			if(OVERLAPS(reg->virt,reg->virt + ROUNDUP(reg->reg->byteCount),start,end))
				return reg;
		}
	}
	return NULL;
}

static u32 vmm_getFirstUsableAddr(sProc *p,bool textNData) {
	u32 i;
	u32 addr = 0;
	for(i = 0; i < p->regSize; i++) {
		sVMRegion *reg = REG(p,i);
		if(reg != NULL && (!textNData || i <= RNO_DATA) && reg->virt + reg->reg->byteCount > addr)
			addr = reg->virt + reg->reg->byteCount;
	}
	return ROUNDUP(addr);
}

static u32 vmm_findFreeAddr(sProc *p,u32 byteCount) {
	u32 addr;
	if(byteCount > FREE_AREA_END - FREE_AREA_BEGIN)
		return 0;
	for(addr = FREE_AREA_BEGIN; addr + byteCount < FREE_AREA_END; ) {
		sVMRegion *reg = vmm_isOccupied(p,addr,addr + byteCount);
		if(reg == NULL)
			return addr;
		/* try again behind this area */
		addr = reg->virt + ROUNDUP(reg->reg->byteCount);
	}
	return 0;
}

static bool vmm_extendRegions(sProc *p,u32 i) {
	u32 count = MAX(4,i * 2);
	sVMRegion **regs = (sVMRegion **)kheap_realloc(p->regions,count * sizeof(sVMRegion*));
	if(regs == NULL)
		return false;
	memclear(regs + p->regSize,(count - p->regSize) * sizeof(sVMRegion*));
	p->regions = regs;
	p->regSize = count;
	return true;
}

static sVMRegion *vmm_alloc(void) {
	return kheap_alloc(sizeof(sVMRegion));
}

static void vmm_free(sVMRegion *vm) {
	kheap_free(vm);
}

static s32 vmm_getAttr(sProc *p,u8 type,u32 bCount,u32 *pgFlags,u32 *flags,u32 *virt,tVMRegNo *rno) {
	*rno = 0;
	switch(type) {
		case REG_TEXT:
			*pgFlags = PF_DEMANDLOAD;
			*flags = RF_SHAREABLE;
			*virt = TEXT_BEGIN;
			*rno = RNO_TEXT;
			break;
		case REG_RODATA:
			*pgFlags = PF_DEMANDLOAD;
			*flags = 0;
			*virt = vmm_getFirstUsableAddr(p,true);
			*rno = RNO_RODATA;
			break;
		case REG_DATA:
			*pgFlags = PF_DEMANDLOAD;
			*flags = RF_GROWABLE | RF_WRITABLE;
			*virt = vmm_getFirstUsableAddr(p,true);
			*rno = RNO_DATA;
			break;
		case REG_STACK:
			*pgFlags = 0;
			*flags = RF_GROWABLE | RF_WRITABLE | RF_STACK;
			*virt = vmm_findFreeStack(p,bCount);
			if(*virt == 0)
				return ERR_NOT_ENOUGH_MEM;
			break;

		case REG_SHLIBTEXT:
		case REG_SHLIBDATA:
		case REG_DLDATA:
		case REG_TLS:
		case REG_DEVICE:
		case REG_PHYS:
		case REG_SHM:
			*pgFlags = 0;
			if(type == REG_TLS)
				*flags = RF_WRITABLE | RF_TLS;
			else if(type == REG_DEVICE)
				*flags = RF_WRITABLE | RF_NOFREE;
			else if(type == REG_PHYS)
				*flags = RF_WRITABLE;
			else if(type == REG_SHM)
				*flags = RF_SHAREABLE | RF_WRITABLE;
			/* Note that this assumes for the dynamic linker that everything is put one after
			 * another @ INTERP_TEXT_BEGIN (begin of free area, too). But this is always the
			 * case since when doing exec(), the process is empty except stacks. */
			else if(type == REG_SHLIBTEXT) {
				*flags = RF_SHAREABLE;
				*pgFlags = PF_DEMANDLOAD;
			}
			else if(type == REG_DLDATA) {
				/* its growable to give the dynamic linker the chance to use dynamic memory
				 * (at least until he maps the first region behind this one) */
				*flags = RF_WRITABLE | RF_GROWABLE;
				*pgFlags = PF_DEMANDLOAD;
			}
			else if(type == REG_SHLIBDATA) {
				*flags = RF_WRITABLE;
				*pgFlags = PF_DEMANDLOAD;
			}
			*virt = vmm_findFreeAddr(p,bCount);
			if(*virt == 0)
				return ERR_NOT_ENOUGH_MEM;
			break;
		default:
			assert(false);
			break;
	}
	/* allocate / increase regions, if necessary */
	if(p->regions == NULL || *rno >= (tVMRegNo)p->regSize) {
		u32 count = MAX(4,*rno + 1);
		void *nr = kheap_realloc(p->regions,count * sizeof(sVMRegion*));
		if(!nr)
			return ERR_NOT_ENOUGH_MEM;
		memclear((sVMRegion**)nr + p->regSize,(count - p->regSize) * sizeof(sVMRegion*));
		p->regions = nr;
		p->regSize = count;
	}
	/* now a few checks; do them here because now we're sure that p->regions exists etc. */
	switch(type) {
		case REG_TEXT:
			assert(REG(p,RNO_TEXT) == NULL);
			/* fall through */
		case REG_RODATA:
			assert(REG(p,RNO_RODATA) == NULL);
			/* fall through */
		case REG_DATA:
			assert(REG(p,RNO_DATA) == NULL);
			break;

		case REG_SHLIBTEXT:
		case REG_SHLIBDATA:
		case REG_DLDATA:
		case REG_TLS:
		case REG_STACK:
		case REG_DEVICE:
		case REG_PHYS:
		case REG_SHM:
			*rno = vmm_findRegIndex(p,false);
			if(*rno < 0)
				return ERR_NOT_ENOUGH_MEM;
			break;
	}
	return 0;
}

void vmm_sprintfRegions(sStringBuffer *buf,sProc *p) {
	u32 i,c = 0;
	sVMRegion *reg;
	for(i = 0; i < p->regSize; i++) {
		reg = REG(p,i);
		if(reg != NULL) {
			if(c)
				prf_sprintf(buf,"\n");
			prf_sprintf(buf,"VMRegion %d (%#08x .. %#08x):\n",i,reg->virt,
					reg->virt + reg->reg->byteCount - 1);
			reg_sprintf(buf,reg->reg,reg->virt);
			c++;
		}
	}
}


#if DEBUGGING

void vmm_dbg_printShort(sProc *p) {
	sVMRegion *reg;
	u32 i;
	for(i = 0; i < p->regSize; i++) {
		reg = REG(p,i);
		if(reg != NULL) {
			u32 start,end;
			vmm_getRegRange(p,i,&start,&end);
			vid_printf("\t\t%08x .. %08x: ",start,end - 1);
			reg_dbg_printFlags(reg->reg);
			vid_printf("\n");
		}
	}
}

void vmm_dbg_print(sProc *p) {
	sStringBuffer buf;
	buf.dynamic = true;
	buf.len = 0;
	buf.size = 0;
	buf.str = NULL;
	vid_printf("Regions of proc %d (%s)\n",p->pid,p->command);
	vmm_sprintfRegions(&buf,p);
	if(buf.str != NULL)
		vid_printf("%s\n",buf.str);
	else
		vid_printf("- no regions -\n");
	kheap_free(buf.str);
}

#endif
