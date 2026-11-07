#ifndef _WM_SIGNALS_H_
#define _WM_SIGNALS_H_

#include <stdbool.h>

/*
 * Read end of the pipe the signal handlers write a byte to. -1 when the pipe
 * could not be created, which tells a poll(2) caller to use a timeout instead.
 */
int signals_wakeup_fd(void);

/*
 * Discards the pending wakeup bytes, so the pipe is not still readable on the
 * next poll(2). Returns without waiting: the pipe is non-blocking.
 */
void signals_drain_wakeups(void);

/*
 * True when a SIGHUP has arrived since the last call, and false otherwise: the
 * loop reloads the config file once per signal. Asking clears the request, so a
 * caller that reloads and fails does not try again on the next iteration.
 */
bool signals_take_reload_request(void);

void sigchld(int unused);
void sighup(int unused);
void sigint(int unused);
void setup_signals();

#endif
