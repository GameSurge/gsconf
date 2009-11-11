#ifndef TOOLS_H
#define TOOLS_H

#define COLOR_BLACK	"0;30"
#define COLOR_RED	"0;31"
#define COLOR_GREEN	"0;32"
#define COLOR_BROWN	"0;33"
#define COLOR_BLUE	"0;34"
#define COLOR_PURPLE	"0;35"
#define COLOR_CYAN	"0;36"
#define COLOR_GRAY	"0;37"
#define COLOR_DARKGRAY	"1;30"
#define COLOR_YELLOW	"1;33"
#define COLOR_WHITE	"1;37"
#define COLOR_LIME	"1;32"
#define COLOR_LIGHT_RED		"1;31"
#define COLOR_LIGHT_BLUE	"1;34"
#define COLOR_LIGHT_PURPLE	"1;35"
#define COLOR_LIGHT_CYAN	"1;36"

void debug(char *text, ...) PRINTF_LIKE(1,2);
void out_prefix(char *text, ...) PRINTF_LIKE(1,2);
void out(char *text, ...) PRINTF_LIKE(1,2);
void out_color(const char *color, char *text, ...) PRINTF_LIKE(2,3);
void error(char *text, ...) PRINTF_LIKE(1,2);
char *ltrim(char *str);
char *rtrim(char *str);
int match(const char *mask, const char *name);
size_t strlcpy(char *out, const char *in, size_t len);
int file_exists(const char *file);
void expand_num_args(char *buf, size_t buf_size, const char *str, unsigned int argc, ...);
char *xstrdup(const char *str);

static inline long min(long a, long b)
{
	return (a < b) ? a : b;
}

static inline long max(long a, long b)
{
	return (a > b) ? a : b;
}

static inline char *trim(char *str)
{
	return ltrim(rtrim(str));
}

#define true_string(STR)	((STR) && (!strcasecmp((STR), "on") || !strcasecmp((STR), "true") || !strcmp((STR), "1") || !strcasecmp((STR), "yes") || !strcasecmp((STR), "y")))
#define false_string(STR)	(!(STR) || !strcasecmp((STR), "off") || !strcasecmp((STR), "false") || !strcmp((STR), "0") || !strcasecmp((STR), "no") || !strcasecmp((STR), "n"))

#endif
