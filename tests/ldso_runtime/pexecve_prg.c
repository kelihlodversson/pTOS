/* Check the vector-to-PRG adapter with the standard libcmini PRG startup. */
#include <string.h>

extern char **environ;

int main(int argc, char **argv)
{
    if (argc != 4 || strcmp(argv[0], "custom name") || argv[1][0] ||
        strcmp(argv[2], "two words") || strcmp(argv[3], "literal\"quote") || argv[4])
        return 94;
    if (!environ[0] || strcmp(environ[0], "KEY=two words") ||
        !environ[1] || strcmp(environ[1], "FLAG=") || environ[2])
        return 95;
    return 77;
}
