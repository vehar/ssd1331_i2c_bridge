// cmd_registry.h
#pragma once

typedef int (*cmd_handler_t)(int argc, char *argv[]);

typedef struct
{
    const char *name;
    const char *help;
    cmd_handler_t handler;
} cmd_t;

/**
 * @brief Return pointer to command table and number of entries.
 *        Implemented by application (app_core).
 */
const cmd_t *cmd_get_table(unsigned *count);
