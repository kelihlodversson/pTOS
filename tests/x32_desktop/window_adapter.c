/* Host regression for contention before and after a native window commit. */
#include "emutos.h"
#include "aesdefs.h"
#include "deskaes.h"
#include "gemerror.h"
#include "string.h"
#include <stdio.h>

static WORD operation, blocked, yields, calls, commits, opened, top;
static BOOL partial;
static LONG error, yield_error;
static UWORD *clobber;
static int failures;
#define CHECK(c) do { if (!(c)) { printf("window adapter FAIL line %d\n", __LINE__); failures++; } } while (0)

static void reset(WORD opcode)
{
    operation = opcode;
    blocked = yields = calls = commits = opened = top = 0;
    partial = FALSE;
    error = yield_error = 0;
    clobber = NULL;
}

static LONG attempt(WORD opcode)
{
    if (opcode != operation)
        return E_OK;
    calls++;
    if (error)
        return error;
    if (blocked) {
        blocked--;
        return EAGAIN;
    }
    return E_OK;
}

LONG aes_appl_yield(void)
{
    yields++;
    if (clobber)
        clobber[0] = 99;
    return yield_error;
}

LONG aes_wind_get(WORD h, AES_WINDOW_INFO *info)
{
    LONG rc = attempt(WIND_GET);

    if (rc) return rc;
    memset(info, 0, sizeof(*info));
    info->handle = h;
    info->flags = opened ? AES_WINDOW_OPEN : 0;
    info->top = top;
    r_set(&info->work, 10, 20, 300, 200);
    return E_OK;
}

LONG aes_wind_open(WORD h, const GRECT *rectangle)
{
    LONG rc = attempt(WIND_OPEN);

    (void)h; (void)rectangle;
    if (rc) return rc;
    if (opened) return EINVAL;
    opened = TRUE; commits++;
    return partial ? EAGAIN : E_OK;
}

LONG aes_wind_close(WORD h)
{
    LONG rc = attempt(WIND_CLOSE);

    (void)h;
    if (rc) return rc;
    if (!opened) return EINVAL;
    opened = FALSE; commits++;
    return partial ? EAGAIN : E_OK;
}

LONG aes_wind_delete(WORD h)
{
    LONG rc = attempt(WIND_DELETE);

    (void)h;
    if (rc) return rc;
    if (opened && partial) {
        opened = FALSE;
        return EAGAIN;
    }
    commits++;
    return E_OK;
}

LONG aes_wind_create(UWORD kind, const GRECT *rectangle)
{
    LONG rc = attempt(WIND_CREATE);

    (void)kind; (void)rectangle;
    if (rc) return rc;
    commits++;
    return 7;
}

LONG aes_wind_top(WORD h)
{
    LONG rc = attempt(WIND_SET);

    if (rc) return rc;
    top = h; commits++;
    return E_OK;
}

LONG aes_wind_set_rect(WORD h, const GRECT *rectangle)
{
    (void)rectangle;
    return aes_wind_top(h);
}
LONG aes_wind_set_text(WORD h, UWORD field, const char *text)
{
    (void)field; (void)text;
    return aes_wind_top(h);
}
LONG aes_wind_slider(WORD h, UWORD field, WORD value)
{
    (void)field; (void)value;
    return aes_wind_top(h);
}
LONG aes_wind_find(WORD x, WORD y)
{
    LONG rc = attempt(WIND_FIND);

    (void)x; (void)y;
    return rc ? rc : 7;
}
LONG aes_wind_update(WORD mode)
{
    (void)mode;
    return attempt(WIND_UPDATE);
}
LONG aes_wind_calc(UWORD mode, UWORD kind, const GRECT *in, GRECT *out)
{
    LONG rc = attempt(WIND_CALC);

    (void)mode; (void)kind;
    if (!rc) *out = *in;
    return rc;
}
LONG aes_wind_rect(WORD h, UWORD next, GRECT *rectangle)
{
    LONG rc = attempt(WIND_GET);

    (void)h; (void)next;
    if (!rc) {
        r_set(rectangle, 10, 20, 30, 40);
        commits++;
    }
    return rc;
}

int main(void)
{
    UWORD in[8] = { 7, WF_TOP, 10, 20, 300, 200 }, out[7];
    static const WORD retryable[] = { WIND_CREATE, WIND_FIND, WIND_UPDATE, WIND_CALC };
    unsigned i;

    reset(WIND_SET); blocked = 3; clobber = in;
    CHECK(desk_window(WIND_SET, in, out) == E_OK);
    CHECK(top == 7 && yields == 3 && commits == 1 && in[0] == 99);
    in[0] = 7;
    reset(WIND_SET); error = EACCDN;
    CHECK(desk_window(WIND_SET, in, out) == EACCDN && !yields);
    reset(WIND_SET); blocked = 1; yield_error = EACCDN;
    CHECK(desk_window(WIND_SET, in, out) == EACCDN && calls == 1);

    reset(WIND_OPEN); partial = TRUE;
    CHECK(desk_window(WIND_OPEN, in, out) == E_OK);
    CHECK(opened && commits == 1 && calls == 1 && yields == 1);
    reset(WIND_CLOSE); opened = TRUE; partial = TRUE;
    CHECK(desk_window(WIND_CLOSE, in, out) == E_OK);
    CHECK(!opened && commits == 1 && calls == 1 && yields == 1);
    reset(WIND_OPEN); opened = TRUE;
    CHECK(desk_window(WIND_OPEN, in, out) == EINVAL && !calls);
    reset(WIND_CLOSE);
    CHECK(desk_window(WIND_CLOSE, in, out) == EINVAL && !calls);
    reset(WIND_DELETE); opened = TRUE; partial = TRUE;
    CHECK(desk_window(WIND_DELETE, in, out) == E_OK);
    CHECK(!opened && commits == 1 && calls == 2 && yields == 1);

    in[1] = WF_WXYWH;
    reset(WIND_GET); blocked = 2;
    CHECK(desk_window(WIND_GET, in, out) == E_OK && yields == 2 && out[3] == 300);
    in[1] = WF_NEXTXYWH;
    reset(WIND_GET); blocked = 2;
    CHECK(desk_window(WIND_GET, in, out) == E_OK && yields == 2 && commits == 1 && out[3] == 30);
    for (i = 0; i < ARRAY_SIZE(retryable); i++) {
        reset(retryable[i]); blocked = 2;
        CHECK(desk_window(retryable[i], in, out) >= 0 && yields == 2);
    }
    if (!failures)
        puts("Desktop window retries and committed open/close: PASS");
    return failures != 0;
}
