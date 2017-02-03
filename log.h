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

#ifndef LOG_H
#define LOG_H

#include <syslog.h>

void logmsg(int level, const char *format, ...);

#define log_notice(...) logmsg(LOG_NOTICE,  __VA_ARGS__)
#define log_info(...) logmsg(LOG_INFO,  __VA_ARGS__)
#define log_warn(...) logmsg(LOG_WARNING, __VA_ARGS__)
#define log_err(...)  logmsg(LOG_ERR, __VA_ARGS__)
#define log_crit(...) logmsg(LOG_CRIT, __VA_ARGS__)
#ifndef NDEBUG
#define log_dbg(...)  logmsg(LOG_DEBUG, __VA_ARGS__)
#else
#define log_dbg(...)  (void)0
#endif

#endif // LOG_H
