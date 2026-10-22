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
    output("tellics ptell Hi.  Thanks for being my partner, I am Sunsetter.  Tell me \"help\" for the commands I understand, or \"help <command>\" for details on one.\n");
    output("tellics ptell I understand these commands: sit, go, sitting (frozen), time, flag, abort, cancel, nevermind, x, help and piece directives (+P, -Q, +++, ----).  If you tell me a move (\"Qf3\", or a drop \"Qh8\") I will play it.\n");
  
  } else if(!strcasecmp(str, "sitting") || !strcasecmp(str, "frozen")) {
    output("tellics ptell \"sitting\" (or \"frozen\"): tells me you are sitting.  I note it (while you sit, only my moves can feed you pieces) and I get up from my own sit. \n");
  } else if(!strcasecmp(str, "sit")) {
    output("tellics ptell \"sit\": I stop moving and sit, waiting for your \"go\" or for a piece to drop for me - even if I am behind on time or not even mated.  One \"sit\" is one sit; say it again if you want me to keep sitting.  A pending one is retracted by \"go\", \"nevermind\", \"x\" or \"cancel\". \n");
  } else if(!strcasecmp(str, "go")) {
    output("tellics ptell \"go\": I get up from sitting, and if I am mated-soon I make exactly one move instead of sitting.  Also retracts a pending \"sit\". \n");
  } else if(!strcasecmp(str, "flag")) {
    output("tellics ptell \"flag\": I flag my opponent on my board.  I usually auto-flag when my clock runs out, but say it if you want to be sure.\n");
  } else if(!strcasecmp(str, "time")) {
    output("tellics ptell \"time <your clock> <your opponents clock>\" (1/100th seconds): tells me your board's clocks so I know when I am up on time.  When my icsdrone relays the server's clocks to me this is redundant but harmless.\n");
  } else if(!strcasecmp(str, "abort")) {
    output("tellics ptell \"abort\": I offer an abort (and accept yours).\n");
  } else if(!strcasecmp(str, "cancel")) {
    output("tellics ptell \"cancel\": reset the sit bookkeeping and the piece directives.\n");
  } else if(!strcasecmp(str, "directive") || !strcasecmp(str, "directives")) {
    output("tellics ptell \"+P\" to \"++++P\" and \"-Q\" to \"----Q\" (one to four signs): try to take an enemy P / protect my Q, harder with more signs.  \"++\" to \"++++\" (one to four signs): trading is good; \"--\" to \"----\": trading is bad.  If a piece I'm protecting can't be saved I sit and tell you the X is coming - tell me \"go\" to make moves anyway.\n");
  } else if(!strcasecmp(str, "nevermind") || !strcasecmp(str, "x")) {
    output("tellics ptell \"nevermind\"/\"x\": retract a pending \"sit\" before I sat - I keep moving.  It does not get me up (use \"go\" for that).\n");
  } else if(!strcasecmp(str, "help")) {
    output("tellics ptell \"help\": the command list; \"help <command>\": details on that command, like this.\n");
  } else if(str[0] == '\0') {
    output("tellics ptell I understand the following commands: sit, go, sitting (frozen), time, flag, abort, cancel, nevermind, x, help and piece directives (+P, -Q, +++, ----).  If you tell me a move (\"Qf3\", or a drop \"Qh8\") I will play it.  \"help <command>\" gives details.\n");

  } else {
    sprintf(buf, "tellics ptell Sorry, I don't have help on %s.  Tell me \"help\" for the command list.\n", str);
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
 *           overtaken).  Because voluntary sitting bleeds our clock
 *           (the server stops it only for a checkmated player), "ahead"
 *           means a real lead (>= 3s and >= double their time), not
 *           just "not behind".  This also gates our voluntary sit:
 *           when we're mated soon (mate in 1) but have legal moves
 *           left, we sit while this returns 1 (we have a real lead on
 *           time), make one move on a partner "go", and keep moving
 *           otherwise (see findMove).
 */

int canSitByClock()
   {
   int ours, theirs;

   ours = boardClock(gameBoard.getDeepBugColor());
   theirs = boardClock(otherColor(gameBoard.getDeepBugColor()));

   /* We're behind on our board, keep moving to catch up on time */
   if (ours < theirs)
      return 0;

   /* Voluntary sitting bleeds our whole clock (the server only stops
    * it for a checkmated player), so "not behind" is not enough:
    * require a real lead - at least 3 full seconds ahead (a 3+0
    * game can never satisfy that, so we never sit there - correct,
    * a sit is never worth it on a 3 second clock) and at least
    * double their time */
   if ((ours - theirs) < 300)
      return 0;
   if (ours < (2 * theirs))
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
      && (ours <= theirs))
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


