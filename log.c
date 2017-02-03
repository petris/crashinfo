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

#include <stdarg.h>
#include <stdio.h>

#include "log.h"
#include "conf.h"

void logmsg(int priority, const char *format, ...)
{
	const char *prefix = "";
	va_list ap;

	switch (priority) {
		case LOG_EMERG: prefix = "EMRG: ";
			break;
		case LOG_ALERT: prefix = "ALRT: ";
			break;
		case LOG_CRIT: prefix = "CRIT: ";
			break;
		case LOG_ERR: prefix = "ERR:  ";
			break;
		case LOG_WARNING: prefix = "WARN: ";
			break;
		case LOG_NOTICE: prefix = "NOTI: ";
			break;
		case LOG_INFO: prefix = "INFO: ";
			break;
		case LOG_DEBUG: prefix = "DBG:  ";
			break;
	}

	if (conf.log.stderr >= priority) {
		flockfile(stderr);
		fputs(prefix, stderr);
	        va_start(ap, format);
	        vfprintf(stderr, format, ap);
	        va_end(ap);
	        fputs("\n", stderr);
		funlockfile(stderr);
	}
	if (conf.log.syslog >= priority) {
	        va_start(ap, format);
	        vsyslog(priority, format, ap);
	        va_end(ap);
	}
	if (conf.log.info >= priority && run.info.output) {
		if (!run.info.output) {
			return;
		}

		flockfile(run.info.output);
		fputs("# ", run.info.output);
		fputs(prefix, run.info.output);
	        va_start(ap, format);
	        vfprintf(run.info.output, format, ap);
	        va_end(ap);
	        fputs("\n", run.info.output);
		funlockfile(run.info.output);
	}
}
