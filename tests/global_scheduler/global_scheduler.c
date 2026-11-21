#include "test.h"
#include <mint/osbind.h>
void test_global_scheduler(void);
void test_global_scheduler(void)
{
#if defined(TEST_WITH_NATIVE_SCHED) && defined(TEST_WITH_USER_ASPACE)
    long h;
    ptest_begin("global_scheduler");
    h=Fopen("C:\\GLOBALOK.ON",0);
    ptest_assert_msg(h>=0,"AUTO user launch returned, later independent entry ran child, retired PD reused and waits consumed once");
    if(h>=0) (void)Fclose(h);
    ptest_pass();
#else
    (void)Cconws("  global_scheduler... SKIP (native MMU scheduler disabled)\r\n");
#endif
}
