/* Real-trap PROCESS descriptor prerequisite. The private transport selects the
 * existing generic clone resource policy; it does not implement public fork. */
#if defined(TEST_WITH_PROCESS_FILES)
#define FILES_CLONE_TAG 0xc9690000UL
static volatile struct {
    long file,duplicate,second,parent,pid,gate,entered,error;
} files_probe;
static long files_query(unsigned index)
{ return pclone_trap(0,FILES_CLONE_TAG+0x100+index); }
static void files_entry(void *argument)
{
    char a=0,b=0,c=0;
    (void)argument;files_probe.entered=1;files_probe.pid=Pgetpid();
    while (!files_probe.gate) (void)Syield();
    if (files_probe.pid==files_probe.parent || Fread(files_probe.file,1,&a)!=1 || a!='a' ||
        Fread(files_probe.duplicate,1,&b)!=1 || b!='b') files_probe.error|=1;
    if (Fclose(files_probe.file) || Fclose(files_probe.file)!=-37L ||
        Fread(files_probe.duplicate,1,&c)!=1 || c!='c') files_probe.error|=2;
    if (Cconin()!='d' || Fclose(0) || Fclose(files_probe.duplicate) ||
        Fwrite(files_probe.second,1,"z")!=1 || Fclose(files_probe.second)) files_probe.error|=4;
    Pterm(73);
}
static void files_counts(long *out)
{ unsigned i;for(i=0;i<8;i++)out[i]=files_query(i); }
static void files_compare(const long *before,const char *message)
{
    long after[8];files_counts(after);
    ptest_assert_msg(!memcmp(before,after,sizeof(after)),message);
}
static void test_process_files(void)
{
    struct ptos_clone_args args={0};
    int32_t pid=0,tid=0;
    long baseline[8],prepared[8],stack,result,status;
    int mode,stage;
    char c=0;
    const char *first,*second;
    ptest_begin("PROCESS-local descriptors (prerequisite)");
    files_counts(baseline);
    ptest_assert_msg(Fclose(81)==-37L && Fclose(32767)==-37L,"invalid local descriptors retain EIHNDL ABI");
    stack=Malloc(RUNNABLE_STACK_SIZE);
    ptest_assert_msg(stack>0,"descriptor native child stack");
    if(stack<=0){ptest_pass();return;}
    args.flags=CLONE_VM|CLONE_COPY_CWD;args.entry=files_entry;
    args.stack_base=(void *)stack;args.stack_size=RUNNABLE_STACK_SIZE;
    args.child_pid=&pid;args.child_tid=&tid;
    for(mode=0;mode<((Drvmap()&(1L<<16))?4:2);mode++) {
        first=mode<2?"C:\\FD969A.TMP":"Q:\\FD969A.TMP";
        second=mode<2?"C:\\FD969B.TMP":"Q:\\FD969B.TMP";
        memset((void *)&files_probe,0,sizeof(files_probe));files_probe.parent=Pgetpid();
        files_probe.file=Fcreate(first,0);
        ptest_assert_msg(files_probe.file>=6,"open explicit parent descriptor");
        if(files_probe.file<6)break;
        ptest_assert_msg(Fwrite(files_probe.file,6,"abcdef")==6 && Fseek(0,files_probe.file,0)==0 &&
                         Fforce(0,files_probe.file)==0,"redirect standard input from explicit binding");
        files_probe.duplicate=Fdup(0);
        files_probe.second=Fcreate(second,0);
        ptest_assert_msg(files_probe.duplicate>=6 && files_probe.second>=6 &&
                         files_probe.duplicate!=files_probe.second,"duplicate binding displaces later open number");
        ptest_assert_msg(Cconin()=='a' && Crawcin()=='b' && Cnecin()=='c' &&
                         Fseek(0,files_probe.file,0)==0,"actual standard-input traps route local descriptor zero");
        files_counts(prepared);
        for(stage=1;stage<=9;stage++) {
            unsigned long injection=FILES_CLONE_TAG+(stage<=4?0x200+stage:0x300+stage-4);
            ptest_assert_msg(pclone_trap(0,injection)==0,"arm existing clone/factory preparation failure");
            pid=0x12345678;tid=0x23456789;
            result=pclone_trap(&args,FILES_CLONE_TAG+sizeof(args));
            ptest_assert_msg(result==-39L && pid==0x12345678 && tid==0x23456789 && !files_probe.entered,
                             "all descriptor resource failure prefixes publish no child or output");
            files_compare(prepared,"failed clone restores SFT, bindings, snapshot admission and operations");
        }
        pid=tid=0;result=pclone_trap(&args,FILES_CLONE_TAG+sizeof(args));
        ptest_assert_msg(result>0 && pid>0 && pid!=Pgetpid() && result==tid,"descriptor constructor publishes distinct child PID/TID");
        if(result>0) {
            ptest_assert_msg(files_query(0)==prepared[0]+4 && files_query(3)==prepared[3]+4 &&
                             !files_query(1) && !files_query(4) && !files_query(5),
                             "publication owns all four inherited references and releases admission");
            if(mode&1) {
                ptest_assert_msg(Fclose(files_probe.file)==0 && Fclose(files_probe.duplicate)==0 &&
                                 Fclose(0)==0 && Fclose(files_probe.second)==0,"parent closes independently before child runs");
            }
            files_probe.gate=1;status=Pwaitpid(pid,0,0);
            ptest_assert_msg((status>>16)==pid && (status&65535)==73 && !files_probe.error &&
                             files_probe.pid==pid,"child inherits same local numbers, OFD cursor and independently closes");
            ptest_assert_msg(Pwaitpid(pid,1,0)==-33L,"descriptor child completion is publicly reaped once");
            if(!(mode&1)) {
                ptest_assert_msg(Fseek(0,files_probe.file,1)==4 && Fseek(0,files_probe.duplicate,1)==4 &&
                                 Fseek(0,files_probe.second,1)==1 && Fread(files_probe.file,1,&c)==1 && c=='e',
                                 "child close preserves parent binding and shared open-file positions");
                ptest_assert_msg(Fclose(files_probe.file)==0 && Fclose(files_probe.duplicate)==0 &&
                                 Fclose(0)==0 && Fclose(files_probe.second)==0,"parent final close consumes its own exact references");
            }
        } else {
            (void)Fclose(files_probe.file);(void)Fclose(files_probe.duplicate);(void)Fclose(0);(void)Fclose(files_probe.second);
        }
        ptest_assert_msg(Fdelete(first)==0 && Fdelete(second)==0,"descriptor fixtures have no leaked file lifetime");
        files_compare(baseline,"both exit orders restore exact SFT/binding/admission/operation baselines");
    }
    ptest_assert_msg(Mfree((void *)stack)==0,"descriptor native stack released");
    ptest_pass();
    if(!ptest_failures())conws("native PROCESS descriptor inheritance/ownership: PASS\r\n");
}
#include "process_search.h"
#endif
