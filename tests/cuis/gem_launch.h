#ifndef TEST_GEM_LAUNCH_H
#define TEST_GEM_LAUNCH_H
int gem_launch_child(AESGLOBAL *global, int argc, char **argv, char **envp);
int gem_launch_checks(AESGLOBAL *global, char **envp);
#endif
