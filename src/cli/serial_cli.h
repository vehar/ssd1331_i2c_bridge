/* serial_cli.h */
#pragma once
#include "cmd_registry.h"
#include <stddef.h>
#include <stdint.h>

#ifndef CLI_MAX_LINE
#define CLI_MAX_LINE 128
#endif

#ifndef CLI_HISTORY_SIZE
#define CLI_HISTORY_SIZE 64
#endif

/* CLI API */
void cli_init(void);
void cli_print_greeting(void);
void cli_poll(void); /* call in main loop */

/**
 * @brief Parse and execute a single command line.
 *
 * @param line Null-terminated command string.
 *
 * Behavior:
 *  - Splits line into argv tokens
 *  - Executes matching command
 *  - Prints error if unknown
 *  - Does NOT modify caller buffer
 *
 * This function is transport-independent.
 */
void cmd_parse_line(const char *line);
bool cli_execute_captured(const char *line, char *output, size_t outputCapacity, bool *truncated);

/*
 * ---------------------------------------------------------------------------
 * Platform Serial Transport Hooks you must implement
 * ---------------------------------------------------------------------------
 *
 * These functions provide a non-blocking, byte-oriented transport layer
 * between the CLI core and the hardware UART.
 *
 * CONTRACT (must be identical across Cube / Arduino / ESP32):
 *
 * 1) Non-blocking:
 *    - serialAvailable() MUST NOT block.
 *    - serialRead() MUST NOT block.
 *    - serialWrite() MAY wait for TX ready, but MUST NOT block indefinitely.
 *
 * 2) Buffering:
 *    - RX must use a ring buffer (recommended ≥128 bytes).
 *    - serialAvailable() returns number of unread bytes.
 *    - serialRead() returns oldest unread byte and advances buffer.
 *    - Behavior is undefined if serialRead() is called when no data available.
 *
 * 3) Interrupt model:
 *    - RX is expected to run in IRQ context and push bytes into buffer.
 *    - serialAvailable()/serialRead() are called from main loop context.
 *    - Head/tail handling must be race-safe (single producer / single consumer).
 *
 * 4) Determinism:
 *    - No dynamic allocation.
 *    - No printf inside these functions.
 *    - Must not break timing of ADC/DMA workloads.
 *
 * 5) Expected semantics:
 *    - No byte duplication.
 *    - No phantom characters.
 *    - No silent blocking.
 *
 * Minimal behavior:
 *    int  serialAvailable(void);  // return number of unread RX bytes (>=0)
 *    char serialRead(void);       // return next RX byte (caller ensures available)
 *    void serialWrite(char c);    // transmit one byte
 *
 * ---------------------------------------------------------------------------
 */

int serialAvailable(void);
char serialRead(void);
void serialWrite(char c);

// private
int str_istartswith(const char *s, const char *prefix);