/* Function: parsePartnerDirective
 * Input:    A token from a partner ptell
 * Output:   1 if it's a piece directive
 * Purpose:  "+P".."++++Q" = try to take enemy piece type (1-4 signs =
 *           how hard), "-P".. = protect my piece type, "++"/"--"
 *           (1-4 signs) = trading is good/bad.  Consumed by
 *           findMove() (guard check / sit) and orderCaptures()
 *           (partTake/partTrade) */
static int parsePartnerDirective(const char *tok)
{
  int n = 0, p;

  if ((tok[0] != '+') && (tok[0] != '-')) return 0;
  while ((tok[n] == tok[0]) && (tok[n] != '\0')) n++;
  if (n > 4) n = 4;
  if (tok[n] == '\0') { partTrade = (tok[0] == '+') ? n : -n; return 1; }
  if (tok[n + 1] != '\0') return 0;
  p = tok[n];
  if ((p >= 'a') && (p <= 'z')) p -= 32;
  switch (p)
  {
  case 'P': p = PAWN; break;
  case 'N': p = KNIGHT; break;
  case 'B': p = BISHOP; break;
  case 'R': p = ROOK; break;
  case 'Q': p = QUEEN; break;
  default: return 0;
  }
  if (tok[0] == '+') partTake[p] = n;
  else partGuard[p] = n;
  return 1;
}

int parsePartnerCommand(const char *arg1, const char *arg2, const char *arg3)
{

  move toldMove;

  if(!strcasecmp(arg1, "help")) {
    givePartnerHelp(arg2);
    return 1;
  }

  /* Piece directives may be one or more tokens ("ptell +Q --") */
  if((parsePartnerDirective(arg1)) || (parsePartnerDirective(arg2)) || (parsePartnerDirective(arg3))) return 1;

  if(!strcasecmp(arg1, "time")) {
    if (arg2[0] != '\0') { partnerTime = atoi(arg2); partOppTime = atoi(arg3); partnerClockKnown = 1; }
    return 1;
  }

  if((currentRules == CRAZYHOUSE) && (gameInProgress)) {
    return 1;
  }

  toldMove = gameBoard.algebraicMoveToDBMove(arg1);

  /* A drop on our board relayed by the wrapper is the partner's
   * event, not one of our moves (our hand is empty in bughouse, so
   * "playing" it would fail, and we'd never get up from sitting =>
   * our clock runs to zero => "resign") */
  if(!toldMove.isBad() && (currentRules == BUGHOUSE) && (toldMove.from() == IN_HAND)
     && (gameBoard.pieceOnSquare(toldMove.to()) == NONE))
  {
  gameBoard.applyPartnerDrop(toldMove);
  stopThought();
  if (sitting) unsit(); /* the drop made our board playable */
  }
  else if(!toldMove.isBad()) doMove(toldMove);  
  else if(!strcasecmp(arg1, "sitting") || !strcasecmp(arg1, "frozen")) { partsitting = 1;  unsit();}
  /* A partner request to sit is obeyed: parttoldsit makes
   * findMove() sit - even if we aren't mated or aren't up on time
   * (the canSitByClock() veto only applies to our *voluntary*
   * sitting, not to an explicit partner request).  A "go"
   * (toldPartGoThisTurn) takes precedence and makes one move, and a
   * "go"/"cancel"/"nevermind"/"x" drops the request; the request is
   * one-shot (consumed when we sit), so a partner that
   * wants a long sit simply says "sit" again.  We say nothing here
   * (no ptell flood); findMove() says "sitting" when it actually
   * sits. */
  else if(!strcasecmp(arg1, "sit")) { parttoldsit = 1; stopThought(); } 
  else if(!strcasecmp(arg1, "go")) { parttoldgo = 1; parttoldsit = 0; unsit(); stopThought(); } 
  else if(!strcasecmp(arg1, "flag")) output("tellics flag\n");
  /* Retraction of a pending sit request: just drop parttoldsit (the
   * partner's "cancel"/"nevermind"/"x" - "go" above does the same). */
  else if(!strcasecmp(arg1, "nevermind") || !strcasecmp(arg1, "x")) { parttoldsit = 0; return 1; }
  else if(!strcasecmp(arg1, "cancel")){ partsitting = 0;   sitting = 0; toldpartisit = 0; parttoldsit = 0; { int di; for (di = 1; di <= 5; di++) partTake[di] = partGuard[di] = 0; } partTrade = 0; }
  else if(!strcasecmp(arg1, "abort")) output("tellics abort\n");
  else if(!strcasecmp(arg1, "hi") || !strcasecmp(arg1, "hello") ||
	  !strcasecmp(arg1, "hiya") || !strcasecmp(arg1, "hi!")) { output("tellics ptell hi!\n");
    return 1; } 
  
  else return -1;
  return 0;
}

