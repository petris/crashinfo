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

#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>

#include "util.h"
#include "conf.h"
#include "log.h"

/** List of delimiter characters. */
const char delim[] = " \n\r\t\f\v";

/** The default configuration. */
struct conf_s conf = {
	.info = {
		.exists = CONF_EXISTS_APPEND,
	},
	.core = {
		.exists = CONF_EXISTS_KEEP,
	},
	.core_buffer_size = 4 * 1024 * 1024,
	.backtrace_max_depth = 50,
	.log = {
		.syslog = -1,
		.info = LOG_NOTICE,
#ifdef NDEBUG
		.stderr = LOG_ERR,
#else
		.stderr = LOG_DEBUG,
#endif
	},
};

/** Running data. */
struct run_s run = {
	.pid = -1,
};

struct parse_keywords_s;

/** Structure representing configuration keyword. */
struct parse_keywords_s {
	/** The keyword. */
	const char *keyword;
	/** The keyword value is stored here. */
	void *storage;
	/** Function parsing the configuration value. */
	int (*parser)(const struct parse_keywords_s *, char *);
	/** Additional data for the parser function. */
	const void *parser_arg;
};

/** Structure used for mapping enum symbolic representation to a number. */
struct parse_enum_s {
	/** Symbolic name of the value. */
	const char *name;
	/** The numerical value. */
	int value;
};

/** Boolean enum values. */
static const struct parse_enum_s parse_enum_bool[] = {
	{ "0", 0 }, { "1", 1 }, {}
};

/** conf_exists_e enum values. */
static const struct parse_enum_s parse_enum_exists[] = {
	{ "append", CONF_EXISTS_APPEND },
	{ "overwrite", CONF_EXISTS_OVERWRITE },
	{ "keep", CONF_EXISTS_KEEP },
	{ "sequence", CONF_EXISTS_SEQUENCE },
	{}
};

/** Log level enum values. */
static const struct parse_enum_s parse_enum_loglevel[] = {
	{ "none", -1 },
	{ "emerg", LOG_EMERG },
	{ "alert", LOG_ALERT },
	{ "crit", LOG_CRIT },
	{ "err", LOG_ERR },
	{ "warning", LOG_WARNING },
	{ "notice", LOG_NOTICE },
	{ "info", LOG_INFO },
	{ "debug", LOG_DEBUG },
	{}
};

/** Verify there are no trailing data on the line
 *  @return 0 if there aren't trailing data. */
static int parse_endline(void)
{
	char *token = strtok(NULL, delim);

	if (!token) {
		return 0;
	}

	log_crit("Garbage at the end of the line: %s...", token);
	return -1;
}

/** Parse an enumeration option.
 *  @param[in] keyword - keyword specification.
 *  @param[in/out] value - string containing one if the enum values. The content
 *                         of it is undefined after the return.
 *  @return 0 on success. */
static int parse_enum(const struct parse_keywords_s *keyword, char *value)
{
	const struct parse_enum_s *parse_enum;
	char values[256], elip[] = "...";
	
	value = strtok(value, delim);
	assert(value);

	for (parse_enum = keyword->parser_arg; parse_enum->name; parse_enum++) {
		if (!strcmp(parse_enum->name, value)) {
			*(int *)keyword->storage = parse_enum->value;
			return parse_endline();
		}
	}

	
	parse_enum = keyword->parser_arg;
	if (parse_enum->name) {
		const int max_len = sizeof values - sizeof elip;
		strncpy(values, parse_enum->name, max_len);
		for (parse_enum++; parse_enum->name; parse_enum++) {
			strncat(values, ", ", max_len);
			strncat(values, parse_enum->name, max_len);
		}
		values[max_len] = 0;
		if (strlen(values) == max_len) {
			strcat(values, elip);
		}
	}

	log_crit("Invalid value '%s' for '%s', expected one of [%s]",
			value, keyword, values);
	return -1;
}

/** Parse multi string configuration option.
 *  @param[in] keyword - keyword specification
 *  @param[in] value - the string value
 *  @return 0 on success. */
static int parse_string_multi(const struct parse_keywords_s *keyword, char *value)
{
	struct conf_multi_str_s *multi_str, **iter;
	int len = strlen_chomp(value);

	if (!strcmp("~", value)) {
		struct conf_multi_str_s *tmp;

		for (tmp = *(struct conf_multi_str_s **)keyword->storage;
		     tmp; tmp = multi_str) {
			multi_str = tmp->next;
			free(tmp);
		}
		*(struct conf_multi_str_s **)keyword->storage = NULL;
		return 0;
	}

	multi_str = malloc(len + sizeof *multi_str + 1);
	if (!multi_str) {
		log_crit("Allocation failed while processing '%s'", keyword->keyword);
		return -1;
	}

	multi_str->next = NULL;
	strncpy(multi_str->str, value, len);
	multi_str->str[len] = 0;

	for (iter = keyword->storage; *iter; iter = &(*iter)->next);
	*iter = multi_str;

	return 0;
}

