/* Standard GEM shell queries use startup metadata and a private search DTA. */
#include "gem.h"
#include "../../lib/aes/app_context.h"
#include "shell_context.h"
#include "resource_context.h"
#include "transport.h"
#include "gemerror.h"
#include "string.h"
#include "runtime.h"
#define VERIFY(c) do { if (!(c)) { __builtin_printf("shell failure: %d\n",__LINE__); return __LINE__; } } while (0)
static PD basepage;
static DL_LAUNCH launch;
static DL_LAUNCH *active_launch = &launch;
static BOOL aux_ready = TRUE;
static AESGLOBAL *nested_global;
static const char *nested_hit;
static BOOL nested_search, nested_active, nested_no_memory, nested_restored;
static int nested_result;
static char environment[AES_SHELL_ENV], long_prefix[AES_SHELL_ENV];
static char short_environment[7] = { 'F','O','O','=','b','a','d' };
static char executable[AES_SHELL_PATH] = "APP\\TEST.TOS", cwd[128] = "\\START";
static UBYTE initial_tail[128];
static DTA caller_dta;
static void *current_dta = &caller_dta;
static LONG drive = 2;
static BOOL get_error, install_error, restore_error, search_error, registration_error;
static LONG first_error;
static int attempts, dta_calls, cwd_calls;
static char tried[16][AES_SHELL_PATH];
static const char *found;
static LONG launch_result = 42;
static LONG watch_result = 17, cancellations, observed_launches, deferred_calls;
static LONG deferred_result;
static UQUAD launched_console;
static int launches, native_calls;
static char launched_path[AES_SHELL_PATH], launched_tail[128], launched_directory[AES_SHELL_PATH];
static char launched_environment[AES_SHELL_ENV];
static LONG allocation_balance;
static ULONG allocation_attempts;
static BOOL fail_allocation, fail_release;
UQUAD aes_memory_call(ULONG function, UQUAD argument)
{
    void *memory;
    if (function == 0x48) {
        allocation_attempts++;
        if (fail_allocation) return (QUAD)ENSMEM;
        memory = __builtin_malloc((size_t)argument);
        if (!memory) return (QUAD)ENSMEM;
        allocation_balance++; return (uintptr_t)memory;
    }
    if (function == 0x49) {
        if (fail_release) return (QUAD)EACCDN;
        allocation_balance--; __builtin_free((void *)(uintptr_t)argument); return E_OK;
    }
    return (QUAD)EINVFN;
}
static void save_environment(UQUAD address)
{
    const char *source = (const char *)(uintptr_t)address;
    ULONG i;
    memset(launched_environment,0,sizeof(launched_environment));
    if (!source) return;
    for (i = 0; i+1 < sizeof(launched_environment); i++) {
        launched_environment[i] = source[i];
        if (!source[i] && !source[i+1]) break;
    }
}
LONG aes_windows_reset_policy(AESGLOBAL *global, BOOL polling, LONG token) { (void)global; (void)polling; (void)token; return E_OK; }
LONG aes_window_messages_poll(LONG session, AESGLOBAL *global) { (void)session; (void)global; return E_OK; }
BOOL aes_frames_wait_display(AESGLOBAL *global) { (void)global; return FALSE; }
BOOL aes_windows_pending(AESGLOBAL *global) { (void)global; return FALSE; }
LONG aes_application_session(AESGLOBAL *global)
{ return global && global->ap_id == 7 && !registration_error ? 9 : EACCDN; }
unsigned long __ptos_getauxval(unsigned long type)
{ return !aux_ready ? 0 : type == 6 ? 4096 : type == DL_AT_PTOS_LAUNCH ? (uintptr_t)active_launch : type == 0x5000 ? (uintptr_t)&basepage : type == 31 ? (uintptr_t)executable : 0; }
UQUAD aes_gemdos_call(ULONG function, UQUAD a, UQUAD b, UQUAD c)
{
    (void)c;
    if (function == 0x19) { cwd_calls++; return drive; }
    if (function == 0x47) { cwd_calls++; strcpy((char *)(uintptr_t)a,cwd); return E_OK; }
    if (function == 0x2f) return get_error ? (UQUAD)(QUAD)EACCDN : (uintptr_t)current_dta;
    if (function == 0x1a) {
        dta_calls++;
        if (install_error && a != (uintptr_t)&caller_dta) return 99;
        if (restore_error && a == (uintptr_t)&caller_dta && current_dta != &caller_dta) { restore_error = FALSE; return 99; }
        current_dta = (void *)(uintptr_t)a; return 99; /* Fsetdta is void. */
    }
    if (function == 0x4e) {
        if (b != (FA_RO|FA_HIDDEN|FA_SYSTEM) || current_dta == &caller_dta) return (QUAD)EACCDN;
        memset(current_dta,nested_active ? 0x77 : 0x66,sizeof(DTA));
        if (nested_search) {
            DTA saved_dta = *(DTA *)current_dta;
            void *saved_current = current_dta;
            const char *saved_found = found;
            char path[AES_SHELL_PATH] = "INNER.TXT";
            int saved_attempts = attempts;
            BOOL saved_failure = fail_allocation;
            nested_search = FALSE; nested_active = TRUE; found = nested_hit;
            fail_allocation = nested_no_memory;
            nested_result = mt_shel_find(path,nested_global) && !strcmp(path,nested_hit);
            fail_allocation = saved_failure;
            nested_restored = current_dta == saved_current &&
                !memcmp(&saved_dta,saved_current,sizeof(DTA));
            nested_active = FALSE; found = saved_found; attempts = saved_attempts;
        }
        if (attempts < 16) strcpy(tried[attempts],(char *)(uintptr_t)a);
        attempts++;
        if (attempts == 1 && first_error) return (QUAD)first_error;
        return found && !search_error && !strcmp(found,(char *)(uintptr_t)a) ? E_OK : (QUAD)EFILNF;
    }
    return (QUAD)EINVFN;
}
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    (void)e; (void)f;
    native_calls++;
    if (a != 9) return EACCDN;
    if (fn == CUIS_CHILD_RESULT) return 0;
    if (fn == CUIS_PROCESS_DEFER || fn == CUIS_PROCESS_DEFER_DIRECTORY) {
        if (f) return EINVAL;
        if (e) strcpy(launched_directory,(const char *)(uintptr_t)e); else launched_directory[0] = 0;
        save_environment(d);
        strcpy(launched_path,(const char *)(uintptr_t)b);
        memcpy(launched_tail,(const char *)(uintptr_t)c,sizeof(launched_tail));
        deferred_calls++; return deferred_result;
    }
    if (fn == CUIS_CHILD_LAUNCH_DIRECTORY || fn == CUIS_CONSOLE_LAUNCH_DIRECTORY) {
        strcpy(launched_directory,(const char *)(uintptr_t)f);
        f = fn == CUIS_CONSOLE_LAUNCH_DIRECTORY;
        fn = CUIS_CHILD_LAUNCH;
    } else launched_directory[0] = 0;
    if (fn == CUIS_CHILD_LAUNCH) {
        if ((b != 17 && b != 0) || f > 1) return EINVAL;
        save_environment(e);
        launched_console = f;
        strcpy(launched_path,(const char *)(uintptr_t)c);
        memcpy(launched_tail,(const char *)(uintptr_t)d,sizeof(launched_tail));
        observed_launches++; launches++;
        if (launch_result < 0 && b) cancellations++;
        return launch_result;
    }
    if (fn != CUIS_CHILD_WATCH) return EINVFN;
    if (b == CUIS_CHILD_RESERVE) return watch_result;
    if (c != 17) return EACCDN;
    if (b == CUIS_CHILD_CANCEL) { cancellations++; return E_OK; }
    return EINVFN;
}
UQUAD aes_gemdos_call4(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    if (function != 0x4b || a != PE_ASYNC_LOADGO || current_dta != &caller_dta) return (QUAD)EINVFN;
    save_environment(d);
    launches++;
    strcpy(launched_path,(const char *)(uintptr_t)b);
    memcpy(launched_tail,(const char *)(uintptr_t)c,sizeof(launched_tail));
    return (QUAD)launch_result;
}

