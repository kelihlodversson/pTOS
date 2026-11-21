/* Small real nested-Pexec child: avoid recursively reserving the full test
 * driver's TPA in the bounded legacy m68k process-memory window. */
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/basepage.h>
static const char environment[2] = { 0, 0 };
static void mark(const char *path)
{
    long h = Fcreate(path,0);
    if (h >= 0) (void)Fclose(h);
}
static int exists(const char *path)
{
    long h = Fopen(path,0);
    if (h < 0) return 0;
    (void)Fclose(h);
    return 1;
}
int main(void)
{
    int depth = _base->p_cmdlin[1] - '0';
    char tail[] = "\1" "0";
    char result[] = "C:\\GRET0.ON";
    if (depth > 0 && depth <= 2) {
        tail[1] = '0' + depth - 1;
        if (Pexec(0,"C:\\GROUPCH.TOS",tail,(void *)environment) != 19) Pterm(91);
    } else if (depth == 0) {
        mark("C:\\GDEEP.ON");
        while (!exists("C:\\GGO.ON")) (void)Syield();
    } else Pterm(92);
    result[7] = '0' + depth;
    mark(result);
    Pterm(19);
    return 93;
}
