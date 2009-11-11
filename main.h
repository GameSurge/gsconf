#ifndef MAIN_H
#define MAIN_H

#include <setjmp.h>

extern sigjmp_buf sigint_jmp_buf;
extern volatile int sigint_jmp_on;
extern volatile int sigint_received;
extern int quit;
extern int debug_output_enabled;
extern int batch_mode;
extern int no_colors;

#endif
