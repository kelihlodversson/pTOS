/* Standard GEM contract shared by host mocks and the real x32 client. */
#define WINDOW_VERIFY(c) do { if (!(c)) return 0x7300 | __LINE__; } while (0)
static int gem_window_checks(short *global)
{
    short id, ids[64], i, x, y, w, h;
    WINDOW_VERIFY(wind_calc(WC_WORK, 0, 10, 20, 100, 80, &x, &y, &w, &h) == 1);
    WINDOW_VERIFY(x == 11 && y == 21 && w == 98 && h == 78);
    WINDOW_VERIFY(mt_wind_calc(WC_BORDER, 0, x, y, w, h, &x, &y, &w, &h, global) == 1);
    WINDOW_VERIFY(x == 10 && y == 20 && w == 100 && h == 80);
    WINDOW_VERIFY(wind_calc(99, 0, 1, 2, 3, 4, &x, &y, &w, &h) == 0 && x == 10);
    WINDOW_VERIFY(wind_calc(WC_WORK, 1, 1, 2, 3, 4, &x, &y, &w, &h) == 0 && x == 10);
    WINDOW_VERIFY(wind_create(1, 0, 0, 100, 80) == -1);
    id = mt_wind_create(0, 10, 20, 300, 200, global);
    WINDOW_VERIFY(id > 0 && wind_close(id) == 0);
    WINDOW_VERIFY(wind_get(id, WF_CURRXYWH, &x, &y, &w, &h) == 1 && !(x | y | w | h));
    WINDOW_VERIFY(wind_get(id, WF_FULLXYWH, &x, &y, &w, &h) == 1);
    WINDOW_VERIFY(x == 10 && y == 20 && w == 300 && h == 200);
    WINDOW_VERIFY(wind_open(id, -5, 6, 100, 80) == 1 && wind_open(id, 0, 0, 10, 10) == 0);
    WINDOW_VERIFY(mt_wind_get(id, WF_WORKXYWH, &x, &y, &w, &h, global) == 1);
    WINDOW_VERIFY(x == -4 && y == 7 && w == 98 && h == 78);
    WINDOW_VERIFY(mt_wind_set(id, WF_CURRXYWH, 20, 30, 200, 160, global) == 1);
    WINDOW_VERIFY(wind_get(id, WF_PREVXYWH, &x, &y, &w, &h) == 1);
    WINDOW_VERIFY(x == -5 && y == 6 && w == 100 && h == 80);
    WINDOW_VERIFY(wind_set(id, WF_CURRXYWH, 20, 30, 0, 160) == 0);
    WINDOW_VERIFY(wind_get(id, WF_CURRXYWH, &x, &y, &w, &h) == 1);
    WINDOW_VERIFY(x == 20 && y == 30 && w == 200 && h == 160);
    WINDOW_VERIFY(wind_get(id, 32767, &x, &y, &w, &h) == 0 && x == 20);
    WINDOW_VERIFY(wind_set(id, 32767, 0, 0, 0, 0) == 0);
    WINDOW_VERIFY(mt_wind_close(id, global) == 1 && wind_close(id) == 0);
    WINDOW_VERIFY(wind_get(id, WF_CURRXYWH, &x, &y, &w, &h) == 1 && !(x | y | w | h));
    WINDOW_VERIFY(wind_get(id, WF_PREVXYWH, &x, &y, &w, &h) == 1 && x == 20 && y == 30);
    WINDOW_VERIFY(mt_wind_open(id, 1, 2, 30, 40, global) == 1 && mt_wind_delete(id, global) == 1);
    WINDOW_VERIFY(wind_delete(id) == 0 && wind_get(id, WF_KIND, &x, &y, &w, &h) == 0);
    WINDOW_VERIFY(wind_open(0, 1, 2, 3, 4) == 0 && wind_delete(-1) == 0);
    for (i = 0; i < 64; i++) {
        ids[i] = wind_create(0, 0, 0, 100, 100);
        WINDOW_VERIFY(ids[i] > 0);
    }
    WINDOW_VERIFY(wind_create(0, 0, 0, 100, 100) == -1);
    for (i = 0; i < 64; i++) WINDOW_VERIFY(wind_delete(ids[i]) == 1);
    return 0;
}
#undef WINDOW_VERIFY
