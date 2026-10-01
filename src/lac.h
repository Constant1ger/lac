#ifndef LAC_H
#define LAC_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

typedef struct { const char *source, *path; size_t pos; int line, col; } Lexer;
typedef struct { int kind, line, col; char *text; } Token;
enum { T_EOF=256, T_ID, T_NUM, T_STR };
_Noreturn void fail(const char *path, int line, int col, const char *fmt, ...);
void *alloc(size_t size);
char *slice(const char *s, size_t n);
Token lex(Lexer *l);

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct { char *name; Expr *value; } Arg;
struct Expr { int kind, line, col, op; char *name; Expr *left, *right; Arg args[32]; int nargs; };
enum { E_NUM, E_STR, E_NAME, E_CALL, E_FIELD, E_INDEX, E_UNARY, E_BINARY };
struct Stmt { int kind, line, col; char *name, *annotation; Expr *expr; Stmt *next; };
enum { S_BIND, S_EXPR, S_RETURN };
typedef struct { char *name, *type; int mode; } Param;
typedef struct { char *name, *result; Param params[16]; int nparams; Stmt *body; int line; } Function;
typedef struct { Function functions[64]; int nfunctions; } Program;
typedef struct { Lexer lexer; Token token; } Parser;
Program parse(const char *source, const char *path);
Expr *parse_expression_text(const char *source, const char *path);
void free_expression(Expr *e);
void free_program(Program *p);
void generate(Program *p, const char *path, FILE *out, const char *runtime_path);
#endif
