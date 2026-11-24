/* Pclone GEMDOS snapshot/validation boundary; trusted startup is separate. */
#include "emutos.h"
#include "gemerror.h"
#include "string.h"
#include "clone.h"
#include "biosext.h"
#if CONF_WITH_USER_ASPACE
#include "fs.h"
#include "bdosstub.h"
#include "kproc.h"
#include "clone_args.h"
#if CONF_WITH_EXEC_HANDOFF_TEST
#include "exec_handoff.h"
#include "exec_handoff_test.h"
#endif
#if CONF_WITH_MULTITHREADING
#include "proc_clone.h"
#if CONF_WITH_PROCESS_FILES_TEST
#include "process_files.h"
#endif
#include "ksched.h"
#include "interrupt_restart.h"
#if CONF_WITH_IMAGE_POLICY_TEST
#include "kproc_image_policy.h"
#include "image_policy_test.h"
#endif
#if CONF_WITH_EXEC_REPLACEMENT_TEST
#include "exec_replace.h"
#include "exec_handoff_test.h"
#endif
#if CONF_WITH_PRIVATE_CLONE_TEST
#include "kthread.h"
#include "kaspace.h"
#include "procmem.h"
#include "kheap.h"
#include "shared_pexec_test.h"
#include "proc.h"
#include "mapping_lifetime_test.h"
#include "fileback.h"
#include "private_snapshot_test.h"
#endif
#endif

static int clone_copy(void *context, uint64_t address, void *dst, uint32_t size)
{
    LONG *error = context;
    *error = kproc_copy_from_user_checked(dst, (VADDR)address, size);
    return *error == E_OK;
}

static int clone_access(void *context, uintptr_t address, uint32_t size, int kind)
{
    (void)context;
    if (kind == CLONE_ACCESS_EXEC) return kproc_validate_user_exec((VADDR)address);
    return kproc_user_access(run, (VADDR)address, size, TRUE);
}
#endif

#if CONF_WITH_USER_ASPACE && CONF_WITH_MULTITHREADING && CONF_WITH_PRIVATE_CLONE_TEST
static LONG mapping_snapshot(VADDR output)
{
    MAPPING_LIFETIME_SNAPSHOT snapshot={ {0} };
    uint32_t *v=snapshot.value;
    ASPACE *as=kproc_user_aspace(run);
    PROCMEM_STATS memory;
    KHEAP_STATS heap;
    int i,j;
    if (!as || !aspace_copy_to_user_ok(as,output,sizeof(snapshot))) return EIMBA;
    kaspace_test_mapping_snapshot(as,v);
    v[MLT_PROCESSES]=kproc_count();v[MLT_IDENTITIES]=kproc_test_identities(FALSE);
    v[MLT_PROCESS_REFS]=kproc_test_identities(TRUE);v[MLT_THREADS]=kthread_test_live();
    v[MLT_ROOTS]=kaspace_test_live();v[MLT_SPACE_REFS]=kaspace_test_references();
    v[MLT_KSTACKS]=kstack_test_live();v[MLT_SCHEDSTACKS]=ksched_test_nested(0x0e);
    procmem_stats(&memory);kheap_stats(&heap);
    v[MLT_ALLOCS]=memory.live_allocs;v[MLT_FREE_PAGES]=memory.free_pages;v[MLT_BAD_FREES]=memory.bad_frees;
    v[MLT_ALLOC_HOLDS]=procmem_test_ownership(0);v[MLT_ALLOC_PINS]=procmem_test_ownership(1);
    v[MLT_ALLOC_PENDING]=procmem_test_ownership(2);
    v[MLT_KHEAP_BLOCKS]=heap.live_blocks;v[MLT_KHEAP_PAGES]=heap.pages;v[MLT_KHEAP_BAD_FREES]=heap.bad_frees;
    v[MLT_PRIVATE_PAGES]=aspace_private_pages(as);v[MLT_TABLES]=aspace_table_pages(as);
    v[MLT_PINS]=aspace_test_pin_count(as,FALSE);v[MLT_RETIRED_PINS]=aspace_test_pin_count(as,TRUE);
#if CONF_WITH_PRIVATE_CLONE
    v[MLT_COW_FRAMES]=aspace_cow_frames();v[MLT_COW_REFS]=aspace_cow_references();
#endif
#if CONF_WITH_MMAP
    v[MLT_FILE_PAGES]=aspace_file_pages();v[MLT_FILE_REFS]=aspace_file_references();
    v[MLT_BACKINGS]=fileback_test_count(FALSE);v[MLT_BACKING_REFS]=fileback_test_count(TRUE);
    v[MLT_OFD_REFS]=fileback_test_orphan_ofd_refs();
#endif
    for (i=0;i<OPNFILES;i++) if (sft[i].f_use) {
        v[MLT_SFT_SLOTS]++;v[MLT_SFT_REFS]+=sft[i].f_use;
        if (!sft[i].f_ofd || IS_BIOS_HANDLE(sft[i].f_ofd)) continue;
        for (j=0;j<i;j++) if (sft[j].f_use && sft[j].f_ofd==sft[i].f_ofd) break;
#if CONF_WITH_FILE_PINS
        if (j==i) v[MLT_OFD_REFS]+=sft[i].f_ofd->o_refs;
#endif
    }
    /* No fault, retain, reap or checkpoint: only resident checked copyout. */
    return aspace_copy_to_user(as,output,&snapshot,sizeof(snapshot))?E_OK:EIMBA;
}
#endif

