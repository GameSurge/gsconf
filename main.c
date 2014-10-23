#include "common.h"
#include "main.h"
#include "conf.h"
#include "database.h"
#include "cmd.h"
#include "tokenize.h"
#include "table.h"
#include "pgsql.h"
#include "stringlist.h"
#include "stringbuffer.h"
#include "serverinfo.h"
#include "configs.h"
#include "ssh.h"
#include "mtrand.h"
#include "input.h"
#include <getopt.h>
#include <setjmp.h>

static void sig_int(int n);
static void signal_init();
static void handle_line(const char *line);

int quit = 0;
static char *history_file = NULL;
sigjmp_buf sigint_jmp_buf;
volatile int sigint_jmp_on = 0;
volatile int sigint_received = 0;
int debug_output_enabled = 1;
int batch_mode = 0;
int no_colors = 0;

int main(int argc, char **argv)
{
	struct stringlist *batch_commands = NULL;
	const char *home;
	out("Welcome to the \033[38;5;34mGame\033[38;5;214mSurge\033[0m config management tool.");

#ifdef DEBUG_OUTPUT
	debug_output_enabled = 1;
#else
	debug_output_enabled = 0;
#endif

	home = getenv("HOME");
	if(home && *home)
	{
		char file[PATH_MAX];
		char buf[256];
		FILE *fp;
		snprintf(file, sizeof(file), "%s/.gsconf_dir", home);
		if((fp = fopen(file, "r")))
		{
			if(fgets(buf, sizeof(buf), fp))
			{
				trim(buf);
				if(*buf)
				{
					out("Working directory: %s", buf);
					chdir(buf);
				}
			}

			fclose(fp);
		}
	}

	int c;
	struct option options[] = {
		{ "workdir", 1, 0, 'w' },
		{ "ssh-passphrase", 2, 0, 's' },
		{ "debug", 0, 0, 'd' },
		{ "batch", 1, 0, 'b' },
		{ "no-colors", 1, 0, 'c' },
		{ NULL, 0, 0, 0 }
	};

	while((c = getopt_long(argc, argv, "s::db:c", options, NULL)) != -1)
	{
		switch(c)
		{
			case 'w':
				if(chdir(optarg) != 0)
				{
					error("Could not chdir() to `%s': %s", optarg, strerror(errno));
					return 1;
				}

				break;

			case 'd':
				debug_output_enabled = !debug_output_enabled;
				break;

			case 's':
				if(!optarg || !strcmp(optarg, "-"))
				{
					// Use readline if on TTY and fgets otherwise so the passphrase is never echoed
					if(isatty(fileno(stdin)))
					{
						char *pass = readline_noecho("SSH passphrase");
						if(!pass)
						{
							error("Could not read passphrase");
							return 1;
						}

						ssh_set_passphrase(pass);
					}
					else
					{
						char buf[128];
						printf("SSH passphrase: ");
						if(!fgets(buf, sizeof(buf), stdin))
						{
							error("Could not read passphrase");
							return 1;
						}

						out("***");
						ssh_set_passphrase(buf);
					}
				}
				else
				{
					ssh_set_passphrase(optarg);
				}

				break;

			case 'b':
				batch_mode = 1;
				if(!batch_commands)
					batch_commands = stringlist_create();
				stringlist_add(batch_commands, strdup(optarg));
				break;

			case 'c':
				no_colors = 1;
				break;
		}
	}

#ifdef DEBUG_OUTPUT
	if(debug_output_enabled)
		out("Debug output is enabled; use -d to disable");
#endif

	if(batch_mode)
		fclose(stdin);

	if(conf_init() != 0)
		return 1;

	if(pgsql_init() != 0)
	{
		conf_fini();
		return 1;
	}

	history_file = NULL;
	if(!batch_mode)
	{
		char *tmp = conf_get("history", DB_STRING);
		if(tmp && !strncmp(tmp, "~/", 2))
		{
			if(!home || !*home)
			{
				error("HOME is not set; disable history or use a path not starting with `~/'");
				error("History will not be saved!");
			}
			else
			{
				size_t len = strlen(tmp) - 1 + strlen(home) + 1;
				history_file = malloc(len);
				snprintf(history_file, len, "%s/%s", home, tmp + 2);
			}
		}
		else if(tmp)
			history_file = strdup(tmp);
	}

	init_genrand(time(NULL));
	database_init();
	signal_init();
	input_init("GSConf", history_file);
	ssh_init();
	cmd_init();

	if(!batch_mode)
	{
		char *line = NULL;
		const char *prompt = conf_get("prompt", DB_STRING);
		while(!quit)
		{
			sigsetjmp(sigint_jmp_buf, 1);
			sigint_received = 0;
			sigint_jmp_on = 1;
			line = readline(prompt);
			sigint_jmp_on = 0;

			if(!line)
				break;
			if(*line)
			{
				handle_line(line);
				add_history(line);
			}
			free(line);
			line = NULL;
		}

		if(line)
			free(line);

		putc('\n', stdout);
	}
	else
	{
		for(unsigned int i = 0; i < batch_commands->count; i++)
		{
			out("Executing: %s", batch_commands->data[i]);
			handle_line(batch_commands->data[i]);
		}
	}

	// Cleanup
	if(batch_commands)
		stringlist_free(batch_commands);
	cmd_fini();
	input_fini();
	ssh_fini();
	database_fini();
	pgsql_fini();
	conf_fini();
	xfree(history_file);

	return 0;
}

static void sig_int(int n)
{
	rl_free_line_state();
	rl_cleanup_after_signal();

	if(sigint_jmp_on)
	{
		putc('\n', stdout);
		sigint_jmp_on = 0;
		siglongjmp(sigint_jmp_buf, 1);
	}

	sigint_received = 1;
}

static void signal_init()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sig_int;
	sigaction(SIGINT, &sa, NULL);
}

static void handle_line(const char *line)
{
	char *dup;
	char *argv[32];
	int argc;
	struct command *cmd;

	dup = strdup(line);
	argc = tokenize_quoted(dup, argv, 32);
	if(argc <= 0)
	{
		free(dup);
		return;
	}

	cmd_handle(line, argc, argv, NULL);
	free(dup);
}

