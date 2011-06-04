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
#include <sys/task/thread.h>
#include <sys/vfs/vfs.h>
#include <sys/vfs/node.h>
#include <sys/vfs/file.h>
#include <sys/video.h>
#include <sys/printf.h>
#include <sys/log.h>
#include <sys/config.h>
#include <esc/esccodes.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>

#define TAB_WIDTH		4
#define BUF_SIZE		2048
#define LOG_DIR			"/system"
#define LOG_FILENAME	"log"
#define DUMMY_STDIN		"/system/stdin"

static void log_printc(char c);
static uchar log_pipePad(void);
static void log_escape(const char **str);
static ssize_t log_write(tPid pid,tFileNo file,sVFSNode *n,const void *buffer,
		off_t offset,size_t count);
static void log_flush(void);

/* don't use a heap here to prevent problems */
static char buf[BUF_SIZE];
static size_t bufPos;
static uint col = 0;

static bool logToSer = true;
static tFileNo logFile;
static bool vfsIsReady = false;
static sPrintEnv env = {
	.print = log_printc,
	.escape = log_escape,
	.pipePad = log_pipePad
};

void log_vfsIsReady(void) {
	tInodeNo inodeNo;
	sVFSNode *logNode;
	tFileNo inFile;
	tFD in,out,err;
	char *nameCpy;
	sProc *p = proc_getRunning();

	/* open log-file */
	assert(vfs_node_resolvePath(LOG_DIR,&inodeNo,NULL,VFS_CREATE) == 0);
	nameCpy = strdup(LOG_FILENAME);
	assert(nameCpy != NULL);
	logNode = vfs_file_create(KERNEL_PID,vfs_node_get(inodeNo),nameCpy,vfs_file_read,log_write);
	assert(logNode != NULL);
	logFile = vfs_openFile(KERNEL_PID,VFS_WRITE,vfs_node_getNo(logNode),VFS_DEV_NO);
	assert(logFile >= 0);

	/* create stdin, stdout and stderr for initloader. out and err should write to the log-file */
	/* stdin is just a dummy file. init will remove these fds before starting the shells which will
	 * create new ones (for the vterm of the shell) */
	assert(vfs_node_resolvePath(DUMMY_STDIN,&inodeNo,NULL,VFS_CREATE) == 0);
	assert((inFile = vfs_openFile(p->pid,VFS_READ,inodeNo,VFS_DEV_NO)) >= 0);
	in = proc_getFreeFd();
	assert(in == 0 && proc_assocFd(in,inFile) == 0);
	out = proc_getFreeFd();
	assert(out == 1 && proc_assocFd(out,logFile) == 0);
	err = proc_getFreeFd();
	assert(err == 2 && proc_assocFd(err,logFile) == 0);

	/* now write the stuff we've saved so far to the log-file */
	vfsIsReady = true;
	/* don't write that again to COM1 */
	logToSer = false;
	log_flush();
	logToSer = true;
}

void log_printf(const char *fmt,...) {
	va_list ap;
	va_start(ap,fmt);
	log_vprintf(fmt,ap);
	va_end(ap);
}

void log_vprintf(const char *fmt,va_list ap) {
	prf_vprintf(&env,fmt,ap);
	log_flush();
}

static void log_printc(char c) {
	if(conf_get(CONF_LOG) && !vfsIsReady)
		log_writeChar(c);
	if(bufPos >= BUF_SIZE)
		log_flush();
	if(bufPos < BUF_SIZE) {
		buf[bufPos++] = c;
		switch(c) {
			case '\n':
			case '\r':
				col = 0;
				break;
			case '\t':
				col = (col + (TAB_WIDTH - col % TAB_WIDTH)) % VID_COLS;
				if(col == 0)
					log_printc(' ');
				break;
			default:
				col = (col + 1) % VID_COLS;
				if(col == 0)
					log_printc('\n');
				break;
		}
	}
}

static uchar log_pipePad(void) {
	return VID_COLS - col;
}

static void log_escape(const char **str) {
	int n1,n2,n3;
	escc_get(str,&n1,&n2,&n3);
}

static ssize_t log_write(tPid pid,tFileNo file,sVFSNode *node,const void *buffer,
		off_t offset,size_t count) {
	if(conf_get(CONF_LOG) && logToSer) {
		char *str = (char*)buffer;
		size_t i;
		for(i = 0; i < count; i++)
			log_writeChar(str[i]);
	}
	return vfs_file_write(pid,file,node,buffer,offset,count);
}

static void log_flush(void) {
	if(vfsIsReady && bufPos) {
		vfs_writeFile(KERNEL_PID,logFile,buf,bufPos);
		bufPos = 0;
	}
}
