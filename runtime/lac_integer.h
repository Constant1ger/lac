#ifndef LAC_INTEGER_H
#define LAC_INTEGER_H
#include <tommath.h>
#include <math.h>
/* Value semantics: small integers stay inline; only large values own limbs. */
typedef struct { int64_t small; mp_int *big; } lac_int;
static lac_int lac_small(int64_t n) { return (lac_int){n,NULL}; }
static void lac_mp_check(mp_err err) { if(err!=MP_OKAY)lac_panic("bignum allocation or arithmetic failure"); }
static mp_int *lac_mp_new(void) { mp_int *p=lac_alloc(sizeof(*p));lac_mp_check(mp_init(p));return p; }
static void lac_drop_i(lac_int n) { if(n.big){mp_clear(n.big);free(n.big);} }
static lac_int lac_copy_i(lac_int n) {
    if(!n.big)return n;
    lac_int r={0,lac_mp_new()};lac_mp_check(mp_copy(n.big,r.big));return r;
}
static void lac_assign_i(lac_int *dest,lac_int value) { lac_int copy=lac_copy_i(value);lac_drop_i(*dest);*dest=copy; }
static const mp_int *lac_mp_ref(lac_int n,mp_int *scratch) {
    if(n.big)return n.big;
    lac_mp_check(mp_init_i64(scratch,n.small));return scratch;
}
static void lac_mp_unref(lac_int n,mp_int *scratch) { if(!n.big)mp_clear(scratch); }
static int lac_cmp_i(lac_int a,lac_int b) {
    if(!a.big&&!b.big)return (a.small>b.small)-(a.small<b.small);
    mp_int aa,bb;const mp_int *pa=lac_mp_ref(a,&aa),*pb=lac_mp_ref(b,&bb);
    int cmp=(int)mp_cmp(pa,pb);lac_mp_unref(a,&aa);lac_mp_unref(b,&bb);return cmp;
}
static int64_t lac_i64(lac_int n) {
    if(!n.big)return n.small;
    mp_int low,high;lac_mp_check(mp_init_i64(&low,INT64_MIN));lac_mp_check(mp_init_i64(&high,INT64_MAX));
    int fits=mp_cmp(n.big,&low)!=MP_LT&&mp_cmp(n.big,&high)!=MP_GT;mp_clear(&low);mp_clear(&high);
    if(!fits)lac_panic("integer outside signed 64-bit boundary");
    return mp_get_i64(n.big);
}
static uint64_t lac_u64(lac_int n) {
    if(!n.big){if(n.small<0)lac_panic("negative integer at unsigned boundary");return (uint64_t)n.small;}
    if(n.big->sign==MP_NEG||mp_count_bits(n.big)>64)lac_panic("integer outside unsigned 64-bit boundary");
    return mp_get_u64(n.big);
}
static int64_t lac_c_signed(lac_int n,size_t bytes) {
    int64_t x=lac_i64(n);
    if(bytes<8){unsigned bits=(unsigned)(bytes*CHAR_BIT);int64_t max=(INT64_C(1)<<(bits-1))-1;if(x>max||x< -max-1)lac_panic("integer outside C argument range");}
    return x;
}
static uint64_t lac_c_unsigned(lac_int n,size_t bytes) {
    uint64_t x=lac_u64(n);
    if(bytes<8&&x>((UINT64_C(1)<<(bytes*CHAR_BIT))-1))lac_panic("integer outside C argument range");
    return x;
}
static double lac_double_i(lac_int n) { return n.big?mp_get_double(n.big):(double)n.small; }
static lac_int lac_unsigned(uint64_t n) {
    if(n<=INT64_MAX)return lac_small((int64_t)n);
    lac_int r={0,lac_mp_new()};mp_set_u64(r.big,n);return r;
}
static lac_int lac_parse_i(const char *text) {
    errno=0;char *end;long long n=strtoll(text,&end,10);
    if(errno!=ERANGE&&!*end)return lac_small((int64_t)n);
    lac_int r={0,lac_mp_new()};lac_mp_check(mp_read_radix(r.big,text,10));return r;
}
static char *lac_decimal_i(lac_int n) {
    if(!n.big){char buf[32];snprintf(buf,sizeof(buf),"%lld",(long long)n.small);size_t len=strlen(buf)+1;char *s=lac_alloc(len);memcpy(s,buf,len);return s;}
    int size;lac_mp_check(mp_radix_size(n.big,10,&size));char *s=lac_alloc((size_t)size);lac_mp_check(mp_to_radix(n.big,s,(size_t)size,NULL,10));return s;
}
static lac_str lac_fmt_i(lac_int n) { char *s=lac_decimal_i(n);return (lac_str){s,strlen(s)}; }
static void lac_pr_i(lac_int n,int newline) { char *s=lac_decimal_i(n);fputs(s,stdout);free(s);if(newline)fputc('\n',stdout); }
/* Keep the agreed atoi-style prefix parsing, without a numeric width limit. */
static lac_int lac_to_i(lac_str s) {
    size_t pos=0;while(pos<s.size&&isspace((unsigned char)s.data[pos]))pos++;
    int negative=0;if(pos<s.size&&(s.data[pos]=='+'||s.data[pos]=='-'))negative=s.data[pos++]=='-';
    size_t start=pos;while(pos<s.size&&isdigit((unsigned char)s.data[pos]))pos++;
    if(start==pos)return lac_small(0);
    size_t count=pos-start;if(count>SIZE_MAX-2)lac_panic("integer text too large");
    char *text=lac_alloc(count+2);text[0]=negative?'-':'+';memcpy(text+1,s.data+start,count);text[count+1]=0;
    /* LibTomMath accepts minus; omit a leading plus. */
    lac_int r=lac_parse_i(text+(negative?0:1));free(text);return r;
}
static lac_int lac_big_op(lac_int a,lac_int b,char op) {
    mp_int aa,bb;const mp_int *pa=lac_mp_ref(a,&aa),*pb=lac_mp_ref(b,&bb);lac_int r={0,lac_mp_new()};mp_err err;
    switch(op){case '+':err=mp_add(pa,pb,r.big);break;case '-':err=mp_sub(pa,pb,r.big);break;case '*':err=mp_mul(pa,pb,r.big);break;
    case '/':err=mp_div(pa,pb,r.big,NULL);break;default:err=mp_div(pa,pb,NULL,r.big);break;}
    lac_mp_unref(a,&aa);lac_mp_unref(b,&bb);lac_mp_check(err);return r;
}
static lac_int lac_add(lac_int a,lac_int b) {
    if(!a.big&&!b.big&&!((b.small>0&&a.small>INT64_MAX-b.small)||(b.small<0&&a.small<INT64_MIN-b.small)))return lac_small(a.small+b.small);
    return lac_big_op(a,b,'+');
}
static lac_int lac_sub(lac_int a,lac_int b) {
    if(!a.big&&!b.big&&!((b.small>0&&a.small<INT64_MIN+b.small)||(b.small<0&&a.small>INT64_MAX+b.small)))return lac_small(a.small-b.small);
    return lac_big_op(a,b,'-');
}
static lac_int lac_mul(lac_int a,lac_int b) {
    if(!a.big&&!b.big){int64_t x=a.small,y=b.small;int overflow=0;
        if(x>0)overflow=(y>0&&x>INT64_MAX/y)||(y<0&&y<INT64_MIN/x);
        else if(x<0)overflow=(y>0&&x<INT64_MIN/y)||(y<0&&x<INT64_MAX/y);
        if(!overflow)return lac_small(x*y);
    }
    return lac_big_op(a,b,'*');
}
static int lac_zero_i(lac_int n) { return n.big?mp_iszero(n.big):n.small==0; }
static lac_int lac_div(lac_int a,lac_int b) {
    if(lac_zero_i(b))lac_panic("division by zero");
    if(!a.big&&!b.big&&!(a.small==INT64_MIN&&b.small==-1))return lac_small(a.small/b.small);
    return lac_big_op(a,b,'/');
}
static lac_int lac_mod(lac_int a,lac_int b) {
    if(lac_zero_i(b))lac_panic("division by zero");
    if(!a.big&&!b.big){if(a.small==INT64_MIN&&b.small==-1)return lac_small(0);return lac_small(a.small%b.small);}
    return lac_big_op(a,b,'%');
}
static lac_int lac_neg(lac_int a) { return lac_sub(lac_small(0),a); }
#endif
