#include "lac.h"
static void next(Parser *p) { free(p->token.text);p->token=lex(&p->lexer); }
static int accept(Parser *p,int k) { if(p->token.kind!=k)return 0;next(p);return 1; }
static void expect(Parser *p,int k) { if(!accept(p,k))fail(p->lexer.path,p->token.line,p->token.col,"expected '%c', got '%s'",k,p->token.text); }
static char *name(Parser *p) { if(p->token.kind!=T_ID)fail(p->lexer.path,p->token.line,p->token.col,"expected name");char *s=slice(p->token.text,strlen(p->token.text));next(p);return s; }
static Expr *expression(Parser *p,int min);
static Stmt *block(Parser *p);
static Expr *node(Parser *p,int kind) { Expr *e=alloc(sizeof(*e));e->kind=kind;e->line=p->token.line;e->col=p->token.col;return e; }
static int prec(int op) { switch(op){case '|':return 1;case '^':return 2;case '&':return 3;case '#':case '~':case '>':case '<':return 4;case '+':case '-':return 5;case '*':case '/':case '%':return 6;default:return 0;} }
static Expr *lambda(Parser *p) {
    if(!p->program)fail(p->lexer.path,p->token.line,p->token.col,"lambda is not allowed in interpolation");
    if(p->program->nfunctions==64)fail(p->lexer.path,p->token.line,p->token.col,"too many functions");
    Expr *e=node(p,E_LAMBDA);e->op=p->program->nfunctions++;
    Function *f=&p->program->functions[e->op];char name[32];snprintf(name,sizeof(name),"_lambda_%d",e->op);
    f->name=slice(name,strlen(name));f->line=e->line;f->assigned=1;f->result_kind=f->result_detail=-1;f->body=block(p);return e;
}
static Expr *primary(Parser *p) {
    Expr *e;
    if(p->token.kind==T_NUM||p->token.kind==T_STR||p->token.kind==T_ID){
        int k=p->token.kind;e=node(p,k==T_NUM?E_NUM:k==T_STR?E_STR:E_NAME);e->name=slice(p->token.text,strlen(p->token.text));next(p);
    } else if(p->token.kind=='{')e=lambda(p);
    else if(accept(p,'(')){if(accept(p,')'))e=lambda(p);else{e=expression(p,1);expect(p,')');}}
    else if(accept(p,'.')) {
        e=alloc(sizeof(*e));e->kind=E_IT;e->line=p->token.line;e->col=p->token.col;
        if(p->token.kind==T_ID){Expr *f=node(p,E_FIELD);f->left=e;f->name=name(p);e=f;}
    }
    else if(accept(p,'+')) {
        e=alloc(sizeof(*e));e->kind=E_BINARY;e->op='+';e->line=p->token.line;e->col=p->token.col;
        e->left=alloc(sizeof(*e));e->left->kind=E_IT;e->left->line=e->line;e->left->col=e->col;e->right=primary(p);return e;
    }
    else if(p->token.kind=='&'||p->token.kind=='*'||p->token.kind=='!'||p->token.kind=='-') {
        e=node(p,E_UNARY);e->op=p->token.kind;next(p);e->left=primary(p);return e;
    } else fail(p->lexer.path,p->token.line,p->token.col,"expected expression, got '%s'",p->token.text);
    for(;;) {
        if(accept(p,'(')) {
            Expr *call=alloc(sizeof(*call));call->kind=E_CALL;call->left=e;call->line=e->line;call->col=e->col;
            if(!accept(p,')'))do {
                if(call->nargs==32)fail(p->lexer.path,e->line,e->col,"too many arguments");
                Expr *v=expression(p,1);Arg *a=&call->args[call->nargs++];
                if(accept(p,':')){
                    if(v->kind!=E_NAME||call->left->kind!=E_NAME)fail(p->lexer.path,v->line,v->col,"expected structure field declaration");
                    call->kind=E_STRUCT;call->op++;a->name=v->name;v->name=NULL;free(v);
                    v=node(p,E_NAME);v->name=name(p);
                }
                else if(accept(p,'=')){if(v->kind!=E_NAME)fail(p->lexer.path,v->line,v->col,"expected field name");a->name=v->name;v->name=NULL;free(v);v=expression(p,1);}
                a->value=v;
                if(accept(p,')'))break;
                expect(p,',');
            }while(1);
            if(call->kind==E_STRUCT&&call->op!=call->nargs)fail(p->lexer.path,call->line,call->col,"all structure fields require name:type");
            e=call;
        }else if(accept(p,'.')){Expr *f=alloc(sizeof(*f));f->kind=E_FIELD;f->left=e;f->name=name(p);f->line=e->line;f->col=e->col;e=f;}
        else if(accept(p,'[')){Expr *i=alloc(sizeof(*i));i->kind=E_INDEX;i->left=e;i->right=expression(p,1);i->line=e->line;i->col=e->col;expect(p,']');e=i;}
        else if(p->token.kind=='{') {
            Expr *method=e,*receiver=NULL,*initial=NULL;char *acc=NULL;int fold=0;
            if(method->kind==E_FIELD&&!strcmp(method->name,"fe"))receiver=method->left;
            else if(method->kind==E_CALL&&method->left->kind==E_FIELD&&!strcmp(method->left->name,"fld")) {
                fold=1;receiver=method->left->left;
                if(method->nargs!=1)fail(p->lexer.path,method->line,method->col,"fld expects one accumulator declaration");
                Arg *a=&method->args[0];
                if(a->name){acc=a->name;a->name=NULL;initial=a->value;a->value=NULL;}
                else if(a->value->kind==E_NAME){acc=a->value->name;a->value->name=NULL;}
                else fail(p->lexer.path,method->line,method->col,"fld requires accumulator name");
            }else fail(p->lexer.path,method->line,method->col,"unsupported trailing block");
            Expr *loop=node(p,fold?E_FOLD:E_FE);loop->left=receiver;loop->third=initial;loop->name=acc;
            if(fold)method->left->left=NULL;else method->left=NULL;
            free_expression(method);expect(p,'{');loop->op=accept(p,'@');loop->right=expression(p,1);expect(p,'}');e=loop;
        }
        else break;
    }
    return e;
}
static Expr *expression(Parser *p,int min) {
    Expr *e=primary(p);
    while(prec(p->token.kind)>=min) {
        int op=p->token.kind,pr=prec(op);Expr *b=node(p,E_BINARY);next(p);b->op=op;b->left=e;b->right=expression(p,pr+1);e=b;
    }
    if(min==1&&accept(p,'?')){Expr *c=node(p,E_COND);c->left=e;c->right=expression(p,1);expect(p,':');c->third=expression(p,1);e=c;}
    return e;
}
static Stmt *block(Parser *p) {
    expect(p,'{');Stmt *first=NULL,**tail=&first;
    while(!accept(p,'}')) {
        if(p->token.kind==T_EOF)fail(p->lexer.path,p->token.line,p->token.col,"unterminated block");
        if(accept(p,';'))continue;
        Stmt *s=alloc(sizeof(*s));s->line=p->token.line;s->col=p->token.col;
        if(accept(p,'@')){s->kind=S_RETURN;s->expr=expression(p,1);}
        else {
            Expr *e=expression(p,1);
            if(accept(p,'=')){
                if(e->kind!=E_NAME)fail(p->lexer.path,e->line,e->col,"assignment requires a variable name");
                s->kind=S_BIND;s->name=e->name;e->name=NULL;free(e);s->expr=expression(p,1);
            }else{s->kind=S_EXPR;s->expr=e;}
        }
        *tail=s;tail=&s->next;
    }
    return first;
}
Program parse(const char *source,const char *path) {
    Program program={0};Parser p={{source,path,0,1,1},{0,0,0,NULL},&program};next(&p);
    while(p.token.kind!=T_EOF) {
        if(accept(&p,';'))continue;
        if(p.token.kind==T_ID&&!strcmp(p.token.text,"import")) {
            next(&p);if(program.nimports==32)fail(path,p.token.line,p.token.col,"too many imports");
            char *header;
            if(p.token.kind==T_STR){header=slice(p.token.text,strlen(p.token.text));next(&p);}
            else {char *base=name(&p);expect(&p,'.');char *ext=name(&p);header=alloc(strlen(base)+strlen(ext)+2);sprintf(header,"%s.%s",base,ext);free(base);free(ext);}
            program.imports[program.nimports++]=header;continue;
        }
        if(program.nfunctions==64)fail(path,p.token.line,p.token.col,"too many functions");
        Function *f=&program.functions[program.nfunctions++];f->line=p.token.line;f->result_kind=-1;f->result_detail=-1;
        if(p.token.kind==T_ID){f->name=name(&p);f->assigned=accept(&p,'=');}
        else {char anon[32];snprintf(anon,sizeof(anon),"_anonymous_%d",program.nfunctions);f->name=slice(anon,strlen(anon));}
        if(p.token.kind!='{'){
        expect(&p,'(');
        if(!accept(&p,')'))do {
            if(f->nparams==16)fail(path,p.token.line,p.token.col,"too many parameters");
            Param *a=&f->params[f->nparams++];a->name=name(&p);
            if(p.token.kind=='&'||p.token.kind=='*'){a->mode=p.token.kind;next(&p);a->type=name(&p);}
            else if(accept(&p,':'))a->type=name(&p);
            if(accept(&p,')'))break;
            expect(&p,',');
        }while(1);
        if(accept(&p,':'))f->result=name(&p);
        }
        f->body=block(&p);
    }
    free(p.token.text);return program;
}
Expr *parse_expression_text(const char *source,const char *path) {
    Parser p={{source,path,0,1,1},{0,0,0,NULL},NULL};next(&p);Expr *e=expression(&p,1);
    if(p.token.kind!=T_EOF)fail(path,p.token.line,p.token.col,"unexpected interpolation token '%s'",p.token.text);
    free(p.token.text);return e;
}
void free_expression(Expr *e) { if(!e)return;free_expression(e->left);free_expression(e->right);free_expression(e->third);for(int i=0;i<e->nargs;i++){free(e->args[i].name);free_expression(e->args[i].value);}free(e->name);free(e); }
void free_program(Program *p) {
    for(int i=0;i<p->nimports;i++)free(p->imports[i]);
    for(int i=0;i<p->nfunctions;i++){Function *f=&p->functions[i];free(f->name);free(f->result);for(int j=0;j<f->nparams;j++){free(f->params[j].name);free(f->params[j].type);}Stmt *s=f->body;while(s){Stmt *n=s->next;free(s->name);free(s->annotation);free_expression(s->expr);free(s);s=n;}}
}
