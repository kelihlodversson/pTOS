#include "config.h"
#include "portab.h"
#include "struct.h"
#include "basepage.h"
#include "obdefs.h"
#include "gemlib.h"
#include "pd.h"
#define _ASMDEFINE(sym, val) asm volatile ("\n#define " #sym " %c0 \n" : : "i" (val))
#define ASMOFFSET(s, m) _ASMDEFINE(s##_##m, offsetof(s, m));
#define ASMSIZE(s) _ASMDEFINE(SIZEOF_##s, sizeof(s));

void foo(void);
void foo(void) {
	ASMOFFSET(AESPROCESS, a_uda);
	ASMOFFSET(AESPROCESS, a_pd);
	ASMOFFSET(AESPROCESS, a_cda);
	ASMOFFSET(AESPROCESS, a_evb);
	ASMSIZE(AESPROCESS);
	ASMOFFSET(SHELL, sh_doexec);
	ASMOFFSET(SHELL, sh_dodef);
	ASMOFFSET(SHELL, sh_isdef);
	ASMOFFSET(SHELL, sh_isgem);
	ASMOFFSET(SHELL, sh_desk);
	ASMOFFSET(SHELL, sh_cdir);
	ASMSIZE(SHELL);
	ASMOFFSET(THEGLO, g_int);
	ASMOFFSET(THEGLO, g_scrap);
	ASMOFFSET(THEGLO, s_cdir);
	ASMOFFSET(THEGLO, s_cmd);
	ASMOFFSET(THEGLO, g_work);
	ASMOFFSET(THEGLO, g_dta);
	ASMOFFSET(THEGLO, g_fpdx);
	ASMOFFSET(THEGLO, g_olist);
	ASMOFFSET(THEGLO, g_rawstr);
	ASMOFFSET(THEGLO, g_tmpstr);
	ASMOFFSET(THEGLO, g_valstr);
	ASMOFFSET(THEGLO, g_fmtstr);
	ASMOFFSET(THEGLO, w_win);
	ASMOFFSET(THEGLO, g_accreg);
	ASMOFFSET(THEGLO, g_acctitle);
	ASMOFFSET(THEGLO, g_acc);
	ASMSIZE(THEGLO);
	ASMOFFSET(UDA, u_insuper);
	ASMOFFSET(UDA, u_regs);
	ASMOFFSET(UDA, u_spsuper);
	ASMOFFSET(UDA, u_spuser);
	ASMOFFSET(UDA, u_oldspsuper);
	ASMOFFSET(UDA, u_super);
	ASMOFFSET(UDA, u_supstk);
	ASMSIZE(UDA);
	ASMOFFSET(AESPD, p_link);
	ASMOFFSET(AESPD, p_thread);
	ASMOFFSET(AESPD, p_uda);
	ASMOFFSET(AESPD, p_name);
	ASMOFFSET(AESPD, p_cda);
	ASMOFFSET(AESPD, p_ldaddr);
	ASMOFFSET(AESPD, p_pid);
	ASMOFFSET(AESPD, p_stat);
	ASMOFFSET(AESPD, p_unused);
	ASMOFFSET(AESPD, p_evwait);
	ASMOFFSET(AESPD, p_evflg);
	ASMOFFSET(AESPD, p_flags);
	ASMOFFSET(AESPD, p_evlist);
	ASMOFFSET(AESPD, p_qdq);
	ASMOFFSET(AESPD, p_qnq);
	ASMOFFSET(AESPD, p_qaddr);
	ASMOFFSET(AESPD, p_qindex);
	ASMOFFSET(AESPD, p_queue);
	ASMOFFSET(AESPD, p_appdir);
	ASMSIZE(AESPD);
	ASMOFFSET(PD, p_lowtpa)
	ASMOFFSET(PD, p_hitpa)
	ASMOFFSET(PD, p_tbase)
	ASMOFFSET(PD, p_tlen)

	ASMOFFSET(PD, p_dbase)
	ASMOFFSET(PD, p_dlen)
	ASMOFFSET(PD, p_bbase)
	ASMOFFSET(PD, p_blen)

	ASMOFFSET(PD, p_xdta)
	ASMOFFSET(PD, p_parent)
	ASMOFFSET(PD, p_flags)
	ASMOFFSET(PD, p_env)

	ASMOFFSET(PD, p_uft)
	ASMOFFSET(PD, p_lddrv)
	ASMOFFSET(PD, p_curdrv)
	ASMOFFSET(PD, p_1fill)

	ASMOFFSET(PD, p_curdir)
	ASMOFFSET(PD, p_2fill)

	ASMOFFSET(PD, p_3fill)
	ASMOFFSET(PD, p_dreg)
	ASMOFFSET(PD, p_areg)

	ASMOFFSET(PD, p_cmdlin)
	ASMSIZE(PD)
}
