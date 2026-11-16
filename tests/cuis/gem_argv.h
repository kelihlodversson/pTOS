#ifndef TEST_GEM_ARGV_H
#define TEST_GEM_ARGV_H
int gem_argv_child(int argc, char **argv, char **envp);
int gem_argv_checks(AESGLOBAL *global, char **envp);
#endif
