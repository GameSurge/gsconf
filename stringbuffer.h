#ifndef STRINGBUFFER_H
#define STRINGBUFFER_H

struct stringbuffer
{
	unsigned int len;
	unsigned int size;

	char *string;
};

struct stringbuffer *stringbuffer_create();
void stringbuffer_free(struct stringbuffer *sbuf);

void stringbuffer_append_char(struct stringbuffer *sbuf, char c);
void stringbuffer_append_string_n(struct stringbuffer *sbuf, const char *str, size_t len);
void stringbuffer_append_string(struct stringbuffer *sbuf, const char *str);

void stringbuffer_erase(struct stringbuffer *sbuf, unsigned int start, unsigned int len);
void stringbuffer_insert_n(struct stringbuffer *sbuf, unsigned int pos, const char *str, size_t n);
void stringbuffer_insert(struct stringbuffer *sbuf, unsigned int pos, const char *str);

char *stringbuffer_shift(struct stringbuffer *sbuf, const char *delim, unsigned char require_token);
char *stringbuffer_shiftspn(struct stringbuffer *sbuf, const char *delim_list, unsigned char require_token);

void stringbuffer_append_vprintf(struct stringbuffer *sbuf, const char *fmt, va_list args);
void stringbuffer_append_printf(struct stringbuffer *sbuf, const char *fmt, ...);

void stringbuffer_flush(struct stringbuffer *sbuf);
char *stringbuffer_flush_return(struct stringbuffer *sbuf, size_t len);

#endif
