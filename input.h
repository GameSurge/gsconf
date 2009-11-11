#ifndef INPUT_H
#define INPUT_H

#include <readline/readline.h>

void input_init(const char *readline_name, const char *history_file);
void input_fini();

char *readline_custom(const char *prompt, const char *default_line, rl_compentry_func_t *autocomplete_func);
int readline_yesno(const char *prompt, const char *default_line);
char *readline_noecho(const char *prompt);
#define readline_noac(PROMPT, DEFAULT)		readline_custom(PROMPT, DEFAULT, NULL)
#define readline_server(PROMPT, DEFAULT)	readline_custom(PROMPT, DEFAULT, server_generator)
#define readline_hub(PROMPT, DEFAULT)		readline_custom(PROMPT, DEFAULT, hub_generator)
#define readline_connclass(PROMPT, DEFAULT)	readline_custom(PROMPT, DEFAULT, connclass_generator)

char *server_generator(const char *text, int state);
char *hub_generator(const char *text, int state);
char *server_nohub_generator(const char *text, int state);
char *connclass_generator(const char *text, int state);
char *onoff_generator(const char *text, int state);

int char_is_quoted(char *string, int end);
char *bash_dequote_filename(const char *text, int quote_char);

extern const char *readline_default_text;

#endif
