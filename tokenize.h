#ifndef TOKENIZE_H
#define TOKENIZE_H

unsigned int tokenize(char *str, char **vec, unsigned int vec_size, char token, unsigned char allow_empty);
int tokenize_quoted(char *str, char **vec, int vec_size);
unsigned int itokenize(char *str, char **vec, unsigned int vec_size, char token, char ltoken);

char *untokenize(unsigned int num_items, char **vec, const char *sep);

#endif
