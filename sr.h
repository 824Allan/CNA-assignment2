/* sr.h - Header for Selective Repeat protocol */
#ifndef SR_H
#define SR_H

#include "emulator.h"

/* Initialization functions */
extern void A_init(void);
extern void B_init(void);

/* Sender (A) side functions */
extern void A_output(struct msg);
extern void A_input(struct pkt);
extern void A_timerinterrupt(void);

/* Receiver (B) side functions */
extern void B_input(struct pkt);

/* Optional: B_output and B_timerinterrupt for bidirectional (not used here) */
extern void B_output(struct msg);
extern void B_timerinterrupt(void);

#endif /* SR_H */
