/* serial_cli.c */
#include "config.h"
#include "serial_cli.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>


/* ---------- Keys / sequences ---------- */
#define CTRL_L 0x0C
#define ESC 0x1B
#define BS 0x08
#define DEL 0x7F
#define CR '\r'
#define LF '\n'
#define TAB '\t'

/* Arrow key final bytes after ESC [ */
#define ARROW_UP 'A'
#define ARROW_DOWN 'B'
#define ARROW_RIGHT 'C'
#define ARROW_LEFT 'D'

/* ---------- Escape/CSI parser state ---------- */
typedef enum
{
    ESC_NONE = 0,
    ESC_GOT_ESC,
    ESC_GOT_CSI,      /* got ESC [ */
    ESC_GOT_CSI_PARAM /* got ESC [ ... (digits/;), waiting final */
} esc_state_t;

static esc_state_t s_esc = ESC_NONE;

/* ---------- Line/editor state ---------- */
static char s_line[CLI_MAX_LINE];
static int s_len = 0; /* number of chars in line */
static int s_cur = 0; /* cursor position: 0..s_len */

/* ---------- History ---------- */
static char s_hist[CLI_HISTORY_SIZE][CLI_MAX_LINE];
static int s_hist_start = 0; /* ring start */
static int s_hist_count = 0; /* number of valid entries */
static int s_hist_nav = -1;  /* -1 = not navigating, 0..count-1 from newest */
// Single main-loop owner; never called from an interrupt.
static char *s_captureOutput = nullptr;
static size_t s_captureCapacity = 0;
static size_t s_captureLength = 0;
static bool s_captureTruncated = false;

/* ---------- Forward decls ---------- */
static void cli_autocomplete(void);

/* ---------- Output helpers ---------- */
void cli_putc(char c)
{
    if (!s_captureOutput)
    {
        serialWrite(c);
        return;
    }
    if (s_captureCapacity > 0 && s_captureLength + 1 < s_captureCapacity)
    {
        s_captureOutput[s_captureLength++] = c;
        s_captureOutput[s_captureLength] = '\0';
    }
    else
    {
        s_captureTruncated = true;
    }
}

void cli_puts(const char *s)
{
    while (*s)
        cli_putc(*s++);
}

void cli_printf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cli_puts(buf);
}

/* Prompt must be printed exactly once when we want a new input line */
static void cli_prompt(void) { cli_puts("> "); }

/* Clear + redraw prompt + buffer; place cursor at s_cur */
static void cli_redraw(void)
{
    /* CR + clear entire line */
    cli_puts("\r\x1b[2K");
    cli_prompt();

    /* print buffer */
    for (int i = 0; i < s_len; i++)
        cli_putc(s_line[i]);

    /* clear to end (in case previous line was longer) */
    cli_puts("\x1b[K");

    /* move cursor back from end to s_cur */
    int back = s_len - s_cur;
    if (back > 0)
        cli_printf("\x1b[%dD", back);
}

/* ---------- Small helpers ---------- */
static int is_enter(char c) { return (c == CR || c == LF); }
static int is_backspace(char c) { return (c == BS || c == DEL); }

static int hist_index_from_newest(int nav)
{
    /* nav=0 => newest entry, nav=count-1 => oldest */
    int newest = (s_hist_start + s_hist_count - 1) % CLI_HISTORY_SIZE;
    int idx = newest - nav;
    if (idx < 0)
        idx += CLI_HISTORY_SIZE;
    return idx;
}

static void hist_add(const char *line)
{
    if (!line || !line[0])
        return;

    /* optional: de-duplicate identical entry */
    for (int i = 0; i < s_hist_count; i++)
    {
        int idx = (s_hist_start + i) % CLI_HISTORY_SIZE;
        if (strncmp(s_hist[idx], line, CLI_MAX_LINE) == 0)
        {
            /* delete by shifting forward until newest */
            int newest = hist_index_from_newest(0);
            for (int j = idx; j != newest;)
            {
                int next = (j + 1) % CLI_HISTORY_SIZE;
                strncpy(s_hist[j], s_hist[next], CLI_MAX_LINE);
                j = next;
            }
            s_hist_count--;
            break;
        }
    }

    int insert;
    if (s_hist_count < CLI_HISTORY_SIZE)
    {
        insert = (s_hist_start + s_hist_count) % CLI_HISTORY_SIZE;
        s_hist_count++;
    }
    else
    {
        /* overwrite oldest */
        insert = s_hist_start;
        s_hist_start = (s_hist_start + 1) % CLI_HISTORY_SIZE;
    }

    strncpy(s_hist[insert], line, CLI_MAX_LINE - 1);
    s_hist[insert][CLI_MAX_LINE - 1] = 0;
}

