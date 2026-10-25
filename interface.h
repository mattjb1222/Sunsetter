/* ***************************************************************************
 *                                Sunsetter                                  *
 *				 (c) Ben Dean-Kawamura, Georg v. Zimmermann                  *
 *   For license terms, see the file COPYING that came with this program.    *
 *                                                                           *
 *  Name: interface.h                                                        *
 *  Purpose: Has the data structures and function prototypes relating to the *
 * user interface                                                            *
 *                                                                           *
 *  Comments: interface.h contatains prototypes, structures and global       *
 * variables for communication.  See DESIGN for a description of the         *
 * what happens.                                                             *
 *                                                                           *
 *************************************************************************** */

#ifndef _INTERFACE_
#define _INTERFACE_

#include "bughouse.h"

/* logFile is the file to log input and output if LOG is defined */

#ifdef LOG

extern FILE *logFile;

#endif

/* clientLogin is what the user sees when loging in.  */

extern char *clientLogin;


#include "board.h"



extern int partner;				/* If we have a partner in
								client mode */

void output(const char *str);              /* output() prints a string to the
											approprate location */


void parseHolding(const char *str);


void parseOption(const char *str);         /* parseOption() reads the input
											that is given to Sunsetter
											and handles it accordingly. */

int checkInput();				/* See's if input is waiting and
								checks other things, like
								flag falls */

void pollForInput();			/* it will
								see if there is input waiting and
								give it to xboardOption. */

void waitForInput();			/* Don't do anything until some kind
								of input comes in */
void giveMove(move m);

void reportResult (gameResult res);

long getSysMilliSecs();



/* Functions for the partner communication */




int parsePartnerCommand(const char *arg1, const char *arg2, const char *arg3);  
									/* Used when Sunsetters partner 
									ptells it something */

void givePartnerHelp(const char *str);     /* Gives help about the
											commands Sunsetter understands
											from it's partner */

int canSitByClock();                  /* 1 if we're up on time enough to count on a partner drop
								(gates the ghost-piece search; see
								canSitByClock in partner.cpp) */

void tellPartnerClock();             /* Tells the partner the clocks of
								our board */

extern int partnerTime;             /* The partners clock,
							  1/100th seconds */
extern int partOppTime;            /* The partners opponents clock,
							  1/100th seconds */
extern int partnerClockKnown;      /* 1 if the partner told us it's
							  clocks */
extern int partnerIsSunsetter;    /* 1 if the partner is another
				  Sunsetter (bot-vs-bot coordination) */
extern int partnerDead;         /* 1 if the partner told us "I (am) dead":
				  no more pieces will come from its board -
				  don't count on drops, play fast to win */
extern int partOppDead;         /* 1 if the partner told us "Opp dead"
				  (its opponent is dead): sit when ahead on
				  time over that opponent, else move until
				  we are (see partnerOppDeadSit) */

int partnerSittingLong();         /* 1 if our partner (another
				  Sunsetter) is sitting on his own for a
				  while and up on time - we move instead
				  of sitting (see partner.cpp) */

int partnerOppDeadSit();         /* 1 if we should sit for "Opp dead":
				  the partner's opponent is dead and we're
				  ahead on time over it - we sit; the caller
				  re-checks each turn, so we move until we are
				  (see partner.cpp) */


void tellPartnerDirectives();        /* Sunsetter's own piece
							   * directives (request / withhold),
							   * once per turn of our own */

#endif
