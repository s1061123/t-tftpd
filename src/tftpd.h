/*
   tftpd.h
   $Id: tftpd.h,v 1.7 2004/01/05 07:58:54 thayashi Exp $
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

#ifndef _TFTPD_H_
#define _TFTPD_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <assert.h>
#include <signal.h>
#include <setjmp.h>
#include <ctype.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/mman.h>
#include <poll.h>

#define _DEBUG

#ifdef _DEBUG
int debug_level = 0;
#define d_setlevel(level) {debug_level = level;}
#define d_printf(level,x) {if (debug_level >= level) printf x;}
#else
#define d_setlevel(level)
#define d_printf(level,x) 
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_TFTPD_H_ */
