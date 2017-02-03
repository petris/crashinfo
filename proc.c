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
#include <inttypes.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

#include "util.h"
#include "info.h"
#include "conf.h"
#include "log.h"

FILE *open_proc(const char *name)
{
	FILE *f;
	int fd;

	fd = openat(run.proc_fd, name, O_RDONLY);
	if (fd < 0) {
		return NULL;
	}

	f = fdopen(fd, "r");
	if (!f) {
		int err = errno;
		close(fd);
		errno = err;
	}

	return f;
}

int read_proc_info(void)
{
	char buf[PATH_MAX + 256];
	int c;

	// Read /proc/PID/exe
	if (!conf.proc.exe) {
		c = readlinkat(run.proc_fd, "exe", buf, sizeof buf - 1);
		if (c >= 0) {
			conf.proc.exe = strndup(buf, c);
		}
		if (!conf.proc.exe) {
			log_crit("Can't get executable path: %s", strerror(errno));
			return -1;
		}
	}

	// Read /proc/PID/maps
	if (!conf.proc.maps) {
		FILE *f = open_proc("maps");

		if (!f) {
			log_crit("Can't open mappings: %s", strerror(errno));
			return -1;
		}

		while (fgets(buf, sizeof buf, f)) {
			int file, end, rtn;
			char exec, tmp;
			uint64_t addr;

			rtn = sscanf(buf, "%jx-%*x %*c%*c%c%*c %*x %*s %*d %n%c%*[^\n]",
					&addr, &exec, &file, &tmp);
			if (3 != rtn || exec != 'x' || tmp != '/') {
				continue;
			}

			end = snprintf(buf, sizeof buf, "proc_maps=%#" PRIx64 ":", addr);
			assert(end < file);
			memmove(buf + end, buf + file, strlen(buf + file) + 1);

			if (parse_line(buf)) {
				log_err("Failed to parse '%s'", buf);
				fclose(f);
				return -1;
			}
		}
		fclose(f);
	}

	return 0;
}

int proc_dump(int dir, const struct conf_multi_str_s *files, int indent)
{
	fputs(spaces(indent), run.info.output);

	if (!files) {
		fputs("proc_dump: ~\n", run.info.output);
		return 0;
	}

	if (conf.proc.ignore) {
		fputs("proc_dump: ~ # proc_ignore = 1\n", run.info.output);
		return 0;
	}

	fputs("proc_dump:\n", run.info.output);
	do {
		char buf[4096];
		FILE *pf;
		int fd;

		fputs(spaces(indent + 2), run.info.output);
		fputy(files->str, run.info.output);

		fd = openat(dir, files->str, O_RDONLY | O_CLOEXEC);
		if (fd < 0 || NULL == (pf = fdopen(fd, "r"))) {
			int err = errno;
			log_err("Can't open proc file '%s': %s", files->str, strerror(err));
			fprintf(run.info.output, ": ~ # Can't open: %s\n", strerror(err));
			if (fd >= 0) close(fd);
			continue;
		}

		fputs(": |\n", run.info.output);
		while (fgets(buf, sizeof buf, pf)) {
			buf[strlen_chomp(buf)] = 0;
			fputs(spaces(indent + 4), run.info.output);
			fputs(buf, run.info.output);
			fputc('\n', run.info.output);
		}
		fclose(pf);
	} while (NULL != (files = files->next));

	return 0;
}
