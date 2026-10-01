#include "lac.h"
_Noreturn void fail(const char *path, int line, int col, const char *fmt, ...) {
    va_list ap; fprintf(stderr, "%s:%d:%d: error: ", path, line, col);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); exit(1);
}
void *alloc(size_t size) { void *p=calloc(1,size); if(!p){fputs("lac: out of memory\n",stderr);exit(1);} return p; }
char *slice(const char *s, size_t n) { char *p=alloc(n+1); memcpy(p,s,n); return p; }
static void advance(Lexer *l) { if(l->source[l->pos]=='\n'){l->line++;l->col=1;}else l->col++; l->pos++; }
Token lex(Lexer *l) {
    const char *s=l->source;
    for(;;) {
        while(s[l->pos] && isspace((unsigned char)s[l->pos])) advance(l);
        if(s[l->pos]=='/' && s[l->pos+1]=='/') { while(s[l->pos] && s[l->pos]!='\n')advance(l); continue; }
        if(s[l->pos]=='/' && s[l->pos+1]=='*') {
            int line=l->line,col=l->col; advance(l);advance(l);
            while(s[l->pos] && !(s[l->pos]=='*'&&s[l->pos+1]=='/'))advance(l);
            if(!s[l->pos])fail(l->path,line,col,"unterminated comment");
            advance(l);advance(l);continue;
        }
        break;
    }
    Token t={0,l->line,l->col,NULL}; size_t start=l->pos; unsigned char c=(unsigned char)s[start];
    if(!c){t.kind=T_EOF;t.text=slice("",0);return t;}
    if(isalpha(c)||c=='_') {
        advance(l);while(isalnum((unsigned char)s[l->pos])||s[l->pos]=='_')advance(l);
        t.kind=T_ID;t.text=slice(s+start,l->pos-start);return t;
    }
    if(isdigit(c)) {
        advance(l);while(isdigit((unsigned char)s[l->pos]))advance(l);
        t.kind=T_NUM;t.text=slice(s+start,l->pos-start);return t;
    }
    if(c=='"') {
        advance(l); start=l->pos;
        while(s[l->pos] && s[l->pos]!='"') {
            if(s[l->pos]=='\\'){advance(l);if(!s[l->pos])break;}
            advance(l);
        }
        if(s[l->pos]!='"')fail(l->path,t.line,t.col,"unterminated string");
        t.kind=T_STR;t.text=slice(s+start,l->pos-start);advance(l);return t;
    }
    if(c>=128)fail(l->path,t.line,t.col,"unexpected non-ASCII character outside comment");
    t.kind=c;t.text=slice(s+start,1);advance(l);return t;
}
