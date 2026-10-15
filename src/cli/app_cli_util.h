#pragma once
#include "cmd_parser.h"
#include "serial_cli.h"

#define CMD_DECL(name) int name(int argc, char *argv[])
#define CMD_ENTRY(name, help_str)                                                                                      \
    {                                                                                                                  \
        (#name) + 4, help_str, name                                                                                    \
    }
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/*
 * Compile-time master switch.
 * If set to 0, color support is completely removed.
 */
#ifndef CLI_USE_COLORS
#define CLI_USE_COLORS 1
#endif

#if CLI_USE_COLORS
extern int g_cli_color_enabled;

static inline void cli_color_begin(const char *ansi_color)
{
    if (g_cli_color_enabled && ansi_color)
        cli_puts(ansi_color);
}

static inline void cli_color_end(void)
{
    if (g_cli_color_enabled)
        cli_puts("\x1b[0m");
}
#else
static inline void cli_color_begin(const char *ansi_color) { (void)ansi_color; }
static inline void cli_color_end(void) {}
#endif

#define CLI_ANSI_RED "\x1b[31m"
#define CLI_ANSI_GREEN "\x1b[32m"
#define CLI_ANSI_YELLOW "\x1b[33m"
#define CLI_ANSI_BLUE "\x1b[34m"
#define CLI_ANSI_CYAN "\x1b[36m"

// clang-format off
/* Logging macros */
#define CLI_OK(fmt, ...)   do { cli_color_begin(CLI_ANSI_GREEN);  cli_puts("OK: ");   cli_color_end(); cli_printf(fmt "\r\n", ##__VA_ARGS__); } while (0)
#define CLI_ERR(fmt, ...)  do { cli_color_begin(CLI_ANSI_RED);    cli_puts("ERR: ");  cli_color_end(); cli_printf(fmt "\r\n", ##__VA_ARGS__); } while (0)
#define CLI_WARN(fmt, ...) do { cli_color_begin(CLI_ANSI_YELLOW); cli_puts("WARN: "); cli_color_end(); cli_printf(fmt "\r\n", ##__VA_ARGS__); } while (0)
#define CLI_INFO(fmt, ...) do { cli_color_begin(CLI_ANSI_CYAN);   cli_puts("INFO: "); cli_color_end(); cli_printf(fmt "\r\n", ##__VA_ARGS__); } while (0)

#define CLI_USAGE(s)       do { cli_color_begin(CLI_ANSI_BLUE);   cli_puts("Usage: ");cli_color_end(); cli_puts(s "\r\n"); } while (0)
// clang-format on

/* REQUIRE macros unchanged */
#define REQUIRE_BSP()                                                                                                  \
    const AppBsp *bsp = app_bsp();                                                                                     \
    if (!bsp)                                                                                                          \
    {                                                                                                                  \
        CLI_ERR("BSP not available");                                                                                  \
        return 1;                                                                                                      \
    }

#define REQUIRE_CAP(expr, msg)                                                                                         \
    if (!(expr))                                                                                                       \
    {                                                                                                                  \
        CLI_ERR("%s", msg);                                                                                            \
        return 1;                                                                                                      \
    }
