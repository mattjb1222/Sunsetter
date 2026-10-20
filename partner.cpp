/* ***************************************************************************
 *                                Sunsetter                                  *
 *				 (c) Ben Dean-Kawamura, Georg v. Zimmermann                  *
 *   For license terms, see the file COPYING that came with this program.    *
 *                                                                           *
 *  Name: partner.cc                                                         *
 *  Purpose: Has functions related to partner communication.                 *
 *                                                                           *
 *************************************************************************** */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "board.h"
#include "brain.h"
#include "interface.h"
#include "notation.h"



/* partner is 1 if we have a partner  */

int partner;

/* The clocks of the partner's board, 1/100th seconds, given to us by the
 * partner through "ptell time <partners clock> <opponents clock>"  */

int partnerTime;          /* The partners clock  */
int partOppTime;         /* The partners opponents clock  */
int partnerClockKnown;   /* 1 if the partner told us it's clocks  */

/* Function: givePartnerHelp
 * Input:    An argument
 * Output:   None
 * Purpose:  Used to give a ptell to Sunsetter's partner telling how to
 *           communicate with Sunsetter
 */

void givePartnerHelp(const char *str)
{
  char buf[MAX_STRING];

  if(!strcasecmp(str, "intro")) {
    output("tellics ptell Hi.  Thanks for being my partner.  Tell me \"help\" for the commands I understand and please visit http://www.d2d4.de/sunsetter.asp\n");
  
  } else if(!strcasecmp(str, "sitting") || !strcasecmp(str, "frozen")) {
    output("tellics ptell if you tell me that you are sitting I know that no stuff comes for me or my opponent. \n");
  } else if(!strcasecmp(str, "sit") || !strcasecmp(str, "go")) {
    output("tellics ptell sit means to sit, go means to stop sitting. \n");
  } else if(!strcasecmp(str, "flag")) {
    output("tellics ptell Use flag to tell me to flag my opponent (I usually have autoflag on, but sometimes I forget).\n");
  } else if(!strcasecmp(str, "time")) {
    output("tellics ptell Tell me time <my clock> <my opponents clock> (in 1/100th seconds) to help me decide when to sit for a piece. I will tell you my clocks the same way.\n");
  } else if(!strcasecmp(str, "abort")) {
    output("tellics ptell Use abort to make me offer or accept an abort request\n");
  } else if(str[0] == '\0') {
    output("tellics ptell I understand the following commands:  sitting/frozen, sit, go/move, time, flag and abort. Also If you tell me a move then I'll play it. Tell me help <the command> for help on that command.\n");

  } else {
    sprintf(buf, "tellics ptell Sorry I don't have help on %s\n", str);
    output(buf);
  }
}

/* Function: parsePartnerCommand
 * Input:    *arg1 , *arg2
 * Output:    0 if successfull and we should give back some acknowledgement,
 *            1 if sucsessfull and we don't need to say anything,
 *           -1 if not sucsessfull
 * Purpose:  Used to interpret a ptell from Sunsetter's partner.  It gets the
 *           second word of the message.
 */

/*
 * Function: boardClock
 * Input:    A color on the main board
 * Output:   That colors remaining clock time in 1/100th seconds
 * Purpose:  Used to read the clock.  getTime() is only updated after a
 *           move, so while a clock is running we have to count the time
 *           since lastMoveTime down from the last reported value
 *           (the same way outOfTime() does it).  While sitting our clock
 *           is stopped, so no counting down then.
 */

static int boardClock(color c)
   {
   int time = gameBoard.getTime(c);

   if ((gameBoard.getColorOnMove() == c)
      && !(sitting && (c == gameBoard.getDeepBugColor())))
      time -= (int)((getSysMilliSecs() - gameBoard.getLastMoveTime()) / 10);
   if (time < 0) time = 0;

   return time;
   }


