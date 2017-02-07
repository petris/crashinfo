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

#ifndef CONF_H
#define CONF_H

#include <stdint.h>
#include <time.h>

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

extern const char delim[];

/** Possible options for handling output path collisions */
enum conf_exists_e {
	CONF_EXISTS_DEFAULT = 0,
	CONF_EXISTS_APPEND,
	CONF_EXISTS_OVERWRITE,
	CONF_EXISTS_KEEP,
	CONF_EXISTS_SEQUENCE,
};

struct conf_multi_str_s;

/** Represents one string of multi-value string configuration option. */
struct conf_multi_str_s {
	/** The next value. */
	struct conf_multi_str_s *next;
	/** The string value. */
	char str[];
};

struct conf_multi_mapping_s;

/** Represents one mapping of multi-value mapping configuration option. */
struct conf_multi_mapping_s {
	/** The next value. */
	struct conf_multi_mapping_s *next;
	/** Mapping address. */
	uint64_t addr;
	/** Mapped image. */
	char file[];
};

/** Output configuration. */
struct conf_output_s {
	/** Output file. */
	const char *output;
	/** How to behave if the output exists. */
	enum conf_exists_e exists;
	/** Maximum sequence number for CONF_EXISTS_SEQUENCE. */
	int exists_seq;
	/** Create the whole path if needed. */
	int mkdir;
	/** Parse output trough filters. */
	struct conf_multi_str_s *filter;
	/** Programs executed after the output is completed. */
	struct conf_multi_str_s *notify;
};

/** Program configuration structure. Populated by command line arguments
 *  and from configuration files. Some information can be read automatically
 *  from /proc, if the process directory is available */
struct conf_s {
	/** Info output. YAML formated process details are send there */
	struct conf_output_s info;
	/** Core output. Core is copied here. */
	struct conf_output_s core;
	/** Buffer for backwards seeks, unwinder argument. */
	int core_buffer_size;
	/** Notify with both info and core streams as arguments */
	struct conf_multi_str_s *info_core_notify;
	/** Logging configuration. */
	struct {
		/** Log level threshold for info output. */
		int info;
		/** Log level threshold for stderr. */
		int stderr;
		/** Log level threshold for syslog. */
		int syslog;
	} log;
	/** Process information. */
	struct {
		/** Do not read information from /proc. */
		int ignore;
		/** /proc/<PID> path. */
		const char *path;
		/** Exe path (/proc/<PID>/exe). */
		const char *exe;
		/** Mappings (/proc/<PID>/maps). */
		struct conf_multi_mapping_s *maps;
	} proc;
	/** /proc dumping configuration. */
	struct {
		/** List of files from /proc/<PID> dumped to info output. */
		struct conf_multi_str_s *root;
		/** List of files from /proc/<PID>/task/<TGID> dumped to info output. */
		struct conf_multi_str_s *task;
	} proc_dump;
};

/** The only instance of configuration structure. */
extern struct conf_s conf;

struct run_multi_filter_s;

/** Run time filter data. */
struct run_multi_filter_s {
	/** Filter connected to the output of this filter. */
	struct run_multi_filter_s *next;
	/** Filter command. */
	const char *filter;
	/** Filter PID. */
	int pid;
};

/** Run time output data. */
struct run_output_s {
	FILE *output;
	int output_fd;
	const char *output_filename;
	struct run_multi_filter_s *filter;
};

/** Runtime structure. Contains global runtime data. */
struct run_s {
	struct run_output_s info;
	struct run_output_s core;
	/** /proc/<PID> fd */
	int proc_fd;
	/** PID of the crashed process. */
	int pid;
	struct timespec start_tp;
	struct tm start_tm;
};

/** The only instance of runtime data structure. */
extern struct run_s run;

int parse_line(char *line);

int parse_file(const char *path);

#define foreach_safe(first, iter, tmp) for (((iter) = (first)) ? ((tmp) = (iter)->next) : 0 ; (iter); (iter) = (tmp), (tmp) = (iter)->next)

#ifndef NDEBUG
void log_conf(void);
#else
#define log_conf() (void)0
#endif

#endif // CONF_H
