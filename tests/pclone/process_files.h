/* Public shared-VM descriptor inheritance. Private selectors only query
 * owners and inject failures here. The search suite also uses private clones. */
#if defined(TEST_WITH_PROCESS_FILES)
#define FILES_CLONE_TAG 0xc9690000UL
static volatile struct {
    long file,duplicate,second,later,parent,pid,gate,entered,error;
    int inherit_cwd;
} files_probe;
static _DTA files_dta;
static char files_parent_cwd[128];
static char files_pattern[]="C:\\FD969?.TMP";
static long files_query(unsigned index)
{ return pclone_trap(0,FILES_CLONE_TAG+0x100+index); }
static void files_dta_descendant(void *argument)
{
    _DTA own_dta;

    if ((_DTA *)Fgetdta()!=argument) Pterm(1);
    (void)Fsetdta(&own_dta);
    Pterm((_DTA *)Fgetdta()==&own_dta ? 0 : 2);
}
static void files_entry(void *argument)
{
    struct ptos_clone_args descendant={0};
    char a=0,b=0,c=0;
    char cwd[128];
    _DTA own_dta;
    int32_t descendant_pid=0;
    long duplicate,status;

    files_probe.entered=1;files_probe.pid=Pgetpid();
    while (!files_probe.gate) (void)Syield();
    if (Fgetdta() || Fsnext()!=-40L || Fsfirst(files_pattern,0)!=-40L) files_probe.error|=8;
    (void)Fsetdta(&files_dta);
    if (Fsnext()!=-49L) files_probe.error|=8;
    (void)Fsetdta(&own_dta);
    if ((_DTA *)Fgetdta()!=&own_dta || Fsfirst(files_pattern,0) || Fsnext() || Fsnext()!=-49L)
        files_probe.error|=64;
    descendant.flags=CLONE_VM|(files_probe.inherit_cwd ? 0 : CLONE_COPY_STANDARD_HANDLES);
    descendant.entry=files_dta_descendant;
    descendant.argument=&own_dta;
    descendant.stack_base=argument;
    descendant.stack_size=RUNNABLE_STACK_SIZE;
    descendant.child_pid=&descendant_pid;
    status=pclone_trap(&descendant,sizeof(descendant));
    if (status>0) status=Pwaitpid(descendant_pid,0,0);
    if (descendant_pid<=0 || (status>>16)!=descendant_pid || (status&65535) ||
        (_DTA *)Fgetdta()!=&own_dta) files_probe.error|=128;
    if (Dgetpath(cwd,3) || ((!strcmp(cwd,files_parent_cwd)) != files_probe.inherit_cwd))
        files_probe.error|=16;
    if (Fread(files_probe.later,1,&c)!=-37L) files_probe.error|=32;
    if (files_probe.pid==files_probe.parent || Fread(files_probe.file,1,&a)!=1 || a!='a' ||
        Fread(files_probe.duplicate,1,&b)!=1 || b!='b') files_probe.error|=1;
    if (Fclose(files_probe.file) || Fclose(files_probe.file)!=-37L ||
        Fread(files_probe.duplicate,1,&c)!=1 || c!='c') files_probe.error|=2;
    duplicate=Fdup(0);
    if (duplicate<6 || Fclose(duplicate) || Cconin()!='d' ||
        Fforce(0,files_probe.second) || Fclose(0) || Fclose(files_probe.duplicate) ||
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
static const char files_orphan_path[]="C:\\FD969O.TMP";
static volatile struct {
    unsigned char *stack;
    long file,gate,done,error;
    int32_t pid;
} files_orphan;
static void files_orphan_entry(void *argument)
{
    char bytes[3];

    (void)argument;
    while (!files_orphan.gate) (void)Syield();
    if (Pgetpid()!=files_orphan.pid || Fread(files_orphan.file,sizeof(bytes),bytes)!=sizeof(bytes) ||
        memcmp(bytes,"ofs",sizeof(bytes)) || Fclose(files_orphan.file)) files_orphan.error=1;
    files_orphan.done=1;
    Pterm(75);
}
static void files_orphan_source(void *argument)
{
    int32_t pid=0;
    long file;

    (void)argument;
    file=Fcreate(files_orphan_path,0);
    files_orphan.file=file;
    if (file<6 || Fwrite(file,3,"ofs")!=3 || Fseek(0,file,0)!=0 ||
        launch_entry(files_orphan_entry,CLONE_VM|CLONE_COPY_ALL_HANDLES,files_orphan.stack,&pid,NULL)<=0)
        files_orphan.error=1;
    files_orphan.pid=pid;
#if defined(TEST_WITH_MULTITHREADING)
    (void)term_group(74); /* retire just the source PROCESS */
    files_orphan.error=1;
#endif
    Pterm(74);
}
static void files_test_orphan(unsigned char *source_stack)
{
    long baseline[8],stack,result,status;
    int32_t pid=0;
    unsigned i;

    stack=Malloc(RUNNABLE_STACK_SIZE);
    ptest_assert_msg(stack>0,"descriptor orphan stack");
    if (stack<=0) return;
    files_orphan.stack=(unsigned char *)stack;
    files_counts(baseline);
    result=launch_entry(files_orphan_source,CLONE_VM,source_stack,&pid,NULL);
    status=result>0 ? Pwaitpid(pid,0,0) : result;
    ptest_assert_msg(result>0 && (status>>16)==pid && (status&65535)==74 &&
                     files_orphan.pid>0 && !files_orphan.done && !files_orphan.error &&
                     files_query(0)==baseline[0]+1 && files_query(3)==baseline[3]+1,
                     "source exit leaves the orphan's one exact binding alive");
    files_orphan.gate=1;
    for (i=0; i<64 && !files_orphan.done; i++) (void)Syield();
    (void)Syield(); (void)Syield();
    ptest_assert_msg(files_orphan.done && !files_orphan.error && Pwaitpid(files_orphan.pid,1,0)==-33L,
                     "orphan retains the shared offset and retires outside the grandparent's wait scope");
    ptest_assert_msg(Fdelete(files_orphan_path)==0,"descriptor orphan releases its file");
    files_compare(baseline,"orphan exit restores exact descriptor owners");
    ptest_assert_msg(Mfree((void *)stack)==0,"descriptor orphan stack released");
}
static void test_process_files(void)
{
    struct ptos_clone_args args={0};
    int32_t pid=0,tid=0;
    long baseline[8],prepared[8],stack,descendant_stack,result,status;
    int mode,stage;
    char c=0;
    char saved_cwd[130]="C:";
    _DTA *saved_dta=(_DTA *)Fgetdta();
    void *parent_dta_mirror;
    const char *first,*second;
    ptest_begin("Public Pclone descriptor inheritance (#1040)");
    ptest_assert_msg(Fclose(81)==-37L && Fclose(32767)==-37L,"invalid local descriptors retain EIHNDL ABI");
    stack=Malloc(RUNNABLE_STACK_SIZE);
    ptest_assert_msg(stack>0,"descriptor native child stack");
    if(stack<=0){ptest_pass();return;}
    descendant_stack=Malloc(RUNNABLE_STACK_SIZE);
    ptest_assert_msg(descendant_stack>0,"descriptor descendant stack");
    if(descendant_stack<=0){(void)Mfree((void *)stack);ptest_pass();return;}
    ptest_assert_msg(!Dgetpath(saved_cwd+2,3) && !Dsetpath("C:\\TESTS") &&
                     !Dgetpath(files_parent_cwd,3) && files_parent_cwd[0],"prepare a non-root parent CWD");
    if (!saved_cwd[2]) strcpy(saved_cwd+2,"\\");
    files_counts(baseline);
    args.entry=files_entry;
    args.argument=(void *)descendant_stack;
    args.stack_base=(void *)stack;args.stack_size=RUNNABLE_STACK_SIZE;
    args.child_pid=&pid;args.child_tid=&tid;
    for(mode=0;mode<((Drvmap()&(1L<<16))?4:2);mode++) {
        unsigned long size=mode&1 ? PTOS_CLONE_ARGS_MIN_SIZE : PTOS_CLONE_ARGS_PID_SIZE;

        first=mode<2?"C:\\FD969A.TMP":"Q:\\FD969A.TMP";
        second=mode<2?"C:\\FD969B.TMP":"Q:\\FD969B.TMP";
        files_pattern[0]=first[0];
        memset((void *)&files_probe,0,sizeof(files_probe));files_probe.parent=Pgetpid();
        files_probe.inherit_cwd=!(mode&1);
        args.flags=CLONE_VM|CLONE_COPY_ALL_HANDLES|
                   (files_probe.inherit_cwd ? CLONE_COPY_CWD : CLONE_COPY_STANDARD_HANDLES);
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
        (void)Fsetdta(&files_dta);
        parent_dta_mirror=_base->p_dta;
        ptest_assert_msg(!Fsfirst(files_pattern,0),"keep a live parent search through clone");
        files_counts(prepared);
        for(stage=1;stage<=9;stage++) {
            unsigned long injection=FILES_CLONE_TAG+(stage<=4?0x200+stage:0x300+stage-4);
            ptest_assert_msg(pclone_trap(0,injection)==0,"arm existing clone/factory preparation failure");
            pid=0x12345678;tid=0x23456789;
            result=pclone_trap(&args,sizeof(args));
            ptest_assert_msg(result==-39L && pid==0x12345678 && tid==0x23456789 && !files_probe.entered,
                             "all descriptor resource failure prefixes publish no child or output");
            files_compare(prepared,"failed clone restores SFT, bindings, snapshot admission and operations");
        }
        args.child_pid=&tid;tid=0x23456789;
        ptest_assert_msg(pclone_trap(&args,sizeof(args))==-25L && tid==0x23456789 && !files_probe.entered,
                         "overlapping output rejection leaves outputs and child unpublished");
        files_compare(prepared,"output rejection releases the complete descriptor snapshot");
        args.child_pid=&pid;
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
        ptest_assert_msg(pclone_trap(0,0xc8920405UL)==0,"arm observer acquisition failure");
        ptest_assert_msg(pclone_trap(&args,sizeof(args))==-39L && !files_probe.entered,
                         "observer failure rolls back the descriptor snapshot");
        files_compare(prepared,"observer failure restores exact descriptor owners");
        ptest_assert_msg(pclone_trap(0,0xc8920600UL)==0,"arm sealed publication abandonment");
        pid=0x12345678;tid=0x23456789;
        ptest_assert_msg(pclone_trap(&args,sizeof(args))==-326L && pid==0x12345678 &&
                         tid==0x23456789 && !files_probe.entered,
                         "sealed publication rollback leaves outputs unchanged");
        files_compare(prepared,"abandoning final publication releases exact staged references");
#endif
        pid=0x12345678;tid=0;result=pclone_trap(&args,size);
        ptest_assert_msg(result>0 && result==tid &&
                         (size==PTOS_CLONE_ARGS_MIN_SIZE ? pid==0x12345678 : pid>0 && pid!=Pgetpid()),
                         "public 28/32-byte clone keeps distinct PID/TID output semantics");
        if(result>0) {
            ptest_assert_msg(files_query(0)==prepared[0]+4 && files_query(3)==prepared[3]+4 &&
                             !files_query(1) && !files_query(4) && !files_query(5),
                             "publication owns all four inherited references and releases admission");
            files_probe.later=Fopen(second,2);
            ptest_assert_msg(files_probe.later>=6 && Fforce(0,files_probe.later)==0,
                             "parent opens a new handle and rebinds its standard alias after publication");
            if(mode&1) {
                ptest_assert_msg(Fclose(files_probe.file)==0 && Fclose(files_probe.duplicate)==0 &&
                                 Fclose(0)==0 && Fclose(files_probe.second)==0,"parent closes before the child accesses its bindings");
            }
            files_probe.gate=1;status=Pwaitpid(size==PTOS_CLONE_ARGS_MIN_SIZE?-1:pid,0,0);
            if (size==PTOS_CLONE_ARGS_MIN_SIZE) pid=status>>16;
            if (files_probe.error || (status>>16)!=pid || (status&65535)!=73)
                printf("descriptor clone mode %d pid %ld worker %ld status %lx checks %lx\r\n",
                       mode,(long)pid,files_probe.pid,(unsigned long)status,(unsigned long)files_probe.error);
            ptest_assert_msg(pid>0 && pid!=Pgetpid() && (status>>16)==pid && (status&65535)==73 && !files_probe.error &&
                             files_probe.pid==pid,"child inherits same local numbers, OFD cursor and independently closes");
            ptest_assert_msg(Pwaitpid(pid,1,0)==-33L,"descriptor child completion is publicly reaped once");
            if(!(mode&1)) {
                ptest_assert_msg(Fseek(0,files_probe.file,1)==4 && Fseek(0,files_probe.duplicate,1)==4 &&
                                 Fseek(0,files_probe.second,1)==1 && Fread(files_probe.file,1,&c)==1 && c=='e',
                                 "child close preserves parent binding and shared open-file positions");
                ptest_assert_msg(Fclose(files_probe.file)==0 && Fclose(files_probe.duplicate)==0 &&
                                 Fclose(0)==0 && Fclose(files_probe.second)==0,"parent final close consumes its own exact references");
            }
            ptest_assert_msg(Fclose(files_probe.later)==0,"child mutations preserve the new parent handle");
        } else {
            (void)Fclose(files_probe.file);(void)Fclose(files_probe.duplicate);(void)Fclose(0);(void)Fclose(files_probe.second);
        }
        ptest_assert_msg((_DTA *)Fgetdta()==&files_dta && _base->p_dta==parent_dta_mirror &&
                         !Fsnext() && Fsnext()==-49L,
                         "parent retains its DTA mirror and search after the child and descendant set their DTAs");
        (void)Fsetdta(saved_dta);
        ptest_assert_msg(Fdelete(first)==0 && Fdelete(second)==0,"descriptor fixtures have no leaked file lifetime");
        files_compare(baseline,"both exit orders restore exact SFT/binding/admission/operation baselines");
    }
    ptest_assert_msg(!Dsetpath(saved_cwd),"restore the parent CWD");
    files_test_orphan((unsigned char *)stack);
    ptest_assert_msg(Mfree((void *)descendant_stack)==0,"descriptor descendant stack released");
    ptest_assert_msg(Mfree((void *)stack)==0,"descriptor native stack released");
    ptest_pass();
    if(!ptest_failures())conws("native PROCESS descriptor inheritance/ownership: PASS\r\n");
}
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
#include "process_search.h"
#endif
#endif
