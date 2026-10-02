#include "lac.h"
#include <stdint.h>
#include <errno.h>
#include <limits.h>

typedef struct { int kind, detail, mode; char code[80]; int temp; } Value;
typedef struct { char *name; Value value; int moved; } Symbol;
typedef struct { char *name; char *fields[32]; int nfields; } Structure;
typedef struct { int kind; char name[80]; } Temporary;
typedef struct {
    Program *program; const char *path; FILE *out; Structure structures[32];int nstructures;
    Symbol symbols[128];int nsymbols;Temporary temps[512];int ntemps,counter;
    int entry,return_kind; Function *current;
    Imports *imports; Value it; int has_it; Expr *skip_zero_array;
} Gen;
static _Noreturn void error(Gen *g,Expr *e,const char *msg){fail(g->path,e?e->line:1,e?e->col:1,"%s",msg);}
static void put(Gen *g,const char *fmt,...) {va_list ap;va_start(ap,fmt);vfprintf(g->out,fmt,ap);va_end(ap);}
static Value val(int k,const char *code){Value v={0};v.kind=k;v.detail=-1;v.temp=-1;if(code)snprintf(v.code,sizeof(v.code),"%s",code);return v;}
static int func_index(Gen *g,const char *s){for(int i=0;i<g->program->nfunctions;i++)if(!strcmp(g->program->functions[i].name,s))return i;return -1;}
static int struct_index(Gen *g,const char *s){for(int i=0;i<g->nstructures;i++)if(!strcmp(g->structures[i].name,s))return i;return -1;}
static Symbol *symbol(Gen *g,const char *s){for(int i=g->nsymbols-1;i>=0;i--)if(!strcmp(g->symbols[i].name,s))return &g->symbols[i];return NULL;}
static void add_symbol(Gen *g,char *name,Value v) {
    if(symbol(g,name))fail(g->path,1,1,"duplicate variable '%s' (reassignment is not supported in bootstrap)",name);
    if(g->nsymbols==128)fail(g->path,1,1,"too many locals");
    for(char *p=name;*p;p++)if(isdigit((unsigned char)*p))fail(g->path,1,1,"variable names cannot contain digits");
    g->symbols[g->nsymbols++]=(Symbol){name,v,0};
}
static int type_kind(Gen *g,const char *name,int *detail) {
    if(!name)return K_VOID;
    if(!strcmp(name,"i"))return K_INT;
    if(!strcmp(name,"d"))return K_DOUBLE;
    if(!strcmp(name,"f"))return K_FLOAT;
    if(!strcmp(name,"s")||!strcmp(name,"S"))return K_STR;
    if(!strcmp(name,"b"))return K_BOOL;
    int i=struct_index(g,name);if(i>=0){*detail=i;return K_POINT;}
    fail(g->path,1,1,"unsupported type '%s' in bootstrap",name);return 0;
}
static const char *ctype(Gen *g,int kind,int detail){static char b[64];switch(kind){case K_VOID:return "void";case K_INT:return "int64_t";case K_DOUBLE:return "double";case K_FLOAT:return "float";case K_BOOL:return "int";case K_STR:return "lac_str";case K_ARGS:return "lac_args";case K_ARRAY:return "lac_array";case K_POINT:snprintf(b,sizeof(b),"lac_struct_%d *",detail);return b;default:fail(g->path,1,1,"unsupported function value in type position");return NULL;}}
static Value temporary(Gen *g,int kind,int detail) {
    if(g->ntemps==512)fail(g->path,1,1,"too many expression temporaries");
    Value v=val(kind,NULL);v.detail=detail;snprintf(v.code,sizeof(v.code),"t%d",g->counter++);
    if(kind==K_POINT||kind==K_STR||kind==K_ARRAY){v.temp=g->ntemps;Temporary *t=&g->temps[g->ntemps++];t->kind=kind;snprintf(t->name,sizeof(t->name),"%s",v.code);}
    put(g,"  %s %s",ctype(g,kind,detail),v.code);return v;
}
static void cleanup(Gen *g,int except) {
    for(int i=g->ntemps-1;i>=0;i--)if(i!=except){Temporary *t=&g->temps[i];put(g,"  %s(%s);\n",t->kind==K_STR?"lac_drop_s":t->kind==K_ARRAY?"lac_drop_array":"free",t->name);}
}
static void scoped_cleanup(Gen *g,int start) {
    for(int i=g->ntemps-1;i>=start;i--){Temporary *t=&g->temps[i];put(g,"  %s(%s);\n",t->kind==K_STR?"lac_drop_s":t->kind==K_ARRAY?"lac_drop_array":"free",t->name);}
    g->ntemps=start;
}
static void quote(Gen *g,const char *s,size_t n) {put(g,"\"");for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)s[i];if(c>=128)fail(g->path,1,1,"non-ASCII string is not supported");switch(c){case '"':put(g,"\\\"");break;case '\\':put(g,"\\\\");break;case '\n':put(g,"\\n");break;case '\r':put(g,"\\r");break;case '\t':put(g,"\\t");break;default:if(c<32)put(g,"\\%03o",c);else fputc(c,g->out);}}put(g,"\"");}
static Value emit(Gen *g,Expr *e);
static int uses_it(Expr *e) {
    if(!e)return 0;
    if(e->kind==E_IT)return 1;
    if(uses_it(e->left)||uses_it(e->right)||uses_it(e->third))return 1;
    for(int i=0;i<e->nargs;i++)if(uses_it(e->args[i].value))return 1;
    return 0;
}
static void infer_function(Gen *g,int fi);
static Value probe(Gen *g,Expr *e) {
    Gen *worker=alloc(sizeof(*worker));*worker=*g;worker->out=tmpfile();if(!worker->out)fail(g->path,1,1,"cannot create inference scratch file");
    Value v=emit(worker,e);fclose(worker->out);free(worker);return v;
}
static Value literal(Gen *g,const char *s,size_t n){Value v=temporary(g,K_STR,-1);put(g," = lac_text(");quote(g,s,n);put(g,");\n");return v;}
static Value emit_string(Gen *g,Expr *e) {
    const char *s=e->name;size_t n=strlen(s),count=0;char *piece=alloc(n+1);Value result=val(K_VOID,NULL);
    for(size_t i=0;i<=n;i++) {
        if(i<n&&s[i]=='\\') {
            char c=s[++i];if(!c)error(g,e,"invalid string escape");
            if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c!='{'&&c!='}'&&c!='"'&&c!='\\')error(g,e,"unknown string escape");
            piece[count++]=c;continue;
        }
        if(i==n||s[i]=='{') {
            Value p=literal(g,piece,count);count=0;
            if(result.kind==K_VOID)result=p;else{Value v=temporary(g,K_STR,-1);put(g," = lac_concat(%s,%s);\n",result.code,p.code);result=v;}
            if(i==n)break;
            size_t start=++i;while(i<n&&s[i]!='}')i++;
            if(i==n)error(g,e,"unterminated interpolation");
            char *text=slice(s+start,i-start);Expr *inside=parse_expression_text(text,g->path);Value x=emit(g,inside);free_expression(inside);free(text);
            if(x.kind==K_INT){Value v=temporary(g,K_STR,-1);put(g," = lac_fmt_i(%s);\n",x.code);x=v;}
            else if(x.kind==K_DOUBLE||x.kind==K_FLOAT){Value v=temporary(g,K_STR,-1);put(g," = lac_fmt_d(%s);\n",x.code);x=v;}
            else if(x.kind==K_BOOL){Value v=temporary(g,K_STR,-1);put(g," = lac_text(%s?\"true\":\"false\");\n",x.code);x=v;}
            else if(x.kind!=K_STR)error(g,e,"interpolation requires number, bool or string");
            Value v=temporary(g,K_STR,-1);put(g," = lac_concat(%s,%s);\n",result.code,x.code);result=v;
        }else if(s[i]=='}')error(g,e,"literal brace requires escape");else piece[count++]=s[i];
    }
    free(piece);return result;
}
static void find_structs(Gen *g,Expr *e) {
    if(!e)return;
    if(e->kind==E_CALL&&e->left->kind==E_NAME&&e->nargs&&e->args[0].name){
        char *name=e->left->name;int idx=struct_index(g,name);
        if(!isupper((unsigned char)name[0]))error(g,e,"stack structure constructors not implemented in bootstrap");
        for(int j=0;j<e->nargs;j++)for(int k=0;k<j;k++)if(e->args[j].name&&e->args[k].name&&!strcmp(e->args[j].name,e->args[k].name))error(g,e,"duplicate constructor field");
        if(idx<0){if(g->nstructures==32)error(g,e,"too many structures");idx=g->nstructures++;g->structures[idx].name=name;g->structures[idx].nfields=e->nargs;for(int j=0;j<e->nargs;j++){if(!e->args[j].name)error(g,e,"all constructor fields must be named");g->structures[idx].fields[j]=e->args[j].name;}}
        else {Structure *s=&g->structures[idx];if(s->nfields!=e->nargs)error(g,e,"constructor field count mismatch");for(int j=0;j<e->nargs;j++)if(!e->args[j].name||strcmp(s->fields[j],e->args[j].name))error(g,e,"constructor field order mismatch");}
    }
    find_structs(g,e->left);find_structs(g,e->right);find_structs(g,e->third);for(int i=0;i<e->nargs;i++)find_structs(g,e->args[i].value);
}
static Foreign *foreign(Gen *g,const char *name) {for(int i=0;i<g->imports->count;i++)if(!strcmp(g->imports->functions[i].name,name))return &g->imports->functions[i];return NULL;}
static Value emit_foreign(Gen *g,Expr *e,Foreign *f) {
    if(f->variadic||f->result==K_UNSUPPORTED)error(g,e,"unsupported C function signature");
    if(e->nargs!=f->nargs)error(g,e,"C function argument count mismatch");
    Value args[16];char adapters[16][80]={{0}};
    for(int j=0;j<e->nargs;j++){
        Expr *argument=e->args[j].value;
        if(argument->kind==E_NAME&&!strcmp(argument->name,"null"))args[j]=val(K_NULL,"NULL");else args[j]=emit(g,argument);
        if(f->params[j]!=args[j].kind)error(g,argument,"C argument type mismatch or unsupported pointer");
        if(args[j].kind==K_STR){snprintf(adapters[j],80,"cstr%d",g->counter++);put(g,"  char *%s = lac_cstr(%s);\n",adapters[j],args[j].code);}
    }
    Value v=val(K_VOID,NULL);if(f->result!=K_VOID){v=temporary(g,f->result,-1);put(g," = ");}else put(g,"  ");
    put(g,"%s(",f->name);
    for(int j=0;j<e->nargs;j++){put(g,"%s",j?",":"");if(args[j].kind==K_NULL)put(g,"NULL");else if(args[j].kind==K_STR)put(g,"%s",adapters[j]);else put(g,"(%s)%s",f->casts[j],args[j].code);}
    put(g,");\n");for(int j=0;j<e->nargs;j++)if(adapters[j][0])put(g,"  free(%s);\n",adapters[j]);return v;
}
static Value emit_call(Gen *g,Expr *e) {
    if(e->left->kind!=E_NAME)error(g,e,"only named function calls supported in bootstrap");
    char *name=e->left->name;
    if(!strcmp(name,"pr")||!strcmp(name,"prl")) {
        if(e->nargs!=1)error(g,e,"print expects one argument");
        Value a=emit(g,e->args[0].value);
        if(a.kind!=K_STR&&a.kind!=K_INT&&a.kind!=K_BOOL&&a.kind!=K_DOUBLE&&a.kind!=K_FLOAT)error(g,e,"unsupported print argument");
        put(g,"  lac_pr_%c(%s,%d);\n",a.kind==K_STR?'s':a.kind==K_BOOL?'b':a.kind==K_DOUBLE?'d':a.kind==K_FLOAT?'f':'i',a.code,!strcmp(name,"prl"));return val(K_VOID,NULL);
    }
    if((!strcmp(name,"d")||!strcmp(name,"f"))&&func_index(g,name)<0&&!symbol(g,name)) {
        if(e->nargs!=1)error(g,e,"numeric conversion expects one argument");
        Value a=emit(g,e->args[0].value);if(a.kind!=K_INT&&a.kind!=K_DOUBLE&&a.kind!=K_FLOAT)error(g,e,"floating conversion requires number");
        int k=!strcmp(name,"d")?K_DOUBLE:K_FLOAT;Value v=temporary(g,k,-1);put(g," = (%s)%s;\n",ctype(g,k,-1),a.code);return v;
    }
    if(!strcmp(name,"i")) {
        if(e->nargs!=1)error(g,e,"i expects one argument");
        Value a=emit(g,e->args[0].value);if(a.kind==K_INT)return a;
        if(a.kind!=K_STR)error(g,e,"i conversion requires string or integer");
        Value v=temporary(g,K_INT,-1);put(g," = lac_to_i(%s);\n",a.code);return v;
    }
    int idx=struct_index(g,name);
    if(idx>=0) {
        Structure *s=&g->structures[idx];Value args[32];for(int j=0;j<e->nargs;j++)args[j]=emit(g,e->args[j].value);
        if(e->nargs==1&&!e->args[0].name&&args[0].kind==K_POINT) {
            if(args[0].detail!=idx||args[0].mode!='&')error(g,e,"copy constructor requires & of same structure");
            Value v=temporary(g,K_POINT,idx);put(g," = lac_alloc(sizeof(*%s));\n  *%s = *%s;\n",v.code,v.code,args[0].code);return v;
        }
        if(e->nargs!=s->nfields)error(g,e,"constructor argument count mismatch");
        for(int j=0;j<e->nargs;j++)if(args[j].kind!=K_INT)error(g,e,"bootstrap structure fields must be integers");
        Value v=temporary(g,K_POINT,idx);put(g," = lac_alloc(sizeof(*%s));\n",v.code);
        for(int j=0;j<e->nargs;j++)put(g,"  %s->f%d = %s;\n",v.code,j,args[j].code);
        return v;
    }
    int fi=func_index(g,name);Symbol *alias=symbol(g,name);
    if(alias){if(alias->value.kind!=K_FUNC)error(g,e,"value is not callable");fi=alias->value.detail;}
    if(fi<0){Foreign *f=foreign(g,name);if(f)return emit_foreign(g,e,f);error(g,e,"unknown function");}
    Function *f=&g->program->functions[fi];
    if(fi==g->entry)error(g,e,"entry function cannot be called in bootstrap");
    if(e->nargs!=f->nparams)error(g,e,"function argument count mismatch");
    Value args[16];
    for(int j=0;j<e->nargs;j++){
        args[j]=emit(g,e->args[j].value);int d=-1,k=type_kind(g,f->params[j].type,&d);
        if(k!=args[j].kind||(k==K_POINT&&d!=args[j].detail))fail(g->path,e->args[j].value->line,e->args[j].value->col,"function '%s' argument %d: expected %s, got %s",name,j+1,f->params[j].type,ctype(g,args[j].kind,args[j].detail));
        if(f->params[j].mode!=args[j].mode)error(g,e,"function argument borrow mode mismatch");
        for(int n=0;n<j;n++)if(!strcmp(args[n].code,args[j].code)&&(args[n].mode=='*'||args[j].mode=='*'||!args[j].mode))error(g,e,"conflicting simultaneous arguments to same object");
        if(k==K_POINT&&!f->params[j].mode)error(g,e,"owned function parameters not implemented in bootstrap");
    }
    infer_function(g,fi);int d=f->result_detail,k=f->result_kind;Value v=val(K_VOID,NULL);
    if(k!=K_VOID){v=temporary(g,k,d);put(g," = ");}else put(g,"  ");
    put(g,"lac_fn_%d(",fi);for(int j=0;j<e->nargs;j++)put(g,"%s%s",j?",":"",args[j].code);put(g,");\n");return v;
}
static Value emit(Gen *g,Expr *e) {
    switch(e->kind) {
    case E_NUM:{
        size_t n=strlen(e->name);int suffix=e->name[n-1];
        if(suffix=='d'||suffix=='f'||strchr(e->name,'.')||strchr(e->name,'e')||strchr(e->name,'E')) {
            char *number=slice(e->name,n-((suffix=='d'||suffix=='f')?1:0));errno=0;char *end;double x=strtod(number,&end);
            if(errno==ERANGE||*end)error(g,e,"invalid floating literal");
            free(number);
            Value v=temporary(g,suffix=='f'?K_FLOAT:K_DOUBLE,-1);put(g," = %.17g;\n",x);return v;
        }
        errno=0;char *end;strtoll(e->name,&end,10);if(errno==ERANGE||*end)error(g,e,"literal exceeds bootstrap int64 range; bignum not implemented");Value v=temporary(g,K_INT,-1);put(g," = INT64_C(%s);\n",e->name);return v;}
    case E_STR:return emit_string(g,e);
    case E_NAME:{if(!strcmp(e->name,"true")||!strcmp(e->name,"false"))return val(K_BOOL,!strcmp(e->name,"true")?"1":"0");Symbol *s=symbol(g,e->name);if(s){if(s->moved)error(g,e,"use after move");return s->value;}int fi=func_index(g,e->name);if(fi>=0){Value v=val(K_FUNC,NULL);v.detail=fi;return v;}Foreign *f=foreign(g,e->name);if(f){Expr call={0};call.kind=E_CALL;call.left=e;call.line=e->line;call.col=e->col;return emit_foreign(g,&call,f);}error(g,e,"unknown variable");break;}
    case E_CALL:return emit_call(g,e);
    case E_INDEX:{
        if(e->left->kind==E_NAME&&(!strcmp(e->left->name,"i")||!strcmp(e->left->name,"d")||!strcmp(e->left->name,"f"))) {
            int kind=type_kind(g,e->left->name,&(int){-1});Value size=emit(g,e->right);if(size.kind!=K_INT)error(g,e,"array size requires integer");
            Value v=temporary(g,K_ARRAY,kind);put(g," = lac_new_array(%s,sizeof(%s),%d);\n",size.code,ctype(g,kind,-1),e!=g->skip_zero_array);return v;
        }
        Value a=emit(g,e->left),b=emit(g,e->right);if(b.kind!=K_INT)error(g,e,"array index requires integer");
        if(a.kind==K_ARRAY){Value v=temporary(g,a.detail,-1);put(g," = ((%s*)%s.data)[lac_index(%s,%s)];\n",ctype(g,a.detail,-1),a.code,a.code,b.code);return v;}
        if(a.kind!=K_ARGS)error(g,e,"indexing requires array");
        Value v=val(K_STR,NULL);snprintf(v.code,sizeof(v.code),"a%d",g->counter++);put(g,"  lac_str %s = lac_arg(%s,%s);\n",v.code,a.code,b.code);return v;}
    case E_FIELD:{
        if(!strcmp(e->name,"pr")||!strcmp(e->name,"prl")||!strcmp(e->name,"d")||!strcmp(e->name,"f")) {
            Expr name={0},call={0};name.kind=E_NAME;name.name=e->name;call.kind=E_CALL;call.left=&name;call.nargs=1;call.args[0].value=e->left;call.line=e->line;call.col=e->col;return emit_call(g,&call);
        }
        Value a=emit(g,e->left);if(!strcmp(e->name,"i")){if(a.kind!=K_STR)error(g,e,".i requires string");Value v=temporary(g,K_INT,-1);put(g," = lac_to_i(%s);\n",a.code);return v;}
        if(!strcmp(e->name,"sz")&&(a.kind==K_STR||a.kind==K_ARGS||a.kind==K_ARRAY)){Value v=temporary(g,K_INT,-1);put(g," = (int64_t)%s.size;\n",a.code);return v;}
        if(a.kind!=K_POINT)error(g,e,"field access requires structure");
        Structure *s=&g->structures[a.detail];int j;for(j=0;j<s->nfields;j++)if(!strcmp(s->fields[j],e->name))break;if(j==s->nfields)error(g,e,"unknown structure field");Value v=temporary(g,K_INT,-1);put(g," = %s->f%d;\n",a.code,j);return v;}
    case E_UNARY:{Value a=emit(g,e->left);if(e->op=='&'||e->op=='*'){if(a.kind!=K_POINT)error(g,e,"bootstrap borrowing requires structure");if(e->left->kind!=E_NAME)error(g,e,"borrow requires named object");a.mode=e->op;return a;}
        if(e->op=='!'){if(a.kind!=K_BOOL)error(g,e,"! requires bool");Value v=temporary(g,K_BOOL,-1);put(g," = !%s;\n",a.code);return v;}
        if(a.kind==K_DOUBLE||a.kind==K_FLOAT){Value v=temporary(g,a.kind,-1);put(g," = -%s;\n",a.code);return v;}
        if(a.kind!=K_INT)error(g,e,"negation requires integer");
        Value v=temporary(g,K_INT,-1);put(g," = lac_sub(0,%s);\n",a.code);return v;}
    case E_BINARY:{Value a=emit(g,e->left);
        if(e->op=='&'||e->op=='|'){if(a.kind!=K_BOOL)error(g,e,"logical operation requires bool");Value v=temporary(g,K_BOOL,-1);put(g," = %s;\n  if(%s%s) {\n",a.code,e->op=='|'?"!":"",v.code);int before=g->ntemps;Value b=emit(g,e->right);if(b.kind!=K_BOOL)error(g,e,"logical operation requires bool");put(g,"  %s = %s;\n",v.code,b.code);for(int i=g->ntemps-1;i>=before;i--)put(g,"  %s(%s);\n",g->temps[i].kind==K_STR?"lac_drop_s":"free",g->temps[i].name);g->ntemps=before;put(g,"  }\n");return v;}
        Value b=emit(g,e->right);if(a.kind!=b.kind||(a.kind==K_POINT&&a.detail!=b.detail))error(g,e,"comparison or arithmetic type mismatch");
        if(a.kind==K_POINT&&(e->op=='#'||e->op=='~')){Value v=temporary(g,K_BOOL,-1);put(g," = ");if(e->op=='#')put(g,"%s == %s",a.code,b.code);else{Structure *s=&g->structures[a.detail];for(int j=0;j<s->nfields;j++)put(g,"%s(%s->f%d == %s->f%d)",j?" && ":"",a.code,j,b.code,j);}put(g,";\n");return v;}
        if(a.kind==K_INT){const char *fn=e->op=='+'?"lac_add":e->op=='-'?"lac_sub":e->op=='*'?"lac_mul":e->op=='/'?"lac_div":e->op=='%'?"lac_mod":NULL;
            Value v=temporary(g,fn?K_INT:K_BOOL,-1);if(fn)put(g," = %s(%s,%s);\n",fn,a.code,b.code);else if(e->op=='>'||e->op=='<')put(g," = %s %c %s;\n",a.code,e->op,b.code);else if(e->op=='~'||e->op=='#')put(g," = %s == %s;\n",a.code,b.code);else error(g,e,"unsupported integer operator");return v;}
        if(a.kind==K_DOUBLE||a.kind==K_FLOAT) {
            if(e->op=='#')error(g,e,"# is forbidden for floating values");
            int comparison=e->op=='>'||e->op=='<'||e->op=='~';
            if(!comparison&&e->op!='+'&&e->op!='-'&&e->op!='*'&&e->op!='/')error(g,e,"unsupported floating operator");
            Value v=temporary(g,comparison?K_BOOL:a.kind,-1);if(e->op=='~')put(g," = %s == %s;\n",a.code,b.code);else put(g," = %s %c %s;\n",a.code,e->op,b.code);return v;
        }
        if(a.kind==K_BOOL&&e->op=='^'){Value v=temporary(g,K_BOOL,-1);put(g," = %s != %s;\n",a.code,b.code);return v;}
        error(g,e,"unsupported binary operation");break;}
    case E_IT:if(!g->has_it)error(g,e,"implicit element is only available in fe/fld");return g->it;
    case E_COND:{
        Value condition=emit(g,e->left);if(condition.kind!=K_BOOL)error(g,e,"condition requires bool");
        Value yes=probe(g,e->right),no=probe(g,e->third);if(yes.kind!=no.kind||yes.detail!=no.detail)error(g,e,"conditional branch type mismatch");
        if(yes.kind!=K_INT&&yes.kind!=K_DOUBLE&&yes.kind!=K_FLOAT&&yes.kind!=K_BOOL)error(g,e,"conditional currently requires scalar branches");
        Value result=temporary(g,yes.kind,yes.detail);put(g," = 0;\n  if(%s) {\n",condition.code);int start=g->ntemps;
        yes=emit(g,e->right);put(g,"  %s = %s;\n",result.code,yes.code);scoped_cleanup(g,start);put(g,"  } else {\n");
        no=emit(g,e->third);put(g,"  %s = %s;\n",result.code,no.code);scoped_cleanup(g,start);put(g,"  }\n");return result;
    }
    case E_FE:case E_FOLD:{
        Expr *previous_skip=g->skip_zero_array;
        int needs_element=uses_it(e->right);
        if(e->kind==E_FE&&e->op&&!needs_element&&e->left->kind==E_INDEX)g->skip_zero_array=e->left;
        Value array=emit(g,e->left);if(array.kind!=K_ARRAY)error(g,e,"fe/fld requires numeric array");
        g->skip_zero_array=previous_skip;
        Value result=array;int nsymbols=g->nsymbols;Value previous=g->it;int previous_has=g->has_it;
        if(e->kind==E_FOLD) {
            Value initial=e->third?emit(g,e->third):val(array.detail,"0");
            if(initial.kind!=K_INT&&initial.kind!=K_DOUBLE&&initial.kind!=K_FLOAT)error(g,e,"fold accumulator must be numeric");
            result=temporary(g,initial.kind,-1);put(g," = %s;\n",initial.code);add_symbol(g,e->name,result);
        }
        int start=g->ntemps;char index[32];snprintf(index,sizeof(index),"index%d",g->counter++);
        put(g,"  for(size_t %s=0;%s<%s.size;%s++) {\n",index,index,array.code,index);
        if(needs_element){g->it=temporary(g,array.detail,-1);put(g," = ((%s*)%s.data)[%s];\n",ctype(g,array.detail,-1),array.code,index);}
        else g->it=val(array.detail,"0");
        g->has_it=1;
        Value operation=emit(g,e->right);
        if(e->kind==E_FOLD){if(operation.kind!=result.kind)error(g,e,"fold operation must return accumulator type");put(g,"  %s = %s;\n",result.code,operation.code);}
        else if(e->op){if(operation.kind!=array.detail)error(g,e,"fe result must match array element type");put(g,"  ((%s*)%s.data)[%s] = %s;\n",ctype(g,array.detail,-1),array.code,index,operation.code);}
        scoped_cleanup(g,start);put(g,"  }\n");g->nsymbols=nsymbols;g->it=previous;g->has_it=previous_has;return result;
    }
    }
    return val(K_VOID,NULL);
}
static void parameters(Gen *g,int fi) {
    Function *f=&g->program->functions[fi];g->current=f;g->nsymbols=g->ntemps=g->counter=g->has_it=0;
    if(fi==g->entry){if(f->nparams)add_symbol(g,f->params[0].name,val(K_ARGS,"arg0"));}
    else for(int j=0;j<f->nparams;j++){int d=-1,k=type_kind(g,f->params[j].type,&d);if(k==K_VOID)fail(g->path,f->line,1,"parameter type is required outside entry");char code[32];snprintf(code,sizeof(code),"arg%d",j);Value v=val(k,code);v.detail=d;v.mode=f->params[j].mode;add_symbol(g,f->params[j].name,v);}
}
static char *binding_source(Expr *e) {if(e->kind==E_NAME)return e->name;if(e->kind==E_FE)return binding_source(e->left);return NULL;}
static void binding(Gen *g,Stmt *s,Value v) {
    if(v.kind==K_VOID)error(g,s->expr,"cannot bind void result");
    if((v.kind==K_INT||v.kind==K_BOOL||v.kind==K_DOUBLE||v.kind==K_FLOAT)&&isupper((unsigned char)s->name[0]))error(g,s->expr,"heap primitives not implemented in bootstrap");
    if(v.kind==K_POINT&&!isupper((unsigned char)s->name[0]))error(g,s->expr,"heap structure owner name must start uppercase");
    if(v.mode&&v.kind==K_POINT)error(g,s->expr,"stored borrowing not supported in bootstrap");
    char *source=binding_source(s->expr);
    if(source&&(v.kind==K_POINT||v.kind==K_ARRAY||(v.kind==K_STR&&v.temp>=0))){Symbol *old=symbol(g,source);if(old)old->moved=1;}
    add_symbol(g,s->name,v);
}
static void infer_function(Gen *g,int fi) {
    Function *f=&g->program->functions[fi];if(f->result_kind>=0)return;
    if(f->inferring)fail(g->path,f->line,1,"recursive function requires declared return type");
    if(f->result){f->result_kind=type_kind(g,f->result,&f->result_detail);return;}
    f->inferring=1;Gen *worker=alloc(sizeof(*worker));*worker=*g;worker->out=tmpfile();
    if(!worker->out)fail(g->path,1,1,"cannot create type inference scratch file");
    parameters(worker,fi);
    int kind=K_VOID,detail=-1,have_return=0;
    for(Stmt *s=f->body;s;s=s->next){Value v=emit(worker,s->expr);if(s->kind==S_BIND)binding(worker,s,v);
        if(s->kind==S_RETURN){if(have_return&&(v.kind!=kind||v.detail!=detail))error(g,s->expr,"inconsistent return types");kind=v.kind;detail=v.detail;have_return=1;}}
    fclose(worker->out);free(worker);f->result_kind=kind;f->result_detail=detail;f->inferring=0;
}
static void signature(Gen *g,int fi,int prototype) {
    Function *f=&g->program->functions[fi];int d=f->result_detail,k=f->result_kind;
    put(g,"static %s lac_fn_%d(",ctype(g,k,d),fi);
    if(fi==g->entry)put(g,"lac_args arg0");
    else if(!f->nparams)put(g,"void");
    else for(int j=0;j<f->nparams;j++){int detail=-1;int kind=type_kind(g,f->params[j].type,&detail);if(kind==K_VOID)fail(g->path,f->line,1,"parameter type is required outside entry");if(kind==K_POINT&&f->params[j].mode=='&')put(g,"%sconst lac_struct_%d *arg%d",j?",":"",detail,j);else put(g,"%s%s arg%d",j?",":"",ctype(g,kind,detail),j);}
    put(g,")%s\n",prototype?";":" {");
}
void generate(Program *p,const char *path,FILE *out,const char *runtime_path) {
    Gen g={0};g.program=p;g.path=path;g.out=out;g.entry=-1;g.imports=load_imports(p,path);
    for(int i=0;i<p->nfunctions;i++) {
        for(int j=0;j<i;j++)if(!strcmp(p->functions[i].name,p->functions[j].name))fail(path,p->functions[i].line,1,"duplicate function");
        if(!p->functions[i].assigned&&(!strcmp(p->functions[i].name,"main")||!strcmp(p->functions[i].name,"m"))){if(g.entry>=0)fail(path,1,1,"main and m cannot coexist");g.entry=i;}
        for(Stmt *s=p->functions[i].body;s;s=s->next)find_structs(&g,s->expr);
    }
    if(!p->nfunctions)fail(path,1,1,"program has no functions");
    if(g.entry<0)for(int i=0;i<p->nfunctions;i++)if(!p->functions[i].assigned){g.entry=i;break;}
    if(g.entry<0)fail(path,1,1,"program has no standalone entry function");
    Function *entry=&p->functions[g.entry];if(entry->nparams>1)fail(path,entry->line,1,"entry accepts at most one argv parameter");
    put(&g,"/* generated by lac bootstrap; not complete 0.0.1 */\n#include ");quote(&g,runtime_path,strlen(runtime_path));put(&g,"\n");
    for(int i=0;i<p->nimports;i++)put(&g,"#include <%s>\n",p->imports[i]);
    for(int i=0;i<g.nstructures;i++){put(&g,"typedef struct {\n");for(int j=0;j<g.structures[i].nfields;j++)put(&g,"  int64_t f%d;\n",j);put(&g,"} lac_struct_%d;\n",i);}
    for(int i=0;i<p->nfunctions;i++)infer_function(&g,i);
    for(int i=0;i<p->nfunctions;i++)signature(&g,i,1);
    for(int i=0;i<p->nfunctions;i++) {
        Function *f=&p->functions[i];parameters(&g,i);g.return_kind=f->result_kind;signature(&g,i,0);
        if(i==g.entry)put(&g,"  (void)arg0;\n");
        else for(int j=0;j<f->nparams;j++)put(&g,"  (void)arg%d;\n",j);
        int returned=0;
        for(Stmt *s=f->body;s;s=s->next){if(returned)fail(path,s->line,s->col,"unreachable statement");Value v=emit(&g,s->expr);
            if(s->kind==S_BIND)binding(&g,s,v);
            else if(s->kind==S_RETURN){if(v.kind!=g.return_kind||v.detail!=f->result_detail)error(&g,s->expr,"return type mismatch");if(v.kind==K_POINT&&v.mode)error(&g,s->expr,"cannot return a borrowed object as owner");if(s->next)fail(path,s->line,s->col,"bootstrap return must end function");cleanup(&g,v.temp);put(&g,"  return %s;\n",v.code);returned=1;}
        }
        if(!returned){if(g.return_kind!=K_VOID)fail(path,f->line,1,"missing return value");cleanup(&g,-1);}put(&g,"}\n");
    }
    int k=entry->result_kind;if(k!=K_VOID&&k!=K_INT)fail(path,entry->line,1,"entry must return integer or no value");
    put(&g,"int main(int argc,char **argv) {\n  lac_args args;args.size=(size_t)(argc-1);args.data=lac_alloc(args.size*sizeof(*args.data));\n  for(size_t i=0;i<args.size;i++){args.data[i].data=argv[i+1];args.data[i].size=strlen(argv[i+1]);}\n");
    if(k==K_VOID)put(&g,"  lac_fn_%d(args);\n  free(args.data);return 0;\n}\n",g.entry);else put(&g,"  int64_t result=lac_fn_%d(args);\n  free(args.data);return (int)result;\n}\n",g.entry);
    free_imports(g.imports);
}