/** Parse string configuration option.
 *  @param[in] keyword - keyword specification
 *  @param[in] value - the string value
 *  @return 0 on success. */
static int parse_string(const struct parse_keywords_s *keyword, char *value)
{
	char **str = keyword->storage;

	if (!strcmp("~", value)) {
		free(*str);
		*str = NULL;
		return 0;
	}

	if (*str) {
		log_info("'%s' specified multiple times", keyword->keyword);
		free(*str);
	}

	*str = strndup(value, strlen_chomp(value));
	if (!*str) {
		log_crit("Allocation failed while processing '%s'", keyword->keyword);
		return -1;
	}

	return 0;
}

/** Parse an integer option.
 *  @param[in] keyword - keyword specification.
 *  @param[in/out] value - string containing the integer value. The content of
 *                         it is undefined after the return.
 *  @return 0 on success. */
static int parse_int(const struct parse_keywords_s *keyword, char *value)
{
	int *num = keyword->storage;
	long int_value;
	char *end;

	if (*num != INT_MIN) {
		log_info("'%s' specified multiple times", keyword->keyword);
	}

	value = strtok(value, delim);
	int_value = strtol(value, &end, 0);
	if (*end != '\0') {
		log_crit("Keyword '%s' requires integer value. Got '%s'",
				keyword->keyword, value);
		return -1;
	}

	*num = (int)int_value;
	return parse_endline();
}

/** Parse a mapping option.
 *  @param[in] keyword - keyword specification
 *  @param[in] value - <addr>:<path> value
 *  @return 0 on success. */
static int parse_mapping_multi(const struct parse_keywords_s *keyword, char *value)
{
	struct conf_multi_mapping_s *multi_map, **iter;
	uint64_t addr;
	char *end;
	int len;

	addr = strtoll(value, &end, 0);
	if (*end != ':') {
		log_crit("Keyword '%s' requires the argument in the form "
				"<addr>:<path>. Got '%s'", keyword->keyword, value);
		return -1;
	}

	len = strlen_chomp(end + 1);
	multi_map = malloc(len + sizeof *multi_map + 1);
	if (!multi_map) {
		log_crit("Allocation failed while processing '%s'", keyword->keyword);
		return -1;
	}

	multi_map->next = NULL;
	strncpy(multi_map->file, end + 1, len);
	multi_map->file[len] = 0;
	multi_map->addr = addr;

	// Keep order for dumping
	for (iter = keyword->storage; *iter; iter = &(*iter)->next);
	*iter = multi_map;

	return 0;
}

/** Configuration options and their parsers */
static const struct parse_keywords_s keywords[] = {
	// Info stream options (YAML)
	{ "info_exists", &conf.info.exists, parse_enum, parse_enum_exists },
	{ "info_exists_seq", &conf.info.exists_seq, parse_int },
	{ "info_filter", &conf.info.filter, parse_string_multi },
	{ "info_mkdir",  &conf.info.mkdir,  parse_enum, parse_enum_bool },
	{ "info_notify", &conf.info.notify, parse_string_multi },
	{ "info_output", &conf.info.output, parse_string },

	{ "backtrace_max_depth", &conf.backtrace_max_depth, parse_int },
	
	// Core stream options
	{ "core_exists",     &conf.core.exists, parse_enum, parse_enum_exists },
	{ "core_exists_seq", &conf.core.exists_seq, parse_int },
	{ "core_filter",     &conf.core.filter, parse_string_multi },
	{ "core_mkdir",      &conf.core.mkdir,  parse_enum, parse_enum_bool },
	{ "core_notify",     &conf.core.notify, parse_string_multi },
	{ "core_output",     &conf.core.output, parse_string },
	{ "core_buffer_size",&conf.core_buffer_size, parse_int },

	{ "info_core_notify",&conf.info_core_notify, parse_string_multi },

	// Core file
	{ "core", &conf.core_path, parse_string },

	// Logging options
	{ "log_info", &conf.log.info, parse_enum, parse_enum_loglevel },
	{ "log_syslog", &conf.log.syslog, parse_enum, parse_enum_loglevel },
	{ "log_stderr", &conf.log.stderr, parse_enum, parse_enum_loglevel },

	// /proc options, used to provide information normally read from /proc
	{ "proc_ignore", &conf.proc.ignore, parse_enum, parse_enum_bool },
	{ "proc_path", &conf.proc.path, parse_string },
	{ "proc_exe", &conf.proc.exe, parse_string },
	{ "proc_maps", &conf.proc.maps, parse_mapping_multi },

	// /proc dumping options
	{ "proc_dump_root", &conf.proc_dump.root, parse_string_multi },
	{ "proc_dump_task", &conf.proc_dump.task, parse_string_multi },
};

