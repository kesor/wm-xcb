#ifndef _WM_SIGNALS_H_
#define _WM_SIGNALS_H_

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

void sigchld(int unused);
void sigint(int unused);
void setup_signals();

/*
 * Returns 1 if a SIGUSR1 arrived since the last call, 0 otherwise. Several
 * signals before the loop runs collapse into one dump. The event loop calls
 * this between dispatches and dumps the state machines when it says yes.
 */
int signals_take_dump_request(void);

#endif
