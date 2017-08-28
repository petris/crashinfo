/**
 * This is part of the crashinfo utility
 *
 * Copyright (C) 2017 Petr Malat
 *
 * Contact: Petr Malat <oss@malat.biz>
 *
 * This utility is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 3 as published by the Free Software Foundation.
 *
 * This utility is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#define _ATFILE_SOURCE
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>

#include "info.h"
#include "conf.h"
#include "log.h"
#include "unw.h"

#ifdef CRASHINFO_WITH_LIBUNWIND

#include <libunwind-coredump.h>

static struct {
	unw_addr_space_t as;
	struct UCD_info *ui;
	int ok;
} core;

/** Prepare for dumping the core, doesn't require mappings
 * @return PID or -1 on error */
int unw_prepare(int core_fd)
{
	int minpid = INT_MAX, minpid_fs = INT_MAX, pid, thread;
	char buf[20];

	core.as = unw_create_addr_space(&_UCD_accessors, 0);
	if (!core.as) {
		log_err("Failed to create address space");
		return -1;
	}

	core.ui = _UCD_create_fd(core_fd, "<pipe>", conf.core_buffer_size);
	if (!core.ui) {
		log_err("Failed to create UCD_info");
		unw_destroy_addr_space(core.as);
		return -1;
	}

	for (thread = 0; thread < _UCD_get_num_threads(core.ui); thread++) {
		_UCD_select_thread(core.ui, thread);
		pid = _UCD_get_pid(core.ui);

		if (pid < minpid) {
			minpid = pid;
		}

		snprintf(buf, sizeof buf, "/proc/%d", pid);
		if (pid < minpid_fs && !access(buf, F_OK)) {
			minpid_fs = pid;
		}
	}

	pid = minpid_fs < INT_MAX ? minpid_fs : minpid;
	log_dbg("Unwinder returned PID: %d", pid);
	core.ok = 1;
	return pid;
}

int unw_dump(task_dumper_t task_dumper)
{
	unw_cursor_t c;
	int rtn, thread, i;

	if (!core.ok) return -1;

	if (!conf.proc.maps) {
		log_warn("Mapping information are not available\n");
	} else {
		const struct conf_multi_mapping_s *map;
		for (map = conf.proc.maps; map; map = map->next) {
			rtn = _UCD_add_backing_file_at_vaddr(core.ui, map->addr, map->file);
		}
	}

	rtn = unw_init_remote(&c, core.as, core.ui);
	if (rtn) {
		log_err("Failed to initialize the unwind cursor: %s",
				unw_strerror(rtn));
		rtn = -1;
		goto rtn0;
	}

	fputs("threads:\n", run.info.output);
	for (thread = 0; thread < _UCD_get_num_threads(core.ui); thread++) {
		const struct timeval *t;
		int depth;

		_UCD_select_thread(core.ui, thread);

		rtn = unw_init_remote(&c, core.as, core.ui);
		if (rtn) {
			log_err("Failed to initialize the unwind cursor: %s",
					unw_strerror(rtn));
			continue;
		}

		task_dumper(_UCD_get_pid(core.ui));

                t = _UCD_get_utime(core.ui);
		fprintf(run.info.output, "    user_time: %ld.%06ld\n", t->tv_sec, t->tv_usec);

		t = _UCD_get_stime(core.ui);
		fprintf(run.info.output, "    system_time: %ld.%06ld\n", t->tv_sec, t->tv_usec);
		
		fputs("    registers: [", run.info.output);
		for (i = 0; i < 256; i++) {
			unw_word_t reg;

			if (unw_get_reg(&c, i, &reg)) {
				break;
			}

			if (i != 0) fputc(',', run.info.output);
			if (i % 4 == 0) {
				fputs("\n     ", run.info.output);
			}
			fprintf(run.info.output, " 0x%016lx", (long)reg);
		}
		fputs(" ]\n", run.info.output);

		fputs("    backtrace: [", run.info.output);
		for (depth = 0; depth < conf.backtrace_max_depth; depth++) {
			unw_word_t off;
			unw_proc_info_t pi;
			char ptr[17], fname[256];
			const char *file;
			int signal, exception;
			unw_word_t ip;
			long length;

			rtn = unw_get_reg(&c, UNW_REG_IP, &ip);
			if (!rtn) {
				snprintf(ptr, sizeof ptr, "%016lx", (long)ip);
			} else {
				strncat(ptr, "UNKNOWN", sizeof ptr);
			}

			rtn = unw_get_proc_info(&c, &pi);
			if (!rtn) {
				length = pi.end_ip - pi.start_ip;
				exception = pi.handler ? 1 : 0;
			} else {
				length = 0;
				exception = -1;
			}

			rtn = unw_is_signal_frame(&c);
			if (rtn > 0) {
				signal = 1;
			} else if (rtn == 0) {
				signal = 0;
			} else {
				signal = -1;
			}

			if (depth > 0) {
				fputc(',', run.info.output);
			}
			fprintf(run.info.output, "\n      { a: %s", ptr);

			rtn = unw_get_proc_name(&c, fname, sizeof fname, &off);
			if (!rtn) {
				fprintf(run.info.output, ", s: %s,%s o: %#5lx, l: %#5lx", fname,
						spaces(20 - strlen(fname)), (long)off, length);
			}

			fprintf(run.info.output, ", e: %d, S: %d", exception, signal);

			file = _UCD_get_proc_backing_file(core.ui, ip);
			if (file) {
				fputs(", f: ", run.info.output);
				fputy(file, run.info.output);
			}
			fputs(" }", run.info.output);

			if (0 >= unw_step(&c)) {
				break;
			}
		}
		fputs(" ]\n", run.info.output);
	}

	rtn = 0;

rtn0:	_UCD_destroy(core.ui);
	unw_destroy_addr_space(core.as);
	return rtn;
}

#else // CRASHINFO_WITH_LIBUNWIND

int unw_prepare(int core_fd)
{
	return -1;
}

int unw_dump(task_dumper_t task_dumper)
{
	struct dirent *dirent;
	int fd;
	DIR *d;

	if (conf.proc.ignore) {
		fputs("threads: ~ # Unwinder is disabled and proc_ignore = 1\n", run.info.output);
		return 0;
	}

	fd = openat(run.proc_fd, "task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		log_err("Can't open '%s/task': %s", conf.proc.path, strerror(errno));
		return -1;
	}

	d = fdopendir(fd);
	if (!d) {
		log_err("Can't open '%s/task': %s", conf.proc.path, strerror(errno));
		close(fd);
		return -1;
	}

	fputs("threads:\n", run.info.output);
	while (NULL != (dirent = readdir(d))) {
		if (isdigit(dirent->d_name[0])) {
			task_dumper(atoi(dirent->d_name));
		}
	}
	closedir(d);

	return 0;
}

#endif // CRASHINFO_WITH_LIBUNWIND