/*
 * Function: canSitByClock
 * Input:    None
 * Output:   1 if we're "up on time" enough to count on pieces being
 *          dropped for us (worth searching for ghost pieces),
 *          0 if we have to keep moving to catch up on time
 * Purpose:  A win that only exists with a dropped piece is only worth
 *           searching for while we're up on time: when we're behind
 *           on time we have to keep moving to catch up, and a ghost
 *           win is no good then.  If the partner's board clocks are
 *           known and the partner is moving (not sitting), we also keep
 *           moving while the partner is behind, to feed it pieces.  If
 *           the partner is sitting, his board is stalled (his opponent
 *           isn't moving either), so only our moves feed him pieces.
 *           If the opponent has sat for a long time, we assume he is
 *           trying to run out the clock and don't count on drops until
 *           we're up on time again (not even level, to avoid being
 *           overtaken).  Note: Sunsetter itself never *sits*
 *           voluntarily (it only sits when checkmated) because xboard
 *           has no "sit" command, so the server keeps running our clock
 *           while we sit.
 */

int canSitByClock()
   {
   /* We're behind on our board, keep moving to catch up on time */
   if (boardClock(gameBoard.getDeepBugColor())
       < boardClock(otherColor(gameBoard.getDeepBugColor())))
      return 0;

   /* The partner is moving and behind, keep moving to feed it pieces */
   if (partnerClockKnown && !partsitting
      && (partnerTime < partOppTime))
      return 0;

   /* The partner is sitting, so his board is stalled (his opponent
    * isn't moving either): only our moves feed him pieces, so
    * keep moving */
   if (partsitting)
      return 0;

   /* The opponent has been sitting for a long time (the partner board
    * has sat for a while, so the opponent's clock is running): assume
    * he is trying to run out the clock and win, keep moving until we
    * are up on time again (so don't sit even level, to avoid being
    * overtaken) */
   if ((psittinglong >= 5)
      && (boardClock(gameBoard.getDeepBugColor())
          <= boardClock(otherColor(gameBoard.getDeepBugColor()))))
      return 0;

   return 1;
   }


/*
 * Function: tellPartnerClock
 * Input:    None
 * Output:   None
 * Purpose:  Used to tell Sunsetter's partner the clocks of our board,
 *          so the partner (e.g. another Sunsetter) can decide when to
 *          sit for a piece.
 */

void tellPartnerClock()
   {
   char buf[MAX_STRING];

   if (partner == 0)
      return;

   sprintf(buf, "tellics ptell time %d %d\n",
           boardClock(gameBoard.getDeepBugColor()),
           boardClock(otherColor(gameBoard.getDeepBugColor())));
   output(buf);
   }


int parsePartnerCommand(const char *arg1, const char *arg2, const char *arg3)
{

  move toldMove;

  if(!strcasecmp(arg1, "help")) {
    givePartnerHelp(arg2);
    return 1;
  }

  if(!strcasecmp(arg1, "time")) {
    if (arg2[0] != '\0') { partnerTime = atoi(arg2); partOppTime = atoi(arg3); partnerClockKnown = 1; }
    return 1;
  }

  if((currentRules == CRAZYHOUSE) && (gameInProgress)) {
    return 1;
  }

  toldMove = gameBoard.algebraicMoveToDBMove(arg1);

  if(!toldMove.isBad()) doMove(toldMove);  
  else if(!strcasecmp(arg1, "sitting") || !strcasecmp(arg1, "frozen")) { partsitting = 1;  unsit();}
  /* We never sit voluntarily, even when our partner asks: xboard
   * has no "sit" command, so the server keeps running our clock
   * while we sit and we'd just flag (we only sit when checkmated,
   * see findMove).  We keep moving instead - our captures feed the
   * partner anyway.  We deliberately say nothing, to avoid an
   * endless sit - refuse - sit loop with other programs */
  else if(!strcasecmp(arg1, "sit")) { /* keep moving */ } 
  else if(!strcasecmp(arg1, "go")) { parttoldgo = 1; unsit(); stopThought(); } 
  else if(!strcasecmp(arg1, "flag")) output("tellics flag\n");
  else if(!strcasecmp(arg1, "cancel")){ partsitting = 0;   sitting = 0; toldpartisit = 0; }
  else if(!strcasecmp(arg1, "abort")) output("tellics abort\n");
  else if(!strcasecmp(arg1, "hi") || !strcasecmp(arg1, "hello") ||
	  !strcasecmp(arg1, "hiya") || !strcasecmp(arg1, "hi!")) { output("tellics ptell hi!\n");
    return 1; } 
  
  else return -1;
  return 0;
}

