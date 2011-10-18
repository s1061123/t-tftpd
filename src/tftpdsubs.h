/*
   tftpdsubs.h
   $Id: tftpdsubs.h,v 1.2 2003/11/16 22:27:59 thayashi Exp $

   This file is part of t-tftpd.

   Copyright 2002,2007 Tomofumi Hayashi <s1061123@gmail.com>.
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
   3. All advertising materials mentioning features or use of this software
      must display the following acknowledgement:

        This product includes software developed by
        Tomofumi Hayashi <s1061123@gmail.com> and its contributors.

   4. Neither the name of authors nor the names of its contributors may be used
      to endorse or promote products derived from this software without
      specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND ANY
   EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
   THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TFTPDSUBS_H
#define _TFTPDSUBS_H

#ifdef __cplusplus
extern "C" {
#endif /* _cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#define NAME_SIZ 256

typedef struct f_node 
{
  char name[NAME_SIZ];
  int is_dir; /* If it is directory, the value is 1. 
	       * if it is directory and not enter, the value is -1.
	       */
  int is_wr; /* If writable, the value is 1 */
  int is_rd; /* */
  struct f_node *next;
  struct f_node *child;
} f_node;

f_node *new_node(char *fname, int dir, int wr);
f_node *get_node(char *path, char *name, int dir, int wr);
f_node *get_leaf(f_node *current, char *str);
void free_node(f_node *current);

void show_node(f_node *ptr, int depth);

void (*m_signal (int signo, void (*func)(int))) (int);
#ifdef HAVE_STRLCPY
  size_t strlcpy(char *dst, const char *src, size_t siz);
#endif /* LINUX */

#ifdef DEBUG
#endif /* DEBUG */

#ifdef __cplusplus
}
#endif /* _cplusplus */

#endif /* _TFTPDSUBS_H */
