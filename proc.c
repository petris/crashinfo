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
#include <inttypes.h>
#include <dirent.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

#include "util.h"
#include "info.h"
#include "conf.h"
#include "log.h"

static struct {
	long pid, nspid;
} *pidmap;

static int pidmap_count;

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
	struct dirent *de;
	int c, fd;
	DIR *d;

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

	// Read /proc/PID/task/TID/status to map namespace and toplevel PIDs
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

	c = 0;
	while (NULL != (de = readdir(d))) {
		if (isdigit(de->d_name[0])) {
			FILE *f;

			snprintf(buf, sizeof buf, "task/%s/status", de->d_name);
			fd = openat(run.proc_fd, buf, O_RDONLY);
			if (fd < 0) {
				log_err("Can't open '%s': %s", buf, strerror(errno));
				goto err;
			}
			f = fdopen(fd, "r");
			if (!f) {
				log_err("Can't open '%s': %s", buf, strerror(errno));
				close(fd);
				goto err;
			}

			while (fgets(buf, sizeof buf, f)) {
				char *nspid, *end;

				if (strncmp(buf, "NSpid:", 6)) continue;

				nspid = strrchr(buf, ' ');
				if (!nspid) {
					nspid = strrchr(buf, '\t');
					if (!nspid) {
						goto malformed;
					}
				}

				if (c == pidmap_count) {
					pidmap_count = pidmap_count * 2 ?: 32;
					pidmap = realloc(pidmap, pidmap_count * sizeof *pidmap);
				}

				pidmap[c].nspid = strtol(nspid + 1, &end, 10);
				if (*end != '\n') {
					goto malformed;
				}

				pidmap[c].pid = strtol(de->d_name, &end, 10);
				if (*end != '\0') {
					goto malformed;
				}

				c++;
				break;

malformed:			log_notice("Malformed line '%s'", buf);
				fclose(f);
				goto err;
			}
			fclose(f);
		}
	}
	pidmap_count = c;
	closedir(d);

	return 0;

err:	closedir(d);
	return -1;
}

int proc_pid_map(int nspid)
{
	int i;

	for (i = 0; i < pidmap_count; i++) {
		if (pidmap[i].nspid == nspid)
			return pidmap[i].pid;
	}

	log_warn("Failed to map NS pid %d", nspid);
	return nspid;
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
