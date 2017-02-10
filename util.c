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

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#include "util.h"

/** Return length of the string without trailing white spaces.
 *  @param[in] value - The length of this string is returned.
 *  @return Length of the string not counting trailing white spaces. */
int strlen_chomp(const char *value) {
	int i;

	for (i = strlen(value); i > 0; i--) {
		if (!isspace(value[i - 1])) break;
	}

	return i;
}

/** Open /dev/null
 *  @return Opened file descriptor or -1 on error */
int open_devnull(void)
{
	static int fd = -1;

	if (fd < 0) {
		fd = open("/dev/null", O_RDWR);
	}

	return dup(fd);
}
