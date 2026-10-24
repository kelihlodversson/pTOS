/* Transitive dependency with root-executable references. */
extern int startup_log;
int leaf(void) { return 7; }
__attribute__((constructor)) static void init(void) { startup_log = startup_log * 10 + 1; }
__attribute__((destructor)) static void fini(void) { startup_log = startup_log * 10 + 6; }
