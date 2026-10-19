/* Public layouts and the existing rectangle helpers, on host and in x32. */
#include "aes_client.h"
#ifdef AES_CONTRACT_HOST_TEST
#include <stdio.h>
#endif

int aes_local_checks(void);
int aes_local_checks(void)
{
    GRECT a = { 10, 10, 20, 20 }, b = { 20, 20, 20, 20 };
    GRECT wanted = { 20, 20, 10, 10 };
    int failure = 0;

#define CHECK(expr) do { if (!(expr) && !failure) failure = __LINE__; } while (0)
    CHECK(inside(10, 10, &a) && inside(29, 29, &a));
    CHECK(!inside(30, 30, &a) && !inside(9, 10, &a));
    CHECK(rc_intersect(&a, &b) && rc_equal(&b, &wanted));
    b.g_x = b.g_y = 40;
    CHECK(!rc_intersect(&a, &b));
    b = wanted;
    rc_union(&a, &b);
    CHECK(rc_equal(&a, &b));
    b.g_x = b.g_y = 0;
    rc_constrain(&a, &b);
    CHECK(rc_equal(&a, &b));
#undef CHECK
    return failure;
}
#ifdef AES_CONTRACT_HOST_TEST
int main(void)
{
    int result = aes_local_checks();

    if (result)
        printf("AES local contract: FAIL line %d\n", result);
    else
        puts("AES local contract: PASS");
    return result != 0;
}
#endif