int64_t dl_syscall(uint32_t fn, uint64_t a, uint64_t b, uint64_t c,
                  uint64_t d, uint64_t e, uint64_t f)
{
    (void)d; (void)e; (void)f;
    return (int64_t)aes_gemdos_call(fn,a,b,c);
}
static void capture(void)
{
    memcpy(launch.tail,basepage.p_cmdlin,128);
    launch.environment = basepage.p_env;
    launch.environment_size = basepage.p_env ? sizeof(environment) : 0;
    dl_capture_launch(&launch,executable);
    aes_shell_init(aes_app_context(&aes_global));
}
static int search(const char *input, const char *hit, const char *result, int count)
{
    char path[AES_SHELL_PATH];
    DTA saved_dta;
    int before = cwd_calls;
    strcpy(path,input); found = hit; attempts = 0; saved_dta = caller_dta;
    VERIFY(mt_shel_find(path,&aes_global) == (result != NULL));
    VERIFY(!strcmp(path,result ? result : input) && attempts == count);
    VERIFY(current_dta == &caller_dta && !memcmp(&caller_dta,&saved_dta,sizeof(DTA)));
    VERIFY(cwd_calls == before);
    return 0;
}
/* Unsupported standard/vendor selectors must reject before interpreting
 * their different command structures, tail buffers or output arguments. */
