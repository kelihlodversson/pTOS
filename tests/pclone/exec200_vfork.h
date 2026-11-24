/* Public Pvfork with a replacement in the other executable format. */
#ifndef EXEC200_VFORK_H
#define EXEC200_VFORK_H

#if defined(__mc68000__) && defined(TEST_IS_PRG) && defined(TEST_WITH_ELF_LOADER) && defined(TEST_WITH_FORK)
#define EXEC200_VFORK_CASE 5
#define EXEC200_VFORK_GATE "VF200GO.TMP"

static __attribute__((unused)) void exec200_vfork_source(const char *target)
{
    static const char data[]="VF200.TMP", environment[]="EXEC=970\0SECOND=yes\0";
    const char *who="Pvfork source";
    char bytes[3];
    long fd, child, status, gate, event;
#if defined(TEST_WITH_FORK_RETURN_TEST) && defined(TEST_WITH_PRIVATE_CLONE_TEST)
    long dependencies=exec200_probe(0,0x895ffffcUL);
#endif
    exec200_verify_startup(who);
    (void)Fdelete(EXEC200_VFORK_GATE);
    (void)Fdelete(data);
    fd=Fcreate(data,0);
    if (fd<0 || Fwrite((short)fd,1,"a")!=1)
        exec200_fail(who,"create inherited descriptor",fd);
    child=Pvfork();
    if (child==0) {
        EXEC_REPLACE_TEST_STATE original;
        EXEC200_TAIL t={0};
        long failed=Pexec(200,"NOVF200.TOS","",0);
        if (failed!=-33L) exec200_fail(who,"failed replacement",failed);
        (void)Syield();
        if (!Fsfirst(EXEC200_VFORK_GATE,0))
            exec200_fail(who,"parent ran before commit",0);
#if defined(TEST_WITH_FORK_RETURN_TEST) && defined(TEST_WITH_PRIVATE_CLONE_TEST)
        if (exec200_probe(0,0x895ffffdUL)!=1)
            exec200_fail(who,"failed exec must leave parent blocked",0);
#endif
        if (exec200_probe(&original,EXEC_REPLACE_QUERY) ||
            original.generation[0] || original.generation[1])
            exec200_fail(who,"initial child exec generation",0);
        t.descriptor=fd;
        t.pid=original.pid;
        t.tid=original.tid;
        t.parent=Pgetppid();
        t.process0=original.process[0];
        t.process1=original.process[1];
        t.root0=original.root[0];
        t.root1=original.root[1];
        t.nested=EXEC200_VFORK_CASE;
        failed=exec200_next(target,0,&t,environment);
        exec200_fail(who,"successful child exec returned",failed);
    }
    if (child<=0 || child==Pgetpid()) exec200_fail(who,"distinct child PID",child);
    /* A blocking observer could let an early wake reach the commit here. */
    event=exec200_probe((const void *)child,EXEC_REPLACE_PEEK);
    if (event!=1) exec200_fail(who,"committed generation at parent wake",event);
    if (Pwaitpid(child,1,0)!=0) exec200_fail(who,"child still live at parent wake",child);
#if defined(TEST_WITH_FORK_RETURN_TEST) && defined(TEST_WITH_PRIVATE_CLONE_TEST)
    if (exec200_probe(0,0x895ffffcUL)!=dependencies)
        exec200_fail(who,"exec consumed the exact dependency",child);
#endif
    gate=Fcreate(EXEC200_VFORK_GATE,0);
    if (gate<0 || Fclose((short)gate)) exec200_fail(who,"release replacement",gate);
    status=Pwaitpid(child,0,0);
    if ((status>>16)!=child || (status&65535)!=76 || Pwaitpid(child,1,0)!=-33L)
        exec200_fail(who,"one exact-child completion from the new image",status);
    if (Fseek(0,(short)fd,0)!=0 || Fread((short)fd,sizeof(bytes),bytes)!=2 ||
        memcmp(bytes,"ab",2) || Fclose((short)fd) || Fdelete(data) ||
        Fdelete(EXEC200_VFORK_GATE))
        exec200_fail(who,"shared descriptor and gate cleanup",fd);
}
#endif
#endif
