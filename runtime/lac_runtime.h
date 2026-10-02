#ifndef LAC_RUNTIME_H
#define LAC_RUNTIME_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
typedef struct { const char *data; size_t size; } lac_str;
typedef struct { lac_str *data; size_t size; } lac_args;
typedef struct { void *data; size_t size; int integer; } lac_array;
static void lac_panic(const char *msg) { fprintf(stderr,"lac: %s\n",msg);exit(1); }
static void *lac_alloc(size_t n) { void *p=malloc(n?n:1);if(!p)lac_panic("out of memory");return p; }
#include "lac_integer.h"
static lac_str lac_text(const char *text) { size_t n=strlen(text);char *p=lac_alloc(n);memcpy(p,text,n);return (lac_str){p,n}; }
static lac_str lac_copy_s(lac_str s) { char *p=lac_alloc(s.size);memcpy(p,s.data,s.size);return (lac_str){p,s.size}; }
static int lac_equal_s(lac_str a,lac_str b) { return a.size==b.size&&(!a.size||!memcmp(a.data,b.data,a.size)); }
static lac_str lac_concat(lac_str a,lac_str b) { if(a.size>SIZE_MAX-b.size)lac_panic("string too large");char *p=lac_alloc(a.size+b.size);memcpy(p,a.data,a.size);memcpy(p+a.size,b.data,b.size);return (lac_str){p,a.size+b.size}; }
static void lac_drop_s(lac_str s) { free((void*)s.data); }
static void lac_pr_s(lac_str s,int newline) { fwrite(s.data,1,s.size,stdout);if(newline)fputc('\n',stdout); }
static void lac_pr_b(int n,int newline) { fputs(n?"true":"false",stdout);if(newline)fputc('\n',stdout); }
static void lac_pr_d(double n,int newline) { printf("%.17g",n);if(newline)fputc('\n',stdout); }
static void lac_pr_f(float n,int newline) { printf("%.9g",(double)n);if(newline)fputc('\n',stdout); }
static lac_str lac_fmt_d(double n) { char b[64];snprintf(b,sizeof(b),"%.17g",n);return lac_text(b); }
static char *lac_cstr(lac_str s) { if(s.size==SIZE_MAX)lac_panic("string too large");char *p=lac_alloc(s.size+1);memcpy(p,s.data,s.size);p[s.size]=0;return p; }
static lac_array lac_new_array(int64_t n,size_t element_size,int zero,int integer) {
    if(n<0||(uint64_t)n>SIZE_MAX/element_size)lac_panic("invalid array size");
    size_t size=(size_t)n;void *data=zero?calloc(size?size:1,element_size):malloc((size?size:1)*element_size);if(!data)lac_panic("out of memory");return (lac_array){data,size,integer};
}
static void lac_drop_array(lac_array a) { if(a.integer)for(size_t i=0;i<a.size;i++)lac_drop_i(((lac_int*)a.data)[i]);free(a.data); }
static size_t lac_index(lac_array a,int64_t n) { if(n<0||(uint64_t)n>=a.size)lac_panic("array index out of bounds");return (size_t)n; }
static lac_str lac_arg(lac_args a,int64_t index) { if(index<0||(uint64_t)index>=a.size)lac_panic("array index out of bounds");return a.data[index]; }
#endif
