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
    WINDOW_VERIFY(wind_calc(WC_WORK, 0x4000, 1, 2, 3, 4, &x, &y, &w, &h) == 0 && x == 10);
    {
        static const short kinds[] = { NAME,CLOSER,FULLER,MOVER,INFO,SIZER,
            UPARROW|DNARROW|VSLIDE,LFARROW|RTARROW|HSLIDE,
            NAME|INFO|SIZER,NAME|CLOSER|FULLER|MOVER|INFO|SIZER|VSLIDE|HSLIDE,
            SIZER|HSLIDE,SIZER|VSLIDE,HOTCLOSEBOX };
        unsigned k;
        for (k = 0; k < sizeof(kinds)/sizeof(kinds[0]); k++) {
            WINDOW_VERIFY(wind_calc(WC_WORK,kinds[k],-20,30,300,200,&x,&y,&w,&h) == 1);
            WINDOW_VERIFY(x == -19 && y >= 31 && w <= 298 && h <= 198);
            WINDOW_VERIFY(mt_wind_calc(WC_BORDER,kinds[k],x,y,w,h,&x,&y,&w,&h,global) == 1);
            WINDOW_VERIFY(x == -20 && y == 30 && w == 300 && h == 200);
        }
        WINDOW_VERIFY(wind_calc(WC_WORK,MOVER,0,0,0,0,&x,&y,&w,&h) == 1);
        WINDOW_VERIFY(x == 1 && y > 1 && w == -2 && h < -2);
        WINDOW_VERIFY(wind_calc(WC_BORDER,MOVER,x,y,w,h,&x,&y,&w,&h) == 1);
        WINDOW_VERIFY(!(x|y|w|h));
        x = y = w = h = 123;
        WINDOW_VERIFY(wind_calc(WC_BORDER,NAME,0,-32768,32767,32767,&x,&y,&w,&h) == 0);
        WINDOW_VERIFY(x == 123 && y == 123 && w == 123 && h == 123);
        WINDOW_VERIFY(wind_calc(WC_WORK,0,32767,0,10,10,&x,&y,&w,&h) == 0 && x == 123);
        WINDOW_VERIFY(wind_calc(WC_WORK,NAME,10,20,100,80,NULL,NULL,NULL,NULL) == 1);
    }
    WINDOW_VERIFY(wind_create(UPARROW, 0, 0, 100, 80) == -1);
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
    WINDOW_VERIFY(wind_set(id, WF_CURRXYWH, 32767, 30, 10, 10) == 0);
    WINDOW_VERIFY(wind_get(id, WF_CURRXYWH, &x, &y, &w, &h) == 1);
    WINDOW_VERIFY(x == 20 && y == 30 && w == 200 && h == 160);
    WINDOW_VERIFY(wind_get(id, 32767, &x, &y, &w, &h) == 0 && x == 20);
    WINDOW_VERIFY(wind_set(id, 32767, 0, 0, 0, 0) == 0);
    WINDOW_VERIFY(mt_wind_close(id, global) == 1 && wind_close(id) == 0);
    WINDOW_VERIFY(wind_get(id, WF_CURRXYWH, &x, &y, &w, &h) == 1 && !(x | y | w | h));
    WINDOW_VERIFY(wind_get(id, WF_PREVXYWH, &x, &y, &w, &h) == 1 && x == 20 && y == 30);
    /* GEM's close path applies WC_WORK to the zero border. Do not clamp
     * these values or substitute a new closed-window geometry convention. */
    WINDOW_VERIFY(wind_get(id, WF_WORKXYWH, &x, &y, &w, &h) == 1);
    WINDOW_VERIFY(x == 1 && y == 1 && w == -2 && h == -2);
    WINDOW_VERIFY(wind_calc(WC_WORK, 0, 0, 0, 0, 0, &x, &y, &w, &h) == 1);
    WINDOW_VERIFY(x == 1 && y == 1 && w == -2 && h == -2);
    WINDOW_VERIFY(wind_set(id, WF_CURRXYWH, 10, 20, 60, 50) == 1);
    WINDOW_VERIFY(wind_get(id, WF_WORKXYWH, &x, &y, &w, &h) == 1);
    WINDOW_VERIFY(x == 11 && y == 21 && w == 58 && h == 48);
    WINDOW_VERIFY(wind_close(id) == 0); /* Geometry does not open a window. */
    WINDOW_VERIFY(mt_wind_open(id, 1, 2, 30, 40, global) == 1 && mt_wind_delete(id, global) == 1);
    WINDOW_VERIFY(wind_delete(id) == 0 && wind_get(id, WF_KIND, &x, &y, &w, &h) == 0);
    WINDOW_VERIFY(wind_open(0, 1, 2, 3, 4) == 0 && wind_delete(-1) == 0);
    id = wind_create(NAME|INFO,0,0,200,160);
    WINDOW_VERIFY(id > 0);
    WINDOW_VERIFY(wind_get(id,WF_KIND,&x,&y,&w,&h) == 1 && x == (NAME|INFO));
    WINDOW_VERIFY(wind_set_str(id,WF_NAME,"Native title") == 1);
    WINDOW_VERIFY(mt_wind_set_str(id,WF_INFO,"Information",global) == 1);
    WINDOW_VERIFY(wind_set_str(id,WF_NAME,NULL) == 0);
    WINDOW_VERIFY(wind_open(id,20,60,100,5) == 0);
    WINDOW_VERIFY(wind_open(id,20,60,120,100) == 1);
    WINDOW_VERIFY(wind_get(id,WF_WORKXYWH,&x,&y,&w,&h) == 1 && x == 21 && y > 61 && w == 118 && h < 98);
    WINDOW_VERIFY(wind_set_str(id,WF_NAME,"Retitled") == 1);
    WINDOW_VERIFY(wind_set(id,WF_CURRXYWH,40,80,140,120) == 1);
    WINDOW_VERIFY(wind_close(id) == 1 && wind_delete(id) == 1);
    id = wind_create(SIZER,0,0,50,60);
    WINDOW_VERIFY(id > 0 && wind_open(id,20,30,50,60) == 1);
    WINDOW_VERIFY(wind_set(id,WF_CURRXYWH,20,30,40,55) == 1);
    WINDOW_VERIFY(wind_set(id,WF_CURRXYWH,20,30,2,16) == 0);
    WINDOW_VERIFY(wind_get(id,WF_CURRXYWH,&x,&y,&w,&h) == 1 && w == 40 && h == 55);
    WINDOW_VERIFY(wind_close(id) == 1 && wind_delete(id) == 1);
    id = wind_create(NAME|SIZER,-32120,30,32320,140);
    WINDOW_VERIFY(id > 0 && wind_open(id,-32120,30,32320,140) == 1);
    WINDOW_VERIFY(wind_set(id,WF_CURRXYWH,-32120,30,32749,131) == 1);
    WINDOW_VERIFY(wind_close(id) == 1 && wind_delete(id) == 1);
    id = wind_create(MOVER,0,30,32763,80);
    WINDOW_VERIFY(id > 0 && wind_open(id,0,30,32763,80) == 1);
    WINDOW_VERIFY(wind_set(id,WF_CURRXYWH,2,30,32763,80) == 1);
    WINDOW_VERIFY(wind_close(id) == 1 && wind_delete(id) == 1);
    id = wind_create(MOVER|SIZER,0,30,32758,140);
    WINDOW_VERIFY(id > 0 && wind_open(id,0,30,32758,140) == 1);
    WINDOW_VERIFY(wind_set(id,WF_CURRXYWH,0,30,32758,140) == 1);
    WINDOW_VERIFY(wind_close(id) == 1 && wind_delete(id) == 1);
    for (i = 0; i < 64; i++) {
        ids[i] = wind_create(0, 0, 0, 100, 100);
        WINDOW_VERIFY(ids[i] > 0);
    }
    WINDOW_VERIFY(wind_create(0, 0, 0, 100, 100) == -1);
    for (i = 0; i < 64; i++) WINDOW_VERIFY(wind_delete(ids[i]) == 1);
    return 0;
}
#undef WINDOW_VERIFY
