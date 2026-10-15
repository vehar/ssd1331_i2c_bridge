/* cmd_parser.h */
#pragma once
#include "cmd_registry.h"
#include <stdint.h>

extern const cmd_t g_cmds[]; /* Command table from commands_impl.c */
extern const unsigned g_cmd_count;

/* NEW: helpers (optional but useful) */
int cmd_find_matches(const char *prefix, const cmd_t **first_match, unsigned *match_count);

// Tools
void cli_puts(const char *s);
void cli_printf(const char *fmt, ...);

int str_ieq(const char *a, const char *b);

int parse_u8(const char *s, uint8_t *out);
int parse_u16(const char *s, uint16_t *out);
int parse_u32(const char *s, uint32_t *out);
int parse_u64(const char *s, uint64_t *out);
int parse_bool(const char *s, uint8_t *out);
// Entry point
void cmd_parse_line(const char *line);
