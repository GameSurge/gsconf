#include "common.h"
#include "cmd.h"
#include "main.h"
#include "input.h"
#include "tokenize.h"

static void cmd_free_subcmds(struct command *cmd);
static char *cmd_generator(const char *text, int state);
static struct command *cmd_find(const char *name, struct dict *list);

CMD_FUNC(help);
CMD_TAB_FUNC(help);
CMD_FUNC(quit);


static struct dict *command_list;
static struct dict *cmd_generator_list = NULL;
// Yay, global variables needed because we can't pass custom args to out rl_compentry_func
char *tc_argv_base[32] = { 0 };
char **tc_argv = NULL;
int tc_argc = 0;


static struct command commands[] = {
	CMD_TC("help", help, "Display help"),
	CMD("quit", quit, "Exit the program"),
	CMD_LIST_END
};


void cmd_init()
{
	command_list = dict_create();
	dict_set_free_funcs(command_list, NULL, (dict_free_f *)cmd_free_subcmds);

	// Initialize our own commands
	cmd_register_list(commands, NULL);
	cmd_alias("exit", "quit", NULL);
	// Initialize external commands
	cmd_server_init();
	cmd_link_init();
	cmd_conf_init();
	cmd_oper_init();
	cmd_service_init();
	cmd_forward_init();
	cmd_feature_init();
	cmd_jupe_init();
	cmd_class_init();
	cmd_pseudo_init();
	cmd_client_init();
	cmd_webirc_init();
}

void cmd_fini()
{
	dict_free(command_list);
}

static void cmd_free_subcmds(struct command *cmd)
{
	if(cmd->subcommands)
		dict_free(cmd->subcommands);
	if(cmd->alias)
		free(cmd);
}

void cmd_register(struct command *cmd, const char *parent_name)
{
	struct command *parent = parent_name ? cmd_find(parent_name, command_list) : NULL;
	assert(!parent_name || parent);
	if(parent && !parent->subcommands)
		parent->subcommands = dict_create();
	dict_insert(parent ? parent->subcommands : command_list, (char *)cmd->name, cmd);
}

void cmd_register_list(struct command *cmd, const char *parent_name)
{
	while(cmd->name)
	{
		cmd_register(cmd, parent_name);
		cmd++;
	}
}

void cmd_alias(const char *name, const char *cmd_name, const char *subcmd_name)
{
	struct command *cmd, *alias;
	assert(strchr(name, ' ') == NULL); // not supported
	assert(cmd = cmd_find(cmd_name, command_list));
	assert(!subcmd_name || (cmd->subcommands && (cmd = cmd_find(subcmd_name, cmd->subcommands))));

	alias = malloc(sizeof(struct command));
	memcpy(alias, cmd, sizeof(struct command));
	alias->name = name;
	alias->alias = 1;
	dict_insert(command_list, (char *)alias->name, alias);
}

void cmd_handle(const char *line, int argc, char **argv, struct command *parent)
{
	struct command *cmd;
	static char cmdbuf[32];

	if(argc < 1)
	{
		error("No %scommand given", parent ? "sub" : "");
		return;
	}

	cmd = cmd_find(argv[0], parent ? parent->subcommands : command_list);

	if(!cmd)
	{
		if(parent)
			error("%s %s: command not found", parent->name, argv[0]);
		else
			error("%s: command not found", argv[0]);
		return;
	}

	if(cmd->subcommands)
		cmd_handle(line, argc - 1, argv + 1, cmd);
	else if(!cmd->func)
	{
		if(parent)
		{
			snprintf(cmdbuf, sizeof(cmdbuf), "%s %s", parent->name, cmd->name);
			argv[0] = cmdbuf;
		}

		error("%s: command not implemented", argv[0]);
	}
	else
	{
		if(parent)
		{
			snprintf(cmdbuf, sizeof(cmdbuf), "%s %s", parent->name, cmd->name);
			argv[0] = cmdbuf;
		}

		cmd->func(line, argc, argv);
	}
}