#if CONF_WITH_SHARED_VM_PEXEC && CONF_WITH_PRIVATE_CLONE_TEST
/* Call the actual production symbol, including the m68k assembly. The byte
 * oracle is computed from the original pattern, never by another memmove. */
static LONG shared_move_probe(void)
{
    static const UWORD lengths[]={0,1,3,4,15,16,17,22,43,44,63,64,65,66,88,127,128,255,256,257,300,511,512,513,600};
    static const UWORD distances[]={0,1,2,3,15,16,18,20,22,31,32,43,44,63,64,65};
    UBYTE actual[768],expected[768];
    void *(*volatile move)(void *,const void *,size_t)=memmove;
    UWORD n,d,direction,parity,i,from,to;
    ULONG cases=0;
    BOOL valid=TRUE;
    for(n=0;n<ARRAY_SIZE(lengths);n++) for(d=0;d<ARRAY_SIZE(distances);d++)
        for(direction=0;direction<2;direction++) for(parity=0;parity<2;parity++) {
            from=16+parity+(direction?distances[d]:0);to=16+parity+(direction?0:distances[d]);
            for(i=0;i<sizeof(actual);i++) actual[i]=expected[i]=(UBYTE)(i*17+(i>>3));
            for(i=0;i<lengths[n];i++) expected[to+i]=(UBYTE)((from+i)*17+((from+i)>>3));
            if(move(actual+to,actual+from,lengths[n])!=actual+to) valid=FALSE;
            cases++;
            for(i=0;i<sizeof(actual);i++) if(actual[i]!=expected[i]) {
                kcprintf("native kernel memmove mismatch: length=%u distance=%u direction=%u byte=%u\n",
                    lengths[n],distances[d],direction,i);
                valid=FALSE;break;
            }
        }
    if(cases!=ARRAY_SIZE(lengths)*ARRAY_SIZE(distances)*4) valid=FALSE;
    if(valid) kcprintf("native kernel memmove overlap: %lu cases PASS\n",(unsigned long)cases);
    return valid?E_OK:ERR;
}
#endif

