/* #984: production private roots and search owners, no public fork ABI. */
static _DTA *search_a,*search_b;
static char search_first[14],search_second[14],search_third[14];
static void search_entry(void *argument)
{
    int bad=0;
    (void)argument;
    if((_DTA *)Fgetdta()!=search_a) bad=1;
    if(Fsnext() || strcmp(search_a->dta_name,search_second)) bad=2;
    if(Fsnext() || strcmp(search_a->dta_name,search_third) || Fsnext()!=-49L) bad=3;
    (void)Fsetdta(search_b);
    if(Fsnext() || strcmp(search_b->dta_name,search_second) ||
       Fsnext() || strcmp(search_b->dta_name,search_third) || Fsnext()!=-49L) bad=4;
    (void)Pterm(bad?84:83);
}
static void test_process_search(void)
{
    char files[3][20]={"C:\\S984A.TMP","C:\\S984B.TMP","C:\\S984C.TMP"};
    char pattern[]="C:\\S984?.TMP";
    struct ptos_clone_args args={0};
    _DTA *original=(_DTA *)Fgetdta(),stack_dta;
    long stack=Malloc(RUNNABLE_STACK_SIZE),storage=Malloc(12288),child,status;
    long before[8],prepared[8];
    int i,mode,provider,stage;
    ptest_begin("trusted DTA and independent private search snapshots");
    search_b=(_DTA *)Malloc(sizeof(_DTA));
    ptest_assert_msg(stack>0 && storage>0 && search_b,"private search fixtures allocated");
    if(stack<=0 || storage<=0 || !search_b) {ptest_pass();return;}
    args.flags=CLONE_VM|CLONE_COPY_CWD;args.entry=search_entry;
    args.stack_base=(void *)stack;args.stack_size=RUNNABLE_STACK_SIZE;
    for(provider=0;provider<((Drvmap()&(1L<<16))?2:1);provider++) {
        pattern[0]=provider?'Q':'C';
        for(i=0;i<3;i++) {
            long fd;files[i][0]=pattern[0];fd=Fcreate(files[i],0);
            ptest_assert_msg(fd>=0 && !Fclose(fd),"create three provider search results");
        }
        files_counts(before);
        for(mode=0;mode<6;mode++) {
            /* A header crossing two private pages must split COW at both. */
            search_a=mode<2?(_DTA *)((((unsigned long)storage+4095UL)&~4095UL)+4096-16):
                     mode<4?original:&stack_dta;
            (void)Fsetdta(search_a);ptest_assert_msg(!Fsfirst(pattern,0),"start original logical DTA search");
            strcpy(search_first,search_a->dta_name);
            (void)Fsetdta(search_b);ptest_assert_msg(!Fsfirst(pattern,0) && !Fsnext(),"measure provider search order");
            strcpy(search_second,search_b->dta_name);
            ptest_assert_msg(!Fsnext(),"third search result");strcpy(search_third,search_b->dta_name);
            ptest_assert_msg(Fsnext()==-49L && !Fsfirst(pattern,0),"restart second independently owned DTA");
            (void)Fsetdta(search_a);files_counts(prepared);
            for(stage=1;stage<=9;stage++) {
                unsigned long injection=FILES_CLONE_TAG+(stage<=4?0x200+stage:0x300+stage-4);
                ptest_assert_msg(pclone_trap(0,injection)==0,"arm existing clone failure prefix with active searches");
                ptest_assert_msg(pclone_trap(&args,0xc9840000UL|sizeof(args))==-39L,"search rollback publishes no child");
                files_compare(prepared,"search rollback preserves exact PROCESS and SFT counts");
            }
            child=pclone_trap(&args,0xc9840000UL|sizeof(args));
            ptest_assert_msg(child>0,"publish private child with staged trusted searches");
            if(child>0) {
                if(mode&1) {
                    status=Pwaitpid(child,0,0);
                    ptest_assert_msg((status>>16)==child && (status&65535)==83,"child-first continuation and cleanup");
                }
                ptest_assert_msg(!Fsnext() && !strcmp(search_a->dta_name,search_second) &&
                                 !Fsnext() && !strcmp(search_a->dta_name,search_third) && Fsnext()==-49L,
                                 "parent independently advances its captured position");
                if(!(mode&1)) {
                    status=Pwaitpid(child,0,0);
                    ptest_assert_msg((status>>16)==child && (status&65535)==83,"parent-first continuation and cleanup");
                }
                ptest_assert_msg(!strcmp(search_b->dta_name,search_first),"child DTA writes leave parent COW bytes intact");
            }
            (void)Fsetdta(search_b);while(Fsnext()==0) { }
            (void)Fsetdta(original);
            files_compare(before,"private search snapshot restores exact PROCESS/SFT baseline");
        }
        for(i=0;i<3;i++) ptest_assert_msg(!Fdelete(files[i]),"provider search fixtures released");
    }
    ptest_assert_msg(!Mfree((void *)storage) && !Mfree(search_b) && !Mfree((void *)stack),"private DTA backing and stack released");
    ptest_pass();
    if(!ptest_failures())conws("native private trusted DTA/search inheritance: PASS\r\n");
}
