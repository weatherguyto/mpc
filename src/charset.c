/* music player command (mpc)
 * Copyright (C) 2003-2008 Warren Dukes <warren.dukes@gmail.com>,
				Eric Wong <normalperson@yhbt.net>,
				Daniel Brown <danb@cs.utexas.edu>
 * Copyright (C) 2008-2009 Max Kellermann <max@duempel.org>
 * Project homepage: http://musicpd.org
 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "charset.h"

#include "mpc.h"
#include "gcc.h"

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#ifdef HAVE_LOCALE
#ifdef HAVE_LANGINFO_CODESET
#include <locale.h>
#include <langinfo.h>
#endif
#endif

static char *locale_charset;

#ifdef HAVE_ICONV
#include <iconv.h>
static iconv_t char_conv_iconv;
static char * char_conv_to;
static char * char_conv_from;
static int ignore_invalid;
#endif

#define BUFFER_SIZE	1024

#ifdef HAVE_ICONV
static void
charset_close(void);
#endif

/* code from iconv_prog.c (omiting invalid symbols): */
#ifdef HAVE_ICONV
static inline char * mpc_strchrnul(const char *s, int c)
{
	char *ret = strchr(s, c);
	if (!ret)
		ret = strchr(s, '\0');
	return ret;
}

static char * skip_invalid(const char *to)
{
	const char *errhand = mpc_strchrnul(to, '/');
	int nslash = 2;
	char *newp, *cp;

	if (*errhand == '/') {
		--nslash;
		errhand = mpc_strchrnul(errhand, '/');

		if (*errhand == '/') {
			--nslash;
			errhand = strchr(errhand, '\0');
		}
	}

	newp = (char *)malloc(errhand - to + nslash + 7 + 1);
	memcpy(newp, to, errhand - to);
	cp = newp + (errhand - to);
	while (nslash-- > 0)
		*cp++ = '/';
	if (cp[-1] != '/')
		*cp++ = ',';
	memcpy(cp, "IGNORE", sizeof("IGNORE"));
	return newp;
}
#endif

static int
charset_set(mpd_unused const char *to, mpd_unused const char *from)
{
#ifdef HAVE_ICONV
	char *allocated = NULL;

	if (ignore_invalid)
		to = allocated = skip_invalid(to);
	if(char_conv_to && strcmp(to,char_conv_to)==0 &&
			char_conv_from && strcmp(from,char_conv_from)==0)
		return 0;

	charset_close();

	if ((char_conv_iconv = iconv_open(to,from))==(iconv_t)(-1))
		return -1;

	char_conv_to = strdup(to);
	char_conv_from = strdup(from);

	if (allocated != NULL)
		free(allocated);

	return 0;
#endif
	return -1;
}

#ifdef HAVE_ICONV
static inline size_t deconst_iconv(iconv_t cd,
				   const char **inbuf, size_t *inbytesleft,
				   char **outbuf, size_t *outbytesleft)
{
	union {
		const char **a;
		char **b;
	} deconst;

	deconst.a = inbuf;

	return iconv(cd, deconst.b, inbytesleft, outbuf, outbytesleft);
}
#endif

static char *
charset_conv_strdup(mpd_unused const char *string)
{
#ifdef HAVE_ICONV
	char buffer[BUFFER_SIZE];
	size_t inleft = strlen(string);
	char * ret;
	size_t outleft;
	size_t retlen = 0;
	size_t err;
	char * bufferPtr;

	if(!char_conv_to) return NULL;

	ret = strdup("");

	while(inleft) {
		bufferPtr = buffer;
		outleft = BUFFER_SIZE;
		err = deconst_iconv(char_conv_iconv,&string,&inleft,&bufferPtr,
				    &outleft);
		if (outleft == BUFFER_SIZE ||
		    (err == (size_t)-1 && errno != E2BIG)) {
			free(ret);
			return NULL;
		}

		ret = realloc(ret,retlen+BUFFER_SIZE-outleft+1);
		memcpy(ret+retlen,buffer,BUFFER_SIZE-outleft);
		retlen+=BUFFER_SIZE-outleft;
		ret[retlen] = '\0';
	}

	return ret;
#endif
	return NULL;
}

#ifdef HAVE_ICONV
static void
charset_close(void)
{
	if(char_conv_to) {
		iconv_close(char_conv_iconv);
		free(char_conv_to);
		free(char_conv_from);
		char_conv_to = NULL;
		char_conv_from = NULL;
	}
}
#endif

void charset_init(void) {
#ifdef HAVE_LOCALE
#ifdef HAVE_LANGINFO_CODESET
	char *original_locale;
        char * charset = NULL;

#ifdef HAVE_ICONV
	ignore_invalid = isatty(STDOUT_FILENO) && isatty(STDIN_FILENO);
#endif

	original_locale = setlocale(LC_CTYPE,"");
	if (original_locale != NULL) {
                char * temp;
                                                                                
                if((temp = nl_langinfo(CODESET))) {
                        charset = strdup(temp);
                }
		setlocale(LC_CTYPE,original_locale);
        }
	
	if (locale_charset != NULL)
		free(locale_charset);
                                                                                
        if(charset) {
		locale_charset = strdup(charset);
                free(charset);
		return;
        }
#endif
#endif
        locale_charset = strdup("ISO-8859-1");
}

char * charset_to_utf8(const char * from) {
	static char * to = NULL;

	if(to) free(to);

	charset_set("UTF-8", locale_charset);
	to = charset_conv_strdup(from);

	if(!to) to = strdup(from);

	return to;
}

char * charset_from_utf8(const char * from) {
	static char * to = NULL;

	if(to) free(to);

	charset_set(locale_charset, "UTF-8");
	to = charset_conv_strdup(from);

	if(!to) {
		/*printf("not able to convert: %s\n",from);*/
		to = strdup(from);
	}

	return to;
}