/* Standard-header shell arguments in isolated dynamically linked children. */
#include <gem.h>
#include "ptos.h"
#include "gem_argv.h"
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
int gem_argv_child(int argc, char **argv, char **envp)
{
    unsigned i;
    uint32_t expected = 0;
    if (argv[1][0] == 'r') {
        VERIFY(argc == 3 && equal(argv[0],"alias") && !*argv[2]);
        VERIFY(envp[0] && equal(envp[0],"ONLY=child") && !envp[1]);
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
static int completed(short pid, short status, short *global)
{
    short message[8] = { 0 };
    VERIFY(mt_evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,
        message,2000,NULL,NULL,NULL,NULL,NULL,NULL,global) == MU_MESAG);
    if (message[4] != status) (void)failure((unsigned short)message[4]);
    VERIFY(message[0] == CH_EXIT && !message[1] && !message[2] && message[3] == pid && message[4] == status);
    VERIFY(mt_evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,
        message,0,NULL,NULL,NULL,NULL,NULL,NULL,global) == MU_TIMER);
    return 0;
}
int gem_argv_checks(short *global, char **envp)
{
    char command[] = "C:\\APPS\\GEMCHILD.PRG", tail[512] = { 127 }, saved[512];
    char console[] = "C:\\APPS\\GEMCHILD.TOS";
    char custom[] = "ARGVCUSTOM=child\0KEEP=still\0\0";
    char raw[] = "ONLY=child\0ARGV=NULL:2\0alias\0raw\0 \0\0";
    char *custom_envp[] = { "ARGVCUSTOM=child","KEEP=still",NULL };
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
    pid = shel_write(SWM_LAUNCH|SW_ENVIRON,GEMAPP,CL_NORMAL,&extended,tail);
    VERIFY(pid > 0 && completed(pid,0x68,global) == 0);
    return 0;
}
