#ifndef _WM_SIGNALS_H_
#define _WM_SIGNALS_H_

void sigchld(int unused);
void sigint(int unused);
void setup_signals();

#endif