static int unsupported_checks(void)
{
    static const UWORD flags[] = { SW_PSETLIMIT,SW_PRENICE,SW_UID,SW_GID,0x4000,XSHD_FLAGS };
    static const WORD executions[] = { SHW_DOS,SHW_SINGLE,-1,3,99,102 };
    UWORD mode;
    ULONG i, j;
    char tail;
    WORD message[8] = { AP_AESTERM,0,0,0,0,0,0,0 };
    int before = launches, before_dta = dta_calls, before_cwd = cwd_calls, before_native = native_calls;
    LONG before_defer = deferred_calls, before_cancel = cancellations;
    ULONG before_allocation = allocation_attempts;
    for (mode = 0; mode < 256; mode++) {
        if (mode == SWM_LAUNCH || mode == SWM_LAUNCHNOW || mode == SWM_SHUTDOWN ||
            mode == SWM_REZCHANGE || mode == SWM_NEWMSG || mode == SWM_AESMSG) continue;
        VERIFY(!shel_write(mode,0,0,(void *)1,(char *)1));
        VERIFY(!mt_shel_write(mode|SW_DEFDIR,GEMAPP,0,(void *)1,(char *)1,&aes_global));
    }
    for (i = 0; i < sizeof(flags)/sizeof(flags[0]); i++) {
        for (mode = SWM_LAUNCH; mode <= SWM_LAUNCHNOW; mode++) {
            VERIFY(!shel_write(flags[i]|mode,GEMAPP,0,(void *)1,(char *)1));
            VERIFY(!shel_write(flags[i]|SW_DEFDIR|SW_ENVIRON|mode,GEMAPP,0,(void *)1,(char *)1));
        }
    }
    for (i = 0; i < sizeof(executions)/sizeof(executions[0]); i++)
        for (j = TOSAPP; j <= GEMAPP; j++)
            VERIFY(!shel_write(SWM_LAUNCHNOW,j,executions[i],(void *)1,(char *)1));
    VERIFY(!shel_write(SWM_LAUNCHNOW,TOSAPP,SHW_CHAIN,(void *)1,(char *)1));
    for (mode = 128; mode < 256; mode++) {
        tail = (char)mode;
        VERIFY(!shel_write(SWM_LAUNCH,GEMAPP,CL_PARSE,(void *)1,&tail));
    }
    VERIFY(!shel_write(SWM_SHUTDOWN,0,-1,(void *)1,(char *)1));
    VERIFY(!shel_write(SWM_SHUTDOWN,0,3,(void *)1,(char *)1));
    VERIFY(!shel_write(SWM_REZCHANGE,2,0,(void *)1,(char *)1));
    VERIFY(!shel_write(SWM_NEWMSG,NM_INHIBIT_HIDE,0,(void *)1,(char *)1));
    VERIFY(!shel_write(SWM_AESMSG,0,0,message,(char *)1));
    VERIFY(launches == before && deferred_calls == before_defer && cancellations == before_cancel);
    VERIFY(dta_calls == before_dta && cwd_calls == before_cwd && !allocation_balance && native_calls == before_native);
    VERIFY(allocation_attempts == before_allocation);
    return 0;
}
static int launch_checks(AESGLOBAL *global)
{
    char name[] = "NEXT.PRG", minimal[] = { 1, 'x' }, empty[] = { 0 };
    char maximum[127], too_long[AES_SHELL_PATH], invalid[] = { 127 };
    DTA saved = caller_dta;
    int before, i;
    found = "GOOD\\NEXT.PRG";
    VERIFY(mt_shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,minimal,global) == 42);
    VERIFY(launches == 1 && !strcmp(launched_path,found) && launched_tail[0] == 1 && launched_tail[1] == 'x');
    for (i = 2; i < 128; i++) VERIFY(!launched_tail[i]);
    VERIFY(!strcmp(name,"NEXT.PRG") && minimal[0] == 1 && minimal[1] == 'x');
    maximum[0] = 126; memset(maximum+1,'a',126);
    VERIFY(shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,maximum) == 42);
    VERIFY(!memcmp(maximum,launched_tail,127) && !launched_tail[127]);
    VERIFY(shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,empty) == 42);
    for (i = 0; i < 128; i++) VERIFY(!launched_tail[i]);
    before = launches;
    /* Unsupported selectors must not dereference their overloaded arguments. */
    VERIFY(!shel_write(SWM_LAUNCH,0,2,(void *)1,(char *)1));
    VERIFY(!shel_write(0x101,GEMAPP,SHW_PARALLEL,(void *)1,(char *)1));
    VERIFY(!shel_write(2,GEMAPP,SHW_PARALLEL,(void *)1,(char *)1));
    VERIFY(!shel_write(SWM_LAUNCHNOW,TOSAPP,SHW_CHAIN,(void *)1,(char *)1));
    VERIFY(!shel_write(SWM_LAUNCHNOW,2,SHW_PARALLEL,(void *)1,(char *)1));
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_DOS,(void *)1,(char *)1));
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_SINGLE,(void *)1,(char *)1));
    VERIFY(!mt_shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,minimal,NULL));
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,NULL,minimal));
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,"",minimal));
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,NULL));
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,invalid));
    invalid[0] = (char)255;
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,invalid));
    memset(too_long,'x',sizeof(too_long));
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,too_long,minimal));
    registration_error = TRUE;
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,minimal)); registration_error = FALSE;
    found = NULL;
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,minimal));
    found = "GOOD\\NEXT.PRG"; first_error = EREADF; attempts = 0;
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,minimal)); first_error = 0;
    restore_error = TRUE;
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,minimal));
    VERIFY(launches == before && !restore_error);
    launch_result = ENSMEM;
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,minimal));
    launch_result = EFILNF;
    VERIFY(!shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,minimal));
    launch_result = 32767;
    VERIFY(shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_PARALLEL,name,minimal) == 32767);
    launch_result = 42;
    VERIFY(shel_write(SWM_LAUNCH,99,CL_NORMAL,name,minimal) == 42 && observed_launches == 1);
    before = launches;
    watch_result = EAGAIN;
    VERIFY(!shel_write(SWM_LAUNCH,0,CL_NORMAL,name,minimal) && launches == before);
    watch_result = 17; launch_result = ENSMEM;
    VERIFY(!shel_write(SWM_LAUNCH,0,CL_NORMAL,name,minimal) && cancellations == 1);
    launch_result = 42;
    VERIFY(shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_CHAIN,name,minimal) == 1 && deferred_calls == 1);
    VERIFY(!strcmp(launched_path,"GOOD\\NEXT.PRG") && launched_tail[0] == 1 && launched_tail[1] == 'x');
    deferred_result = ENSMEM;
    VERIFY(!mt_shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_CHAIN,name,minimal,global) && deferred_calls == 2);
    deferred_result = E_OK;
    found = "GOOD\\NEXT.TOS";
    VERIFY(shel_write(SWM_LAUNCH,0,CL_NORMAL,"NEXT.TOS",minimal) == 42 && launched_console == 1);
    VERIFY(shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_CHAIN,"NEXT.TOS",minimal) == 1);
    found = "GOOD\\NEXT.gTp";
    before = observed_launches;
    VERIFY(shel_write(SWM_LAUNCH,GEMAPP,CL_NORMAL,"NEXT.gTp",minimal) == 42);
    VERIFY(launched_console == 0);
    VERIFY(observed_launches == before+1 && launched_tail[0] == 1 && launched_tail[1] == 'x');
    VERIFY(shel_write(SWM_LAUNCH,GEMAPP,CL_NORMAL,"NEXT.gTp",maximum) == 42);
    VERIFY(!memcmp(maximum,launched_tail,127) && !launched_tail[127]);
    VERIFY(!shel_write(SWM_LAUNCH,GEMAPP,CL_NORMAL,"NEXT.gTp",invalid));
    before = observed_launches;
    VERIFY(shel_write(SWM_LAUNCHNOW,GEMAPP,SHW_IMMED,"NEXT.gTp",minimal) == 42 && !launched_console);
    VERIFY(shel_write(SWM_LAUNCHNOW,TOSAPP,SHW_IMMED,"NEXT.gTp",minimal) == 42 && launched_console);
    /* Explicit kind overrides the .gTp suffix; a parallel console launch
     * does not reserve an AES child-watch or require a free watch slot. */
    watch_result = EAGAIN;
    VERIFY(shel_write(SWM_LAUNCHNOW,TOSAPP,SHW_PARALLEL,"NEXT.gTp",minimal) == 42 && launched_console);
    VERIFY(observed_launches == before+3);
    before = cancellations; launch_result = ENSMEM;
    VERIFY(!shel_write(SWM_LAUNCHNOW,TOSAPP,SHW_PARALLEL,"NEXT.gTp",minimal) && cancellations == before);
    launch_result = 42; watch_result = 17;
    VERIFY(current_dta == &caller_dta && !memcmp(&caller_dta,&saved,sizeof(saved)));
    return 0;
}
static int directory_checks(void)
{
    char tail[1] = { 0 };
    SHELW command = { "NEXT.PRG",0,0,"C:\\WORK",(char *)1,0,0 };
    found = "GOOD\\NEXT.PRG";
    VERIFY(shel_write(SWM_LAUNCH|SW_DEFDIR,GEMAPP,CL_NORMAL,&command,tail) == 42);
    VERIFY(!strcmp(launched_path,"D:\\CHANGED\\GOOD\\NEXT.PRG") && !strcmp(launched_directory,"C:\\WORK"));
    VERIFY(!launched_environment[0]); /* SW_DEFDIR must not read SHELW.env. */
    command.defdir = NULL;
    VERIFY(shel_write(SWM_LAUNCHNOW|SW_DEFDIR,GEMAPP,SHW_PARALLEL,&command,tail) == 42);
    VERIFY(!strcmp(launched_directory,"D:\\CHANGED\\GOOD\\"));
    command.defdir = "RELATIVE";
    VERIFY(shel_write(SWM_LAUNCHNOW|SW_DEFDIR,GEMAPP,SHW_CHAIN,&command,tail) == 1);
    VERIFY(!strcmp(launched_directory,"D:\\CHANGED\\RELATIVE"));
    command.defdir = "C:WORK";
    VERIFY(shel_write(SWM_LAUNCHNOW|SW_DEFDIR,TOSAPP,SHW_PARALLEL,&command,tail) == 42 && launched_console);
    VERIFY(!strcmp(launched_directory,"C:\\CHANGED\\WORK"));
    command.defdir = "";
    VERIFY(!shel_write(SWM_LAUNCH|SW_DEFDIR,GEMAPP,CL_NORMAL,&command,tail));
    command.defdir = "!:invalid";
    VERIFY(!shel_write(SWM_LAUNCH|SW_DEFDIR,GEMAPP,CL_NORMAL,&command,tail));
    VERIFY(drive == 3 && !strcmp(cwd,"\\CHANGED"));
    return 0;
}
static int argument_checks(void)
{
    char name[] = "NEXT.PRG", tail[512] = { 0 };
    char marker[] = { 127 }, empty_environment[] = { 0,0 };
    char custom[] = "ONLY=child\0ARGV=\0old\0old-argument\0\0";
    SHELW extended = { name,0,0,NULL,custom,0,0 };
    static const char expected[] = "ONLY=child\0ARGV=NULL:2\0D:\\CHANGED\\GOOD\\NEXT.PRG\0one two\0 \0a'b\0C:\\test\0\0";
    int before;
    found = "GOOD\\NEXT.PRG";
    strcpy(tail+1,"\"one two\" '' 'a''b' C:\\test"); tail[0] = strlen(tail+1);
    VERIFY(shel_write(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_PARSE,&extended,tail) == 42);
    VERIFY(launched_tail[0] == 127 && !memcmp(launched_environment,expected,sizeof(expected)-1));
    VERIFY(!allocation_balance && !strcmp(name,"NEXT.PRG") && !strcmp(custom,"ONLY=child"));
    tail[0] = 127; memset(tail+1,'x',400); tail[401] = 0;
    VERIFY(mt_shel_write(SWM_LAUNCH,GEMAPP,CL_PARSE,name,tail,&aes_global) == 42);
    VERIFY(launched_tail[0] == 127 && !allocation_balance);
    /* Explicit ARGV is copied unchanged for CL_NORMAL, including argv[0]. */
    tail[0] = 127;
    VERIFY(shel_write(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_NORMAL,&extended,marker) == 42);
    VERIFY(!memcmp(launched_environment,custom,sizeof(custom)) && !allocation_balance);
    tail[0] = 0;
    VERIFY(shel_write(SWM_LAUNCHNOW|SW_ENVIRON,GEMAPP,SHW_PARALLEL,&extended,tail) == 42);
    VERIFY(!memcmp(launched_environment,custom,sizeof(custom)) && !allocation_balance);
    VERIFY(shel_write(SWM_LAUNCHNOW|SW_ENVIRON,GEMAPP,SHW_CHAIN,&extended,tail) == 1);
    VERIFY(!memcmp(launched_environment,custom,sizeof(custom)) && !allocation_balance);
    before = launches;
    strcpy(tail+1,"'unclosed"); tail[0] = strlen(tail+1);
    VERIFY(!shel_write(SWM_LAUNCH,GEMAPP,CL_PARSE,name,tail) && launches == before);
    tail[0] = 0; fail_allocation = TRUE;
    VERIFY(!shel_write(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_NORMAL,&extended,tail));
    fail_allocation = FALSE; launch_result = ENSMEM;
    VERIFY(!shel_write(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_PARSE,&extended,tail) && !allocation_balance);
    launch_result = 42;
    extended.env = empty_environment;
    VERIFY(shel_write(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_PARSE,&extended,tail) == 42);
    VERIFY(!memcmp(launched_environment,"ARGV=\0D:\\CHANGED\\GOOD\\NEXT.PRG\0\0",32) && !allocation_balance);
    memset(long_prefix,'X',sizeof(long_prefix)); long_prefix[32764] = long_prefix[32765] = 0;
    extended.env = long_prefix;
    VERIFY(shel_write(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_NORMAL,&extended,tail) == 42 && !allocation_balance);
    before = launches;
    VERIFY(!shel_write(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_PARSE,&extended,tail) && launches == before);
    long_prefix[32765] = 'X';
    VERIFY(!shel_write(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_NORMAL,&extended,tail) && launches == before);
    return 0;
}
static int context_checks(void)
{
    static char other_environment[] = "FOO=second\0PATH=SECOND\0\0";
    AES_APP_CONTEXT late = { 0 }, other = { 0 }, early = { 0 };
    AESGLOBAL second = { .ap_id = 7, .ap_3resv = &late }, alias = aes_global;
    DL_LAUNCH other_launch = launch;
    char command[AES_SHELL_PATH], tail[128], path[AES_SHELL_PATH], *value;
    int i, before = cwd_calls;
    /* Registration from a preinitializer precedes libptos auxiliary access.
     * Early queries fail without committing outputs; an alias can complete
     * capture once the dependency is ready, without reinitializing context. */
    aux_ready = FALSE; aes_shell_init(&early); second.ap_3resv = &early;
    strcpy(command,"unchanged"); tail[0] = 99; value = command;
    VERIFY(early.shell.capture_pending && !mt_shel_read(command,tail,&second));
    VERIFY(!strcmp(command,"unchanged") && tail[0] == 99);
    VERIFY(!mt_shel_envrn(&value,"FOO=",&second) && value == command);
    aux_ready = TRUE;
    VERIFY(mt_shel_read(command,tail,&second) && !early.shell.capture_pending);
    VERIFY(!strcmp(command,"C:\\START\\APP\\TEST.TOS") && !memcmp(tail,initial_tail,128));
    VERIFY(mt_shel_envrn(&value,"FOO=",&second) && value == environment+4);
    second.ap_3resv = &late;
    /* The launch was captured before cwd/basepage/AT_EXECFN mutations. */
    aes_shell_init(&late);
    VERIFY(mt_shel_read(command,tail,&second) && !strcmp(command,"C:\\START\\APP\\TEST.TOS"));
    VERIFY(!memcmp(tail,initial_tail,128) && cwd_calls == before);
    strcpy(other_launch.command,"D:\\OTHER\\SECOND.TOS");
    other_launch.tail[0] = 2; other_launch.tail[1] = 'y'; other_launch.tail[2] = 'o';
    other_launch.environment = PTR_TO_USERPTR(other_environment);
    other_launch.environment_size = sizeof(other_environment);
    active_launch = &other_launch; aes_shell_init(&other); active_launch = &launch;
    second.ap_3resv = &other;
    other_launch.command[0] = 'X'; other_launch.tail[1] = 'X';
    VERIFY(mt_shel_read(command,tail,&second) && !strcmp(command,"D:\\OTHER\\SECOND.TOS"));
    VERIFY(tail[0] == 2 && tail[1] == 'y' && tail[2] == 'o');
    VERIFY(mt_shel_envrn(&value,"FOO=",&second) && value == other_environment+4);
    VERIFY(mt_shel_envrn(&value,"FOO=",&alias) && value == environment+4);
    aes_app_context(&aes_global)->shell.command[0] = 'Q';
    VERIFY(mt_shel_read(command,tail,&alias) && command[0] == 'Q');
    VERIFY(mt_shel_read(command,tail,&second) && command[0] == 'D');
    aes_app_context(&aes_global)->shell.command[0] = 'C';
    /* Nested searches cover an independent context and an explicit alias.
     * Both restore the active outer helper and its DTA contents. */
    for (i = 0; i < 4; i++) {
        ULONG allocations_before = allocation_attempts;
        nested_global = i ? &alias : &second;
        nested_hit = i ? "C:\\START\\APP\\INNER.TXT" : "D:\\OTHER\\INNER.TXT";
        nested_search = TRUE; nested_result = 0; nested_restored = FALSE;
        nested_no_memory = i == 1;
        found = "C:\\START\\APP\\OUTER.TXT";
        strcpy(path,"OUTER.TXT");
        VERIFY(mt_shel_find(path,&aes_global) && !strcmp(path,found) && nested_result == (i != 1));
        VERIFY(nested_restored && (i != 3 || allocation_attempts == allocations_before));
        VERIFY(current_dta == &caller_dta && !nested_search);
    }
    nested_global = NULL; nested_hit = NULL;
    fail_release = TRUE;
    aes_shell_reset(&aes_global);
    VERIFY(allocation_balance == 1 && !aes_app_context(&aes_global)->shell.search.next);
    fail_release = FALSE;
    VERIFY(aes_resources_reset(&aes_global) && !allocation_balance);
    return 0;
}

