#include "common.h"
#include "input.h"
#include "main.h"
#include "pgsql.h"
#include "stringlist.h"
#include "cmd.h"
#include <setjmp.h>
#include <termios.h>

static int readline_set_default_text();
static char **input_tabcomp(const char *text, int start, int end);
static int skip_quoted(const char *str, int i, char quote);
static char *bash_quote_filename(char *s, int rtype, char *qcp);

enum { COMPLETE_DQUOTE, COMPLETE_SQUOTE, COMPLETE_BSQUOTE };

static const char *history_file = NULL;
static int readline_custom_autocomplete = 0;
static rl_compentry_func_t *readline_custom_autocomplete_func = NULL;
const char *readline_default_text = NULL;

void input_init(const char *readline_name, const char *history)
{
	history_file = history;
	rl_readline_name = readline_name;
	rl_attempted_completion_function = input_tabcomp;
	rl_char_is_quoted_p = char_is_quoted;
	rl_completer_word_break_characters = " \"'";
	rl_completer_quote_characters = "'\"";
	rl_filename_quote_characters = " \"'\\";
	rl_filename_quoting_function = bash_quote_filename;
	rl_filename_dequoting_function = (rl_dequote_func_t*)bash_dequote_filename;
	rl_startup_hook = readline_set_default_text;
	rl_catch_signals = 1;
	rl_set_signals();
	if(history_file)
		read_history(history_file);
}

void input_fini()
{
	if(history_file)
		write_history(history_file);
}

static int readline_set_default_text()
{
	if(readline_default_text)
	{
		rl_insert_text(readline_default_text);
		readline_default_text = NULL;
	}

	return 0;
}


char *readline_custom(const char *prompt, const char *default_line, rl_compentry_func_t *autocomplete_func)
{
	static char buf[1024];
	char *line;

	if(default_line)
		snprintf(buf, sizeof(buf), "%s [%s]: ", prompt, default_line);
	else
		snprintf(buf, sizeof(buf), "%s: ", prompt);

	readline_custom_autocomplete = 1;
	readline_custom_autocomplete_func = autocomplete_func;
	sigsetjmp(sigint_jmp_buf, 1);
	sigint_jmp_on = 1;
	line = readline(buf);
	sigint_jmp_on = 0;
	readline_custom_autocomplete_func = NULL;
	readline_custom_autocomplete = 0;

	if(!line) // EOF / ctrl+l -> null
	{
		putc('\n', stdout);
		return NULL;
	}
	else if(!*line && default_line) // empty -> use default
		strlcpy(buf, default_line, sizeof(buf));
	else if(!strcmp(line, ".")) // force empty
		buf[0] = '\0';
	else
		strlcpy(buf, line, sizeof(buf));

	trim(buf);

	if(line)
		free(line);
	return buf;
}

int readline_yesno(const char *prompt, const char *default_line)
{
	while(1)
	{
		char *line = readline_noac(prompt, default_line);
		if(true_string(line))
			return 1;
		else if(false_string(line))
			return 0;
	}
}

char *readline_noecho(const char *prompt)
{
	char *line;
	struct termios old, new;

	tcgetattr(fileno(stdin), &old);
	new = old;
	new.c_lflag &= ~ECHO;
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);

	line = readline_noac(prompt, NULL);

	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
	if(line)
		putc('\n', stdout);

	return line;
}

static char **input_tabcomp(const char *text, int start, int end)
{
	char **list = NULL;

	rl_attempted_completion_over = 1;

	if(readline_custom_autocomplete)
	{
		if(!readline_custom_autocomplete_func)
			return NULL;
		list = rl_completion_matches(text, readline_custom_autocomplete_func);
		readline_custom_autocomplete_func(NULL, -1);
		return list;
	}

	return cmd_tabcomp(text, start, end);
}

// Some common generator functions
char *server_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT name FROM servers WHERE name ILIKE $1||'%'", 1, stringlist_build(text, NULL));
		rows = pgsql_num_rows(res);
	}
	else if(state == -1) // Cleanup
	{
		pgsql_free(res);
		return NULL;
	}

	while(row < rows)
	{
		name = pgsql_value(res, row, 0);
		row++;
		if(!strncasecmp(name, text, len))
			return strdup(name);
	}

  	return NULL;
}

