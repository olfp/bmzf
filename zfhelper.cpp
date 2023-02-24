#define __time_t_defined
#define _TIME_T_DECLARED

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <math.h>

#include <circle/startup.h>
#include <fatfs/ff.h>

#include "kernel.h"
#include "microrl.h"
#include "zforth.h"

/*
 * Evaluate buffer with code, check return value and report errors
 */

extern "C"
zf_result do_eval(const char *src, int line, const char *buf)
{
	const char *msg = NULL;

	zf_result rv = zf_eval(buf);

	switch(rv)
	{
		case ZF_OK: break;
		case ZF_ABORT_INTERNAL_ERROR: msg = "internal error"; break;
		case ZF_ABORT_OUTSIDE_MEM: msg = "outside memory"; break;
		case ZF_ABORT_DSTACK_OVERRUN: msg = "dstack overrun"; break;
		case ZF_ABORT_DSTACK_UNDERRUN: msg = "dstack underrun"; break;
		case ZF_ABORT_RSTACK_OVERRUN: msg = "rstack overrun"; break;
		case ZF_ABORT_RSTACK_UNDERRUN: msg = "rstack underrun"; break;
		case ZF_ABORT_NOT_A_WORD: msg = "not a word"; break;
		case ZF_ABORT_COMPILE_ONLY_WORD: msg = "compile-only word"; break;
		case ZF_ABORT_INVALID_SIZE: msg = "invalid size"; break;
		case ZF_ABORT_DIVISION_BY_ZERO: msg = "division by zero"; break;
		default: msg = "unknown error";
	}

	if(msg) {
		fprintf(stderr, "\033[31m");
		if(src) fprintf(stderr, "%s:%d: ", src, line);
		fprintf(stderr, "%s\033[0m\n", msg);
	}

	return rv;
}


/*
 * Load given forth file
 */

extern "C"
void include(const char *fname)
{
	char buf[256];

	FILE *f = fopen(fname, "rb");
	int line = 1;
	if(f) {
		while(fgets(buf, sizeof(buf), f)) {
			do_eval(fname, line++, buf);
		}
		fclose(f);
	} else {
		fprintf(stderr, "error opening file '%s': %s\n", fname, strerror(errno));
	}
}


/*
 * Save dictionary
 */

extern "C"
void save(const char *fname)
{
	size_t len;
	void *p = zf_dump(&len);
	FILE *f = fopen(fname, "wb");
	if(f) {
		fwrite(p, 1, len, f);
		fclose(f);
	}
}


/*
 * Load dictionary
 */

extern "C"
void load(const char *fname)
{
	size_t len;
	void *p = zf_dump(&len);
	FILE *f = fopen(fname, "rb");
	if(f) {
		fread(p, 1, len, f);
		fclose(f);
	} else {
		perror("read");
	}
}


/*
 * Sys callback function
 */

extern "C"
zf_input_state zf_host_sys(zf_syscall_id id, const char *input)
{
	int c;
	zf_addr a;
	char buf[256];
	int res;
	DIR *dp;

	static FILINFO info;

	switch((int)id) {


		/* The core system callbacks */

		case ZF_SYSCALL_EMIT:
			putchar((char)zf_pop());
			fflush(stdout);
			break;

		case ZF_SYSCALL_PRINT:
			printf(ZF_CELL_FMT " ", zf_pop());
			break;

		case ZF_SYSCALL_TELL: {
			zf_cell len = zf_pop();
			void *buf = (uint8_t *)zf_dump(NULL) + (int)zf_pop();
			(void)fwrite(buf, 1, len, stdout);
			fflush(stdout); }
			break;

		/* bmzF callbacks to circle */

                case ZF_SYSCALL_REBOOT:
			printf("Rebooting...");
                        reboot();
                        break;

                case ZF_SYSCALL_OPENDIR:
			c = (int)zf_pop(); // char cnt
			a = zf_pop(); // char ptr
			dict_get_bytes(a, buf, c);
			buf[c] = '\0';
			dp = (DIR *)malloc(sizeof(DIR));
			res = f_opendir(dp, buf);
  			if (res == 12 /*FR_NOT_ENABLED*/) {
    				// try to mount volume 
    				res = f_mount(&(CKernel::Get()->mFileSystem), buf, 1);
    				if (res == 0) {
      					// retry opendir
      					res = f_opendir(dp, buf);
    				}
  			}
			zf_push((zf_addr)dp);
			zf_push(res);
                        break;

                case ZF_SYSCALL_READDIR:
			dp = (DIR *)((int)zf_pop());
			res = f_readdir(dp, &info);
			zf_push(dict_add_str(info.fname));
			zf_push(strlen(info.fname));
			zf_push(res);
                        break;

                case ZF_SYSCALL_CLOSEDIR:
			dp = (DIR *)((int)zf_pop());
			res = f_closedir(dp);
			free(dp);
			zf_push(res);
                        break;


		/* Application specific callbacks */

		case ZF_SYSCALL_USER + 0:
			printf("\n");
			exit(0);
			break;

		case ZF_SYSCALL_USER + 1:
			zf_push(sin(zf_pop()));
			break;

		case ZF_SYSCALL_USER + 2:
			if(input == NULL) {
				return ZF_INPUT_PASS_WORD;
			}
			include(input);
			break;
		
		case ZF_SYSCALL_USER + 3:
			save("zforth.save");
			break;

		default:
			printf("unhandled syscall %d\n", id);
			break;
	}

	return ZF_INPUT_INTERPRET;
}


/*
 * Tracing output
 */

extern "C"
void zf_host_trace(const char *fmt, va_list va)
{
	fprintf(stderr, "\033[1;30m");
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "\033[0m");
}


/*
 * Parse number
 */

extern "C"
zf_cell zf_host_parse_num(const char *buf)
{
	zf_cell v;
	int n = 0;
	int r = sscanf(buf, "%f%n", &v, &n);
	if(r == 0 || buf[n] != '\0') {
		zf_abort(ZF_ABORT_NOT_A_WORD);
	}
	return v;
}


// print callback for microrl library
extern "C"
void print (const char * str)
{
	fprintf (stdout, "%s", str);
	fflush(stdout);
}

// execute callback for microrl library
extern "C"
int execute (const char * str)
{
	do_eval("console", 1, str);
	return 0;
}


/*
 * Main
 */

//int main(int argc, char **argv)
extern "C"
int zf_repl()
{
	int trace = 0;
	const char *fname_load = NULL;
	/* Initialize zforth */

	zf_init(trace);


	/* Load dict from disk if requested, otherwise bootstrap fort
	 * dictionary */

	if(fname_load) {
		load(fname_load);
	} else {
		zf_bootstrap();
	}

	include("core.zf");

	/* Interactive interpreter: read a line using readline library,
	 * and pass to zf_eval() for evaluation*/

	// create microrl object
	microrl_t rl;
	// call init with ptr to microrl instance and print callback
	microrl_init (&rl, print);
	// set callback for execute
	microrl_set_execute_callback (&rl, execute);

	static char buf[10];
        CKernel::Get()->mConsole.SetOptions(0);
	printf("\033[?25h"); // cursor on
	while (1) {
		  int n = CKernel::Get()->mConsole.Read(buf, 1);
		  buf[n] = '\0';

		// put received chars from stdin to microrl lib
		for(int i = 0; i < n; i++) {
			microrl_insert_char (&rl, buf[i]);
		}
	}

	return 0;
}
/*
 * End
 */

