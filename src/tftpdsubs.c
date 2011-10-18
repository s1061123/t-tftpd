/*
   tftpdsubs.c
   $Id: tftpdsubs.c,v 1.2 2003/11/16 22:27:59 thayashi Exp $

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

#include "tftpdsubs.h"

/* 
 * Linux does not support the strlcpy function.
 * So that it supports, OpenBSD's implementation is used.
*/
#ifdef HAVE_STRLCPY
#include "strlcpy.c"
#endif

f_node *new_node(char *fname, int dir, int wr)
{
    f_node *ptr;
	
    ptr = (f_node *)malloc(sizeof(f_node));
    strlcpy(ptr->name, fname, NAME_SIZ);

    ptr->is_dir = dir;
    ptr->is_wr = wr;
    ptr->next = NULL;
    ptr->child = NULL;

    return ptr;
}

f_node *get_node(char *path, char *name, int dir, int wr)
{
    f_node *r_ptr, *c_ptr;
    struct stat st;
    struct dirent *d_ent;
    int ch_dir, ch_wr;
    int first;
    DIR *dp;
    char fullpath[NAME_SIZ*10], fullname[NAME_SIZ*10];

    memset(fullpath, 0, sizeof(char)*NAME_SIZ*10);
    if (dir == 0) 
    {
	return new_node(name, dir, wr);
    }
    else 
    {
	snprintf(fullpath, NAME_SIZ, "%s/%s\0", path, name);

	dp = opendir(fullpath);
	first = 1;

	if (dp == NULL) 
	{
	    fprintf(stderr, "opendir(%s) is failed.\n", fullpath);
	    perror("");
	    return NULL;
	    /*	    return new_node(name, -1, wr); */
	} 

	while ((d_ent = readdir(dp)) != NULL) 
	{

	    if (d_ent->d_name[0] != '.')
	    {
		snprintf(fullname, NAME_SIZ, "%s/%s", fullpath, d_ent->d_name);
		memset(&st, 0, sizeof(st));
		lstat(fullname, &st);
	      
/*		if (st.st_mode & S_IROTH)   */
		{
					
		    if (S_ISDIR(st.st_mode)) 
		    {
			ch_dir = 1;
			if (((st.st_mode & S_IWOTH) > 0) &&
			    ((st.st_mode & S_IXOTH) > 0))
			{
			    ch_wr = 1;
			}
			else
			    ch_wr = 0;
		    }
		    else 
		    {
			ch_dir = 0;
			if (st.st_mode & S_IWOTH)
			    ch_wr = 1;
			else
			    ch_wr = 0;
		    }

		    if (ch_dir && !(st.st_mode & S_IXOTH)){
			closedir(dp);
			return  new_node(name, -1, wr); 
		    }
	
		    if (first) 
		    {
			r_ptr = new_node(name, 1, wr);
			c_ptr = get_node(fullpath, d_ent->d_name, ch_dir, ch_wr);
			r_ptr->child = c_ptr;
			first = 0;
		    }
		    else 
		    {
			c_ptr->next = get_node(fullpath, d_ent->d_name, ch_dir, ch_wr);
			c_ptr = c_ptr->next;
		    }

		} /* if (st.st_mode & S_IROTH)...  */
	    }	    /* if (strcmp(d_ent->d_name,".")  */
	} /* while().... */
    } /* if (dir == 0) else...... */

    closedir(dp);
    c_ptr = r_ptr->child;
    free(r_ptr);
    return c_ptr;
}

/* for debug. */
void show_node(f_node *ptr, int depth)
{
    int cnt;
    for (cnt = 0; cnt < depth; cnt++)
	printf("\t");

    printf("name: %s/%d(%d %d)\n", ptr->name, depth, ptr->is_dir, ptr->is_wr);
    if (ptr->child != NULL) 
    {
	show_node(ptr->child, depth+1);
    }
    if (ptr->next != NULL) 
    {
	show_node(ptr->next, depth);
    }
}


f_node *get_leaf(f_node *current, char *str)
{
    f_node *ptr;
    for (ptr = current; ptr != NULL; ptr=ptr->next)
    {
	if (strncmp(ptr->name, str, NAME_SIZ) == 0) {
	    return ptr;
	}
    }
    return NULL;
}

void free_node(f_node *current)
{
    if(current->child != NULL)
    {
	free_node(current->child);
    }
    if(current->next != NULL)
    {
	free_node(current->next);
    }
    free(current);
    return ;
}

void (*m_signal (int signo, void (*func)(int))) (int)
{
    struct sigaction act, oact;

    act.sa_handler = func;
/* Signal の実行中ブロックするシグナルはないことを宣言 */
    sigemptyset(&act.sa_mask);
    
    /* */
    act.sa_flags = 0;

    if (signo == SIGALRM)
    {
#ifdef SA_NTERRPUT
	act.sa_flags |= SA_INTERRPUT;
#endif
    }
    else 
    {
#ifdef SA_RESTART
	act.sa_flags |= SA_RESTART;
#endif
    }

    if (sigaction(signo, &act, &oact) < 0)
    {
	return SIG_ERR;
    }

    return oact.sa_handler;
}

/* for debug. */
#ifdef TEST
int main(int argc, char *argv[])
{
    char cpath[NAME_SIZ];
    char str[NAME_SIZ] = ".";
    char *cptr;
    f_node *ptr, *root;
    int cnt = 0;

    bzero(cpath, sizeof(char)*NAME_SIZ);
    getcwd(cpath, NAME_SIZ);
    while (1)
    {
	printf("get node(%d)\n", cnt);
	if ((root = get_node(cpath, ".", 1, 1)) == NULL)
	{
	    exit(1);
	}
	cnt++;
    }
    ptr = root;
    //    show_node(ptr, 0);

    cptr = strtok(str, "/");
    while (cptr != NULL) 
    {
	ptr = ptr->child;
 	printf("%s ", cptr);
	ptr = get_leaf(ptr, cptr);
	if (ptr == NULL) 
	{
	    perror("not find!");
	    break;
	}
	cptr = strtok(NULL, "/");
    }
  
    printf("\nfind!\n");
    return 0;
}
#endif



