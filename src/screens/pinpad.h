/* the parent PIN (four digits, on this TV): checked, or made (typed twice) */
#ifndef SB_PINPAD_H
#define SB_PINPAD_H
#include "core/app.h"
enum { PIN_CHECK, PIN_NEW };
typedef void (*PinDone)(int ok, void *ud);
extern Screen pinPad;
int  pin_is_set(void);
void pin_ask(int mode, const char *title, PinDone done, void *ud);  /* PIN_CHECK with no PIN made: done(1) at once */
int  pin_open(void);
#endif
