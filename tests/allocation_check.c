/* Link-time allocator instrumentation includes the static LibTomMath library. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#undef main
void *__real_malloc(size_t size);
void *__real_calloc(size_t count,size_t size);
void *__real_realloc(void *p,size_t size);
void __real_free(void *p);
static int64_t live_allocations;
void *__wrap_malloc(size_t size) { void *p=__real_malloc(size);if(p)live_allocations++;return p; }
void *__wrap_calloc(size_t count,size_t size) { void *p=__real_calloc(count,size);if(p)live_allocations++;return p; }
void *__wrap_realloc(void *p,size_t size) {
    void *next=__real_realloc(p,size);
    if(!p&&next)live_allocations++;
    else if(p&&!size&&!next)live_allocations--;
    return next;
}
void __wrap_free(void *p) { if(p)live_allocations--;__real_free(p); }
int lac_program_main(int argc,char **argv);
int main(int argc,char **argv) {
    int64_t baseline=live_allocations; /* MinGW startup can allocate before main. */
    int status=lac_program_main(argc,argv);
    if(live_allocations!=baseline){fprintf(stderr,"allocation imbalance: %lld\n",(long long)(live_allocations-baseline));return 99;}
    return status;
}
