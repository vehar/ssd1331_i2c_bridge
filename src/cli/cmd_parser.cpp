/* cmd_parser.c */
#include "cmd_parser.h"
#include "serial_cli.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const cmd_t *cmd_get_table(unsigned *count)
{
    if (count)
        *count = g_cmd_count;
    return g_cmds;
}

int cmd_find_matches(const char *prefix, const cmd_t **first_match, unsigned *match_count)
{
    if (first_match)
        *first_match = NULL;
    if (match_count)
        *match_count = 0;

    unsigned n = 0;
    const cmd_t *t = cmd_get_table(&n);

    unsigned cnt = 0;
    const cmd_t *first = NULL;

    for (unsigned i = 0; i < n; i++)
    {
        if (str_istartswith(t[i].name, prefix)) // reuse your case-insens startswith
        {
            if (!first)
                first = &t[i];

            cnt++;
        }
    }

    if (first_match)
        *first_match = first;

    if (match_count)
        *match_count = cnt;

    return (int)cnt;
}

/* ---------- Helpers ---------- */
int str_ieq(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;

        a++;
        b++;
    }
    return (*a == 0 && *b == 0);
}

/* Split "CMD arg1 arg2" in-place.
   Returns argc, argv points inside buf. */
static int split_tokens(char *buf, char *argv[], int max_argv)
{
    int argc = 0;
    char *p = buf;
    while (*p) {
        while (isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        if (argc == max_argv) return -1;
        char quote = (*p == '"' || *p == '\'') ? *p++ : 0;
        argv[argc++] = p;
        while (*p && (quote ? *p != quote : !isspace((unsigned char)*p))) ++p;
        if (quote && !*p) return -1;
        if (*p) *p++ = 0;
    }
    return argc;
}

int parse_u8(const char *s, uint8_t *out)
{
    uint32_t v;
    if (!parse_u32(s, &v) || v > 0xFF)
        return 0;

    *out = (uint8_t)v;
    return 1;
}

int parse_u16(const char *s, uint16_t *out)
{
    uint32_t v;
    if (!parse_u32(s, &v) || v > 0xFFFF)
        return 0;

    *out = (uint16_t)v;
    return 1;
}

int parse_u32(const char *s, uint32_t *out)
{
    if (!s || !*s || *s == '-')
        return 0;

    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 0); /* supports 0x.. */
    if (end == s || *end != 0 || errno == ERANGE)
        return 0;

    if (v > UINT32_MAX) return 0;
    *out = (uint32_t)v;
    return 1;
}

int parse_u64(const char *s, uint64_t *out)
{
    if (!s || !*s || *s == '-')
        return 0;

    char *end = 0;
    unsigned long long v = strtoull(s, &end, 0);
    if (end == s || *end != 0)
        return 0;

    *out = (uint64_t)v;
    return 1;
}

int parse_bool(const char *s, uint8_t *out)
{
    if (!s)
        return 0;

    if (!strcmp(s, "1") || !strcasecmp(s, "on") || !strcasecmp(s, "true"))
    {
        *out = 1;
        return 1;
    }
    if (!strcmp(s, "0") || !strcasecmp(s, "off") || !strcasecmp(s, "false"))
    {
        *out = 0;
        return 1;
    }
    return 0;
}

/* ---------- Entry point ---------- */
void cmd_parse_line(const char *line)
{
    if (!line)
        return;

    char buf[CLI_MAX_LINE];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    /* Trim leading spaces */
    char *p = buf;
    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p == 0)
        return;

    char *argv[16];
    int argc = split_tokens(p, argv, (int)(sizeof(argv) / sizeof(argv[0])));

    if (argc < 1) { cli_puts("ERR: too many args or unterminated quote\r\n"); return; }

    /* Uppercase command in-place for matching (args untouched) */
    for (char *c = argv[0]; *c; c++)
        *c = (char)toupper((unsigned char)*c);

    for (unsigned i = 0; i < g_cmd_count; i++)
    {
        if (str_ieq(argv[0], g_cmds[i].name))
        {
            if (!g_cmds[i].handler(argc, argv))
                cli_printf("ERR: bad args. Usage: %s\r\n", g_cmds[i].help);

            return;
        }
    }

    cli_puts("ERR: unknown command. Type HELP\r\n");
}
