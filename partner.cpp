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
    output("tellics ptell I understand these commands: sit, go, sitting (frozen), time, flag, abort, cancel, nevermind, x, help, Mates Me/Him and piece directives (+P, -Q, ++, ----).  If you tell me a move (\"Qf3\", or a drop \"Qh8\") I will play it.\n");
  
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
    output("tellics ptell \"cancel\": like \"nevermind\"/\"x\" (see there): retract a pending \"sit\" and reset the piece directives to default.\n");
  } else if(!strcasecmp(str, "directive") || !strcasecmp(str, "directives")) {
    output("tellics ptell \"+P\" to \"++Q\": try to take an enemy P (feed me a P).  \"-Q\" to \"--Q\": protect my Q - I move it to safety / refuse to trade it.  One sign is one-time (I reset it when it's done - the piece was taken/lost); two or more signs stand until \"cancel\"/\"nevermind\"/\"x\" (\"+++Q\" = \"++Q\", \"----Q\" = \"--Q\").  \"++\" to \"++++\": trading is good; \"--\" to \"----\": trading is bad.\n");
    output("tellics ptell After a \"-Q\": add \"Mates Me\" and if I can't save the Q I sit and tell you it's coming - tell me \"go\" to make moves anyway.  After a \"+X\": \"Mates Him\" = I sacrifice material to take the X.\n");
    output("tellics ptell I send these directives myself, once a turn: \"++X\" + \"Mates Him\" = I'm behind in material and want an X (sacrifice to take it - feed me); \"--X\" + \"Mates Me\" = please withhold an X (it would hang on my board, or I'm getting mated).  \"nevermind\" retracts a piece that is no longer good or bad (the state resets when it changes or goes away).\n");
  } else if(!strcasecmp(str, "nevermind") || !strcasecmp(str, "x")) {
    output("tellics ptell \"nevermind\"/\"x\"/\"cancel\": retract a pending \"sit\" before I sat - I keep moving; also resets the piece directives to default (no piece is \"good\" or \"bad\" anymore).  It does not get me up (use \"go\" for that).\n");
  } else if(!strcasecmp(str, "mates")) {
    output("tellics ptell After a \"-X\":  \"Mates Me\" - I protect the X to the max: I move it to safety / refuse to trade it, and if I can't save it I sit before it must be given up (tell me \"go\" to move anyway).\n");
    output("tellics ptell After a \"+X\":  \"Mates Him\" - I sacrifice material to take the X.\n");
  } else if(!strcasecmp(str, "help")) {
    output("tellics ptell \"help\": the command list; \"help <command>\": details on that command, like this.\n");
  } else if(str[0] == '\0') {
    output("tellics ptell I understand the following commands: sit, go, sitting (frozen), time, flag, abort, cancel, nevermind, x, help, Mates Me/Him and piece directives (+P, -Q, ++, ----).  If you tell me a move (\"Qf3\", or a drop \"Qh8\") I will play it.  \"help <command>\" gives details.\n");

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


/*
 * Function: tellPartnerDirectives
 * Input:    None
 * Output:   None (ptells the partner when the state changes)
 * Purpose:  Sunsetter gives piece directives too, once per turn of our
 *           own, before the search.  If we're behind in material
 *           (board + both holdings, see materialBalanceForDeepBug())
 *           we request a piece that plausibly closes the gap:
 *           "++q" + "Mates Him" (I take the X at any cost - feed me
 *           an X), or "++r"/"++n" for smaller deficits.  We don't
 *           request a piece we can drop from our own holding, and we
 *           don't request one that would just hang (no safe square).
 *           Otherwise we ask to withhold the most valuable piece type
 *           that can't be placed safely on our board (every empty
 *           square attacked): "--q" + "Mates Me" (an X there would
 *           be lost - material loss; if the king has no safe square
 *           this is the "we're getting mated" case).  A mated
 *           position retracts everything (nothing would help).  The
 *           directives are standing (two signs) and live in the
 *           static; a changed or vanished state retracts with
 *           "nevermind"
 *           first (the partner's "x" = reset the piece directives).
 */
void tellPartnerDirectives()
{
  static int lastSent = 0;	/* 0 = nothing; 10+p = request type p
					   * (standing "++"), 60+p = withhold type p
					   * (standing "--") */
  move aim[MAX_MOVES];
  int balance, requested, take, hold, code, safe;

  if ((currentRules != BUGHOUSE) || (!gameInProgress) || (analyzeMode)) return;

  if (gameBoard.moves(aim) == 0)
  {
	 /* checkmated (findMove will sit "I am mated" for it) - nothing
	  * we ask for will help */
	 if (lastSent) { output("tellics ptell nevermind\n"); lastSent = 0; }
	 return;
	}

  balance = gameBoard.materialBalanceForDeepBug();

  /* A request if we're behind, for a piece whose value plausibly
   * closes the gap (the deficit thresholds are in pValue units) */
  requested = 0;
  if (balance <= -(2 * pValue[QUEEN])) requested = QUEEN;
  else if (balance <= -pValue[QUEEN]) requested = ROOK;
  else if (balance <= -pValue[ROOK]) requested = KNIGHT;

  /* Safe/unsafe is piece-independent (a square safe for a rook is safe for a
   * queen and vice versa - hasSafeDropSquare ignores the piece type), so
   * evaluate it once. */
  safe = gameBoard.hasSafeDropSquare(requested);

  take = requested;
  if ((take != 0) && (gameBoard.getPieceInHand(gameBoard.getDeepBugColor(), take) >= 1)) take = 0;	/* we can drop it ourselves */
  if ((take != 0) && (!safe)) take = 0;	/* it would just hang */

  /* Withhold, if we can't place anything safely: withhold the piece we were
   * about to request (directly responsive - "I wanted X but it would hang")
   * if one was requested, else the most valuable piece (the queen, the worst
   * waste). */
  hold = 0;
  if ((take == 0) && (!safe))
	 hold = (requested != 0) ? requested : QUEEN;

  code = (take != 0) ? (10 + take) : (hold != 0) ? (60 + hold) : 0;

  if (code == lastSent) return;	/* state unchanged, no repeat */

  if (lastSent) output("tellics ptell nevermind\n");

  if (code != 0)
  {
	 char pieceLetter[PIECES] = { 0, 'p', 'r', 'n', 'b', 'q' };
	 char buf[MAX_STRING];

	 sprintf(buf, "tellics ptell %c%c%c\n", (code < 60) ? '+' : '-', (code < 60) ? '+' : '-', pieceLetter[code % 10]);
	 output(buf);
	 output((code < 60) ? "tellics ptell Mates Him\n" : "tellics ptell Mates Me\n");
	 lastSent = code;
	}
  else
	 lastSent = 0;
	}


/* Function: parsePartnerDirective
 * Input:    A token from a partner ptell
 * Output:   1 if it's a piece directive
 * Purpose:  "+P".."++Q": try to take the enemy piece type, "-P"..: protect my piece type.  One sign is a one-time request (value 1: auto-reset when fulfilled - the piece was taken / the piece was lost); two or more signs stand (value 2: reset by nevermind/x/cancel; "+++Q" = "++Q", "----Q" = "--Q").  "++" to "++++" (one to four signs): trading is good; "--" to "----": trading is bad.  "Mates Me"/"Mates Him" (see parsePartnerCommand) strengthen a pending request to the max (partMatesMe/partMatesHim).  Consumed by findMove() (guard check / sit) and orderCaptures() (partTake/partTrade) */
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
  if (n > 2) n = 2;   /* one sign = one-time (1), two or more
                       * signs = standing (2) */
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

  /* Piece directives may be one or more tokens ("ptell +Q --").  A
   * directive may be followed by "Mates Me"/"Mates Him" in the same
   * ptell ("ptell -r Mates Me"), so remember whether one was parsed and
   * still check for "Mates" below before exiting. */
  int gotDirective = (parsePartnerDirective(arg1)) || (parsePartnerDirective(arg2)) || (parsePartnerDirective(arg3));

  /* "Mates Me"/"Mates Him" after a piece directive: strengthen it to
   * the max - "Mates Me": protect the piece to the max (move it to
   * safety / refuse to trade it, and sit before it must be given up);
   * "Mates Him": sacrifice material to take it.  It may ride in the same
   * ptell as the directive (arg2="Mates", arg3="me"/"him") or be its own
   * ptell (arg1="Mates", arg2="me"/"him").  "Mates Me" applies to a
   * pending "-x" (partGuard - our piece, the guard-sit can fire); for
   * legacy it also applies to "+x" (partTake, though we never hold an
   * enemy piece so it can't sit). */
  {
  int di;
  const char *mh = NULL;
  if (!strcasecmp(arg1, "mates")) mh = arg2;
  else if (!strcasecmp(arg2, "mates")) mh = arg3;
  if (mh)
  {
  if (!strcasecmp(mh, "me")) { for (di = PAWN; di <= QUEEN; di++) { if (partTake[di]) { partTake[di] = 2; partMatesMe[di] = 1; } if (partGuard[di]) { partGuard[di] = 2; partMatesMe[di] = 1; } } return 1; }
  if (!strcasecmp(mh, "him")) { for (di = PAWN; di <= QUEEN; di++) if (partTake[di]) { partTake[di] = 2; partMatesHim[di] = 1; } return 1; }
  }
  }

  if (gotDirective) return 1;

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
  /* startSearchOver(): the position changed mid-turn, so whatever
   * search result we had (an aborted pass, or a pre-sit best move
   * - main plays a non-bad rightMove as-is, without an isLegal
   * check) is stale.  Re-search from scratch with the dropped
   * piece - the same as the move-command delivery path in
   * interface.cpp.  Harmless while waiting for the opponent
   * (reSearch is reset at the top of findMove). */
  startSearchOver();
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
  /* Retraction of a pending sit request, and a reset of the piece
   * directives to default (no piece is "good"/"bad"/"to take"
   * anymore):  the partner's "cancel"/"nevermind"/"x".  These do NOT
   * get us up from sitting (a "go" does) and leave the sit
   * bookkeeping (partsitting, toldpartisit) alone. */
  else if(!strcasecmp(arg1, "cancel") || !strcasecmp(arg1, "nevermind") || !strcasecmp(arg1, "x")) { parttoldsit = 0; { int di; for (di = 1; di <= 5; di++) partTake[di] = partGuard[di] = partMatesMe[di] = partMatesHim[di] = 0; } partTrade = 0; return 1; }
  else if(!strcasecmp(arg1, "abort")) output("tellics abort\n");
  else if(!strcasecmp(arg1, "hi") || !strcasecmp(arg1, "hello") ||
	  !strcasecmp(arg1, "hiya") || !strcasecmp(arg1, "hi!")) { output("tellics ptell hi!\n");
    return 1; } 
  
  else return -1;
  return 0;
}

