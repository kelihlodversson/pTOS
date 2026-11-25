/* Standard-header shell arguments in isolated dynamically linked children. */
#include <gem.h>
#include "ptos.h"
#include "gem_argv.h"
#include "gem_process.h"
static int failure(unsigned line)
{
    const char *digits = "0123456789abcdef";
    char text[] = "x32 GEM ARGV failure: 0000\r\n";
    unsigned i;
    for (i = 0; i < 4; i++) text[22+i] = digits[(line >> (12-i*4)) & 15];
    (void)ptos_write(2,text,sizeof(text)-1);
    return 0x9500 | line;
}
#define VERIFY(c) do { if (!(c)) return failure(__LINE__); } while (0)
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static uint32_t hash(char **envp)
{
    uint32_t value = 2166136261UL;
    unsigned i, j;
    for (i = 0; envp[i]; i++) {
        for (j = 0; envp[i][j]; j++) value = (value ^ (unsigned char)envp[i][j])*16777619UL;
        value *= 16777619UL;
    }
    return value;
}
static int32_t launch_direct(const char *path, char *const argv[], char *const envp[])
{
    register uint64_t ax __asm__("rax") = (1ULL << 32) | 0x193;
    register uint64_t env __asm__("r10") = (uintptr_t)envp;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(100ULL), "S"((uintptr_t)path),
                      "d"((uintptr_t)argv), "r"(env) : "rcx","r11","memory","cc");
    return (int32_t)ax;
}
int gem_argv_child(int argc, char **argv, char **envp)
{
    unsigned i;
    uint32_t expected = 0;
    if (argv[1][0] == 'r') {
        const char *path = NULL, *again;
        const char *const *saved_argv = NULL, *const *saved_envp = NULL;
        const char *const *read_args, *const *read_env;
        AESGLOBAL alias;
        char command[260], tail[128], *live;
        int empty = equal(argv[1],"read-empty");
        unsigned j;
        VERIFY(argc == 4 && equal(argv[0],"alias") && !*argv[2]);
        for (j = 0; j < 400; j++) VERIFY(argv[3][j] == 'y');
        VERIFY(!argv[3][400]);
        VERIFY(empty ? !envp[0] : (envp[0] && equal(envp[0],"ONLY=child") && !envp[1]));
        VERIFY(appl_init() > 0);
        alias = aes_global;
        VERIFY(shel_readve(&path,&saved_argv,&saved_envp));
        VERIFY(equal(path,"C:\\APPS\\GEMCHILD.PRG") && equal(saved_argv[0],"alias") &&
               equal(saved_argv[1],argv[1]) && !*saved_argv[2] && equal(saved_argv[3],argv[3]) && !saved_argv[4]);
        VERIFY(empty ? !saved_envp[0] : (saved_envp[0] && equal(saved_envp[0],"ONLY=child") && !saved_envp[1]));
        command[0] = tail[0] = 'x';
        VERIFY(!shel_read(command,tail) && command[0] == 'x' && tail[0] == 'x');
        argv[0][0] = 'X'; argv[3][0] = 'Z';
        if (envp[0]) {
            envp[0][5] = 'X';
            VERIFY(shel_envrn(&live,"ONLY=") && equal(live,"Xhild"));
            VERIFY(equal(saved_envp[0],"ONLY=child"));
        }
        VERIFY(mt_shel_readve(&again,&read_args,&read_env,&alias) &&
               again == path && read_args == saved_argv && read_env == saved_envp);
        VERIFY(equal(saved_argv[0],"alias") && saved_argv[3][0] == 'y');
        VERIFY(appl_exit());
        VERIFY(!shel_readve(&path,&saved_argv,&saved_envp));
        return 0x68;
    }
    VERIFY(argc == 8 && (equal(argv[0],"C:\\APPS\\GEMCHILD.PRG") || equal(argv[0],"C:\\APPS\\GEMCHILD.TOS")));
    for (i = 0; i < 8; i++)
        expected = (expected << 4) | (argv[2][i] <= '9' ? argv[2][i]-'0' : argv[2][i]-'a'+10);
    VERIFY(hash(envp) == expected && !*argv[3] && equal(argv[4],"two words"));
    VERIFY(equal(argv[5],"can't") && equal(argv[6],"C:\\test"));
    for (i = 0; i < 400; i++) VERIFY(argv[7][i] == 'x');
    VERIFY(!argv[7][400]);
    return 0x67;
}
static int completed(short pid, short status, AESGLOBAL *global)
{
    short message[8] = { 0 };
    VERIFY(mt_evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,
        message,2000,NULL,NULL,NULL,NULL,NULL,NULL,global) == MU_MESAG);
    if (message[4] != status) (void)failure((unsigned short)message[4]);
    VERIFY(message[0] == CH_EXIT && !message[1] && !message[2] && message[3] == pid && message[4] == status);
    VERIFY(gem_wait_child(pid) == (int32_t)(((uint32_t)pid << 16) | ((unsigned short)status & 255)));
    VERIFY(mt_evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,
        message,0,NULL,NULL,NULL,NULL,NULL,NULL,global) == MU_TIMER);
    return 0;
}
int gem_argv_checks(AESGLOBAL *global, char **envp)
{
    char command[] = "C:\\APPS\\GEMCHILD.PRG", tail[512] = { 127 }, saved[512];
    char console[] = "C:\\APPS\\GEMCHILD.TOS";
    char custom[] = "ARGVCUSTOM=child\0KEEP=still\0\0";
    char raw[] = "ONLY=child\0ARGV=NULL:2\0alias\0raw\0 \0\0";
    char *custom_envp[] = { "ARGVCUSTOM=child","KEEP=still",NULL };
    char only[] = "ONLY=child", long_arg[401];
    char *custom_args[] = { "alias", "read", "", long_arg, NULL };
    char *only_env[] = { only, NULL }, *no_env[] = { NULL };
    volatile uintptr_t invalid_vector = 1;
    const char *digits = "0123456789abcdef", *arguments = " '' \"two words\" 'can''t' C:\\test ";
    SHELW extended = { command,0,0,NULL,custom,0,0 };
    short pid;
    unsigned pass, i, at;
    uint32_t value;
    for (pass = 0; pass < 3; pass++) {
        extended.newcmd = pass == 2 ? console : command;
        value = hash(pass ? custom_envp : envp);
        tail[1] = 'a'; tail[2] = 'r'; tail[3] = 'g'; tail[4] = 'v'; tail[5] = ' ';
        for (i = 0; i < 8; i++) tail[6+i] = digits[(value >> (28-i*4)) & 15];
        at = 14;
        for (i = 0; arguments[i]; i++) tail[at++] = arguments[i];
        for (i = 0; i < 400; i++) tail[at++] = 'x';
        tail[at] = 0;
        for (i = 0; i < sizeof(tail); i++) saved[i] = tail[i];
        pid = pass ? mt_shel_write(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_PARSE,&extended,tail,global) :
                     shel_write(SWM_LAUNCH,GEMAPP,CL_PARSE,command,tail);
        VERIFY(pid > 0 && completed(pid,0x67,global) == 0);
        for (i = 0; i < sizeof(tail); i++) VERIFY(tail[i] == saved[i]);
        VERIFY(equal(command,"C:\\APPS\\GEMCHILD.PRG") && equal(custom,"ARGVCUSTOM=child"));
    }
    extended.newcmd = command;
    extended.env = raw;
    VERIFY(!shel_write(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_NORMAL,&extended,tail));
    for (i = 0; i < 400; i++) long_arg[i] = 'y';
    long_arg[400] = 0;
    VERIFY(!shel_writeve(SWM_LAUNCH,GEMAPP,CL_PARSE,command,custom_args,only_env));
    VERIFY(!shel_writeve(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_NORMAL,&extended,custom_args,only_env));
    VERIFY(!shel_writeve(SWM_LAUNCH,GEMAPP,CL_NORMAL,command,(char *const *)invalid_vector,only_env));
    pid = shel_writeve(SWM_LAUNCH,GEMAPP,CL_NORMAL,command,custom_args,only_env);
    long_arg[0] = 'z'; only[5] = 'x';
    VERIFY(pid > 0 && completed(pid,0x68,global) == 0);
    long_arg[0] = 'y'; only[5] = 'c';
    custom_args[1] = "read-empty";
    pid = launch_direct(command,custom_args,no_env);
    if (pid <= 0) (void)failure((uint16_t)pid);
    VERIFY(pid > 0 && gem_wait_child_flags(pid,0) == (int32_t)(((uint32_t)pid << 16) | 0x68));
    (void)ptos_write(1,"x32 GEM shell vectors: PASS\r\n",29);
    return 0;
}
