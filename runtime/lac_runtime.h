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
typedef struct { void *data; size_t size; } lac_array;
static void lac_panic(const char *msg) { fprintf(stderr,"lac: %s\n",msg);exit(1); }
static void *lac_alloc(size_t n) { void *p=malloc(n?n:1);if(!p)lac_panic("out of memory");return p; }
static lac_str lac_text(const char *text) { size_t n=strlen(text);char *p=lac_alloc(n);memcpy(p,text,n);return (lac_str){p,n}; }
static lac_str lac_concat(lac_str a,lac_str b) { if(a.size>SIZE_MAX-b.size)lac_panic("string too large");char *p=lac_alloc(a.size+b.size);memcpy(p,a.data,a.size);memcpy(p+a.size,b.data,b.size);return (lac_str){p,a.size+b.size}; }
static lac_str lac_fmt_i(int64_t n) { char b[32];snprintf(b,sizeof(b),"%lld",(long long)n);return lac_text(b); }
static void lac_drop_s(lac_str s) { free((void*)s.data); }
static void lac_pr_s(lac_str s,int newline) { fwrite(s.data,1,s.size,stdout);if(newline)fputc('\n',stdout); }
static void lac_pr_i(int64_t n,int newline) { printf("%lld",(long long)n);if(newline)fputc('\n',stdout); }
static void lac_pr_b(int n,int newline) { fputs(n?"true":"false",stdout);if(newline)fputc('\n',stdout); }
static void lac_pr_d(double n,int newline) { printf("%.17g",n);if(newline)fputc('\n',stdout); }
static void lac_pr_f(float n,int newline) { printf("%.9g",(double)n);if(newline)fputc('\n',stdout); }
static lac_str lac_fmt_d(double n) { char b[64];snprintf(b,sizeof(b),"%.17g",n);return lac_text(b); }
static char *lac_cstr(lac_str s) { if(s.size==SIZE_MAX)lac_panic("string too large");char *p=lac_alloc(s.size+1);memcpy(p,s.data,s.size);p[s.size]=0;return p; }
static lac_array lac_new_array(int64_t n,size_t element_size,int zero) {
    if(n<0||(uint64_t)n>SIZE_MAX/element_size)lac_panic("invalid array size");
    size_t size=(size_t)n;void *data=zero?calloc(size?size:1,element_size):malloc((size?size:1)*element_size);if(!data)lac_panic("out of memory");return (lac_array){data,size};
}
static void lac_drop_array(lac_array a) { free(a.data); }
static size_t lac_index(lac_array a,int64_t n) { if(n<0||(uint64_t)n>=a.size)lac_panic("array index out of bounds");return (size_t)n; }
static lac_str lac_arg(lac_args a,int64_t index) { if(index<0||(uint64_t)index>=a.size)lac_panic("array index out of bounds");return a.data[index]; }
/* Bootstrap slice: reject numbers beyond int64; this is not the final bignum i. */
static int64_t lac_to_i(lac_str s) {
    char *p=lac_alloc(s.size+1);memcpy(p,s.data,s.size);p[s.size]=0;errno=0;char *end;
    long long n=strtoll(p,&end,10);if(errno==ERANGE){free(p);lac_panic("integer exceeds bootstrap int64 range; bignum is not implemented yet");}
    if(end==p)n=0;
    free(p);return (int64_t)n;
}
static int64_t lac_add(int64_t a,int64_t b) { if((b>0&&a>INT64_MAX-b)||(b<0&&a<INT64_MIN-b))lac_panic("bootstrap integer overflow");return a+b; }
static int64_t lac_sub(int64_t a,int64_t b) { if((b>0&&a<INT64_MIN+b)||(b<0&&a>INT64_MAX+b))lac_panic("bootstrap integer overflow");return a-b; }
static int64_t lac_mul(int64_t a,int64_t b) {
    if(a>0){if((b>0&&a>INT64_MAX/b)||(b<0&&b<INT64_MIN/a))lac_panic("bootstrap integer overflow");}
    else if(a<0){if((b>0&&a<INT64_MIN/b)||(b<0&&a<INT64_MAX/b))lac_panic("bootstrap integer overflow");}
    return a*b;
}
static int64_t lac_div(int64_t a,int64_t b) { if(!b)lac_panic("division by zero");if(a==INT64_MIN&&b==-1)lac_panic("bootstrap integer overflow");return a/b; }
static int64_t lac_mod(int64_t a,int64_t b) { if(!b)lac_panic("division by zero");if(a==INT64_MIN&&b==-1)return 0;return a%b; }
#endif
