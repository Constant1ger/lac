#include "lac.h"
#include <clang-c/Index.h>
#ifndef LAC_CLANG_INCLUDE
#define LAC_CLANG_INCLUDE ""
#endif
#ifndef LAC_CLANG_RESOURCE
#define LAC_CLANG_RESOURCE ""
#endif
static char *cx_text(CXString s) {const char *text=clang_getCString(s);char *copy=slice(text?text:"",text?strlen(text):0);clang_disposeString(s);return copy;}
static int ckind(CXType t,int parameter) {
    t=clang_getCanonicalType(t);
    switch(t.kind){
    case CXType_Void:return K_VOID;
    case CXType_Bool:return K_BOOL;
    case CXType_Char_U:case CXType_UChar:case CXType_Char_S:case CXType_SChar:
    case CXType_Short:case CXType_UShort:case CXType_Int:case CXType_UInt:
    case CXType_Long:case CXType_ULong:case CXType_LongLong:case CXType_ULongLong:return K_INT;
    case CXType_Double:return K_DOUBLE;case CXType_Float:return K_FLOAT;
    case CXType_Pointer:{
        CXType p=clang_getCanonicalType(clang_getPointeeType(t));
        if(parameter&&(p.kind==CXType_Char_S||p.kind==CXType_Char_U)&&clang_isConstQualifiedType(p))return K_STR;
        return parameter?K_NULL:K_UNSUPPORTED;
    }
    default:return K_UNSUPPORTED;
    }
}
static int unsigned_type(CXType t) {
    enum CXTypeKind k=clang_getCanonicalType(t).kind;
    return k==CXType_Char_U||k==CXType_UChar||k==CXType_UShort||k==CXType_UInt||k==CXType_ULong||k==CXType_ULongLong;
}
static enum CXChildVisitResult visit(CXCursor cursor,CXCursor parent,CXClientData data) {
    (void)parent;Imports *imports=data;
    if(clang_getCursorKind(cursor)!=CXCursor_FunctionDecl)return CXChildVisit_Recurse;
    char *name=cx_text(clang_getCursorSpelling(cursor));
    for(int i=0;i<imports->count;i++)if(!strcmp(imports->functions[i].name,name)){free(name);return CXChildVisit_Continue;}
    if(imports->count==1024){free(name);return CXChildVisit_Break;}
    Foreign *f=&imports->functions[imports->count++];f->name=name;CXType type=clang_getCursorType(cursor);
    f->result=ckind(clang_getResultType(type),0);f->unsigned_result=unsigned_type(clang_getResultType(type));f->nargs=clang_Cursor_getNumArguments(cursor);f->variadic=clang_isFunctionTypeVariadic(type);
    if(f->nargs<0||f->nargs>16){f->result=K_UNSUPPORTED;f->nargs=0;return CXChildVisit_Continue;}
    for(int i=0;i<f->nargs;i++){CXType p=clang_getCursorType(clang_Cursor_getArgument(cursor,(unsigned)i));f->params[i]=ckind(p,1);f->unsigned_params[i]=unsigned_type(p);f->casts[i]=cx_text(clang_getTypeSpelling(p));}
    return CXChildVisit_Continue;
}
Imports *load_imports(Program *program,const char *path) {
    Imports *imports=alloc(sizeof(*imports));if(!program->nimports)return imports;
    size_t capacity=1;for(int i=0;i<program->nimports;i++)capacity+=strlen(program->imports[i])+16;
    char *source=alloc(capacity);
    for(int i=0;i<program->nimports;i++){
        const char *h=program->imports[i];for(const char *p=h;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='_'&&*p!='.'&&*p!='/'&&*p!='-')fail(path,1,1,"invalid import header");
        strcat(source,"#include <");strcat(source,h);strcat(source,">\n");
    }
    CXIndex index=clang_createIndex(0,0);struct CXUnsavedFile file={"lac_imports.c",source,(unsigned long)strlen(source)};
    char inc[1024];snprintf(inc,sizeof(inc),"-I%s",LAC_CLANG_INCLUDE);
    char resource[1024];const char *resource_dir=getenv("LAC_CLANG_RESOURCE");if(!resource_dir)resource_dir=LAC_CLANG_RESOURCE;
    snprintf(resource,sizeof(resource),"-resource-dir=%s",resource_dir);
    const char *args[]={"-x","c","-std=c11",inc,resource};
    CXTranslationUnit unit=clang_parseTranslationUnit(index,file.Filename,args,5,&file,1,CXTranslationUnit_SkipFunctionBodies);
    if(!unit)fail(path,1,1,"libclang cannot parse imported headers");
    for(unsigned i=0;i<clang_getNumDiagnostics(unit);i++){
        CXDiagnostic diag=clang_getDiagnostic(unit,i);enum CXDiagnosticSeverity severity=clang_getDiagnosticSeverity(diag);
        if(severity>=CXDiagnostic_Error){char *msg=cx_text(clang_formatDiagnostic(diag,clang_defaultDiagnosticDisplayOptions()));fprintf(stderr,"%s\n",msg);free(msg);clang_disposeDiagnostic(diag);fail(path,1,1,"C header import failed");}
        clang_disposeDiagnostic(diag);
    }
    clang_visitChildren(clang_getTranslationUnitCursor(unit),visit,imports);
    clang_disposeTranslationUnit(unit);clang_disposeIndex(index);free(source);return imports;
}
void free_imports(Imports *imports) {for(int i=0;i<imports->count;i++){Foreign *f=&imports->functions[i];free(f->name);for(int j=0;j<f->nargs;j++)free(f->casts[j]);}free(imports);}
