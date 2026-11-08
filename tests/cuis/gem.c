/* Standard GEM source calls, with raw process/queue probes kept test-only. */
#include <gem.h>
#include "cuis.h"
#include "gemerror.h"
#include "ptos.h"
#include "gem_shell.h"
#include "gem_buffer.h"
#include "gem_metadata.h"
#include "gem_launch.h"
#include "gem_exit.h"
#include "gem_chain.h"
#include "gem_argv.h"
#include "gem_lifecycle.h"
#include "gem_explicit.h"
#include "process_identity.h"
#define GEM_TEST_CHECK(c) do { if (!(c)) return 0x7000 | __LINE__; } while (0)
static LONG raw(ULONG cls, ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)cls << 32) | fn;
    register UQUAD fourth __asm__("r10") = d, fifth __asm__("r8") = e, sixth __asm__("r9") = f;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth), "r"(fifth), "r"(sixth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
#define PROC(fn,a,b,c,d) raw(1,fn,a,b,c,d,0,0)
/* Public native GUI endpoint operations in one real PROCESS/address space. */
static int two_client_identity(short id, LONG pid)
{
    LONG primary, extra, peer, after, sender, seen=0;
    PROCESS_IDENTITY process={0,0,0,0}, again={0,0,0,0};
    CUIS_PEER_IDENTITY_DATA a,b;
    WORD data[8]={700,32766,0,123,456,0,0,0}, out[8];
    primary=raw(CUIS_SERVICE_CLASS,CUIS_SESSION_START,0,0,0,0,0,0);
    extra=raw(CUIS_SERVICE_CLASS,CUIS_SESSION_START,CUIS_SESSION_NEW,0,0,0,0,0);
    GEM_TEST_CHECK(primary>=0 && extra>primary);
    GEM_TEST_CHECK(PROC(0x154,(WORD)-8,(uintptr_t)&process,sizeof(process),0)==0 && process.pid==pid);
    GEM_TEST_CHECK(raw(CUIS_SERVICE_CLASS,CUIS_PEER_QUERY,primary,primary,CUIS_PEER_IDENTITY,(uintptr_t)&a,16,0)==16);
    GEM_TEST_CHECK(raw(CUIS_SERVICE_CLASS,CUIS_PEER_QUERY,extra,extra,CUIS_PEER_IDENTITY,(uintptr_t)&b,16,0)==16);
    GEM_TEST_CHECK(a.client_id==(ULONG)id && b.client_id!=a.client_id &&
                   a.process_low==b.process_low && a.process_high==b.process_high &&
                   a.process_low==process.number_low && a.process_high==process.number_high);
    for(after=-1;;after=peer) {
        peer=raw(CUIS_SERVICE_CLASS,CUIS_PEER_NEXT,primary,(QUAD)after,0,0,0,0);
        if(peer<0) break;
        if(peer==primary) seen|=1;
        if(peer==extra) seen|=2;
    }
    GEM_TEST_CHECK(seen==3 && appl_find((const char *)(uintptr_t)(0xffff0000UL|(UWORD)pid))==id);
    GEM_TEST_CHECK(appl_write((short)b.client_id,16,data)==1 && data[1]==32766);
    GEM_TEST_CHECK(raw(CUIS_SERVICE_CLASS,CUIS_STREAM_READ,extra,(uintptr_t)out,16,0,(uintptr_t)&sender,0)==16 &&
                   sender==id && out[1]==id && out[3]==123 && out[4]==456);
    GEM_TEST_CHECK(raw(CUIS_SERVICE_CLASS,CUIS_STREAM_WRITE,primary,(uintptr_t)data,16,0,(UQUAD)extra+1,0)==16);
    GEM_TEST_CHECK(raw(CUIS_SERVICE_CLASS,CUIS_STREAM_READ,primary,(uintptr_t)out,16,0,(uintptr_t)&sender,0)==16 &&
                   sender==(LONG)b.client_id && out[3]==123);
    GEM_TEST_CHECK(raw(CUIS_SERVICE_CLASS,CUIS_SESSION_END,extra,0,0,0,0,0)==0 &&
                   raw(CUIS_SERVICE_CLASS,CUIS_SESSION_QUERY,primary,0,0,0,0,0)==0);
    GEM_TEST_CHECK(raw(CUIS_SERVICE_CLASS,CUIS_PEER_QUERY,primary,extra,CUIS_PEER_IDENTITY,(uintptr_t)&b,16,0)==EFILNF);
    GEM_TEST_CHECK(PROC(0x154,(WORD)-8,(uintptr_t)&again,sizeof(again),0)==0 &&
                   again.pid==process.pid && again.number_low==process.number_low && again.number_high==process.number_high);
    GEM_TEST_CHECK(appl_find((const char *)(uintptr_t)(0xffff0000UL|(UWORD)pid))==id && appl_yield()==1);
    { static const char text[]="native same-PROCESS two-client identity/routing/retirement: verified\r\n";
      (void)PROC(0x40,1,sizeof(text)-1,(uintptr_t)text,0); }
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    short id, globals[15], a = 1, b = 2, c = 3, d = 4;
    LONG pid, handle, child, status, sender;
    ULONG w[4], i;
    char tail[128] = { 1, 'c', 0 };
    static const char environment[] = "SHELLTEST=value\0PATH=C:\\BAD;C:\\RPATH,C:\\LOOKUP\0\0";
    static const char argv_environment[] = "SHELLTEST=value\0ARGV=\0fake-name\0c\0PATH=C:\\FROM_ARG\0\0";
    (void)envp;
    if (argc > 1 && (argv[1][0] == 'a' || argv[1][0] == 'r'))
        return gem_argv_child(argc,argv,envp);
    /* Exit/fault before AES registration must still notify the shell parent. */
    if (argc > 1 && argv[1][0] == 'e') return 0x5678;
    if (argc > 1 && argv[1][0] == 't') return -1;
    if (argc > 1 && argv[1][0] == 'f') __asm__ volatile ("ud2");
    GEM_TEST_CHECK(argc >= 1 && gl_apid == -1 && aes_global[2] == -1 && aes_global[3] == 0);
    GEM_TEST_CHECK(appl_yield() == 0 && evnt_timer(0) == 0 && appl_exit() == 0);
    pid = PROC(0x10b, 0, 0, 0, 0);
    id = appl_init();
    GEM_TEST_CHECK(id > 0 && appl_find((const char *)(uintptr_t)(0xffff0000UL | (UWORD)pid)) == id && gl_apid == id && aes_global[2] == id && aes_global[1] == -1);
    GEM_TEST_CHECK(appl_init() == id && mt_appl_init(globals) == id);
    if (argc > 1 && (argv[1][0] == 'l' || argv[1][0] == 'm'))
        return gem_launch_child(globals,argc,argv,envp);
    if (argc > 1 && argv[1][0] == 'n') return 0x1234;
    if (argc == 1) {
        GEM_TEST_CHECK(two_client_identity(id,pid)==0);
        GEM_TEST_CHECK(menu_register(id+1,"Wrong identity") == -1);
        GEM_TEST_CHECK(menu_register(REG_NEWNAME,"ACCTEST ") == 0);
        GEM_TEST_CHECK(appl_find("ACCTEST ") == id);
        for (i = 0; i < 6; i++) GEM_TEST_CHECK(mt_menu_register(id,"  Accessory",globals) == (short)i);
        GEM_TEST_CHECK(menu_register(id,"Full") == -1);
        GEM_TEST_CHECK(appl_exit() == 1);
        id = appl_init(); GEM_TEST_CHECK(id > 0 && mt_appl_init(globals) == id);
        GEM_TEST_CHECK(appl_find("ACCTEST ") == -1);
        /* All six old registrations disappeared when the session ended. */
        for (i = 0; i < 6; i++) GEM_TEST_CHECK(menu_register(id,"  Replacement") == (short)i);
        GEM_TEST_CHECK(appl_exit() == 1);
        id = appl_init(); GEM_TEST_CHECK(id > 0 && mt_appl_init(globals) == id);
    }
    aes_global[3] = 1234;
    GEM_TEST_CHECK(mt_appl_yield(globals) == 1 && mt_evnt_timer(1, globals) == 1);
    GEM_TEST_CHECK(appl_getinfo(32767, &a, &b, &c, &d) == 0 && !(a | b | c | d));
    GEM_TEST_CHECK(gem_shell_checks(globals,argc > 1,argc > 2) == 0);
    GEM_TEST_CHECK(gem_buffer_checks(globals,argc > 1) == 0);
    GEM_TEST_CHECK(gem_metadata_checks(globals,argc > 1) == 0);
    if (argc == 1) GEM_TEST_CHECK(gem_launch_checks(globals,envp) == 0);
    if (argc == 1) GEM_TEST_CHECK(gem_explicit_checks(globals) == 0);
    if (argc == 1) GEM_TEST_CHECK(gem_lifecycle_checks() == 0);
    if (argc == 1) GEM_TEST_CHECK(gem_exit_checks(globals) == 0);
    if (argc == 1) GEM_TEST_CHECK(gem_argv_checks(globals,envp) == 0);
    if (argc == 1) GEM_TEST_CHECK(gem_chain_checks(globals) == 0);
    if (argc > 1) {
        if (argv[1][0] == 'x') _Exit(41);
        return 37; /* Ordinary CRT finalization closes implicit registration. */
    }
    handle = raw(CUIS_SERVICE_CLASS, CUIS_SESSION_START, 0, 0, 0, 0, 0, 0);
    GEM_TEST_CHECK(handle >= 0);
    for (i = 0; i < 3; i++) {
        w[0]=11; w[1]=22; w[2]=33; w[3]=44;
        GEM_TEST_CHECK(raw(CUIS_SERVICE_CLASS, CUIS_STREAM_WRITE, handle, (uintptr_t)w, sizeof(w), 0, 0, 0) == sizeof(w));
        tail[0] = i == 2 ? 127 : 1; tail[1] = i == 1 ? 'x' : 'c';
        child = PROC(0x4b, 100, (uintptr_t)"APPS\\GEMSHELL.TOS", (uintptr_t)tail, (uintptr_t)(i == 2 ? argv_environment : environment));
        GEM_TEST_CHECK(child > 0 && child != pid);
        GEM_TEST_CHECK(evnt_timer(20) == 1 && appl_yield() == 1);
        status = PROC(0x13a, child, 0, 0, 0);
        GEM_TEST_CHECK(status == (LONG)(((ULONG)child << 16) | (i == 1 ? 41 : 37)));
        GEM_TEST_CHECK(gem_buffer_after_child(globals) == 0);
        GEM_TEST_CHECK(gem_metadata_after_child(globals) == 0);
        GEM_TEST_CHECK(raw(CUIS_SERVICE_CLASS, CUIS_STREAM_READ, handle, (uintptr_t)w,
                           sizeof(w), 0, (uintptr_t)&sender, 0) == sizeof(w));
        GEM_TEST_CHECK(sender == id && w[0] == 11 && w[1] == 22 && w[2] == 33 && w[3] == 44);
        GEM_TEST_CHECK(aes_global[3] == 1234 && gl_apid == id && globals[2] == id);
    }
    GEM_TEST_CHECK(evnt_timer(0) == 1 && mt_appl_exit(globals) == 1);
    GEM_TEST_CHECK(gl_apid == -1 && aes_global[2] == -1 && globals[2] == -1);
    GEM_TEST_CHECK(appl_exit() == 0);
    id = appl_init(); GEM_TEST_CHECK(id > 0 && appl_exit() == 1);
    {
        static const char marker[] = "x32 GEM lifecycle/timers: PASS\r\n";
        static const char buffer_marker[] = "x32 GEM shared shell buffer: PASS\r\n";
        static const char metadata_marker[] = "x32 GEM desktop/clipboard metadata: PASS\r\n";
        static const char launch_marker[] = "x32 GEM parallel shell launch: PASS\r\n";
        static const char chain_marker[] = "x32 GEM chain handoff: PASS\r\n";
        static const char exit_marker[] = "x32 GEM child exit: PASS\r\n";
        static const char shell_marker[] = "x32 GEM shell queries: PASS\r\n";
        static const char accessory_marker[] = "x32 GEM accessory registration: PASS\r\n";
        ptos_write(1, marker, sizeof(marker)-1);
        ptos_write(1, shell_marker, sizeof(shell_marker)-1);
        ptos_write(1, accessory_marker, sizeof(accessory_marker)-1);
        ptos_write(1, buffer_marker, sizeof(buffer_marker)-1);
        ptos_write(1, metadata_marker, sizeof(metadata_marker)-1);
        ptos_write(1, launch_marker, sizeof(launch_marker)-1);
        ptos_write(1, exit_marker, sizeof(exit_marker)-1);
        ptos_write(1, chain_marker, sizeof(chain_marker)-1);
    }
    return 0;
}
