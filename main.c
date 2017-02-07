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

#define _GNU_SOURCE
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#include "info.h"
#include "conf.h"
#include "proc.h"
#include "log.h"
#include "unw.h"

#define OUT_BUFSIZE (64*1024)
#define ESC '@'
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

/** Exit the program */
static void handler(int sig)
{
	_exit(2);
}

/** Set core limit to zero and catch signals, which default action is
 * to dump the core. This should prevent an infinite loop. */
static void disable_core_generation(void)
{
	static char stack[MINSIGSTKSZ];
	const stack_t ss = { .ss_size = MINSIGSTKSZ, .ss_sp = stack };
	struct sigaction act = { .sa_handler = handler, .sa_flags = SA_ONSTACK };
	int sig[] = {
			SIGQUIT, SIGILL, SIGABRT, SIGFPE, SIGSEGV,
			SIGBUS, SIGSYS, SIGTRAP, SIGXCPU, SIGXFSZ,
		};
	int i;

	// Different kernel versions interprets this a different way,
	// but in all cases it's inherited by our children.
	setrlimit(RLIMIT_CORE, 0);

	// In a case kernel ignores RLIMIT_CORE, we must catch core generating
	// signals. This doesn't protect programs we exec().
	if (sigaltstack(&ss, NULL)) {
		log_warn("Can't configure alternative stack: %s", strerror(errno));
		act.sa_flags = 0;
	}

	for (i = 0; i < ARRAY_SIZE(sig); i++) {
		if (0 != sigaction(sig[i], &act, NULL)) {
			log_warn("Can't change action of the signal %d: %s",
					sig[i], strerror(errno));
		}
	}
}

/** Create all directories from the path */
static int make_path(char *path)
{
	char *p;

	for (p = path; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (0 != mkdir(path, 0700) && errno != EEXIST) {
				log_crit("Can't create directory '%s': %s",
						path, strerror(errno));
				*p = '/';
				return -1;
			}
			*p = '/';
		}
	}

	return 0;
}

/** Returns true if the current character is the escape character. Helper for
 *  open_output(). */
static int is_escape(char *str, int idx)
{
	int i;
	for (i = idx; i >= 0 && str[i] == ESC; i--);
	return (idx - i) & 1;
}

/** Return number of digits in the integer. Helper for open_output(). */
static int intlen(unsigned i)
{
	return i < 10 ? 1 : 1 + intlen(i / 10);
}

static int spawn_proc(const char *cmd, int infd, int outfd,
		const char *arg1, const char *arg2)
{
	int pid = fork();

	if (pid < 0) {
		log_crit("Forking process '%s' failed: %s",
				cmd, strerror(errno));
		return -1;
	} else if (pid == 0) {
		char *exe = strdup(cmd);
		char *argv[32];
		int i;

		if (!exe) exe = (char*)cmd;

		log_dbg("Starting program '%s'", cmd);

		dup2(infd, 0);
		dup2(outfd, 1);

		argv[0] = strtok(exe, delim);
		for (i = 1; i < ARRAY_SIZE(argv) - 1; i++) {
			argv[i] = strtok(NULL, delim);
			// execvp must behave as if the const char *const[]
			// argument is used, but the prototype differs due to
			// historical reasons
			if (arg1 && strcmp(argv[i], "@1")) {
				argv[i] = (char*)arg1;
			} else if (arg2 && strcmp(argv[i], "@2")) {
				argv[i] = (char*)arg2;
			}
		}
		argv[i] = NULL;

		execvp(argv[0], argv);
		log_crit("Starting executable '%s' failed: %s", exe, strerror(errno));
		exit(1);
	}

	return pid;
}

/** Close output */
static void close_output(const struct conf_output_s *c, struct run_output_s *r)
{
	struct run_multi_filter_s *iter, *tmp;
	struct conf_multi_str_s *str;

	if (r->output_fd < 0) {
		return;
	}

	if (r->output) {
		fflush(r->output);
		fsync(r->output_fd);
		fclose(r->output);
		r->output = NULL;
	} else {
		fsync(r->output_fd);
		close(r->output_fd);
	}
	r->output_fd = -1;

	foreach_safe (r->filter, iter, tmp) {
		int status, pid;

		pid = waitpid(iter->pid, &status, 0);
		if (pid != iter->pid) {
			log_crit("Waiting for filter '%s' failed: %s",
					iter->filter, strerror(errno));
		} else if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) == 0) {
				log_dbg("Filter '%s' ended successfully", iter->filter);
			} else {
				log_err("Filter '%s' failed with return code %d",
						iter->filter, WEXITSTATUS(status));
			}
		} else if (WIFSIGNALED(status)) {
				log_err("Filter '%s' was terminated by signal %d",
						iter->filter, WTERMSIG(status));
		} else {
			assert(0);
		}

		tmp = iter->next;
		free(iter);
	}

	if (r->output_filename) for (str = c->notify; str; str = str->next) {
		int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
		spawn_proc(str->str, nullfd, nullfd, r->output_filename, NULL);
	}
}

