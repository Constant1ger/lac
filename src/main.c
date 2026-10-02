#ifndef _WIN32
#define _XOPEN_SOURCE 700
#endif
#include "lac.h"
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif
#ifndef LAC_RUNTIME_PATH
#define LAC_RUNTIME_PATH "runtime/lac_runtime.h"
#endif
#ifndef LAC_TOMMATH_LIBRARY
#define LAC_TOMMATH_LIBRARY "-ltommath"
#endif
#ifndef LAC_TOMMATH_INCLUDE
#define LAC_TOMMATH_INCLUDE ""
#endif
static char *read_source(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);if(n<0){fclose(f);return NULL;}rewind(f);
    char *s=alloc((size_t)n+1);if(fread(s,1,(size_t)n,f)!=(size_t)n){fclose(f);free(s);return NULL;}fclose(f);return s;
}
static int same_path(const char *a,const char *b) {
#ifdef _WIN32
    char *aa=_fullpath(NULL,a,0),*bb=_fullpath(NULL,b,0);
    int same=aa&&bb&&!_stricmp(aa,bb);free(aa);free(bb);return same;
#else
    /* Existing source/output aliases are resolved; a new output cannot alias source. */
    char *aa=realpath(a,NULL),*bb=realpath(b,NULL);
    int same=aa&&bb&&!strcmp(aa,bb);free(aa);free(bb);return same;
#endif
}
#ifdef _WIN32
/* _spawnvp builds a command line: quote each path, without involving a shell. */
static char *win_quote(const char *s) {
    char *q=alloc(strlen(s)*2+3),*o=q;*o++='"';
    for(;*s;){size_t n=0;while(*s=='\\'){n++;s++;}if(*s=='"'||!*s)for(size_t i=0;i<n;i++)*o++='\\';for(size_t i=0;i<n;i++)*o++='\\';if(*s=='"')*o++='\\';if(*s)*o++=*s++;}
    *o++='"';*o=0;return q;
}
#endif
static int compile_c(const char *cc,const char *source,const char *output) {
#ifdef _WIN32
    char *src=win_quote(source),*dest=win_quote(output),*command=win_quote(cc);
    char *library=win_quote(LAC_TOMMATH_LIBRARY);char inc[1024];snprintf(inc,sizeof(inc),"-I%s",LAC_TOMMATH_INCLUDE);char *include=win_quote(inc);
    const char *args[]={command,"-std=c11","-O0",include,src,library,"-o",dest,NULL};
    intptr_t status=_spawnvp(_P_WAIT,cc,args);free(src);free(dest);free(command);free(library);free(include);
    if(status==-1){fprintf(stderr,"lac: cannot run C compiler '%s'; set LAC_CC\n",cc);return 1;}return (int)status;
#else
    pid_t pid=fork();if(pid<0)return 1;if(!pid){char inc[1024];snprintf(inc,sizeof(inc),"-I%s",LAC_TOMMATH_INCLUDE);execlp(cc,cc,"-std=c11","-O0",inc,source,LAC_TOMMATH_LIBRARY,"-o",output,(char*)NULL);perror("lac: C compiler");_exit(127);}int status;if(waitpid(pid,&status,0)<0)return 1;return WIFEXITED(status)?WEXITSTATUS(status):1;
#endif
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--version")){puts("lac 0.0.1-dev (bootstrap slice)");return 0;}
    if(argc==2&&!strcmp(argv[1],"--help")){puts("Usage: lac file.l [@ output.exe]\n       lac 'source' [@ output.exe]\nLAC_CC: C compiler executable (default gcc). Generated C is retained as output.exe.c.");return 0;}
    if(argc!=2&&(argc!=4||strcmp(argv[2],"@"))){fputs("Usage: lac file.l [@ output.exe]\n       lac 'source' [@ output.exe]\n",stderr);return 1;}
    const char *output=argc==4?argv[3]:"a.exe";if(!*output){fputs("lac: empty output name\n",stderr);return 1;}
    char *source=read_source(argv[1]);const char *path=source?argv[1]:"<command>";
    if(!source){size_t n=strlen(argv[1]);if(n>=2&&!strcmp(argv[1]+n-2,".l")){fprintf(stderr,"lac: cannot read '%s'\n",argv[1]);return 1;}source=slice(argv[1],n);}
    Program p=parse(source,path);char *generated=alloc(strlen(output)+3);sprintf(generated,"%s.c",output);
    if(same_path(generated,argv[1])||same_path(output,argv[1])){fputs("lac: output would overwrite source\n",stderr);return 1;}
    FILE *f=fopen(generated,"wb");if(!f){fprintf(stderr,"lac: cannot create '%s'\n",generated);return 1;}
    generate(&p,path,f,LAC_RUNTIME_PATH);if(fclose(f)){fputs("lac: cannot finish generated C\n",stderr);return 1;}
    const char *cc=getenv("LAC_CC");if(!cc||!*cc)cc="gcc";int status=compile_c(cc,generated,output);
    free_program(&p);free(source);free(generated);return status?1:0;
}