char *hub_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT name FROM servers WHERE name ILIKE $1||'%' AND type = 'HUB'", 1, stringlist_build(text, NULL));
		rows = pgsql_num_rows(res);
	}
	else if(state == -1) // Cleanup
	{
		pgsql_free(res);
		return NULL;
	}

	while(row < rows)
	{
		name = pgsql_value(res, row, 0);
		row++;
		if(!strncasecmp(name, text, len))
			return strdup(name);
	}

  	return NULL;
}

char *server_nohub_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT name FROM servers WHERE name ILIKE $1||'%' AND type != 'HUB'", 1, stringlist_build(text, NULL));
		rows = pgsql_num_rows(res);
	}
	else if(state == -1) // Cleanup
	{
		pgsql_free(res);
		return NULL;
	}

	while(row < rows)
	{
		name = pgsql_value(res, row, 0);
		row++;
		if(!strncasecmp(name, text, len))
			return strdup(name);
	}

  	return NULL;
}

char *connclass_generator(const char *text, int state)
{
	static size_t textlen;
	static PGresult *res;
	static int row, rows;

	if(state == 0)
	{
		textlen = strlen(text);
		row = 0;
		res = pgsql_query("SELECT name FROM connclasses_users", 1, NULL);
		rows = pgsql_num_rows(res);
	}
	else if(state == -1)
	{
		pgsql_free(res);
		res = NULL;
		return NULL;
	}

	while(row < rows)
	{
		const char *name = pgsql_value(res, row, 0);
		row++;
		if(!strncasecmp(name, text, textlen))
			return strdup(name);
	}

	return NULL;
}

char *onoff_generator(const char *text, int state)
{
	static char *values[] = { "on", "off", NULL };
	static int idx;
	static size_t len;
	const char *val;

	if(!state) // New word
	{
		len = strlen(text);
		idx = 0;
	}
	else if(state == -1) // Cleanup
	{
		return NULL;
	}

	while((val = values[idx]))
	{
		idx++;
		if(!strncasecmp(val, text, len))
			return strdup(val);
	}

  	return NULL;
}


// The following functions are for proper quoting/unquoting for tab completion.
// Most of them are taken from bash or lftp (which copied from bash)

/* Function taken from lftp */
static int skip_quoted(const char *str, int i, char quote)
{
	while(str[i] && str[i] != quote)
	{
		if(str[i]=='\\' && str[i + 1])
			i++;
		i++;
	}

	if(str[i])
		i++;
	return i;
}

/* Function taken from lftp */
int char_is_quoted(char *string, int end)
{
	int i, pass_next;

	for(i = pass_next = 0; i <= end; i++)
	{
		if(pass_next)
		{
			pass_next = 0;
			if(i >= end)
				return 1;
			continue;
		}
		else if(string[i] == '"' || string[i] == '\'')
		{
			char quote = string[i];
			i = skip_quoted(string, ++i, quote);
			if(i > end)
				return 1;
			i--; // the skip function increments past the closing quote
		}
		else if(string[i] == '\\')
		{
			pass_next = 1;
			continue;
		}
	}

	return 0;
}

/* Filename quoting for completion. Taken from bash. */
/* A function to strip quotes that are not protected by backquotes.  It
   allows single quotes to appear within double quotes, and vice versa.
   It should be smarter. */
char *bash_dequote_filename(const char *text, int quote_char)
{
	char *ret, *r;
	const char *p;
	int quoted;

	ret = malloc(strlen(text) + 1);
	for(quoted = quote_char, p = text, r = ret; p && *p; p++)
	{
		/* Allow backslash-quoted characters to pass through unscathed. */
		if(*p == '\\')
        	{
			*r++ = *++p;
		        if(*p == '\0')
				break;
			continue;
		}
		/* Close quote. */
		if(quoted && *p == quoted)
		{
			quoted = 0;
			continue;
		}
		/* Open quote. */
		if(quoted == 0 && (*p == '\'' || *p == '"'))
		{
			quoted = *p;
			continue;
		}
		*r++ = *p;
	}

	*r = '\0';
	return ret;
}

/* Quote characters that the readline completion code would treat as
   word break characters with backslashes.  Pass backslash-quoted
   characters through without examination.
   Taken from bash. */