/** Prevent dumping until the output is opened */
static pthread_mutex_t dump_lock = PTHREAD_MUTEX_INITIALIZER;

/** Info dumper thread */
static void *info_dump_thread(void *arg)
{
	int inputfd = *(int *)arg;
	int pid;

	pid = unw_prepare(inputfd);
	ACCESS_ONCE(run.pid) = pid < 0 ? -2 : pid;

	pthread_mutex_lock(&dump_lock);
	pthread_mutex_unlock(&dump_lock);

	return (void*)(long)info_dump();
}

/** Open a file interpreting all wildcard characters and create filter chain */
static int open_output(const struct conf_output_s *c, struct run_output_s *r)
{
	const struct conf_multi_str_s *filter;
	struct run_multi_filter_s **prev_f;
	char path[PATH_MAX], buf[32];
	int i, j, seq_len, flags, fd;
	int mkdir = c->mkdir;
	int counter = 0;
	int pipefd[2], next_infd, next_outfd;

	if (c->output[0] != '/') {
		log_crit("Output filename '%s' is not a full path", c->output);
		goto err0;
	}

restart:
	strftime(path, sizeof path, c->output, &run.start_tm);
	seq_len = c->exists_seq > 0 ? intlen(c->exists_seq - 1) : intlen(counter);

	for (i = strlen(path), j = sizeof path - 1; i >= 1; i--, j--) {
		const char *p;

		if (!is_escape(path, i - 1)) {
			path[j] = path[i];
		} else switch (path[i]) {
			case ESC:
				path[j] = path[i--];
				break;
			case 'Q': // Counter mark
				j -= seq_len;
				if (j < --i) {
					goto too_long;
				}
				snprintf(buf, sizeof buf, "%0*d", seq_len, counter);
				memcpy(&path[j], buf, seq_len);
				if (c->exists != CONF_EXISTS_SEQUENCE) {
					log_info("Sequence wild card in '%s', but sequence mode is not used", c->output);
				}
				break;
			case 'e': // executable filename
			case 'E': // pathname of executable, / replaced by !
				for (p = conf.proc.exe + strlen(conf.proc.exe) - 1;
				     p >= conf.proc.exe; p--, j--) {
					if (j <= i) {
						goto too_long;
					}

					if (*p == '/') {
						if (path[i] == 'e') {
							break;
						} else {
							path[j] = '!';
						}
					} else {
						path[j] = *p;
					}
				}
				i--; j++;
				break;
			default:
				log_info("Unknown wild card '@%c' in '%s'", path[i], c->output);
				path[j--] = path[i--];
				path[j] = path[i];
				break;
		}
	}
	path[j] = '/';
	
	log_dbg("Expanded output: %s", &path[j]);

	switch (c->exists) {
		case CONF_EXISTS_APPEND:
			flags = O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC;
			break;
		case CONF_EXISTS_OVERWRITE:
			flags = O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC;
			break;
		case CONF_EXISTS_KEEP:
		case CONF_EXISTS_SEQUENCE:
			flags = O_WRONLY | O_EXCL | O_CREAT | O_CLOEXEC;
			break;
		default:
			assert(0);
			return -1; // Make the compiler happy, never reached
	}

reopen: fd = open(&path[j], flags, 0600);
	if (fd < 0) {
		if (errno == EEXIST && c->exists == CONF_EXISTS_KEEP) {
			log_notice("File '%s' already exists, ignoring the output");
			fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
			if (fd < 0) {
				log_crit("Can't open /dev/null: %s", strerror(errno));
				goto err0;
			}
		} else if (errno == EEXIST && c->exists == CONF_EXISTS_SEQUENCE) {
			if (++counter < c->exists_seq || c->exists_seq <= 0) goto restart;
			log_crit("Filename sequence limit reached");
			goto err0;
		} else if (errno == ENOENT && mkdir) {
			if (!make_path(&path[j])) {
				mkdir = 0;
				goto reopen;
			}
			goto err0;
		} else {
			log_crit("Can't open '%s': %s", path, strerror(errno));
			goto err0;
		}
	}

	r->output_filename = strdup(path + j);

	r->filter = NULL;
	r->output_fd = fd;
	r->output = NULL;

	if (!c->filter) {
		return 0;
	}

	// Setup filters
	if (pipe2(pipefd, O_CLOEXEC)) {
		log_err("Can't create filter pipe: %s", strerror(errno));
		close(fd);
		return -1;
	}

	r->output_fd = pipefd[1];
	next_infd = pipefd[0];
	next_outfd = fd;
	for (filter = c->filter, prev_f = &r->filter;
	     filter;
	     filter = filter->next, prev_f = &(*prev_f)->next) {
		int infd, outfd, pid;

		if (filter->next) {
			if (pipe2(pipefd, O_CLOEXEC)) {
				log_err("Can't create filter pipe: %s", strerror(errno));
				goto err1;
			}
			outfd = pipefd[1];
			infd = next_infd;
			next_infd = pipefd[0];
		} else {
			infd = next_infd;
			outfd = next_outfd;
		}

		*prev_f = malloc(sizeof **prev_f);
		if (!*prev_f) {
			log_err("Can't allocate memory for the filter: %s", strerror(errno));
			goto err1;
		}
		(*prev_f)->next = NULL;

		pid = spawn_proc(filter->str, infd, outfd, NULL, NULL);
		if (pid < 0) {
			free(*prev_f);
			*prev_f = NULL;
			goto err1;
		} else {
			close(infd);
			close(outfd);
			(*prev_f)->filter = filter->str;
			(*prev_f)->pid = pid;
		}
	}

	return 0;

too_long:
	log_crit("Expanded output filename '%s' is too long", c->output);
	goto err0;

err1:	while (r->filter) {
		struct run_multi_filter_s *tmp = r->filter;
		r->filter = r->filter->next;

		log_dbg("Killing %d: '%s'", tmp->pid, tmp->filter);
		kill(tmp->pid, SIGKILL);
		waitpid(tmp->pid, NULL, 0);
		free(tmp);
	}
	close(r->output_fd);
	close(next_infd);
	close(next_outfd);
	
err0:	r->output = NULL;
	r->output_fd = -1;

	return -1;
}


