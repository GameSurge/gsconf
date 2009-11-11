#include "common.h"
#include "table.h"
#include "stringbuffer.h"

struct table *table_create(unsigned int cols, unsigned int rows)
{
	struct table *table = malloc(sizeof(struct table));
	memset(table, 0, sizeof(struct table));

	table->cols = cols;
	table->rows = rows;

	table->data = calloc(table->rows, sizeof(char **));
	for(unsigned int i = 0; i < table->rows; i++)
		table->data[i] = calloc(table->cols, sizeof(char *));

	return table;
}

void table_set_header(struct table *table, const char *str, ...)
{
	va_list args;

	assert(str);
	table->header = calloc(table->cols, sizeof(char *));

	va_start(args, str);
	table->header[0] = str;
	for(unsigned int i = 1; i < table->cols; i++)
		table->header[i] = va_arg(args, const char *);
	va_end(args);
}

void table_bold_column(struct table *table, unsigned int col, unsigned char enable)
{
	assert(col < table->cols);

	if(enable)
		table->bold_cols |= (1 << col);
	else
		table->bold_cols &= ~(1 << col);
}

void table_free_column(struct table *table, unsigned int col, unsigned char enable)
{
	assert(col < table->cols);

	if(enable)
		table->free_cols |= (1 << col);
	else
		table->free_cols &= ~(1 << col);
}

void table_ralign_column(struct table *table, unsigned int col, unsigned char enable)
{
	assert(col < table->cols);

	if(enable)
		table->ralign_cols |= (1 << col);
	else
		table->ralign_cols &= ~(1 << col);
}


#define col_free(TABLE, COL)	((TABLE)->free_cols & (1 << COL))
void table_free(struct table *table)
{
	for(unsigned int i = 0; i < table->rows; i++)
	{
		if(table->free_cols)
		{
			for(unsigned int j = 0; j < table->cols; j++)
			{
				if(col_free(table, j) && table->data[i][j])
					free(table->data[i][j]);
			}
		}

		free(table->data[i]);
	}

	if(table->header)
		free(table->header);
	free(table->data);
	free(table);
}
#undef col_free

#define col_bold(TABLE, COL)	((TABLE)->bold_cols & (1 << COL))
#define col_ralign(TABLE, COL)	((TABLE)->ralign_cols & (1 << COL))
void table_send(struct table *table)
{
	unsigned int len, spaces, *maxlens;
	struct stringbuffer *line;

	maxlens = calloc(table->cols, sizeof(unsigned int));

	if(table->header)
	{
		for(unsigned int col = 0; col < table->cols; col++)
			maxlens[col] = strlen(table->header[col]);
	}

	for(unsigned int col = 0; col < table->cols; col++)
	{
		for(unsigned int row = 0; row < table->rows; row++)
		{
			len = table->data[row][col] ? strlen(table->data[row][col]) : 0;
			if(len > maxlens[col])
				maxlens[col] = len;
		}
	}

	line = stringbuffer_create();
	for(int row = (table->header ? -1 : 0); row < (int)table->rows; row++)
	{
		const char **ptr = ((row == -1) ? table->header : (const char **)table->data[row]);

		for(unsigned int col = 0; col < table->cols; col++)
		{
			if(row == -1) // Header
			{
				spaces = maxlens[col] - (ptr[col] ? strlen(ptr[col]) : 0);
				stringbuffer_append_string(line, "\033[4m");
				if(ptr[col])
					stringbuffer_append_string(line, ptr[col]);
				stringbuffer_append_string(line, "\033[24m");
			}
			else // Data
			{
				spaces = maxlens[col] - (ptr[col] ? strlen(ptr[col]) : 0);
				if(col_bold(table, col))
					stringbuffer_append_string(line, "\033[1m");
				while(col_ralign(table, col) && spaces--)
					stringbuffer_append_char(line, ' ');
				if(ptr[col])
					stringbuffer_append_string(line, ptr[col]);
			}

			if(col < table->cols - 1) // Not the last column
			{
				while((!col_ralign(table, col) || row == -1) && spaces--)
					stringbuffer_append_char(line, ' ');
				if(col_bold(table, col))
					stringbuffer_append_string(line, "\033[22m");

				stringbuffer_append_string(line, "  ");
			}
			else if(col_bold(table, col))
			{
				stringbuffer_append_string(line, "\033[22m");
			}
		}

		out("%s%s", table->prefix ? table->prefix : "", line->string);
		line->len = 0;
	}

	stringbuffer_free(line);
	free(maxlens);
}
#undef col_bold
#undef col_ralign

static void table_alloc(struct table *table)
{
	table->rows++;
	table->data = realloc(table->data, table->rows * sizeof(char **));
	table->data[table->rows - 1] = calloc(table->cols, sizeof(char *));
}

void table_col_str(struct table *table, unsigned int row, unsigned int col, char *val)
{
	assert(col < table->cols);
	if(row >= table->rows)
		table_alloc(table);
	assert(row < table->rows);

	table->data[row][col] = val;
}

void table_col_num(struct table *table, unsigned int row, unsigned int col, unsigned int val)
{
	assert(col < table->cols);
	if(row >= table->rows)
		table_alloc(table);
	assert(row < table->rows);

	asprintf(&table->data[row][col], "%u", val);
}

void table_col_fmt(struct table *table, unsigned int row, unsigned int col, const char *fmt, ...)
{
	va_list args;

	assert(col < table->cols);
	if(row >= table->rows)
		table_alloc(table);
	assert(row < table->rows);

	va_start(args, fmt);
	vasprintf(&table->data[row][col], fmt, args);
	va_end(args);
}
