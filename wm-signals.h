#ifndef _WM_SIGNALS_
#define _WM_SIGNALS_

void sigchld(int unused);
void sigint(int unused);
void setup_signals();

#endif
