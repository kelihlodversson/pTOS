#ifndef TEST_GEM_LAUNCH_H
#define TEST_GEM_LAUNCH_H
int gem_launch_child(short *global, int argc, char **argv, char **envp);
int gem_launch_checks(short *global, char **envp);
#endif
