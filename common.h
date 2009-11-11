#ifndef COMMON_H
#define COMMON_H

// Show debug() output by default
#define DEBUG_OUTPUT
// Config file name
#define CFG_FILE "gsconf.cfg"
// mmap() support
#define HAVE_MMAP

// Needed to prevent struct stat problems
#define _FILE_OFFSET_BITS 64

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifndef NULL
#define NULL 0
#endif

#ifdef __GNUC__
#define PRINTF_LIKE(M,N) __attribute__((format (printf, M, N)))
#else
#define PRINTF_LIKE(M,N)
#endif

#if __GNUC__ >= 2
# define UNUSED_ARG(ARG) ARG __attribute__((unused))
# if __GNUC__ >= 4
#  define NULL_SENTINEL __attribute__((sentinel))
# endif
#else
# define UNUSED_ARG(ARG) ARG
#endif

#define ArraySize(ARRAY)		(sizeof((ARRAY)) / sizeof((ARRAY)[0]))
#define xfree(PTR)	do { if(PTR) free(PTR); } while(0)

#include "tools.h"
#include "dict.h"

#endif
