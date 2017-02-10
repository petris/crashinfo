#include <google/coredumper.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

static void complete(long i)
{
	if (i == 0) {
		sleep(100);
		exit(1);
	} else {
		char buf[256];

		sleep(1);
		snprintf(buf, sizeof buf, "../dump_proc %d proc", getpid());
		system(buf);
		WriteCoreDump("core");
	}
}

static void recurse(long i)
{
	if (i > 0) recurse(i - 1);
	else complete(i);
}

static void *thread_main(void *arg)
{
	recurse((long)arg);
	return NULL;
}

int main(int argc, char *argv[])
{
	int args[] = {0, 3, 5, -1};
	pthread_t tid;
	int i;

	for (i = 0; i < sizeof args/sizeof args[0]; i++) {
		pthread_create(&tid, NULL, thread_main, (void*)(long)args[i]);
	}

	return pthread_join(tid, NULL);
}