static inline int unblockfd(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1 ? -1 : 0;
}

static inline int blockfd(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1 ? -1 : 0;
}

static inline ssize_t safe_read(int fd, void *buf, size_t count)
{
	ssize_t size = 0, rtn;

repeat: rtn = read(fd, (char*)buf + size, count - size);
	if (rtn > 0) {
		size += rtn;
		if (size < count) {
			goto repeat;
		}
	} else if (rtn < 0) {
		if (errno == EINTR) {
			goto repeat;
		} else if (size == 0) {
			return rtn;
		}
	}
	return size;
}

static inline ssize_t safe_write(int fd, const void *buf, size_t count)
{
	ssize_t size = 0, rtn;

repeat: rtn = write(fd, (const char*)buf + size, count - size);
	if (rtn > 0) {
		size += rtn;
		if (size < count) {
			goto repeat;
		}
	} else if (rtn < 0) {
		if (errno == EINTR) {
			goto repeat;
		} else if (size == 0) {
			return rtn;
		}
	}
	return size;
}

int main(int argc, char *argv[])
{
	static char buf[32*1024];
	struct conf_multi_str_s *str;
	int buf_read = 0, buf_write = 0;
	int info_pipe[2] = { -1, -1 };
	pthread_t tid;
	int c, rtn;

	disable_core_generation();

	openlog("crash-info", LOG_PID | LOG_NDELAY, LOG_DAEMON);

	clock_gettime(CLOCK_REALTIME, &run.start_tp);
	gmtime_r(&run.start_tp.tv_sec, &run.start_tm);

	while (-1 != (c = getopt(argc, argv, "c:o:h"))) {
		switch (c) {
			case 'c':
				if (parse_file(optarg)) {
					return 1;
				}
				break;
			case 'o':
				if (parse_line(optarg)) {
					return 1;
				}
				break;
			case 'h':
				printf("Usage: %s [-h] [-c config_file] [-o option=value]\n", argv[0]);
				return 0;
			case '?':
				fprintf(stderr, "Unknown option, use %s -h for help\n", argv[0]);
				return 1;
		}
	}

	log_dbg("Configuration before reading /proc/<PID>:");
	log_conf();

	// Create info dump thread (may be needed to obtain PID)
	run.pid = -1;
	pthread_mutex_lock(&dump_lock);
	if (pipe2(info_pipe, O_CLOEXEC) || unblockfd(info_pipe[1])) {
		log_crit("Can't create info pipe: %s", strerror(errno));
	} else {
		int err = pthread_create(&tid, NULL, info_dump_thread, &info_pipe[0]);
		if (err) {
			log_crit("Failed to create dumping thread: %s", strerror(err));
			tid = -1;
		} else {
			int tries;
			buf_read = safe_read(0, buf, sizeof buf);
			buf_write = 0;
			if (buf_read <= 0) {
				log_crit("Can't read the core: %s", strerror(err));
			} else for (tries = buf_write = 0; buf_write < buf_read && tries < 5;) {
				// We do not know how much data we must feed the unwinder to get
				// the PID. We will feed it until one of the following occurs:
				//  - We get the PID (victory)
				//  - We run out of the buffer
				//  - Feeding fails with an error other than EAGAIN
				//  - We reach maximum number of attempts
				rtn = safe_write(info_pipe[1], buf + buf_write, buf_read - buf_write);
				if (rtn > 0) {
					buf_write += rtn;
					tries = 0;
				}

				if (ACCESS_ONCE(run.pid) != -1) {
					break;
				}
				
				if (rtn < 0) {
					if (errno != EAGAIN) break;

					// We are not able to distinguish the case when the unwinder
					// is just too busy from the case where it wants to read more
					// data than we have, so we will just give it some time.
					// We could address this by tuning RT priorities, but we prefer
					// not to disturb RT tasks for now.
					// Maybe, this could be made configurable.
					usleep(10000);
					tries++;
				}
			}
		}
	}

	// Read information from proc if not disabled
	if (!conf.proc.ignore) {
		if (!conf.proc.path && run.pid > 0) {
			static char proc_path[16];
			snprintf(proc_path, sizeof proc_path, "/proc/%d/", run.pid);
			conf.proc.path = proc_path;
		}

		if (conf.proc.path) {
			run.proc_fd = open(conf.proc.path, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
			if (run.proc_fd < 0) {
				log_err("Can't open proc directory '%s': %s",
						conf.proc.path, strerror(errno));
			} else if (read_proc_info()) {
				log_err("Failed to read /proc info");
			} else {	
				log_dbg("Configuration after reading %s:", conf.proc.path);
				log_conf();
			}
		} else {
			log_err("Can't determine /proc path");
		}
	}

	// Open outputs
	if (conf.info.output) {
		if (!open_output(&conf.info, &run.info)) {
			run.info.output = fdopen(run.info.output_fd, "w");
			if (run.info.output) {
				setvbuf(run.info.output, NULL, _IOFBF, OUT_BUFSIZE);
			} else {
				log_crit("Failed to open output: %s", strerror(errno));
				close_output(&conf.info, &run.info);
			}
		} else {
			log_crit("Failed to open info output, ignoring");
		}
	}

	if (conf.core.output) {
		if (open_output(&conf.core, &run.core)) {
			log_crit("Failed to open core output, ignoring");
		}
	}
	
	pthread_mutex_unlock(&dump_lock);

	if (info_pipe[1] >= 0) {
#ifdef CRASHINFO_WITH_LIBUNWIND
		blockfd(info_pipe[1]);
		safe_write(run.core.output_fd, buf, buf_read);
		if (buf_read > buf_write) {
			safe_write(info_pipe[1], buf + buf_write, buf_read - buf_write);
		}
#else
		close(info_pipe[1]);
#endif // CRASHINFO_WITH_LIBUNWIND
	}

	do {
		rtn = safe_read(0, buf, sizeof buf);
		if (rtn > 0) {
			safe_write(run.core.output_fd, buf, rtn);
			if (info_pipe[1] >= 0) {
				safe_write(info_pipe[1], buf, rtn);
			}
		}
	} while (rtn > 0);

	close(info_pipe[1]);

	rtn = pthread_join(tid, NULL);
	if (rtn) {
		log_err("Failed to join dumping thread: %s", strerror(rtn));
	}

	close_output(&conf.core, &run.core);
	close_output(&conf.info, &run.info);

	if (run.info.output_filename && run.core.output_filename) {
		for (str = conf.info_core_notify; str; str = str->next) {
			int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
			spawn_proc(str->str, nullfd, nullfd,
					run.info.output_filename,
					run.core.output_filename);
		}
	}

	return 0;
}