static void hist_load_nav(int nav)
{
    if (nav < 0)
    {
        s_len = 0;
        s_cur = 0;
        s_line[0] = 0;
        cli_redraw();
        return;
    }
    if (nav >= s_hist_count)
        return;

    int idx = hist_index_from_newest(nav);
    strncpy(s_line, s_hist[idx], CLI_MAX_LINE - 1);
    s_line[CLI_MAX_LINE - 1] = 0;
    s_len = (int)strnlen(s_line, CLI_MAX_LINE);
    s_cur = s_len;
    cli_redraw();
}

static void hist_navigate(int dir)
{
    if (s_hist_count == 0)
        return;

    if (dir < 0) /* up => older => nav++ */
    {
        if (s_hist_nav < s_hist_count - 1)
            s_hist_nav++;
        else
            return;
    }
    else /* down => newer => nav-- */
    {
        if (s_hist_nav >= 0)
            s_hist_nav--;
        else
            return;
    }
    hist_load_nav(s_hist_nav);
}

static void cursor_move(int dir)
{
    int newpos = s_cur + dir;
    if (newpos < 0 || newpos > s_len)
        return;
    s_cur = newpos;
    cli_puts(dir > 0 ? "\x1b[C" : "\x1b[D");
}

static void handle_backspace(void)
{
    if (s_cur <= 0)
        return;

    /* delete char before cursor */
    memmove(&s_line[s_cur - 1], &s_line[s_cur], (size_t)(s_len - s_cur));
    s_len--;
    s_cur--;
    s_line[s_len] = 0;
    cli_redraw();
}

static void handle_insert_char(char c)
{
    if (s_len >= (CLI_MAX_LINE - 1))
        return;

    if (s_cur == s_len) // append at end
    {
        s_line[s_len++] = c;
        s_cur++;
        s_line[s_len] = 0;
        cli_putc(c);
        return;
    }

    memmove(&s_line[s_cur + 1], &s_line[s_cur], (size_t)(s_len - s_cur));
    s_line[s_cur] = c;
    s_len++;
    s_cur++;
    s_line[s_len] = 0;
    cli_redraw();
}

/* ---------- History print ---------- */
static void print_history(void)
{
    cli_puts("\r\nCommand History:\r\n");
    for (int i = 0; i < s_hist_count; i++)
    {
        int idx = (s_hist_start + i) % CLI_HISTORY_SIZE;
        cli_printf("%d: %s\r\n", i + 1, s_hist[idx]);
    }
    cli_prompt();
    /* after printing prompt, we need to print current buffer */
    for (int i = 0; i < s_len; i++)
        cli_putc(s_line[i]);
    /* reposition cursor if not at end */
    int back = s_len - s_cur;
    if (back > 0)
        cli_printf("\x1b[%dD", back);
}

/* ---------- Execute current line ---------- */
static void process_line(void)
{
    cli_puts("\r\n");

    if (s_len > 0)
    {
        hist_add(s_line);
        cmd_parse_line(s_line);
    }

    /* reset editor */
    s_len = 0;
    s_cur = 0;
    s_line[0] = 0;
    s_hist_nav = -1;

    cli_prompt();
}

/* returns 1 if char was consumed by escape parser (do NOT echo) */
static int esc_consume(char c)
{
    switch (s_esc)
    {
    case ESC_NONE:
        if ((unsigned char)c == ESC)
        {
            s_esc = ESC_GOT_ESC;
            return 1;
        }
        return 0;

    case ESC_GOT_ESC:
        if (c == '[')
        {
            s_esc = ESC_GOT_CSI;
            return 1;
        }
        /* Not a CSI sequence. Drop ESC state; let this char be processed normally. */
        s_esc = ESC_NONE;
        return 0;

    case ESC_GOT_CSI:
        /* Simple arrows are ESC [ A/B/C/D */
        s_esc = ESC_NONE;
        switch (c)
        {
        case ARROW_UP:
            hist_navigate(-1);
            return 1;
        case ARROW_DOWN:
            hist_navigate(+1);
            return 1;
        case ARROW_LEFT:
            cursor_move(-1);
            return 1;
        case ARROW_RIGHT:
            cursor_move(+1);
            return 1;
        default:
            break;
        }

        /* Some terminals send extended sequences: ESC [ 1 ; 5 A etc */
        if ((c >= '0' && c <= '9') || c == ';')
        {
            s_esc = ESC_GOT_CSI_PARAM;
            return 1;
        }

        /* Unknown CSI -> consumed */
        return 1;

    case ESC_GOT_CSI_PARAM:
        /* Consume until final byte in range @..~ */
        if (c >= '@' && c <= '~')
            s_esc = ESC_NONE;
        return 1;
    }

    s_esc = ESC_NONE;
    return 0;
}

/* ---------- TAB completion ---------- */
static int is_token_char(char c)
{
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '_') || (c == '.'));
}

static int get_first_token_len(const char *buf, int len)
{
    int i = 0;
    while (i < len && (buf[i] == ' ' || buf[i] == '\t'))
        i++;
    int start = i;
    while (i < len && is_token_char(buf[i]))
        i++;
    return i - start;
}

// clang-format off
static char apply_case_pattern(char pattern, char c)
{
    if (pattern >= 'a' && pattern <= 'z') {
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    } else if (pattern >= 'A' && pattern <= 'Z') {
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    }
    
    return c;
}