/** Parse an option line.
 *  @param[in] line - expected form: keyword = value
 *  @return 0 on success. */
int parse_line(char *line)
{
	char *keyword, *value;
	int i;

	value = strchr(line, '=');
	if (value) {
		*value++ = 0;
		while (*value && strchr(delim, *value)) value++;
	}

	keyword = strtok(line, delim);
	if (!keyword || *keyword == '#') {
		return 0;
	}

	if (!value || !*value) {
		log_crit("Missing value for '%s'", keyword);
		return 1;
	}

	value[strlen_chomp(value)] = 0;

	for (i = 0; i < ARRAY_SIZE(keywords); i++) {
		if (!strcmp(keywords[i].keyword, keyword)) {
			return keywords[i].parser(&keywords[i], value);
		}
	}

	log_warn("Unknown keyword '%s'", keyword);
	return 1;
}

/** Parse configuration file.
 *  @param[in] path - configuration file path
 *  @return 0 on success. */
int parse_file(const char *path)
{
	char line[4096];
	int linenum;
	FILE *f;

	f = fopen(path, "r");
	if (!f) {
		log_crit("Can't open configuration file '%s': %m", path);
		goto err0;
	}

	for (linenum = 1; fgets(line, sizeof line, f); linenum++) {
		if (strlen(line) == sizeof line - 1) {
			log_crit("Line too long: %s...", line);
			goto err1;
		}
		
		if (parse_line(line)) {
			goto err1;
		}
	}

	fclose(f);
	return 0;

err1:	log_crit("Fatal error while parsing %s:%d", path, linenum);
	fclose(f);
err0:	return -1;
}

#ifndef NDEBUG
static void log_enum(const struct parse_keywords_s *keyword)
{
	const struct parse_enum_s *parse_enum;

	for (parse_enum = keyword->parser_arg; parse_enum->name; parse_enum++) {
		if (parse_enum->value == *(int*)keyword->storage) {
			log_dbg("%s = %s", keyword->keyword, parse_enum->name);
			return;
		}
	}

	log_dbg("%s = UNKNOWN_VALUE_%d", keyword->keyword, *(int*)keyword->storage);
}

static void log_string(const struct parse_keywords_s *keyword)
{
	const char *str = *(char**)keyword->storage;

	if (!str) str = "~";
	log_dbg("%s = %s", keyword->keyword, str);
}

static void log_string_multi(const struct parse_keywords_s *keyword)
{
	const struct conf_multi_str_s *str = *(struct conf_multi_str_s**)keyword->storage;

	if (!str) {
		log_dbg("%s = ~", keyword->keyword);	
	} else for (; str; str = str->next) {
		log_dbg("%s = %s", keyword->keyword, str->str);
	}
}

static void log_int(const struct parse_keywords_s *keyword)
{
	log_dbg("%s = %d", keyword->keyword, *(int*)keyword->storage);
}

static void log_mapping_multi(const struct parse_keywords_s *keyword)
{
	const struct conf_multi_mapping_s *map = *(struct conf_multi_mapping_s**)keyword->storage;

	if (!map) {
		log_dbg("%s = ~", keyword->keyword);	
	} else for (; map; map = map->next) {
		log_dbg("%s = %#" PRIx64 ":%s", keyword->keyword, map->addr, map->file);
	}
}

void log_conf(void)
{
	static const struct {
		int (*parser)(const struct parse_keywords_s *, char *);
		void (*dumper)(const struct parse_keywords_s *);
	} dumpers[] = {
		{ parse_enum, log_enum },
		{ parse_int, log_int },
		{ parse_string, log_string },
		{ parse_string_multi, log_string_multi },
		{ parse_mapping_multi, log_mapping_multi },
	};
	int i, j;

	for (i = 0; i < ARRAY_SIZE(keywords); i++) {
		for (j = 0; j < ARRAY_SIZE(dumpers); j++) {
			if (keywords[i].parser == dumpers[j].parser) {
				dumpers[j].dumper(&keywords[i]);
				break;
			}
		}
	}
}
#endif