static char *quote_word_break_chars(char *text)
{
	char *ret, *r, *s;

	ret = malloc((2 * strlen(text)) + 1);
	for(s = text, r = ret; *s; s++)
	{
		/* Pass backslash-quoted characters through, including the backslash. */
		if(*s == '\\')
		{
			*r++ = '\\';
			*r++ = *++s;
			if(*s == '\0')
				break;
			continue;
		}
		/* OK, we have an unquoted character.  Check its presence in
		   rl_completer_word_break_characters. */
		if(strchr (rl_completer_word_break_characters, *s))
			*r++ = '\\';
		*r++ = *s;
	}
	*r = '\0';
	return ret;
}

/* Return a new string which is the single-quoted version of STRING.
   Taken from bash. */
static char *single_quote(char *string)
{
	int c;
	char *result, *r, *s;

	result = malloc(3 + (4 * strlen(string)));
	r = result;
	*r++ = '\'';

	for(s = string; s && (c = *s); s++)
	{
		*r++ = c;

		if(c == '\'')
		{
			*r++ = '\\';  /* insert escaped single quote */
			*r++ = '\'';
			*r++ = '\'';  /* start new quoted string */
		}
	}

	*r++ = '\'';
	*r = '\0';

	return result;
}

/* Quote STRING using double quotes.  Return a new string.
   Taken from bash. */
static char *double_quote(char *string)
{
	int c;
	char *result, *r, *s;

	result = malloc (3 + (2 * strlen(string)));
	r = result;
	*r++ = '"';

	for(s = string; s && (c = *s); s++)
	{
		switch (c)
		{
			case '"':
			case '\\':
				*r++ = '\\';
				// Fallthrough
			default:
				*r++ = c;
				break;
		}
	}

	*r++ = '"';
	*r = '\0';

	return result;
}

/* Quote special characters in STRING using backslashes.  Return a new
   string. Taken from bash. */
static char *backslash_quote(char *string)
{
	int c;
	char *result, *r, *s;

	result = malloc (2 * strlen(string) + 1);

	for(r = result, s = string; s && (c = *s); s++)
	{
		switch (c)
		{
			case ' ': case '\t': case '\n':
			case '"': case '\'': case '\\':
				*r++ = '\\';
				// Fallthrough
			default:
				*r++ = c;
				break;
		}
	}

	*r = '\0';
	return result;
}

/* Quote a filename using double quotes, single quotes, or backslashes
   depending on the value of completion_quoting_style.  If we're
   completing using backslashes, we need to quote some additional
   characters (those that readline treats as word breaks), so we call
   quote_word_break_chars on the result.
   Taken from bash. */
static char *bash_quote_filename(char *s, int rtype, char *qcp)
{
	char *rtext, *mtext, *ret;
	int rlen, cs;

	rtext = NULL;

	/* If RTYPE == MULT_MATCH, it means that there is
	   more than one match.  In this case, we do not add
	   the closing quote or attempt to perform tilde
	   expansion.  If RTYPE == SINGLE_MATCH, we try
	   to perform tilde expansion, because single and double
	   quotes inhibit tilde expansion by the shell. */

	mtext = s;
	cs = COMPLETE_BSQUOTE; // COMPLETE_BSQUOTE
	/* Might need to modify the default completion style based on *qcp,
	   since it's set to any user-provided opening quote. */
	if(*qcp == '"')
		cs = COMPLETE_DQUOTE;
	else if (*qcp == '\'')
		cs = COMPLETE_SQUOTE;

	switch(cs)
	{
		case COMPLETE_DQUOTE:
			rtext = double_quote(mtext);
			break;
		case COMPLETE_SQUOTE:
			rtext = single_quote(mtext);
			break;
		case COMPLETE_BSQUOTE:
			rtext = backslash_quote(mtext);
			break;
	}

	if(mtext != s)
		free(mtext);

	/* We may need to quote additional characters: those that readline treats
	   as word breaks that are not quoted by backslash_quote. */
	if(rtext && cs == COMPLETE_BSQUOTE)
	{
		mtext = quote_word_break_chars(rtext);
		free(rtext);
		rtext = mtext;
	}

	/* Leave the opening quote intact.  The readline completion code takes
	   care of avoiding doubled opening quotes. */
	rlen = strlen(rtext);
	ret = malloc(rlen + 1);
	strcpy(ret, rtext);

	/* If there are multiple matches, cut off the closing quote. */
	if(rtype == MULT_MATCH && cs != COMPLETE_BSQUOTE)
		ret[rlen - 1] = '\0';
	free(rtext);
	return ret;
}
