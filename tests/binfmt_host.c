/* Host tests of the production bounded parser and argument builder (#944). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "portab.h"
#include <string.h>
#include "gemerror.h"
#include "binfmt.h"

static BINFMT_REGISTRY registry;
static BINFMT_PARSER parser;
static unsigned int reports;
static char storage[BINFMT_ENV_WORK], tail[128];

static void report(ULONG line, const char *message, void *context)
{
    assert(line && *message && context == &reports);
    reports++;
}

static void parse(const char *config)
{
    ULONG number = 1;
    reports = 0;
    binfmt_parser_init(&parser, &registry, 3, report, &reports);
    while (*config) {
        char line[BINFMT_LINE + 2];
        ULONG n = 0, kept;
        while (config[n] && config[n] != '\n') n++;
        kept = n > BINFMT_LINE + 1 ? BINFMT_LINE + 1 : n;
        memcpy(line, config, kept); line[kept] = 0;
        binfmt_config_line(&parser, line, kept, n > BINFMT_LINE + 1, number++);
        config += n;
        if (*config) config++;
    }
    binfmt_config_end(&parser);
}

static void rules(void)
{
    BINFMT_SELECTION selection;
    const UBYTE data[] = {0, 0, 0x60, 0x1f};
    parse("; comment\r\n[future]\nkey = ignored\n[ BINFMT.Tos68k ]\r\n"
          "Enabled = TRUE\nOFFSET = 0x2\nMagic = 601a\nMask = FFF0\n"
          "Interpreter = /Bin\\Paratos With Spaces.TOS\n");
    assert(!reports && registry.count == 1 && registry.rules[0].valid);
    assert(binfmt_select(&registry, data, sizeof(data), &selection) == 0);
    assert(!strcmp(selection.interpreter, "D:\\Bin\\Paratos With Spaces.TOS"));
    assert(!selection.has_argument);
    assert(binfmt_select(&registry, data, 3, &selection) == 1);
    parse("[binfmt.disabled]\nenabled=false\nmagic=60\ninterpreter=/NO.TOS\n"
          "[binfmt.first]\nmagic=60\ninterpreter=C:/ONE.TOS\n"
          "[binfmt.second]\nmagic=601A\ninterpreter=/TWO.TOS\n");
    assert(!reports && binfmt_select(&registry, data + 2, 2, &selection) == 0);
    assert(!strcmp(selection.interpreter, "C:\\ONE.TOS"));
    parse("[binfmt.first]\nmagic=60\ninterpreter=/ONE\n"
          "[binfmt.FIRST]\nmagic=61\ninterpreter=/TWO\n");
    assert(reports && registry.count == 1 && registry.rules[0].magic[0] == 0x60);
    parse("[binfmt.a]\nmagic=60\nMAGIC=61\ninterpreter=/BAD\n"
          "[binfmt.b]\nmagic=60\ninterpreter=/GOOD\nnew_key=allowed\n");
    assert(reports == 2 && !registry.rules[0].valid && registry.rules[1].valid);
    assert(binfmt_select(&registry, data + 2, 2, &selection) == 0);
    assert(!strcmp(selection.interpreter, "D:\\GOOD"));
    parse("[binfmt.a]\nmagic=60\ninterpreter=/ONE\n"
          "[broken\nmagic=61\ninterpreter=/TWO\n");
    assert(reports && registry.rules[0].valid && registry.rules[0].magic[0] == 0x60);
    parse("outside=bad\n[binfmt.a]\nmagic=60\ninterpreter=relative\n");
    assert(reports && !registry.rules[0].valid);
    parse("[binfmt.a]\nmagic=60\ninterpreter=/ONE\nx=1\nX=2\n");
    assert(reports == 2 && !registry.rules[0].valid);
    parse("[binfmt.a]\nmagic=60\nmask=FFFF\ninterpreter=/ONE\n");
    assert(reports && !registry.rules[0].valid);
    parse("[binfmt.a]\nmagic=60\noffset=4294967296\ninterpreter=/ONE\n");
    assert(reports && !registry.rules[0].valid);
    parse("[binfmt.a]\nmagic=60\noffset=4096\ninterpreter=/ONE\n");
    assert(reports && !registry.rules[0].valid);
    parse("[binfmt.a]\nmagic=6\ninterpreter=/ONE\n");
    assert(reports && !registry.rules[0].valid);
    parse("[binfmt.a]\nmagic=GG\ninterpreter=/ONE\n");
    assert(reports && !registry.rules[0].valid);
    parse("[binfmt.bad.name]\nmagic=60\ninterpreter=/ONE\n");
    assert(reports && !registry.count);
}

static void shebangs(void)
{
    BINFMT_SELECTION s;
    char limit[BINFMT_SHEBANG + 4];
    const char lf[] = "#! /BIN\\SH.TOS  -s   --literal 'quotes'  \nbody";
    const char crlf[] = "#!E:\\BIN/SH.TOS\r\n";
    const char eof[] = "#!/BIN/SH.TOS";
    const char relative[] = "#!relative\n";
    const char empty[] = "#! \t\n";
    parse("[binfmt.hash]\nmagic=2321\ninterpreter=/FALLBACK\n");
    assert(binfmt_select(&registry, (const UBYTE *)lf, sizeof(lf) - 1, &s) == 0);
    assert(!strcmp(s.interpreter, "D:\\BIN\\SH.TOS") && s.has_argument);
    assert(!strcmp(s.argument, "-s   --literal 'quotes'"));
    assert(binfmt_select(&registry, (const UBYTE *)crlf, sizeof(crlf) - 1, &s) == 0);
    assert(!strcmp(s.interpreter, "E:\\BIN\\SH.TOS") && !s.has_argument);
    assert(binfmt_select(&registry, (const UBYTE *)eof, sizeof(eof) - 1, &s) == 0);
    assert(binfmt_select(&registry, (const UBYTE *)relative, sizeof(relative) - 1, &s) == EPLFMT);
    assert(binfmt_select(&registry, (const UBYTE *)empty, sizeof(empty) - 1, &s) == EPLFMT);
    assert(binfmt_select(&registry, (const UBYTE *)" #!/BIN/SH", 10, &s) == 1);
    assert(binfmt_select(&registry, (const UBYTE *)"\357\273\277#!/BIN/SH", 12, &s) == 1);
    memset(limit, 'x', sizeof(limit));
    memcpy(limit, "#!/A ", 5); limit[256] = '\r'; limit[257] = '\n';
    assert(binfmt_select(&registry, (const UBYTE *)limit, 258, &s) == 0);
    limit[256] = 'x';
    assert(binfmt_select(&registry, (const UBYTE *)limit, 258, &s) == EPLFMT);
    limit[10] = 0;
    assert(binfmt_select(&registry, (const UBYTE *)limit, 255, &s) == EPLFMT);
}

static void arguments(void)
{
    BINFMT_SELECTION s;
    const char env[] = "DEMO=present\0ARGV=NULL:2,4\0old\0one\0 \0two words\0 \0\0";
    const char expected[] = "DEMO=present\0ARGV=NULL:3,5\0C:\\EMU.TOS\0C:/APP.TOS\0one\0 \0two words\0 \0\0";
    const char expected_option[] = "DEMO=present\0ARGV=NULL:4,6\0C:\\EMU.TOS\0-s  --one-argument\0C:/APP.TOS\0one\0 \0two words\0 \0\0";
    const char classic[] = "one 'two words' '' 'it''s' C:\\DIR\\FILE";
    const char expected_classic[] = "X=1\0ARGV=NULL:4\0C:\\EMU.TOS\0APP.TOS\0one\0two words\0 \0it's\0C:\\DIR\\FILE\0\0";
    char input_tail[128] = {127, 0};
    char *large = malloc(BINFMT_ENV + 2);
    assert(large);
    memset(&s, 0, sizeof(s)); strcpy(s.interpreter, "C:\\EMU.TOS");
    assert(binfmt_arguments(env, input_tail, &s, "C:/APP.TOS", storage, tail) == 0);
    assert(!memcmp(storage, expected, sizeof(expected) - 1) && tail[0] == 127);
    assert(!strcmp(tail + 1, "'C:/APP.TOS' 'one' '' 'two words' ''"));
    s.has_argument = TRUE; strcpy(s.argument, "-s  --one-argument");
    assert(binfmt_arguments(env, input_tail, &s, "C:/APP.TOS", storage, tail) == 0);
    assert(!memcmp(storage, expected_option, sizeof(expected_option) - 1));
    s.has_argument = FALSE;
    input_tail[0] = strlen(classic); strcpy(input_tail + 1, classic);
    assert(binfmt_arguments("X=1\0", input_tail, &s, "APP.TOS", storage, tail) == 0);
    assert(!memcmp(storage, expected_classic, sizeof(expected_classic) - 1));
    assert(binfmt_arguments("\0", "", &s, "APP.TOS", storage, tail) == 0);
    assert(!memcmp(storage, "ARGV=\0C:\\EMU.TOS\0APP.TOS\0\0", 26));
    assert(tail[0] == 7 && !strcmp(tail + 1, "APP.TOS"));
    input_tail[0] = 5; strcpy(input_tail + 1, "'oops");
    assert(binfmt_arguments("\0", input_tail, &s, "APP.TOS", storage, tail) == EINVAL);
    assert(binfmt_arguments("ARGV=NULL:9\0old\0arg\0\0", "", &s, "APP.TOS", storage, tail) == EINVAL);
    memset(large, 'a', BINFMT_ENV + 2);
    assert(binfmt_arguments(large, "", &s, "APP.TOS", storage, tail) == ERANGE);
    memcpy(large, "ARGV=\0old\0", 10); large[30000] = large[30001] = 0;
    assert(binfmt_arguments(large, "", &s, "APP.TOS", storage, tail) == 0 && tail[0] == 127);
    assert(strlen(storage + 25) > 29000);
    memcpy(large, "X=", 2); memset(large + 2, 'a', BINFMT_ENV - 4);
    large[BINFMT_ENV - 2] = large[BINFMT_ENV - 1] = 0;
    assert(binfmt_arguments(large, "", &s, "APP.TOS", storage, tail) == ERANGE);
    free(large);
}

int main(void)
{
    rules(); shebangs(); arguments();
    puts("binfmt parser and arguments: PASS");
    return 0;
}