static long clone_request(uint64_t address, uint64_t size)
{
#if CONF_WITH_USER_ASPACE
    CLONE_ARGS args;
    LONG copy_error = E_OK;
    enum clone_copy_result copied;
    enum clone_validate_result valid;
#if CONF_WITH_PROCESS_FILES_TEST && CONF_WITH_MULTITHREADING
    BOOL files_test=FALSE;
#endif
    static const CLONE_ABI abi = { CONF_PCLONE_STACK_ALIGNMENT,
        CONF_PCLONE_MIN_STACK, CONF_PCLONE_ENTRY_ALIGNMENT };
#if CONF_WITH_IMAGE_POLICY_TEST
    BOOL image_policy_test=FALSE;
#endif
#if CONF_WITH_EXEC_REPLACEMENT_TEST
    if (!address && (size&0xffffffffffff0000ULL)==EXEC_REPLACEMENT_PROBE)
        return kproc_exec_replacement_probe((UWORD)size);
#endif
#if CONF_WITH_PRIVATE_CLONE_TEST
    BOOL private_test = FALSE;
    BOOL scoped_test = FALSE;
#endif
#if CONF_WITH_IMAGE_POLICY_TEST
    if (size==IMAGE_POLICY_QUERY_DIGEST || size==IMAGE_POLICY_QUERY_INSTALLED) {
        KPROC *source=kproc_retain(run);
        LONG result=kproc_image_policy_test_query(source,(UWORD)(size&255));
        kproc_release(source);return result;
    }
    if (!address && size>=IMAGE_POLICY_FAIL_AFTER && size<=IMAGE_POLICY_FAIL_AFTER+2) {
        kproc_image_policy_test_fail_after((UWORD)(size&255));return E_OK;
    }
    if ((size&0xffffffffffff0000ULL)==IMAGE_POLICY_CLONE) { image_policy_test=TRUE;size&=0xffff; }
#endif
#if CONF_WITH_SHARED_VM_PEXEC && CONF_WITH_PRIVATE_CLONE_TEST
    if(!address && size==SHARED_PEXEC_MOVE_QUERY) return shared_move_probe();
    if(!address && size>=SHARED_PEXEC_DEPENDENCY_FAIL && size<=SHARED_PEXEC_DEPENDENCY_FAIL+255) {
        kproc_shared_dependency_test_fail_after((UWORD)(size&255));return E_OK;
    }
    if(!address && size==SHARED_PEXEC_DEPENDENCY_STEPS) return kproc_shared_dependency_test_steps();
    if(size==SHARED_PEXEC_PROTECTED_QUERY) {
        SHARED_PEXEC_PROTECTED probe;
        if(kproc_copy_from_user_checked(&probe,(VADDR)address,sizeof(probe)) ||
           !aspace_copy_to_user_ok(kproc_user_aspace(run),(VADDR)address,sizeof(probe))) return EIMBA;
        probe.tid_before=kthread_tid(ksched_exit_thread(run));
        probe.result=proc_shared_vm_exec((PD *)(uintptr_t)probe.image,FALSE);
        probe.pending=ksched_cancellation_pending();
        probe.tid_after=kthread_tid(ksched_exit_thread(run));
        /* xclone's enclosing cleanup scope owns this private experiment.
         * Its ordinary final checkpoint must deliver the latched request. */
        /* The shared wait can yield after preflight. An ordinary peer may
         * revoke this diagnostic output; no retained lease protects it. */
        if(!kproc_copy_to_user_resident((VADDR)address,&probe,sizeof(probe))) return EIMBA;
        return E_OK;
    }
    if(!address && size>=0xc8940900UL && size<=0xc89409ffUL) {
        kproc_shared_test_fail_after((UWORD)(size&255));return E_OK;
    }
    if(!address && size==0xc8940a00UL) return kproc_shared_test_steps();
    if(size==SHARED_PEXEC_SNAPSHOT_QUERY) {
        SHARED_PEXEC_SNAPSHOT out={ {0} };KHEAP_STATS heap;ULONG i;
        /* Preflight only resident output. The read itself cannot fault,
         * allocate, reap pending work, retain an owner, callback or yield. */
        if(!address || address>=USER_VA_LIMIT ||
           !aspace_copy_to_user_ok(kproc_user_aspace(run),(VADDR)address,sizeof(out))) return EIMBA;
        out.count[SP_PROCESSES]=kproc_count();
        out.count[SP_IDENTITIES]=kproc_test_identities(FALSE);
        out.count[SP_PROCESS_REFS]=kproc_test_identities(TRUE);
        out.count[SP_THREADS]=kthread_test_live();
        out.count[SP_CURRENT_PRIVATE]=aspace_private_pages(kproc_user_aspace(run));
        procmem_shared_snapshot(&out);kaspace_shared_snapshot(kproc_user_aspace(run),&out);
        out.count[SP_KSTACKS]=kstack_test_live();
        out.count[SP_SCHEDSTACKS]=(ULONG)ksched_test_nested(14);
        kheap_stats(&heap);out.count[SP_KHEAP_BLOCKS]=heap.live_blocks;
        out.count[SP_KHEAP_PAGES]=heap.pages;out.count[SP_KHEAP_BAD_FREES]=heap.bad_frees;
#if CONF_WITH_MMAP
        out.count[SP_FILE_PAGES]=aspace_file_pages();out.count[SP_FILE_REFS]=aspace_file_references();
        fileback_shared_snapshot(&out);
#endif
#if CONF_WITH_PRIVATE_CLONE
        out.count[SP_COW_FRAMES]=aspace_cow_frames();out.count[SP_COW_REFS]=aspace_cow_references();
#endif
        for(i=0;i<OPNFILES;i++) {
            out.count[SP_SFT_USES]+=sft[i].f_use;
            out.count[SP_SFT_STAGED]+=sft[i].f_staged_refs;
            out.count[SP_SFT_CLEANUP]+=sft[i].f_cleanup_refs;
        }
        for(i=0;i<NCURDIR;i++) out.count[SP_DIRECTORY_USES]+=dirtbl[i].use;
        return kproc_copy_to_user_resident((VADDR)address,&out,sizeof(out))?E_OK:EIMBA;
    }
#endif
#if CONF_WITH_IRQ_RESTART_TEST
    if (size>=IRQ_RESTART_BEGIN && size<=IRQ_RESTART_FAIL_STACK)
        return interrupt_restart_test(address,size);
#endif
#if CONF_WITH_CUIS_WAIT_TEST
    if (size==KSCHED_TEST_WAIT_ADMISSION || size==KSCHED_TEST_RAW_WAIT_ADMISSION)
        return address<=0xffffffffULL?
            ksched_test_wait_admission((ULONG)address,size==KSCHED_TEST_RAW_WAIT_ADMISSION):EINVAL;
#endif
#if CONF_WITH_PREEMPTION && CONF_WITH_PRIVATE_CLONE_TEST
    if (!address && (size==IRQ_PREEMPT_TEST_RESET || size==IRQ_PREEMPT_TEST_PATTERNS))
        return ksched_preempt_test_patterns(size==IRQ_PREEMPT_TEST_RESET);
    if (size==IRQ_PREEMPT_TEST_DELAY) return interrupt_preempt_test_delay((ULONG)address);
#endif
#if CONF_WITH_PRIVATE_CLONE_TEST
    if (size==PRIVATE_SNAPSHOT_QUERY) {
#if CONF_WITH_PRIVATE_SNAPSHOT_TEST
        UWORD rights=0;
        LONG value=0;
        KASPACE *space;
        KPROC *source=kproc_retain(run);
        if (address>=USER_VA_LIMIT || !(space=kproc_active_space(source))) {
            kproc_release(source);return EINVAL;
        }
        if (aspace_translate(kaspace_backend(space),(VADDR)address,NULL,NULL)) value|=PRIVATE_SNAPSHOT_PRESENT;
        if (kaspace_retained_lazy_protection(space,(VADDR)address,&rights)) value|=PRIVATE_SNAPSHOT_LAZY;
        if (rights&ASPACE_PROT_WRITE) value|=PRIVATE_SNAPSHOT_WRITE;
        if (rights&ASPACE_PROT_USER) value|=PRIVATE_SNAPSHOT_USER;
        kproc_release(source);
        return value;
#else
        return EINVFN;
#endif
    }
    if ((size&0xffffffffffff0000ULL)==MLT_SCOPED_CLONE) {
        if (address>=USER_VA_LIMIT) return EIMBA;
        if (!address && size==MLT_CONTENDED_ALLOC) return (LONG)kproc_test_contended_allocation();
        if (!address && size==MLT_WAIT_COUNT) return (LONG)kproc_test_mapping_wait_count();
        if (size==MLT_PROCESS_WAITERS)
            return address && address<=32767 ? ksched_test_process_waiters((WORD)address) : EINVAL;
        if (size==MLT_PROCESS_ZOMBIE)
            return address && address<=32767 ? ksched_test_process_zombie((WORD)address) : EINVAL;
        if (size==MLT_FAIL_COPY) {
            KPROC *source=kproc_retain(run);
            LONG error=kaspace_test_fail_copy_at(kproc_active_space(source),(VADDR)address);
            kproc_release(source);
            return error;
        }
        if (size==MLT_SNAPSHOT) return mapping_snapshot((VADDR)address);
        if (size>=MLT_INFO && size<=MLT_DETACH) {
            MAPPING_LIFETIME_CLAIM claim;
            LONG error;
            ASPACE *as=kproc_user_aspace(run);
            if (!as || !aspace_copy_to_user_ok(as,(VADDR)address,sizeof(claim)) ||
                !aspace_copy_from_user(as,&claim,(VADDR)address,sizeof(claim))) return EIMBA;
            error=kproc_test_mapping_claim((ULONG)size,&claim);
            if (!error && !aspace_copy_to_user(as,(VADDR)address,&claim,sizeof(claim))) return EIMBA;
            return error;
        }
        if (!address && size>=0xc9670200ULL && size<=0xc96702ffULL) {
            kaspace_test_fail_mapping_after((UWORD)(size&255));return E_OK;
        }
        scoped_test=TRUE;size&=0xffff;
    }
    if ((size & 0xffffffffffff0000ULL) == 0xc8920000ULL) {
        if (!address && size >= 0xc8920100ULL) {
            KPROC *source = kproc_retain(run);
            LONG value = EINVFN;
            if (source) {
                if (size == 0xc8920100ULL) value = (LONG)kproc_user_root(run);
#if CONF_WITH_PRIVATE_CLONE
                if (size == 0xc8920101ULL) value = aspace_cow_frames();
                if (size == 0xc8920102ULL) value = aspace_cow_references();
#endif
                if (size == 0xc8920103ULL) {
                    KTHREAD *thread = ksched_exit_thread(run);
                    if (thread) value = kthread_tid(thread);
                }
#if CONF_WITH_PRIVATE_CLONE
                if (size >= 0xc8920200ULL && size <= 0xc89202ffULL) {
                    aspace_clone_test_fail_after((ULONG)size & 255); value = E_OK;
                }
                if (size == 0xc8920300ULL || size == 0xc8920301ULL) {
                    aspace_cow_test_fail_allocation(size == 0xc8920301ULL); value = E_OK;
                }
#endif
                if (size >= 0xc8920400ULL && size <= 0xc8920405ULL) {
                    proc_clone_test_fail_after((UWORD)(size & 255)); value = E_OK;
                }
                if (size >= 0xc8920500ULL && size <= 0xc8920505ULL) {
                    proc_clone_factory_test_fail_after((UWORD)(size & 255)); value = E_OK;
                }
                if (size == 0xc8920600ULL) {
                    proc_clone_test_cancel_publication(); value = E_OK;
                }
                /* Nested continuation ownership (#713): scheduler counters and
                 * one-shot injections, then global identity/resource counts. */
                if (size >= 0xc8920700ULL && size <= 0xc892077fULL)
                    value = ksched_test_nested((UWORD)(size & 255));
                if (size == 0xc8920780ULL) value = (LONG)kproc_count();
                if (size == 0xc8920781ULL) value = (LONG)kthread_test_live();
                if (size == 0xc8920782ULL) value = (LONG)kaspace_test_live();
                if (size == 0xc8920783ULL) value = (LONG)kstack_test_live();
                if (size == 0xc8920784ULL) value = (LONG)kproc_pid(run);
                if (size == 0xc8920785ULL) value = (LONG)kproc_test_identities(FALSE);
                if (size == 0xc8920786ULL) value = (LONG)kproc_test_identities(TRUE);
                if (size == 0xc8920787ULL) value = (LONG)kaspace_test_references();
                if (size == 0xc8920788ULL || size == 0xc8920789ULL) {
                    PROCMEM_STATS stats;
                    procmem_stats(&stats);
                    value = size == 0xc8920788ULL ? (LONG)stats.live_allocs : (LONG)stats.free_pages;
                }
                if (size == 0xc892078aULL) {
                    KHEAP_STATS stats;
                    kheap_stats(&stats); value = (LONG)stats.live_blocks;
                }
                kproc_release(source);
            }
            return value;
        }
        private_test = TRUE; size &= 0xffff;
    }
#endif
#if CONF_WITH_PROCESS_FILES_TEST && CONF_WITH_MULTITHREADING
#if CONF_WITH_PRIVATE_CLONE_TEST
    /* Real private search inheritance transport; public fork remains gated. */
    if ((size&0xffffffffffff0000ULL)==0xc9840000ULL) {
        private_test=TRUE;files_test=TRUE;size&=0xffff;
    }
#endif
    /* Private transport for the common resource policy, never a new ABI flag. */
    if ((size&0xffffffffffff0000ULL)==0xc9690000ULL) {
        if (!address && size>=0xc9690100ULL && size<=0xc9690107ULL) {
            ULONG count=0,i;
            switch ((UWORD)(size&255)) {
            case 0: for (i=0;i<OPNFILES;i++) count+=sft[i].f_use;break;
            case 1: for (i=0;i<OPNFILES;i++) count+=sft[i].f_staged_refs;break;
            case 2: for (i=0;i<OPNFILES;i++) count+=sft[i].f_cleanup_refs;break;
            default: return kproc_files_test_count((UWORD)((size&255)-3));
            }
            return (LONG)count;
        }
        if (!address && size>=0xc9690200ULL && size<=0xc9690204ULL) {
            proc_clone_test_fail_after((UWORD)(size&255));return E_OK;
        }
        if (!address && size>=0xc9690300ULL && size<=0xc9690305ULL) {
            proc_clone_factory_test_fail_after((UWORD)(size&255));return E_OK;
        }
        files_test=TRUE;size&=0xffff;
    }
#endif
    copied = clone_copy_v1(&args, address, size, clone_copy, &copy_error);
    if (copied == CLONE_COPY_BAD_SIZE) return EINVAL;
    if (copied != CLONE_COPY_OK) return copy_error != E_OK ? copy_error : EIMBA;
    valid = clone_validate_v1(&args, &abi, clone_access, NULL);
    if (valid == CLONE_INVALID) return EINVAL;
    if (valid != CLONE_VALID) return EIMBA;
    if (!(args.flags & CLONE_VM)) return EINVFN;
#if !CONF_WITH_PROCESS_FILES
    if (args.flags & CLONE_COPY_ALL_HANDLES) return EINVFN;
#endif
#if CONF_WITH_IMAGE_POLICY_TEST
    if (image_policy_test && ((args.flags&CLONE_SAME_PROCESS) || args.child_pid || args.child_tid)) return EINVAL;
#endif
#if CONF_WITH_PROCESS_FILES_TEST && CONF_WITH_MULTITHREADING
    if (files_test && (args.flags&CLONE_SAME_PROCESS)) return EINVAL;
#endif
#if CONF_WITH_PRIVATE_CLONE_TEST
    if ((private_test && ((args.flags & CLONE_SAME_PROCESS) || args.child_pid || args.child_tid)) ||
        (scoped_test && (args.flags&CLONE_SAME_PROCESS))) return EINVAL;
#endif
#if CONF_WITH_MULTITHREADING
    {
        PROC_CLONE_ARGS policy;
        THREAD_STARTUP_ARGS startup;
        LONG result;
        policy.source = kproc_retain(run);
        if (!policy.source) return EINVFN;
        policy.prepared_process = NULL;
        policy.process_policy = (args.flags & CLONE_SAME_PROCESS) ? PROC_CLONE_SAME_PROCESS : PROC_CLONE_NEW_PROCESS;
        policy.vm_policy =
#if CONF_WITH_PRIVATE_CLONE_TEST
            private_test ? PROC_CLONE_PRIVATE_VM :
            scoped_test ? PROC_CLONE_SCOPED_VM :
#endif
            PROC_CLONE_SHARE_VM;
        policy.resources = ((args.flags & CLONE_COPY_STANDARD_HANDLES) ? PROC_CLONE_COPY_STANDARD_HANDLES : 0) |
                           ((args.flags & CLONE_COPY_CWD) ? PROC_CLONE_COPY_CWD : 0);
#if CONF_WITH_PROCESS_FILES
        if (args.flags & CLONE_COPY_ALL_HANDLES) policy.resources |= PROC_CLONE_COPY_DESCRIPTORS;
#endif
#if CONF_WITH_PROCESS_FILES_TEST
        if (files_test) policy.resources|=PROC_CLONE_COPY_FILES;
#endif
        startup.entry = (VADDR)(uintptr_t)args.entry;
        startup.argument = (VADDR)(uintptr_t)args.argument;
        startup.stack_base = (VADDR)(uintptr_t)args.stack_base;
        startup.stack_size = args.stack_size;
        startup.flags = 0;
        result =
#if CONF_WITH_IMAGE_POLICY_TEST
            image_policy_test ? proc_clone_start_image_policy_test(&policy,&startup) :
#endif
#if CONF_WITH_PRIVATE_CLONE_TEST
            private_test ? proc_clone_start_private_test(&policy, &startup) :
            scoped_test ? proc_clone_start_scoped_test(&policy,&startup,
                    (VADDR)(uintptr_t)args.child_pid,(VADDR)(uintptr_t)args.child_tid) :
#endif
            proc_clone_start_public(&policy, &startup,
                    (VADDR)(uintptr_t)args.child_pid, (VADDR)(uintptr_t)args.child_tid);
        kproc_release(policy.source);
        return result;
    }
#endif
#else
    (void)address;
    (void)size;
#endif
    /* Configurations without the native execution backend remain unsupported. */
    return EINVFN;
}

