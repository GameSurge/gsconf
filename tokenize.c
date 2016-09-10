#include "common.h"
#include "tokenize.h"

/**
 * @param str Input string; this string gets destroyed!
 * @param vec Output vector
 * @param vec_size Size of output vector array
 * @param token Token to use
 * @param allow_empty Allow empty elements
 *
 * @return Count of output vector items
 *
 * This function splits up a string using the given token
 */
unsigned int tokenize(char *str, char **vec, unsigned int vec_size, char token, unsigned char allow_empty)
{
	unsigned int count;
	char *last, *ch;

	if(vec_size == 0)
		return 0;

	count = 1;
	last = ch = str;

	// String starts with tokens, skip them
	if(!allow_empty)
	{
		while(*last == token)
			last++;
		// String already ends here
		if(!*last)
			return 0;
	}

	vec[0] = last;

	while((ch = strchr(last, token)))
	{
		*ch++ = '\0';

		if(!allow_empty)
		{
			while(*ch == token)
				ch++;
		}

		// String may end here after token
		if(!allow_empty && !*ch)
			return count;

		vec[count++] = ch;
		last = ch;

		if(count == vec_size)
			return count;
	}

	// If there were no tokens, the first and last element are the same
	if(ch == str)
		vec[count++] = last;

	return count;
}

// Like tokenize, taking care of quotes and backslashes
int tokenize_quoted(char *str, char **vec, int vec_size)
{
	int count = 0;
	int empty_end_token = 0, incomplete_backslash = 0, escaped_space = 0;
	char quote;
	char *current;

	vec_size--; // reserve one slot for argv[argc] metadata
	assert(vec_size > 1);

	// String starts with delimiters, skip them
	while(*str == ' ')
		str++;
	// String already ends here
	if(!*str)
	{
		vec[count] = "";
		return 0;
	}

	quote = '\0';
	current = str;
	while(count < vec_size && *str)
	{
		empty_end_token = 0;
		switch(*str)
		{
			case '"': case '\'':
				if(quote && quote != *str) // Still inside quote
				{
					str++;
					continue;
				}
				else if(quote) // End quote
				{
					quote = '\0';
					memmove(str, str + 1, strlen(str));
					continue;
				}
				else
				{
					quote = *str;
					memmove(str, str + 1, strlen(str));
					continue;
				}
				break;

			case '\\':
				if(!*(str + 1))
					incomplete_backslash = 1;
				else if(*(str + 1) == ' ' && !*(str + 2))
					escaped_space = 1;
				memmove(str, str + 1, strlen(str));
				str++; // Do not handle next char, it is escaped!
				break;

			case ' ':
				if(quote)
				{
					str++;
					continue;
				}

				*str++ = '\0';
				vec[count++] = current;
				empty_end_token = 1; // In case this is the last token, we add an empty end token
				while(*str == ' ')
					str++;
				current = str;
				break;

			default:
				str++;
		}
	}

	if(current != str && count < vec_size)
		vec[count++] = current;
	else if(quote && count < vec_size)
	{
		assert(*str == '\0');
		vec[count++] = str;
	}

	if(quote)
		vec[count] = "\"";
	else if(incomplete_backslash || escaped_space)
		vec[count] = "*";
	else if(empty_end_token)
		vec[count] = " ";
	else
		vec[count] = "";
	return count;
}

/**
 * @param str Input string; this string gets destroyed
 * @param vec Output vector
 * @param vec_size Size of output vector
 * @param token Token to use
 * @param ltoken Token marking the last item
 *
 * @return Count of output vector items
 *
 * This function splits up a string using the given token and
 * the given last-item token
 */
unsigned int itokenize(char *str, char **vec, unsigned int vec_size, char token, char ltoken)
{
	char *ch = str;
	unsigned int count = 1;
	unsigned char inside_string = 0;

	vec[0] = ch;

	for(ch = str; *ch; ch++)
	{
		if((*ch == token) && (inside_string == 0))
			*ch = '\0';

		while((*ch == token) && (inside_string == 1))
		{
			*ch++ = '\0';

			if(*ch == ltoken)
			{
				*ch++ = '\0';
				vec[count++] = ch;
				return count;
			}

			vec[count++] = ch;

			if(((count + 1) >= vec_size) || (*ch == '\0'))
				return count;
		}

		if(inside_string == 0)
			inside_string = 1;
	}

	return count;
}

/**
 * @param num_items Number of vector items to use
 * @param vec Input vector
 * @param sep Char to insert between vector items
 *
 * @return String containing num_items elements from vec, separated by sep; must be free'd
 *
 * This function "unsplits" a vector using the given separator and
 * returns the string.
 */
char *untokenize(unsigned int num_items, char **vec, const char *sep)
{
	char *str;
	size_t len;
	unsigned int i;

	assert(num_items > 0);
	len = 1; // '\0'
	len += strlen(sep) * (num_items - 1); // separators between items
	len += strlen(vec[0]); // first item

	if(num_items > 1)
	{
		for(i = 1; i < num_items; i++)
		{
			len += strlen(vec[i]);
		}
	}

	str = malloc(len);
	memset(str, 0, len);

	strncpy(str, vec[0], len);
	if(num_items > 1)
	{
		for(i = 1; i < num_items; i++)
		{
			strncat(str, sep, len);
			strncat(str, vec[i], len);
		}
	}

	return str;
}
