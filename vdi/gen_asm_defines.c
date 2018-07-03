#include "config.h"
#include "portab.h"
#include "../bios/lineavars.h"

#define _ASMDEFINE(sym, val) asm volatile ("\n#define LINEA_" #sym " _linea_vars+%c0 \n" : : "i" (val))
#define LA(m) _ASMDEFINE(m, offsetof(struct _lineavars, m))

void foo(void);
void foo(void) {
	asm volatile ("\n#define LA(val) LINEA_##val\n" : :);
	LA(mouse_cdb);
    LA(INQ_TAB);
    LA(DEV_TAB);

    LA(GCURX);
    LA(GCURY);
    LA(HIDE_CNT);
    LA(MOUSE_BT);

    LA(REQ_COL);
    
    LA(SIZ_TAB);

    LA(TERM_CH);
    LA(chc_mode);
    LA(CUR_WORK);
    LA(def_font);
    LA(font_ring);
    LA(font_count);
    
    LA(line_cw);
    LA(loc_mode);
    LA(num_qc_lines);
    
    LA(str_mode);
    LA(val_mode);

    LA(cur_ms_stat);
    LA(disab_cnt);
    LA(newx);
    LA(newy);
    LA(draw_flag);
    LA(mouse_flag);

    LA(sav_cur_x);
    LA(sav_cur_y);
    LA(retsav);

    LA(mouse_cursor_save);

    LA(tim_addr);
    LA(tim_chain);
    LA(user_but);
    LA(user_cur);
    LA(user_mot);

    LA(v_cel_ht);
    LA(v_cel_mx);
    LA(v_cel_my);
    LA(v_cel_wr);
    LA(v_col_bg);
    LA(v_col_fg);
    LA(v_cur_ad);
    LA(v_cur_of);
    LA(v_cur_cx);
    LA(v_cur_cy);
    LA(v_period);
    LA(v_cur_tim);
    LA(v_fnt_ad);
    LA(v_fnt_nd);
    LA(v_fnt_st);
    LA(v_fnt_wr);
    LA(V_REZ_HZ);
    LA(v_off_ad);
    LA(v_stat_0);
    LA(V_REZ_VT);
    LA(BYTES_LIN);
    LA(v_planes);
    LA(v_lin_wr);

    LA(local_pb);
    LA(COLBIT0);
    LA(COLBIT1);
    LA(COLBIT2);
    LA(COLBIT3);

    LA(LSTLIN);
    LA(LN_MASK);
    LA(WRT_MODE);

    LA(X1);
    LA(Y1);
    LA(X2);
    LA(Y2);
    LA(PATPTR);
    LA(PATMSK);
    LA(MFILL);

    LA(CLIP);
    LA(XMINCL);
    LA(YMINCL);
    LA(XMAXCL);
    LA(YMAXCL);

    LA(XDDA);
    LA(DDAINC);
    LA(SCALDIR);
    LA(MONO);
    LA(SOURCEX);
    LA(SOURCEY);
    LA(DESTX);
    LA(DESTY);
    LA(DELX);
    LA(DELY);
    LA(FBASE);
    LA(FWIDTH);
    LA(STYLE);
    LA(LITEMASK);
    LA(SKEWMASK);
    LA(WEIGHT);
    LA(ROFF);
    LA(LOFF);
    LA(SCALE);
    LA(CHUP);
    LA(TEXTFG);
    LA(SCRTCHP);
    LA(SCRPT2);
    LA(TEXTBG);
    LA(COPYTRAN);
    LA(FILL_ABORT);
}

