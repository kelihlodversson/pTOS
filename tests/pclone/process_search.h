/* #984: production private roots and search owners, no public fork ABI. */
static _DTA *search_a,*search_b;
static char search_second[14],search_third[14];
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
    static const char *files[]={"C:\\S984A.TMP","C:\\S984B.TMP","C:\\S984C.TMP"};
    struct ptos_clone_args args={0};
    _DTA *original=(_DTA *)Fgetdta();
    long stack=Malloc(RUNNABLE_STACK_SIZE),child,status;
    long before[8];
    int i;
    ptest_begin("trusted DTA and independent private search snapshots");
    search_a=(_DTA *)Malloc(sizeof(_DTA));search_b=(_DTA *)Malloc(sizeof(_DTA));
    ptest_assert_msg(stack>0 && search_a && search_b,"private search fixtures allocated");
    if(stack<=0 || !search_a || !search_b) {ptest_pass();return;}
    for(i=0;i<3;i++) {long fd=Fcreate(files[i],0);ptest_assert_msg(fd>=0 && !Fclose(fd),"create three search results");}
    files_counts(before);
    (void)Fsetdta(search_a);ptest_assert_msg(!Fsfirst("C:\\S984?.TMP",0),"start original logical DTA search");
    (void)Fsetdta(search_b);ptest_assert_msg(!Fsfirst("C:\\S984?.TMP",0) && !Fsnext(),"measure provider search order");
    strcpy(search_second,search_b->dta_name);
    ptest_assert_msg(!Fsnext(),"third search result");strcpy(search_third,search_b->dta_name);
    ptest_assert_msg(Fsnext()==-49L && !Fsfirst("C:\\S984?.TMP",0),"restart second independently owned DTA");
    (void)Fsetdta(search_a);
    args.flags=CLONE_VM|CLONE_COPY_CWD;args.entry=search_entry;
    args.stack_base=(void *)stack;args.stack_size=RUNNABLE_STACK_SIZE;
    child=pclone_trap(&args,0xc9840000UL|sizeof(args));
    ptest_assert_msg(child>0,"publish private child with staged trusted searches");
    if(child>0) {
        ptest_assert_msg(!Fsnext() && !strcmp(search_a->dta_name,search_second) &&
                         !Fsnext() && !strcmp(search_a->dta_name,search_third) && Fsnext()==-49L,
                         "parent advances and closes its iterator before child continuation");
        status=Pwaitpid(child,0,0);
        ptest_assert_msg((status>>16)==child && (status&65535)==83,"child independently resumes both inherited positions and logical DTA");
    }
    (void)Fsetdta(search_b);while(Fsnext()==0) { }
    (void)Fsetdta(original);
    for(i=0;i<3;i++) ptest_assert_msg(!Fdelete(files[i]),"search fixtures released");
    ptest_assert_msg(!Mfree(search_a) && !Mfree(search_b) && !Mfree((void *)stack),"private DTA backing and stack released");
    files_compare(before,"private search snapshot leaves exact PROCESS/SFT baseline");
    ptest_pass();
    if(!ptest_failures())conws("native private trusted DTA/search inheritance: PASS\r\n");
}
