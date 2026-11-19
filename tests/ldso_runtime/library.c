extern int startup_log;
extern int leaf(void);
int dynvalue = 5;
int dynbss;
int answer(void) { return leaf() + dynvalue + dynbss; }
__attribute__((constructor)) static void init(void)
{
    startup_log = startup_log * 10 + 2;
    dynvalue = 9;
}
__attribute__((destructor)) static void fini(void)
{
    startup_log = startup_log * 10 + 5;
    dynvalue = -7;
}
