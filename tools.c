#include "common.h"
#include "tools.h"
#include "main.h"

static const char whitespace_chars[] = " \t\n\v\f\r";
static char output_prefix[64] = "";

static void strip_colors(char *str)
{
	for(char *ptr = str; *ptr; ptr++)
	{
		if(*ptr == '\033')
		{
			while(*ptr != 'm')
				memmove(ptr, ptr + 1, strlen(ptr));
			// Now get rid of the 'm'
			memmove(ptr, ptr + 1, strlen(ptr));
		}
	}
}

static int my_vprintf(const char *fmt, va_list args)
{
	static char buf[2048]; // Just assume this is big enough for any output..
	int ret;

	if(!no_colors)
		return vprintf(fmt, args);

	ret = vsnprintf(buf, sizeof(buf), fmt, args);
	strip_colors(buf);

	printf("%s", buf);
	return ret;
}

void debug(char *text, ...)
{
	va_list	va;

	if(!debug_output_enabled)
		return;

	if(*output_prefix)
		printf("%s", output_prefix);

	if(!no_colors)
		printf("\033[" COLOR_LIGHT_BLUE "m");
	else
		printf("DEBUG: ");
	va_start(va, text);
	my_vprintf(text, va);
	if(!no_colors)
		printf("\033[0m");
	printf("\n");
	va_end(va);
}

void out_prefix(char *text, ...)
{
	va_list va;

	if(!text)
	{
		*output_prefix = '\0';
		return;
	}

	va_start(va, text);
	vsnprintf(output_prefix, sizeof(output_prefix), text, va);
	if(no_colors)
		strip_colors(output_prefix);
	va_end(va);
}

void out(char *text, ...)
{
	va_list	va;

	if(*output_prefix)
		printf("%s", output_prefix);

	va_start(va, text);
	my_vprintf(text, va);
	printf("\n");
	va_end(va);
}

void out_color(const char *color, char *text, ...)
{
	va_list	va;

	if(*output_prefix)
		printf("%s", output_prefix);

	if(!no_colors)
		printf("\033[%sm", color);
	va_start(va, text);
	my_vprintf(text, va);
	if(!no_colors)
		printf("\033[0m");
	printf("\n");
	va_end(va);
}

void error(char *text, ...)
{
	va_list	va;

	if(!no_colors)
		printf("\033[" COLOR_LIGHT_RED "m");
	else
		printf("ERROR: ");
	va_start(va, text);
	my_vprintf(text, va);
	if(!no_colors)
		printf("\033[0m");
	printf("\n");
	va_end(va);
}

char *ltrim(char *str)
{
	size_t len;
	for(len = strlen(str); len && isspace(str[len - 1]); len--)
		;
	str[len] = '\0';
	return str;
}

char *rtrim(char *str)
{
	char *tmp = str + strspn(str, whitespace_chars);
	memmove(str, tmp, strlen(str) - (tmp - str) + 1);
	return str;
}

// from ircu 2.10.12
/*
 * Compare if a given string (name) matches the given
 * mask (which can contain wild cards: '*' - match any
 * number of chars, '?' - match any single character.
 *
 * return  0, if match
 *         1, if no match
 *
 *  Originally by Douglas A Lewis (dalewis@acsu.buffalo.edu)
 *  Rewritten by Timothy Vogelsang (netski), net@astrolink.org
 */

/** Check a string against a mask.
 * This test checks using traditional IRC wildcards only: '*' means
 * match zero or more characters of any type; '?' means match exactly
 * one character of any type.  A backslash escapes the next character
 * so that a wildcard may be matched exactly.
 * @param[in] mask Wildcard-containing mask.
 * @param[in] name String to check against \a mask.
 * @return Zero if \a mask matches \a name, non-zero if no match.
 */
int match(const char *mask, const char *name)
{
	const char *m = mask, *n = name;
	const char *m_tmp = mask, *n_tmp = name;
	int star_p;

	for (;;) switch (*m) {
	case '\0':
		if (!*n)
			return 0;
	backtrack:
		if (m_tmp == mask)
			return 1;
		m = m_tmp;
		n = ++n_tmp;
		break;
	case '\\':
		m++;
		/* allow escaping to force capitalization */
		if (*m++ != *n++)
			return 1;
		break;
	case '*': case '?':
		for (star_p = 0; ; m++) {
			if (*m == '*')
				star_p = 1;
			else if (*m == '?') {
				if (!*n++)
					goto backtrack;
			} else break;
		}
		if (star_p) {
			if (!*m)
				return 0;
			else if (*m == '\\') {
				m_tmp = ++m;
				if (!*m)
					return 1;
				for (n_tmp = n; *n && *n != *m; n++) ;
			} else {
				m_tmp = m;
				for (n_tmp = n; *n && tolower(*n) != tolower(*m); n++) ;
			}
		}
		/* and fall through */
	default:
		if (!*n)
			return *m != '\0';
		if (tolower(*m) != tolower(*n))
			goto backtrack;
		m++;
		n++;
		break;
	}
}

size_t strlcpy(char *out, const char *in, size_t len)
{
	size_t in_len;

	in_len = strlen(in);
	if (in_len < --len)
		memcpy(out, in, in_len + 1);
	else
	{
		memcpy(out, in, len);
		out[len] = '\0';
	}
	return in_len;
}

int file_exists(const char *file)
{
	struct stat statbuf;
	return (stat(file, &statbuf) == 0);
}

void expand_num_args(char *buf, size_t buf_size, const char *str, unsigned int argc, ...)
{
	va_list args;
	size_t len = 0;
	char **arg_list;

	va_start(args, argc);
	arg_list = calloc(argc, sizeof(char *));
	for(unsigned int i = 0; i < argc; i++)
		arg_list[i] = va_arg(args, char *);
	va_end(args);

	while(*str && len < buf_size - 1)
	{
		if(*str != '$' || !isdigit(*(str + 1)))
			buf[len++] = *(str++);
		else // $something
		{
			char *end;
			unsigned long idx = strtoul(str + 1, &end, 10);
			if(!idx || idx > argc || (len + strlen(arg_list[idx - 1]) >= buf_size - 1))
			{
				buf[len++] = *(str++); // copy $
				continue;
			}

			len += strlcpy(buf + len, arg_list[idx - 1], buf_size - len);
			str = end;
		}
	}

	buf[len] = '\0';
	free(arg_list);
}

char *xstrdup(const char *str)
{
	if(!str)
		return NULL;
	return strdup(str);
}
