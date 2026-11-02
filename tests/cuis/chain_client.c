#include <gem.h>
#include "gem_chain.h"
int main(int argc, char **argv, char **envp)
{
    (void)envp;
    if (appl_init() < 0) return 1;
    return gem_chain_client(argc,argv);
}
