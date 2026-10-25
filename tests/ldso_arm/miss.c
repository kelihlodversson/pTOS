/* A library the failure fixtures link against and that is never installed
 * (libmiss.so), or that is installed truncated (libbad.so). */
int ldso_missing(void) { return 1; }