int main(void)
{
    static const char argv_env[] = "FOO=value\0ARGV=\0alias\0PATH=FROM_ARG\0\0";
    static const char env[] = "FOO=value\0PATH=BAD;GOOD,OTHER\0NEXT=not-a-path\0\0";
    char command[AES_SHELL_PATH], tail[128], path[AES_SHELL_PATH], *value;
    AESGLOBAL global = { 0 };
    int i, before;
    aes_global = (AESGLOBAL){ .ap_version = 0x400, .ap_count = -1, .ap_id = 7 };
    {
        static AES_APP_CONTEXT context;
        aes_global.ap_3resv = &context;
    }
    memcpy(&global,&aes_global,sizeof(global));
    memcpy(environment,env,sizeof(env)); basepage.p_env = PTR_TO_USERPTR(environment);
    initial_tail[0] = 5; memcpy(initial_tail+1,"a b c",5);
    for (i = 6; i < 128; i++) initial_tail[i] = (UBYTE)i;
    memcpy(basepage.p_cmdlin,initial_tail,128); memset(&caller_dta,0x55,sizeof(DTA));
    capture();
    strcpy(cwd,"\\CHANGED"); drive = 3; executable[0] = 'X'; basepage.p_cmdlin[1] = 'X';
    VERIFY(mt_shel_read(command,tail,&global) == 1 && !strcmp(command,"C:\\START\\APP\\TEST.TOS"));
    VERIFY(!memcmp(tail,initial_tail,128) && shel_read(command,tail) == 1);
    strcpy(command,"unchanged"); tail[0] = 99;
    VERIFY(!mt_shel_read(command,tail,NULL) && !strcmp(command,"unchanged") && tail[0] == 99);
    VERIFY(!shel_read(NULL,tail) && !shel_read(command,NULL));
    VERIFY(shel_envrn(&value,"FOO=") == 1 && value == environment+4 && !strcmp(value,"value"));
    VERIFY(mt_shel_envrn(&value,"FO",&global) == 1 && !strcmp(value,"O=value"));
    VERIFY(shel_envrn(&value,"missing=") == 1 && !value);
    VERIFY(shel_envrn(&value,"") == 1 && value == environment);
    value = command; VERIFY(!mt_shel_envrn(&value,"FOO=",NULL) && value == command);
    VERIFY(!shel_envrn(NULL,"FOO=") && !shel_envrn(&value,NULL) && value == command);
    VERIFY(!context_checks());
    VERIFY(!unsupported_checks());
    VERIFY(!launch_checks(&global));
    VERIFY(!argument_checks());
    VERIFY(!directory_checks());
    VERIFY(!search("DATA.TXT","C:\\START\\APP\\DATA.TXT","C:\\START\\APP\\DATA.TXT",1));
    VERIFY(!search("DIR\\DATA.TXT","DIR\\DATA.TXT","DIR\\DATA.TXT",2));
    VERIFY(!search("DIR\\DATA.TXT","\\DATA.TXT","\\DATA.TXT",3));
    VERIFY(!search("DATA.TXT","BAD\\DATA.TXT","BAD\\DATA.TXT",4));
    VERIFY(!search("DATA.TXT","GOOD\\DATA.TXT","GOOD\\DATA.TXT",5));
    VERIFY(!search("DATA.TXT","OTHER\\DATA.TXT","OTHER\\DATA.TXT",6));
    VERIFY(!search("DATA.TXT",NULL,NULL,6));
    VERIFY(!search("*.TXT","C:\\START\\APP\\*.TXT","C:\\START\\APP\\*.TXT",1));
    strcpy(environment,"PATH=C:;D:\\"); environment[strlen(environment)+1] = 0;
    VERIFY(!search("DATA.TXT","C:DATA.TXT","C:DATA.TXT",4));
    VERIFY(!search("DATA.TXT","D:\\DATA.TXT","D:\\DATA.TXT",5));
    strcpy(environment,"PATH="); environment[6] = 0;
    VERIFY(shel_envrn(&value,"PATH=") == 1 && value == environment+5 && !*value);
    VERIFY(!search("DATA.TXT",NULL,NULL,3));
    memcpy(environment,"PATH=\0NEXT=not-a-path\0\0",23);
    VERIFY(!search("DATA.TXT",NULL,NULL,3));
    strcpy(environment,"PATH="); memset(environment+5,'L',300);
    strcpy(environment+305,";GOOD"); environment[311] = 0;
    VERIFY(!search("DATA.TXT","GOOD\\DATA.TXT","GOOD\\DATA.TXT",4));
    memcpy(environment,env,sizeof(env));
    get_error = TRUE; VERIFY(!search("DATA.TXT",NULL,NULL,0)); get_error = FALSE;
    install_error = TRUE; VERIFY(!search("DATA.TXT",NULL,NULL,0)); install_error = FALSE;
    restore_error = TRUE;
    VERIFY(!search("DATA.TXT","C:\\START\\APP\\DATA.TXT",NULL,1) && !restore_error);
    search_error = TRUE; VERIFY(!search("DATA.TXT","GOOD\\DATA.TXT",NULL,6)); search_error = FALSE;
    first_error = EREADF; VERIFY(!search("DATA.TXT","GOOD\\DATA.TXT",NULL,1));
    first_error = EACCDN; VERIFY(!search("DATA.TXT","GOOD\\DATA.TXT",NULL,1));
    first_error = ENMFIL; VERIFY(!search("DATA.TXT","GOOD\\DATA.TXT","GOOD\\DATA.TXT",5));
    first_error = EPTHNF; VERIFY(!search("DATA.TXT","GOOD\\DATA.TXT","GOOD\\DATA.TXT",5));
    first_error = 0;
    before = dta_calls;
    memset(path,'L',sizeof(path)); VERIFY(!shel_find(path) && dta_calls == before);
    VERIFY(!shel_find(NULL) && !shel_find("") && dta_calls == before);
    strcpy(path,"DATA.TXT"); registration_error = TRUE;
    VERIFY(!shel_find(path) && !strcmp(path,"DATA.TXT") && dta_calls == before); registration_error = FALSE;
    memset(long_prefix,'X',sizeof(long_prefix)); value = command;
    VERIFY(!shel_envrn(&value,long_prefix) && value == command);
    memset(environment,'X',sizeof(environment));
    VERIFY(!shel_envrn(&value,"FOO=") && value == command && !shel_find(path) && dta_calls == before);
    launch.environment = PTR_TO_USERPTR(short_environment);
    launch.environment_size = sizeof(short_environment);
    aes_shell_init(aes_app_context(&aes_global)); value = command;
    VERIFY(!shel_envrn(&value,"FOO=") && value == command && !shel_find(path) && dta_calls == before);
    basepage.p_env = 0; capture();
    VERIFY(shel_envrn(&value,"FOO=") == 1 && !value);
    basepage.p_cmdlin[0] = (char)128; capture();
    strcpy(command,"unchanged"); VERIFY(!shel_read(command,tail) && !strcmp(command,"unchanged"));
    strcpy(executable,"C:/START/APP/TEST.TOS"); basepage.p_cmdlin[0] = 0;
    capture();
    VERIFY(shel_read(command,tail) == 1 && !strcmp(command,"C:\\START\\APP\\TEST.TOS"));
    memcpy(environment,argv_env,sizeof(argv_env));
    basepage.p_env = PTR_TO_USERPTR(environment); capture();
    VERIFY(shel_envrn(&value,"PATH=") == 1 && !value);
    VERIFY(shel_envrn(&value,"ARGV=") == 1 && !value);
    VERIFY(!search("DATA.TXT","FROM_ARG\\DATA.TXT",NULL,3));
    for (i = 128; i < AES_SHELL_PATH; i++) {
        int j;
        executable[0] = 'C'; executable[1] = ':'; executable[2] = '\\';
        for (j = 3; j < i-9; j++) executable[j] = 'D';
        strcpy(executable+i-9,"\\TEST.TOS");
        capture();
        VERIFY(launch.ready && shel_read(command,tail) == 1 && !strcmp(command,executable));
        strcpy(path,executable); strcpy(path+i-8,"DATA.TXT");
        VERIFY(!search("DATA.TXT",path,path,1));
    }
    return 0;
}
