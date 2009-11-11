#ifndef CMD_H
#define CMD_H

typedef void (cmd_func)(const char *cmd_line, int argc, char **argv);
typedef char* (cmd_tab_func)(const char *text, int state);

struct command
{
	const char *name;
	cmd_func *func;
	cmd_tab_func *tabfunc;
	const char *doc;
	struct dict *subcommands;
	int alias;
};

void cmd_init();
void cmd_fini();

void cmd_register(struct command *cmd, const char *parent_name);
void cmd_register_list(struct command *cmd, const char *parent_name);
void cmd_alias(const char *name, const char *cmd_name, const char *subcmd_name);
void cmd_handle(const char *line, int argc, char **argv, struct command *parent);
char **cmd_tabcomp(const char *text, int start, int end);

// cmd_*.c
void cmd_server_init();
void cmd_link_init();
void cmd_conf_init();
void cmd_oper_init();
void cmd_service_init();
void cmd_forward_init();
void cmd_feature_init();
void cmd_jupe_init();
void cmd_class_init();
void cmd_pseudo_init();
void cmd_client_init();
void cmd_webirc_init();

// Global vars, macros, etc.
extern char **tc_argv;
extern int tc_argc;

#define CMD_FUNC(FUNC)		static void cmd_func_ ## FUNC(const char *cmd_line, int argc, char **argv)
#define CMD_TAB_FUNC(FUNC)	static char* cmd_tabfunc_ ## FUNC(const char *text, int state)

#define CMD(NAME, FUNC, DOC)	{ NAME, cmd_func_ ## FUNC, NULL, DOC, NULL, 0 }
#define CMD_TC(NAME, FUNC, DOC)	{ NAME, cmd_func_ ## FUNC, cmd_tabfunc_ ## FUNC, DOC, NULL, 0 }
#define CMD_STUB(NAME, DOC)	{ NAME, NULL, NULL, DOC, NULL, 0 }
#define CMD_LIST_END		{ NULL, NULL, NULL, NULL, NULL, 0 }

// An argument can be completed if:
// a) it's completely empty and there's a space after the previous argument
// b) it's either a (quoted) string or right after a backslash/escaped space
#define CAN_COMPLETE_ARG(IDX)	((tc_argc == (IDX) && tc_argc == 0 && !*tc_argv[tc_argc]) || \
				(tc_argc == (IDX) && *tc_argv[tc_argc] == ' ') || \
				(tc_argc == ((IDX) + 1) && (*tc_argv[tc_argc] == '"' || !*tc_argv[tc_argc] || *tc_argv[tc_argc] == '*')))

#endif