long xclone(uint64_t address, uint64_t size)
{
#if CONF_WITH_EXEC_REPLACEMENT_TEST
    if (size >= EXEC_REPLACE_ARM && size <= EXEC_REPLACE_WAIT + 0xff)
        return kproc_exec_replace_test((VADDR)address, (ULONG)size);
#endif
#if CONF_WITH_EXEC_HANDOFF_TEST
    if (size == EXEC_HANDOFF_QUERY) return ksched_exec_handoff_query((VADDR)address);
    if (size >= EXEC_HANDOFF_RUN && size <= EXEC_HANDOFF_RUN + EXEC_HANDOFF_FAILURES) {
        CLONE_ARGS args;
        THREAD_STARTUP_ARGS startup;
        LONG error = E_OK;
        static const CLONE_ABI abi = { CONF_PCLONE_STACK_ALIGNMENT,
            CONF_PCLONE_MIN_STACK, CONF_PCLONE_ENTRY_ALIGNMENT };
        if (clone_copy_v1(&args, address, sizeof(CLONE_USER_V1), clone_copy, &error) != CLONE_COPY_OK)
            return error ? error : EIMBA;
        if (args.flags || args.tls || args.child_tid || args.child_pid) return EINVAL;
        if (clone_validate_v1(&args, &abi, clone_access, NULL) != CLONE_VALID) return EIMBA;
        startup.entry = (VADDR)(uintptr_t)args.entry;
        startup.argument = (VADDR)(uintptr_t)args.argument;
        startup.stack_base = (VADDR)(uintptr_t)args.stack_base;
        startup.stack_size = args.stack_size;
        startup.flags = 0;
        /* This private experiment owns its own exact cleanup scope. Ordinary
         * Pclone remains inside the unchanged factory scope below. */
        return ksched_exec_handoff_test(&startup, (UWORD)(size - EXEC_HANDOFF_RUN));
    }
#endif
#if CONF_WITH_USER_ASPACE && CONF_WITH_MULTITHREADING
    KSCHED_CLEANUP_SCOPE scope = { 0 };
    LONG result;
    /* Factory rollback may release files and yield. Keep local transactions
     * alive until cleanup ends; osif owns the later cancellation checkpoint. */
    ksched_cleanup_begin(&scope);
    result = clone_request(address, size);
    ksched_cleanup_end(&scope);
    return result;
#else
    return clone_request(address, size);
#endif
}