char **cmd_tabcomp(const char *text, int start, int end)
{
	char **list;
	char quote;
	char *dequoted;
	char *tc_line_dup;
	static char cmdbuf[32];
	struct command *cmd = NULL, *subcmd = NULL;
	cmd_tab_func *func = NULL;

	tc_line_dup = strdup(rl_line_buffer);
	tc_argc = tokenize_quoted(tc_line_dup, tc_argv_base, 32);
	tc_argv = tc_argv_base;

	if(CAN_COMPLETE_ARG(0))
	{
		cmd_generator_list = command_list;
		func = cmd_generator;
	}
	else if(CAN_COMPLETE_ARG(1) && (cmd = cmd_find(tc_argv[0], command_list)))
	{
		if(cmd->subcommands)
		{
			cmd_generator_list = cmd->subcommands;
			func = cmd_generator;
		}
		else if(cmd->tabfunc)
			func = cmd->tabfunc;
	}
	else if(tc_argc > 0 && (cmd = cmd_find(tc_argv[0], command_list)))
	{
		if(cmd->tabfunc) // single command
			func = cmd->tabfunc;
		else if(cmd->subcommands && tc_argc > 1 && (subcmd = cmd_find(tc_argv[1], cmd->subcommands)))
		{
			snprintf(cmdbuf, sizeof(cmdbuf), "%s %s", cmd->name, subcmd->name);
			tc_argv[1] = cmdbuf;

			tc_argv++;
			tc_argc--;
			func = subcmd->tabfunc;
		}
	}

	if(!func)
	{
		free(tc_line_dup);
		return NULL;
	}

	rl_filename_completion_desired = 1;
	quote = ((char_is_quoted(rl_line_buffer, start) &&
		strchr(rl_completer_quote_characters, rl_line_buffer[start - 1]))
		? rl_line_buffer[start - 1] : 0);

	dequoted = bash_dequote_filename(text, quote);

	list = rl_completion_matches(dequoted, func);
	func(NULL, -1);

	free(tc_line_dup);
	free(dequoted);
	return list;
}

static char *cmd_generator(const char *text, int state)
{
	static int idx;
	static size_t len;
	int skipped = 0;

	if(!state) // New word
	{
		idx = 0;
		len = strlen(text);
	}
	else if(state == -1) // Cleanup
		return NULL;

	// Return the next name which partially matches from the command list.
	dict_iter(node, cmd_generator_list)
	{
		if(skipped++ < idx)
			continue;
		idx++;
		if(!strncmp(node->key, text, len))
			return strdup(node->key);
	}

  	return NULL;
}

static struct command *cmd_find(const char *name, struct dict *list)
{
	dict_iter(node, list)
	{
		if(!strcmp(name, node->key))
			return node->data;
	}

	return NULL;
}

CMD_FUNC(help)
{
	struct command *cmd, *subcmd;

	if(argc < 2)
	{
		out("Usage: help <command> [subcommand]");
		return;
	}

	if(!(cmd = cmd_find(argv[1], command_list)))
	{
		out("No commands match `%s'", argv[1]);
		return;
	}

	if(cmd->subcommands && argc > 2 && (subcmd = cmd_find(argv[2], cmd->subcommands)))
		out("%s %s: %s", cmd->name, subcmd->name, subcmd->doc);
	else
		out("%s: %s", cmd->name, cmd->doc);
}

CMD_TAB_FUNC(help)
{
	struct command *cmd, *subcmd;

	if(CAN_COMPLETE_ARG(1))
	{
		cmd_generator_list = command_list;
		return cmd_generator(text, state);
	}
	else if(CAN_COMPLETE_ARG(2) && (cmd = cmd_find(tc_argv[1], command_list)) && cmd->subcommands)
	{
		cmd_generator_list = cmd->subcommands;
		return cmd_generator(text, state);
	}

	return NULL;
}

CMD_FUNC(quit)
{
	quit = 1;
}
