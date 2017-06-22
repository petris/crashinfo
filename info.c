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
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#include "util.h"
#include "info.h"
#include "conf.h"
#include "proc.h"
#include "log.h"
#include "unw.h"

const char SP[24] = "                       ";

typedef char yaml_esc_buf_t[6];

static const char *yaml_esc(char c, yaml_esc_buf_t *buf)
{
	switch (c) {
		case '\n': return "\\n";
		case '\t': return "\\t";
		case '\r': return "\\r";
		case '\\': return "\\\\";
		case '"': return "\\\"";
	}

	if (isprint(c)) {
		(*buf)[0] = c; (*buf)[1] = '\0';
	} else {
		sprintf(*buf, "\\x%02x", c);
	}

	return *buf;
}

int fputy(const char *s, FILE *stream)
{
	yaml_esc_buf_t buf;

	if (EOF == fputc('"', stream)) {
		return EOF;
	}

	for (; *s; s++) {
		if (EOF == fputs(yaml_esc(*s, &buf), stream)) {
			return EOF;
		}
	}

	if (EOF == fputc('"', stream)) {
		return EOF;
	}

	return 0;
}

#if 0
static int fputy_readlinkat(int dirfd, const char *name, FILE *stream)
{
	char buf[PATH_MAX + 1];
	int c;

	c = readlinkat(dirfd, name, buf, sizeof buf - 1);
	if (c >= 0) {
		buf[c] = 0;
	} else {
		snprintf(buf, sizeof buf, "ERROR_%d", errno);
	}

	return fputy(buf, stream);
}
#endif

static int proc_dump(int dir, const struct conf_multi_str_s *files, int indent)
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

static void task_dumper(int tid)
{
	int taskfd;
	char path[32];

	fprintf(run.info.output, "  - tid: %d # -------------------------"
				"-------------------------\n", tid);

	snprintf(path, sizeof path, "task/%d", tid);
	taskfd = openat(run.proc_fd, path, O_RDONLY | O_DIRECTORY);
	if (taskfd >= 0) {
		proc_dump(taskfd, conf.proc_dump.task, 4);
		close(taskfd);
	} else {
		log_err("Can't open '%s/%s': %s", conf.proc.path, path, strerror(errno));
	}
}

int info_dump(void)
{
	struct timespec end_tp;
	yaml_esc_buf_t buf;
	char datetime[24];
	FILE *p;

	// datetime: 2001-12-15T02:59:43Z
	fputs("---\ndatetime: ", run.info.output);
	strftime(datetime, sizeof datetime, "%Y-%m-%dT%H:%M:%SZ\n", &run.start_tm);
	fputs(datetime, run.info.output);

	// exe: "/usr/bin/vi"
	fputs("exe: ", run.info.output);
	fputy(conf.proc.exe, run.info.output);
	fputc('\n', run.info.output);

	// cmdline: [ "vi", "/etc/passwd" ]
	// cmdline can have arguments members separated by spaces or zeroes
	fputs("cmdline: [ ", run.info.output);
	p = open_proc("cmdline");
	if (p) {
		int c = fgetc(p);
		if (c != EOF) {
			fputc('"', run.info.output);
			do {
				if (c != 0) {
					fputs(yaml_esc(c, &buf), run.info.output);
				} else {
					c = fgetc(p);
					if (c != EOF) {
						fputs("\", \"", run.info.output);
						ungetc(c, p);
					}
				}
			} while (EOF != (c = fgetc(p)));
			fputc('"', run.info.output);
		}
		fclose(p);
	}
	fputs(" ]\n", run.info.output);

	// mappings:
	if (!conf.proc.maps) {
		fputs("executable_mappings: ~\n", run.info.output);
	} else {
		const struct conf_multi_mapping_s *map;
		fputs("executable_mappings:\n", run.info.output);
		for (map = conf.proc.maps; map; map = map->next) {
			fprintf(run.info.output, "  0x%016" PRIx64 ": ", map->addr);
			fputy(map->file, run.info.output);
			fputc('\n', run.info.output);
		}
	}


	// proc_dump:
	proc_dump(run.proc_fd, conf.proc_dump.root, 0);

	// dump information from the unwinder
	unw_dump(task_dumper);
	
	// processing_time: 12.123456
	clock_gettime(CLOCK_REALTIME, &end_tp);
	end_tp.tv_sec -= run.start_tp.tv_sec;
	if (end_tp.tv_nsec < run.start_tp.tv_nsec) {
		end_tp.tv_nsec += 1000000000;
		end_tp.tv_sec -= 1;
	}
	end_tp.tv_nsec -= run.start_tp.tv_nsec;
	fprintf(run.info.output, "processing_time: %d.%06ld\n", (int)end_tp.tv_sec, end_tp.tv_nsec/1000);

	if (0 != fflush(run.info.output) || ferror(run.info.output)) {
		if (errno == EPIPE) {
			log_warn("Info stream truncated");
		} else {
			log_err("Failed flushing the info stream: %s", strerror(errno));
		}
	}
	if (0 != fsync(run.info.output_fd) && errno != EROFS && errno != EINVAL) {
		log_err("Failed synchronizing the info stream: %s", strerror(errno));
	}

	return 0;
}
