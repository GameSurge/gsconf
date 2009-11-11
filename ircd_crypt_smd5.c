/*
* IRC - Internet Relay Chat, ircd/ircd_crypt_smd5.c
* Copyright (C) 2002 hikari
*
*/

/**
* @file
* @brief Routines for Salted MD5 passwords
* @version $Id$
*
* ircd_crypt_smd5 is largely taken from md5_crypt.c from the Linux PAM
* source code.  it's been modified to fit in with ircu and some of the
* unneeded code has been removed.  the source file md5_crypt.c has the
* following license, so if any of our opers or admins are in Denmark
* they better go buy them a drink ;) -- hikari
*
* ----------------------------------------------------------------------------
* "THE BEER-WARE LICENSE" (Revision 42):
* <phk@login.dknet.dk> wrote this file.  As long as you retain this notice you
* can do whatever you want with this stuff. If we meet some day, and you think
* this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
* ----------------------------------------------------------------------------
*
*/

#include "common.h"
#include "ircd_crypt_smd5.h"
#include "ircd_md5.h"
#include "mtrand.h"

const char* default_salts = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";

static unsigned char itoa64[] = /* 0 ... 63 => ascii - 64 */
"./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/** Converts a binary value into a BASE64 encoded string.
* @param s Pointer to the output string
* @param v The unsigned long we're working on
* @param n The number of bytes we're working with
*
* This is used to produce the normal MD5 hash everyone is familiar with.
* It takes the value v and converts n bytes of it it into an ASCII string in
* 6-bit chunks, the resulting string is put at the address pointed to by s.
*
*/
static void to64(char *s, unsigned long v, int n)
{
	while(--n >= 0)
	{
		*s++ = itoa64[v & 0x3f];
		v >>= 6;
	}
}

/** Produces a Salted MD5 crypt of a password using the supplied salt
* @param key The password we're encrypting
* @param salt The salt we're using to encrypt it
* @return The Salted MD5 password of key and salt
*
* Erm does exactly what the brief comment says.  If you think I'm writing a
* description of how MD5 works, you have another think coming.  Go and read
* Applied Cryptography by Bruce Schneier.  The only difference is we use a
* salt at the beginning of the password to perturb it so that the same password
* doesn't always produce the same hash.
*
*/
static char *ircd_crypt_smd5(const char *key, const char *salt)
{
	const char *magic = "$1$";
	static char passwd[120];
	char *p;
	const char *sp, *ep;
	unsigned char final[16];
	int sl, pl, i, j;
	MD5_CTX ctx, ctx1;
	unsigned long l;

	assert(NULL != key);
	assert(NULL != salt);

	/* Refine the Salt first */
	ep = sp = salt;

	for(ep = sp; *ep && *ep != '$' && ep < (sp + 8); ep++)
		continue;

	/* get the length of the true salt */
	sl = ep - sp;

	MD5Init(&ctx);

	/* The password first, since that is what is most unknown */
	MD5Update(&ctx, (unsigned const char *)key, strlen(key));

	/* Then our magic string */
	MD5Update(&ctx, (unsigned const char *)magic, strlen(magic));

	/* Then the raw salt */
	MD5Update(&ctx, (unsigned const char *)sp, sl);

	/* Then just as many characters of the MD5(key,salt,key) */
	MD5Init(&ctx1);
	MD5Update(&ctx1, (unsigned const char *)key, strlen(key));
	MD5Update(&ctx1, (unsigned const char *)sp, sl);
	MD5Update(&ctx1, (unsigned const char *)key, strlen(key));
	MD5Final(final, &ctx1);
	for(pl = strlen(key); pl > 0; pl -= 16)
		MD5Update(&ctx, (unsigned const char *)final, pl>16 ? 16 : pl);

	/* Don't leave anything around in vm they could use. */
	memset(final, 0, sizeof final);

	/* Then something really weird... */
	for(j = 0, i = strlen(key); i; i >>= 1)
	{
		if (i & 1)
			MD5Update(&ctx, (unsigned const char *)final+j, 1);
		else
			MD5Update(&ctx, (unsigned const char *)key+j, 1);
	}

	/* Now make the output string. */
	memset(passwd, 0, 120);
	strncpy(passwd, sp, sl);
	strcat(passwd, "$");

	MD5Final(final,&ctx);

	/*
	 * and now, just to make sure things don't run too fast
	 * On a 60 Mhz Pentium this takes 34 msec, so you would
	 * need 30 seconds to build a 1000 entry dictionary...
	 */
	for(i = 0; i < 1000; i++)
	{
		MD5Init(&ctx1);

		if(i & 1)
			MD5Update(&ctx1,(unsigned const char *)key,strlen(key));
		else
			MD5Update(&ctx1,(unsigned const char *)final,16);

		if (i % 3)
			MD5Update(&ctx1,(unsigned const char *)sp,sl);

		if (i % 7)
			MD5Update(&ctx1,(unsigned const char *)key,strlen(key));

		if (i & 1)
			MD5Update(&ctx1,(unsigned const char *)final,16);
		else
			MD5Update(&ctx1,(unsigned const char *)key,strlen(key));

		MD5Final(final,&ctx1);
	}

	p = passwd + strlen(passwd);

	/* Turn the encrypted binary data into a BASE64 encoded string we can read
	 * and display -- hikari */
	l = (final[0] << 16) | (final[6] << 8) | final[12];
	to64(p, l, 4);
	p += 4;
	l = (final[1] << 16) | (final[7] << 8) | final[13];
	to64(p, l, 4);
	p += 4;
	l = (final[2] << 16) | (final[8] << 8) | final[14];
	to64(p, l, 4);
	p += 4;
	l = (final[3] << 16) | (final[9] << 8) | final[15];
	to64(p, l, 4);
	p += 4;
	l = (final[4] << 16) | (final[10] << 8) | final[5];
	to64(p, l, 4);
	p += 4;
	l = final[11];
	to64(p, l, 2);
	p += 2;
	*p = '\0';

	/* Don't leave anything around in vm they could use. */
	memset(final, 0, sizeof final);

	return passwd;
}

/* end borrowed code */

/* quick and dirty salt generator */
char *make_salt(const char *salts)
{
	char *tmp = NULL;
	int n = 0;

	if((tmp = calloc(3, sizeof(char))) != NULL)
	{
		/* can't optimize this much more than just doing it twice */
		n = mt_rand(0, strlen(salts) - 1);
		memcpy(tmp, (salts+n), 1);
		n = mt_rand(0, strlen(salts) - 1);
		memcpy((tmp+1), (salts+n), 1);
	}

	return tmp;
}


char *crypt_pass_smd5(const char *pw)
{
	char *pass;
	char *salt;

	salt = make_salt(default_salts);
	pass = ircd_crypt_smd5(pw, salt);
	free(salt);
	return pass;
}
