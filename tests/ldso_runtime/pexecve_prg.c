/* Check the vector-to-PRG adapter with the standard libcmini PRG startup. */
#include <string.h>
#include <mint/basepage.h>

extern char **environ;

int main(int argc, char **argv)
{
    static const char classic_tail[128] = "\007one two";

    if (!environ[0] || strcmp(environ[0], "KEY=two words") ||
        !environ[1] || strcmp(environ[1], "FLAG=") || environ[2])
        return 95;
    if (!argc || argv[argc])
        return 94;
    if (!argv[0][0]) {
        if (argc != 3 || strcmp(argv[1], "one") || strcmp(argv[2], "two") ||
            memcmp(_base->p_cmdlin, classic_tail, sizeof(classic_tail)))
            return 97;
        return 78;
    }
    if (argc != 4 || _base->p_cmdlin[0] != 127 ||
        strcmp(argv[0], "custom name") || argv[1][0] ||
        strcmp(argv[2], "two words") || strcmp(argv[3], "literal\"quote"))
        return 96;
    return 77;
}