// clang-format on

/* Complete only the first token, only if cursor is at end (MVP) */
static void cli_autocomplete(void)
{
    if (s_cur != s_len)
    {
        cli_putc('\a'); /* MVP: no mid-line completion */
        return;
    }

    int tok_len = get_first_token_len(s_line, s_len);

    unsigned cmd_count = 0;
    const cmd_t *cmds = cmd_get_table(&cmd_count);

    if (tok_len <= 0)
    {
        /* Nothing typed: list all commands */
        cli_puts("\r\n");
        for (unsigned i = 0; i < cmd_count; i++)
            cli_printf("%s\r\n", cmds[i].name);
        cli_prompt();
        return;
    }

    char prefix[32];
    if ((unsigned)tok_len >= sizeof(prefix))
        return;

    memcpy(prefix, s_line, (size_t)tok_len);
    prefix[tok_len] = '\0';

    unsigned match_count = 0;
    const cmd_t *first_match = NULL;

    for (unsigned i = 0; i < cmd_count; i++)
    {
        if (str_istartswith(cmds[i].name, prefix))
        {
            if (!first_match)
                first_match = &cmds[i];
            match_count++;
        }
    }

    if (match_count == 0)
    {
        cli_putc('\a');
        return;
    }

    if (match_count == 1)
    {
        const char *full = first_match->name;
        int full_len = (int)strlen(full);

        /*
         * Case strategy:
         * - For each appended character, follow the case of the character
         *   at the same position in the user prefix if available.
         * - If prefix shorter, follow last prefix character case.
         */

        for (int k = tok_len; k < full_len; k++)
        {
            char out = full[k];

            if (tok_len > 0)
            {
                char pattern;
                if (k < tok_len)
                    pattern = prefix[k];
                else
                    pattern = prefix[tok_len - 1];

                out = apply_case_pattern(pattern, out);
            }

            handle_insert_char(out);
        }

        /* Add a space after command */
        handle_insert_char(' ');
        return;
    }

    /* Multiple matches: list + redraw */
    cli_puts("\r\n");
    for (unsigned i = 0; i < cmd_count; i++)
        if (str_istartswith(cmds[i].name, prefix))
            cli_printf("%s\r\n", cmds[i].name);

    cli_redraw();
}

/* Case-insensitive startswith */
int str_istartswith(const char *s, const char *prefix)
{
    while (*prefix)
    {
        char a = *s++;
        char b = *prefix++;

        if (a >= 'a' && a <= 'z')
            a = (char)(a - 32);
        if (b >= 'a' && b <= 'z')
            b = (char)(b - 32);

        if (a != b)
            return 0;
        if (a == 0)
            return 0;
    }
    return 1;
}

/* ---------- Public ---------- */
void cli_init(void)
{
    s_esc = ESC_NONE;

    s_len = 0;
    s_cur = 0;
    s_line[0] = 0;

    s_hist_start = 0;
    s_hist_count = 0;
    s_hist_nav = -1;
}

void cli_print_greeting(void)
{
    cli_puts("\r\nWelcome to CLI\r\n");
    cli_printf(" - History size: %d\r\n", CLI_HISTORY_SIZE);
    cli_puts(" - Ctrl+L: show history\r\n");
    cli_puts(" - Arrows: history/cursor\r\n");
    cli_puts(" - Tab: autocomplete command\r\n");
    cli_puts(" - Enter: execute\r\n");
    cli_puts(" - Backspace: edit\r\n");
    cli_puts(" - Type 'HELP' to list commands\r\n");
    cli_prompt();
}

void cli_poll(void)
{

    while (serialAvailable())
    {
        char c = serialRead();

        /* 1) Escape/CSI sequences (arrows, etc.) */
        if (esc_consume(c))
            continue;

        /* 2) Control commands */
        if ((unsigned char)c == CTRL_L)
        {
            print_history();
            continue;
        }

        /* 3) TAB completion */
        if (c == TAB)
        {
            cli_autocomplete();
            continue;
        }

        /* 4) Enter / Backspace / printable */
        if (is_enter(c))
            process_line();
        else if (is_backspace(c))
            handle_backspace();
        else if ((unsigned char)c >= 0x20 && (unsigned char)c != 0x7F)
            handle_insert_char(c);
        else
        {
            /* ignore other control chars */
        }
    }

}

bool cli_execute_captured(const char *line, char *output, size_t outputCapacity, bool *truncated)
{
    if (!line || !output || outputCapacity == 0 || s_captureOutput)
        return false;

    s_captureOutput = output;
    s_captureCapacity = outputCapacity;
    s_captureLength = 0;
    s_captureTruncated = false;
    output[0] = '\0';
    cmd_parse_line(line);
    const bool wasTruncated = s_captureTruncated;
    s_captureOutput = nullptr;
    s_captureCapacity = 0;
    if (truncated)
        *truncated = wasTruncated;

    return true;
}
