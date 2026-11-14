/* Production endian-specific bootstraps; hosted low user mappings only. */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "runtime.h"
#if defined(BOOT_TEST_ARM)
#define KIND 23
#define RECORD 8
#define TABLE_TAG 17
#define SIZE_TAG 18
#define ENTRY_TAG 19
#else
#if defined(BOOT_TEST_M68K)
#define KIND 22
#else
#define KIND 8
#endif
#define RECORD 12
#define TABLE_TAG 7
#define SIZE_TAG 8
#define ENTRY_TAG 9
#endif
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"bootstrap:%d: %s\n",__LINE__,#c);goto done; } } while (0)
static void put(unsigned char *p,uint32_t value)
{
#if defined(BOOT_TEST_M68K)
    p[0]=value>>24;p[1]=value>>16;p[2]=value>>8;p[3]=value;
#else
    p[0]=value;p[1]=value>>8;p[2]=value>>16;p[3]=value>>24;
#endif
}
static uint32_t get(const unsigned char *p)
{
#if defined(BOOT_TEST_M68K)
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
#else
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
#endif
}
static void count(unsigned char *p,unsigned n)
{
#if defined(BOOT_TEST_M68K)
    p[44]=n>>8;p[45]=n;
#else
    p[44]=n;p[45]=n>>8;
#endif
}
static void populate(unsigned char *p,int bad)
{
    memset(p,0,8192);
#if defined(BOOT_TEST_M68K)
    put(p,0x7f454c46);p[5]=2;
#else
    put(p,0x464c457f);p[5]=1;
#endif
    p[4]=1;put(p+28,52);count(p,2);
    put(p+52,1);put(p+72,8192);put(p+76,6);
    put(p+84,2);put(p+92,0x200);put(p+100,32);
    put(p+0x200,TABLE_TAG);put(p+0x204,0x300);
    put(p+0x208,SIZE_TAG);put(p+0x20c,3*RECORD);
    put(p+0x210,ENTRY_TAG);put(p+0x214,RECORD);
    memset(p+0x400,0xa5,40);
    put(p+0x300,0x400);put(p+0x304,KIND);
    put(p+0x300+RECORD,0x408);
#if !defined(BOOT_TEST_ARM) && !defined(BOOT_TEST_M68K)
    put(p+0x304+RECORD,38);put(p+0x308+RECORD,(uint32_t)-4096);
#else
    put(p+0x304+RECORD,KIND);
#endif
#if defined(BOOT_TEST_ARM)
    put(p+0x400,0x1000);put(p+0x408,0x2000);
#else
    put(p+0x308,0x1000);
#if defined(BOOT_TEST_M68K)
    put(p+0x308+RECORD,0x2000);
#endif
#endif
    put(p+0x300+2*RECORD,0x420);put(p+0x304+2*RECORD,bad?0x101:0);
}
static int verify(uint32_t base)
{
    unsigned char *p=mmap((void *)(uintptr_t)base,8192,PROT_READ|PROT_WRITE,
                            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    uint32_t stack[]={0,0,0,7,0,0,0};unsigned char before[40];
    unsigned i;int rc=1;
    static const uint32_t invalid[]={0,1,0x0f000001,0x80000000U,0xfffff000U};
    if(p==MAP_FAILED) return 10;
    stack[4]=base;populate(p,1);memcpy(before,p+0x400,sizeof(before));
    CHECK(dl_bootstrap(stack) && !memcmp(before,p+0x400,sizeof(before)));
    put(p+0x304+2*RECORD,0);CHECK(!dl_bootstrap(stack));
    CHECK(get(p+0x400)==base+0x1000);
#if !defined(BOOT_TEST_ARM) && !defined(BOOT_TEST_M68K)
    {
        uint64_t value;memcpy(&value,p+0x408,8);
        CHECK(value==(uint64_t)base-4096 && get(p+0x404)==0xa5a5a5a5U);
    }
#else
    CHECK(get(p+0x408)==base+0x2000);
#endif
    for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        populate(p,0);memcpy(before,p+0x400,sizeof(before));stack[4]=invalid[i];
        CHECK(dl_bootstrap(stack) && !memcmp(before,p+0x400,sizeof(before)));
    }
    stack[4]=base;
    for(i=0;i<9;i++) {
        populate(p,0);memcpy(before,p+0x400,sizeof(before));
        switch(i) {
        case 0: put(p+28,4090);break; /* PHDR crosses initial page */
        case 1: put(p+60,0xfffff000U);break; /* base+VA wraps32 */
        case 2: put(p+72,0xffffffffU);break; /* load end wraps32 */
        case 3: put(p+92,0xffffff00U);break; /* dynamic outside loads */
        case 4: put(p+0x204,0xffffff00U);break; /* relocation table outside loads */
        case 5: put(p+0x300,0x200);break; /* metadata destination */
        case 6: put(p+0x300,0x408);break; /* overlapping destinations */
        case 7: /* An unused late PT_LOAD must also pass the full bounds check. */
            count(p,3);put(p+116,1);put(p+124,0xfffff000U);put(p+136,16);put(p+140,6);break;
        case 8: put(p+72,0x80000000U-base+1);break; /* one byte beyond user ceiling */
        }
        CHECK(dl_bootstrap(stack) && !memcmp(before,p+0x400,sizeof(before)));
    }
    /* Unused table addresses need not form a pointer when relocation size0. */
    populate(p,0);put(p+0x204,0xffffffffU);put(p+0x20c,0);memcpy(before,p+0x400,sizeof(before));
    CHECK(!dl_bootstrap(stack) && !memcmp(before,p+0x400,sizeof(before)));
    rc=0;
done:
    munmap(p,8192);return rc;
}
int main(void)
{
    int rc=verify(0x0f000000);if(rc) return rc;
    rc=verify(0x10000000);if(rc) return rc;
    return verify(0x7ff00000); /* near user ceiling, below hosted ASan shadow */
}
