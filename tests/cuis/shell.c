/* Standard GEM shell queries use startup metadata and a private search DTA. */
#include "gem.h"
#include "shell_context.h"
#include "transport.h"
#include "gemerror.h"
#include "string.h"
#include "runtime.h"
#define VERIFY(c) do { if (!(c)) { __builtin_printf("shell failure: %d\n",__LINE__); return __LINE__; } } while (0)
short aes_global[15] = { 0x400,-1,7 };
static PD basepage;
static DL_LAUNCH launch;
static char environment[AES_SHELL_ENV], long_prefix[AES_SHELL_ENV];
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
LONG aes_application_session(short *global)
{ return global && global[2] == 7 && !registration_error ? 9 : EACCDN; }
unsigned long __ptos_getauxval(unsigned long type)
{ return type == DL_AT_PTOS_LAUNCH ? (uintptr_t)&launch : type == 0x5000 ? (uintptr_t)&basepage : type == 31 ? (uintptr_t)executable : 0; }
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
        memset(current_dta,0x66,sizeof(DTA));
        if (attempts < 16) strcpy(tried[attempts],(char *)(uintptr_t)a);
        attempts++;
        if (attempts == 1 && first_error) return (QUAD)first_error;
        return found && !search_error && !strcmp(found,(char *)(uintptr_t)a) ? E_OK : (QUAD)EFILNF;
    }
    return (QUAD)EINVFN;
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
    dl_capture_launch(&launch,executable);
    aes_library_init();
}
static int search(const char *input, const char *hit, const char *result, int count)
{
    char path[AES_SHELL_PATH];
    DTA saved_dta;
    SHELL_CONTEXT sentinel;
    int before = cwd_calls;
    strcpy(path,input); found = hit; attempts = 0; saved_dta = caller_dta;
    aes_shell = &sentinel;
    VERIFY(mt_shel_find(path,aes_global) == (result != NULL));
    VERIFY(!strcmp(path,result ? result : input) && attempts == count);
    VERIFY(current_dta == &caller_dta && !memcmp(&caller_dta,&saved_dta,sizeof(DTA)));
    VERIFY(aes_shell == &sentinel && cwd_calls == before);
    aes_shell = NULL; return 0;
}
int main(void)
{
    static const char argv_env[] = "FOO=value\0ARGV=\0alias\0PATH=FROM_ARG\0\0";
    static const char env[] = "FOO=value\0PATH=BAD;GOOD,OTHER\0NEXT=not-a-path\0\0";
    char command[AES_SHELL_PATH], tail[128], path[AES_SHELL_PATH], *value;
    short global[15];
    int i, before;
    memcpy(global,aes_global,sizeof(global));
    memcpy(environment,env,sizeof(env)); basepage.p_env = PTR_TO_USERPTR(environment);
    initial_tail[0] = 5; memcpy(initial_tail+1,"a b c",5);
    for (i = 6; i < 128; i++) initial_tail[i] = (UBYTE)i;
    memcpy(basepage.p_cmdlin,initial_tail,128); memset(&caller_dta,0x55,sizeof(DTA));
    capture();
    strcpy(cwd,"\\CHANGED"); drive = 3; executable[0] = 'X'; basepage.p_cmdlin[1] = 'X';
    VERIFY(mt_shel_read(command,tail,global) == 1 && !strcmp(command,"C:\\START\\APP\\TEST.TOS"));
    VERIFY(!memcmp(tail,initial_tail,128) && shel_read(command,tail) == 1);
    strcpy(command,"unchanged"); tail[0] = 99;
    VERIFY(!mt_shel_read(command,tail,NULL) && !strcmp(command,"unchanged") && tail[0] == 99);
    VERIFY(!shel_read(NULL,tail) && !shel_read(command,NULL));
    VERIFY(shel_envrn(&value,"FOO=") == 1 && value == environment+4 && !strcmp(value,"value"));
    VERIFY(mt_shel_envrn(&value,"FO",global) == 1 && !strcmp(value,"O=value"));
    VERIFY(shel_envrn(&value,"missing=") == 1 && !value);
    VERIFY(shel_envrn(&value,"") == 1 && value == environment);
    value = command; VERIFY(!mt_shel_envrn(&value,"FOO=",NULL) && value == command);
    VERIFY(!shel_envrn(NULL,"FOO=") && !shel_envrn(&value,NULL) && value == command);
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
