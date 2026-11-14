/* Formatting sinks retain independent automatic state during reentry. */
#include "emutos.h"
#include "string.h"
#include "stdarg.h"
#include "doprintf.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
typedef struct { char text[128]; unsigned at; BOOL entered, ok; } SINK;
static int format(SINK *sink, const char *fmt, ...);
static void emit(void *context, int character)
{
    SINK *sink = context;
    if (character == '[' && !sink->entered) {
        SINK inner = { {0},0,FALSE,TRUE };
        char text[32];
        sink->entered = TRUE;
        sink->ok = format(&inner,"%s:%04x","inner",0xab) == 10 && !strcmp(inner.text,"inner:00ab");
        sink->ok = sink->ok && sprintf(text,"%u/%s",17U,"nested") == 9 && !strcmp(text,"17/nested");
    }
    if (sink->at+1 >= sizeof(sink->text)) { sink->ok = FALSE; return; }
    sink->text[sink->at++] = character; sink->text[sink->at] = 0;
}
static int format(SINK *sink, const char *fmt, ...)
{
    int count;
    va_list args;
    va_start(args,fmt); count = doprintf_context(emit,sink,fmt,args); va_end(args);
    return count;
}
static SINK legacy;
static void legacy_emit(int character) { emit(&legacy,character); }
static int legacy_format(const char *fmt, ...)
{
    int count;
    va_list args;
    va_start(args,fmt); count = doprintf(legacy_emit,fmt,args); va_end(args);
    return count;
}
int main(void)
{
    SINK outer = { {0},0,FALSE,TRUE };
    char text[32];
    CHECK(format(&outer,"outer[%d]tail",-7) == 13 && !strcmp(outer.text,"outer[-7]tail"));
    CHECK(outer.entered && outer.ok);
    legacy.ok = TRUE;
    CHECK(legacy_format("legacy[%u]",42U) == 10 && !strcmp(legacy.text,"legacy[42]"));
    CHECK(legacy.entered && legacy.ok);
    CHECK(sprintf(text,"%ld:%s",-123L,"done") == 9 && !strcmp(text,"-123:done"));
    return 0;
}
