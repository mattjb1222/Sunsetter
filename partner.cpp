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

/* 1 if the partner is another Sunsetter (identified itself, intro
 * ptell "I am Sunsetter", or "Sunsetter" in the partner name):
 * enables bot-vs-bot coordination (partnerSittingLong).  Reset on
 * "new"/"accept", on a partner change and on a new game; NOT on
 * setboard (the intro ptell arrives before it, see board.cpp) */
int partnerIsSunsetter;

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
    output("tellics ptell I understand these commands: sit, go, sitting (frozen), time, flag, abort, cancel, x, nevermind (cancel +X), OK now (cancel -X), help, Mates Me/Him and piece directives (+P, -Q, ++, ----).  If you tell me a move (\"Qf3\", or a drop \"Qh8\") I will play it.\n");
    output("tellics ptell If I am going to get mated I sit for a piece (telling you so) - except when YOU are another Sunsetter sitting on your own, up on time, for a while: then I move instead, my captures feed you, and I sit on my next turn.\n");
  
  } else if(!strcasecmp(str, "sitting") || !strcasecmp(str, "frozen")) {
    output("tellics ptell \"sitting\" (or \"frozen\"): tells me you are sitting.  I note it (while you sit, only my moves can feed you pieces) and I get up from my own sit. \n");
  } else if(!strcasecmp(str, "sit")) {
    output("tellics ptell \"sit\": I stop moving and sit, waiting for your \"go\" or for a piece to drop for me - even if I am behind on time or not even mated.  One \"sit\" is one sit; say it again if you want me to keep sitting.  A pending one is retracted by \"go\", \"x\" or \"cancel\". \n");
  } else if(!strcasecmp(str, "go")) {
    output("tellics ptell \"go\": I get up from sitting, and if I am mated-soon I make exactly one move instead of sitting.  Also retracts a pending \"sit\". \n");
  } else if(!strcasecmp(str, "flag")) {
    output("tellics ptell \"flag\": I flag my opponent on my board.  I usually auto-flag when my clock runs out, but say it if you want to be sure.\n");
  } else if(!strcasecmp(str, "time")) {
    output("tellics ptell \"time <your clock> <your opponents clock>\" (1/100th seconds): tells me your board's clocks so I know when I am up on time.  When my icsdrone relays the server's clocks to me this is redundant but harmless.\n");
  } else if(!strcasecmp(str, "abort")) {
    output("tellics ptell \"abort\": I offer an abort (and accept yours).\n");
  } else if(!strcasecmp(str, "cancel")) {
    output("tellics ptell \"cancel\"/\"x\": retract a pending \"sit\" and reset ALL the piece directives to default (both the \"+X\" and the \"-X\" ones).  \"nevermind\" cancels only the \"+X\" requests; \"OK now\" cancels only the \"-X\" protections (see there).\n");
  } else if(!strcasecmp(str, "directive") || !strcasecmp(str, "directives")) {
    output("tellics ptell \"+P\" to \"++Q\": try to take an enemy P (feed me a P).  \"-Q\" to \"--Q\": protect my Q - I move it to safety / refuse to trade it.  One sign is one-time (I reset it when it's done - the piece was taken/lost); two or more signs stand until reset - \"+X\" by \"nevermind\", \"-X\" by \"OK now\", both by \"cancel\"/\"x\" (\"+++Q\" = \"++Q\", \"----Q\" = \"--Q\").  \"++\" to \"++++\": trading is good; \"--\" to \"----\": trading is bad.\n");
    output("tellics ptell After a \"-Q\": add \"Mates Me\" and if I can't save the Q I sit and tell you it's coming - tell me \"go\" to make moves anyway.  After a \"+X\": \"Mates Him\" = I sacrifice material to take the X.  A pawn guard (\"-P\") with \"Mates Me\" also covers my bishop and queen (\"-P\" -> \"-B\" \"-Q\"); a pawn take (\"+P\") with \"Mates Him\" also covers them (\"+P\" -> \"+B\" \"+Q\").\n");
    output("tellics ptell I send these directives myself, once a turn: \"+X\" = I am losing BECAUSE I am down about an X in material (losing by more than I am down is positional - I don't ask then) - feed me an X (one-time: it resets itself once you have taken the X; a rook also pulls in a queen - a queen is a rook and more); \"-X\" = protect an X of mine that is bad for you to take (an enemy piece can net it), or withhold an X (it would hang on my board) - a rook also implies a queen.  I send them bare - without \"Mates Me/Him\": a material request is not a mate claim (send ME \"Mates Me/Him\" to strengthen a directive, see there).  When I cancel a \"+X\" request (no longer needed) I say \"nevermind\"; when a \"-X\" threat is resolved (the piece is safe again) I say \"OK now\".  When I get your \"+X\" I ack \"OK\" and say I will take/drop it (\"++X\" -> \"OK, I will trade more\"); for \"-X\" I ack I will protect it (\"--X\" -> \"OK, I will avoid trading\").\n");
  } else if(!strcasecmp(str, "nevermind") || !strcasecmp(str, "x")) {
    output("tellics ptell \"nevermind\": cancel the \"+X\" requests (\"feed me a piece\" - I stop trying to take them).  \"OK now\": cancel the \"-X\" protections (\"protect my piece\" - the piece is safe again, I stop guarding it).  \"cancel\"/\"x\": the full reset - both, plus retract a pending \"sit\" before I sat (I keep moving) and reset the trading directive.  None of them gets me up (use \"go\" for that).\n");
  } else if(!strcasecmp(str, "mates")) {
    output("tellics ptell After a \"-X\":  \"Mates Me\" - I protect the X to the max: I move it to safety / refuse to trade it, and if I can't save it I sit before it must be given up (tell me \"go\" to move anyway).\n");
    output("tellics ptell After a \"+X\":  \"Mates Him\" - I sacrifice material to take the X.\n");
    output("tellics ptell A pawn guard (\"-P\") with \"Mates Me\" also covers my bishop and queen (\"-P\" -> \"-B\" \"-Q\"); a pawn take (\"+P\") with \"Mates Him\" also covers them (\"+P\" -> \"+B\" \"+Q\").\n");
  } else if(!strcasecmp(str, "help")) {
    output("tellics ptell \"help\": the command list; \"help <command>\": details on that command, like this.\n");
  } else if(str[0] == '\0') {
    output("tellics ptell I understand the following commands: sit, go, sitting (frozen), time, flag, abort, cancel, x, nevermind (cancel +X), OK now (cancel -X), help, Mates Me/Him and piece directives (+P, -Q, ++, ----).  If you tell me a move (\"Qf3\", or a drop \"Qh8\") I will play it.  \"help <command>\" gives details.\n");

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
 *           overtaken).  Because relying on a drop bleeds our clock
 *           (the server stops it only for a checkmated player), "ahead"
 *           means a real lead (>= 3s and >= double their time), not
 *           just "not behind".  This gates the ghost-piece search: a
 *           win that only exists with a dropped piece is only worth
 *           searching while we're up on time (see findMove).  It does
 *           NOT gate sitting: findMove() sits whenever the mate is
 *           proven (clocks irrelevant) and makes one move on a
 *           partner "go".
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
 * Function: partnerSittingLong
 * Input:    None
 * Output:   1 if the partner is another Sunsetter, has been sitting
 *          on his own for a while (psittinglong >= 5, the same
 *          threshold canSitByClock uses) and is up on time
 *          (partnerTime > partOppTime, clocks known)
 * Purpose:  Bot-vs-bot coordination.  Two Sunsetters sitting at the
 *          same time is a double stall: neither board moves, neither
 *          board gets fed, the game can sit out to a time result.  If
 *          our board is going to get mated (n moves) we sit - but
 *          when our partner is ALREADY sitting, on his own, for an
 *          extended period and is up on time, he is banking time /
 *          running the opponent down, and only our captures can feed
 *          him: instead of piling onto his sit we make a move (our
 *          captures feed him) and sit on the next turn (findMove).
 *          Only for a bot partner: a *human* partner sitting while up
 *          on time is trying to run out the clock - we keep the old
 *          behavior then (see canSitByClock).  The clock comparison
 *          is the partner's own board (his "ptell time" / the
 *          server's "parntime"), not ours.  psittinglong counts our
 *          turns since the partner started sitting (findMove keeps it
 *          growing while he stays seated, resets when he gets up) */

int partnerSittingLong()
   {
   if ((!partnerIsSunsetter) || (!partsitting) || (!partnerClockKnown))
      return 0;
   if ((psittinglong < 5) || (partnerTime <= partOppTime))
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
 *           own, before the search.  A request only when we are
 *           actually LOSING (the previous findMove() score,
 *           lastSearchScore, is negative - a raw material-count
 *           deficit alone is no reason to demand a piece; it may be
 *           a won position or a deliberate trade) AND the material
 *           deficit (board + both holdings, see
 *           materialBalanceForDeepBug()) is at least as large as the
 *           loss (balance <= lastSearchScore): in bughouse a winning
 *           position matters more than material count - if we lose by
 *           more than we are down in material, the loss is positional
 *           (or we are getting mated) and begging to be fed won't fix
 *           it.  When it does explain the loss, we request the largest
 *           piece whose value does not exceed the *loss* (not the raw
 *           deficit - we ask to close the gap we are actually losing
 *           by, not to be equal on material: "+q"/"+r"/"+n").  We
 *           don't
 *           request a piece we can drop from our own holding, and we
 *           don't request one that would just hang (no safe square).
 *           The requests are sent bare ("+X"/"-X") - one sign, a
 *           one-time request (it resets itself once the piece is
 *           taken/lost) - and without a "Mates Him/Me" attach: that
 *           wording claims mate, and a material request is not a mate
 *           claim.  (Receiving "Mates Me/Him" from the partner still
 *           works: see parsePartnerCommand.)  We also protect our own
 *           board pieces that are bad for the opponent to take: for
 *           every piece type X we have on the board where the
 *           opponent can capture it for a clear net (partnerBadPieceGain
 *           >= pValue[PAWN] - captureGain >= a pawn's worth: the
 *           opponent nets at least a pawn's worth of the X into their
 *           hand to drop back on our board; a smaller net is not worth
 *           asking to protect, and it also guards against captureGain's
 *           imprecise min-max returning small positives for defended
 *           pieces), we send "-X"
 *           (protect my X - the receiver's partGuard moves it to safety
 *           / refuses the trade).  And if we can't place anything
 *           safely (every empty square attacked), we ask to withhold the
 *           piece we were about to request (or the queen, if none was
 *           requested - an X there would be lost; if the king has no
 *           safe square this is the "we're getting mated" case).  A
 *           checkmated position retracts everything (nothing would help).
 *           The active set (the "+" request plus the "-" protects) lives
 *           in the static (lastSig); a changed or vanished set retracts the
 *           vanished part first (a canceled "+X" with "nevermind", a resolved
 *           "-X" with "OK now" - the receiver understands both) and re-sends
 *           the new set ("x"/"cancel" = full reset also clears them).
 */
void tellPartnerDirectives()
{
  static int lastSig = 0;	/* signature of the active directive set:
					   * takeSet*32 + guard mask. Both are piece
					   * bitmasks (bit (p-PAWN) set if "+p"/"-p" is
					   * active; a rook also sets the queen - a queen
					   * contains a rook). 0 = nothing sent */
  move aim[MAX_MOVES];
  int balance, requested, takeSet, hold, safe, p, guard, sig;

  if ((currentRules != BUGHOUSE) || (!gameInProgress) || (analyzeMode)) return;

  if (gameBoard.moves(aim) == 0)
  {
	 /* checkmated (findMove will sit "I am mated" for it) - nothing
	  * we ask for will help */
	 if (lastSig)
	 {
	 int prevTakeSet = lastSig / 32;
	 int prevGuard = lastSig % 32;
	 if (prevTakeSet) output("tellics ptell nevermind\n");
	 if (prevGuard) output("tellics ptell OK now\n");
	 lastSig = 0;
	 }
	 return;
	}

  balance = gameBoard.materialBalanceForDeepBug();

  /* A request only when actually losing (lastSearchScore < 0 - the
   * previous findMove()) AND the material deficit can explain the loss
   * (balance <= lastSearchScore: both negative - the deficit is at
   * least as big as the loss; if we lose by more than we are down in
   * material, the loss is positional - a piece feed won't fix it).
   * The ask is sized to close the *loss* (-lastSearchScore; the gate
   * guarantees that is <= -balance) - not to be equal on material:
   * in bughouse a winning position matters more than material count */
  requested = 0;
  if ((lastSearchScore < 0) && (balance <= lastSearchScore))
  {
  if (-lastSearchScore >= pValue[QUEEN]) requested = QUEEN;
  else if (-lastSearchScore >= pValue[ROOK]) requested = ROOK;
  else if (-lastSearchScore >= pValue[KNIGHT]) requested = KNIGHT;
  }

  /* The request set (takeSet): the requested piece, plus the queen if a
   * rook was requested - a queen moves like a rook and more, so it
   * satisfies a rook need and then some (if the partner can't grab a
   * rook, a queen is an even better gift). */
  takeSet = 0;
  if (requested != 0)
  {
   takeSet |= (1 << (requested - PAWN));
   if (requested == ROOK) takeSet |= (1 << (QUEEN - PAWN));
  }

  /* Safe/unsafe is piece-independent (a square safe for a rook is safe for a
   * queen and vice versa - hasSafeDropSquare ignores the piece type), so
   * evaluate it once. */
  safe = gameBoard.hasSafeDropSquare(requested);

  /* If we already hold any piece we'd ask for, we can drop it ourselves */
  if (takeSet != 0)
     for (p = PAWN; p <= QUEEN; p++)
        if ((takeSet & (1 << (p - PAWN))) && (gameBoard.getPieceInHand(gameBoard.getDeepBugColor(), p) >= 1))
        { takeSet = 0; break; }
  if ((takeSet != 0) && (!safe)) takeSet = 0;   /* it would just hang */

  /* Withhold, if we can't place anything safely: withhold the piece we were
   * about to request (directly responsive - "I wanted X but it would hang")
   * if one was requested, else the most valuable piece (the queen, the worst
   * waste). */
  hold = 0;
  if ((takeSet == 0) && (!safe))
	 hold = (requested != 0) ? requested : QUEEN;

  /* The "-X" (protect my X) set, two sources:
   * (a) pieces bad for the opponent to take - one of our X on the board that
   *     an enemy piece can capture for a significant net (partnerBadPieceGain
   *     >= pValue[KNIGHT]: captureGain >= a knight's worth - the opponent
   *     nets at least a knight's worth of the X into their hand to drop back
   *     on our board; a mere pawn loss is not significant, and the bar is
   *     well above captureGain's imprecise min-max small positives for
   *     defended pieces).  Ask to protect it (the receiver's partGuard: move
   *     it to safety / refuse the trade).
   * (b) the withhold piece (hold, above).
   * A rook in the set also pulls in the queen - a queen moves like a rook
   * and more, so whatever a rook fears a queen fears too (and is worth more,
   * so more worth protecting). */
  guard = 0;
  for (p = PAWN; p <= QUEEN; p++)
     if (gameBoard.partnerBadPieceGain(p) >= pValue[KNIGHT]) guard |= (1 << (p - PAWN));
  if (hold) guard |= (1 << (hold - PAWN));
  if (guard & (1 << (ROOK - PAWN))) guard |= (1 << (QUEEN - PAWN));

  /* signature: takeSet (request bitmask) * 32 + guard mask */
  sig = takeSet * 32 + guard;

  if (sig == lastSig) return;	/* state unchanged, no repeat */

  /* Retract only what is going away (delta): a canceled "+X" request is
   * "nevermind" (I no longer want that); a "-X" that is no longer a
   * threat is "OK now" (the piece is safe again - not a retraction). */
  if (lastSig)
  {
  int prevTakeSet = lastSig / 32;
  int prevGuard = lastSig % 32;

  if (prevTakeSet & ~takeSet) output("tellics ptell nevermind\n");
  if (prevGuard & ~guard) output("tellics ptell OK now\n");
  }

  {
	 char pieceLetter[PIECES] = { 0, 'p', 'r', 'n', 'b', 'q' };
	 char buf[MAX_STRING];

	 for (p = PAWN; p <= QUEEN; p++)
	   if (takeSet & (1 << (p - PAWN)))
	   {
	   sprintf(buf, "tellics ptell +%c\n", pieceLetter[p]);
	   output(buf);   /* sent bare - no "Mates Him/Me" attach: a
	                   * material request is not a mate claim */
	   }
	 for (p = PAWN; p <= QUEEN; p++)
	   if (guard & (1 << (p - PAWN)))
	   {
	   sprintf(buf, "tellics ptell -%c\n", pieceLetter[p]);
	   output(buf);   /* sent bare: "protect my X" (partGuard) */
	   }
	}
  lastSig = sig;
  }


/* Function: parsePartnerDirective
 * Input:    A token from a partner ptell
 * Output:   1 if it's a piece directive
 * Purpose:  "+P".."++Q": try to take the enemy piece type, "-P"..: protect my piece type.  One sign is a one-time request (value 1: auto-reset when fulfilled - the piece was taken / the piece was lost); two or more signs stand (value 2: reset by "nevermind" for "+X", "OK now" for "-X", or "x"/"cancel" for both; "+++Q" = "++Q", "----Q" = "--Q").  "++" to "++++" (one to four signs): trading is good; "--" to "----": trading is bad.  "Mates Me"/"Mates Him" (see parsePartnerCommand) strengthen a pending request to the max (partMatesMe/partMatesHim).  Consumed by findMove() (guard check / sit) and orderCaptures() (partTake/partTrade) */
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

  /* Acknowledge: "OK" + what I will do for the directive (bot-vs-bot
   * coordination - the partner knows I got it and how I'll act). */
  {
  char buf[MAX_STRING];
  static const char *pl[PIECES] = { 0, "pawn", "rook", "knight", "bishop", "queen" };

  output("tellics ptell OK\n");
  if (tok[0] == '+')
  {
     if (n == 2)
        sprintf(buf, "tellics ptell I will trade more\n");
     else
        sprintf(buf, "tellics ptell I will try to take an enemy %s and drop it for you\n", pl[p]);
  }
  else
  {
     if (n == 2)
        sprintf(buf, "tellics ptell I will avoid trading\n");
     else
        sprintf(buf, "tellics ptell I will protect my %s (move it to safety / refuse to trade it)\n", pl[p]);
  }
  output(buf);
  }
  return 1;
}

int parsePartnerCommand(const char *arg1, const char *arg2, const char *arg3)
{

  move toldMove;
  char buf[MAX_STRING];

  /* Bot-vs-bot recognition (partnerIsSunsetter) is done in the ptell
   * handler in interface.cpp on the WHOLE line - the intro ptell
   * "...I am Sunsetter..." puts the name past the 5-token window
   * this function sees.  A command ptell never contains "Sunsetter",
   * so only a real self-identification (or a partner that says the
   * word in prose - harmless, it only changes what we do when *it*
   * is sitting, up on time, for a while) sets it. */

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
  if (!strcasecmp(mh, "me")) { for (di = PAWN; di <= QUEEN; di++) { if (partTake[di]) { partTake[di] = 2; partMatesMe[di] = 1; } if (partGuard[di]) { partGuard[di] = 2; partMatesMe[di] = 1; } }
   /* "Mates Me" is about protection: a pawn GUARD directive (with the mate
    * attach) also covers bishop and queen - "-P" -> "-B" "-Q" (partMatesMe).
    * (The take propagation "+P" -> "+B" "+Q" belongs to "Mates Him" - the
    * sacrifice-to-take side, below.) */
   if (partGuard[PAWN]) { partGuard[BISHOP] = partGuard[QUEEN] = 2; partMatesMe[BISHOP] = partMatesMe[QUEEN] = 1; }
   return 1; }
  if (!strcasecmp(mh, "him")) { for (di = PAWN; di <= QUEEN; di++) if (partTake[di]) { partTake[di] = 2; partMatesHim[di] = 1; }
   /* A pawn directive (with the mate attach) also covers bishop and queen:
    * "+P" -> "+B" "+Q" ("Mates Him" = sacrifice material to take) */
   if (partTake[PAWN]) { partTake[BISHOP] = partTake[QUEEN] = 2; partMatesHim[BISHOP] = partMatesHim[QUEEN] = 1; }
   return 1; }
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

  /* A drop in bughouse, three-way (a relayed partner drop can never
   * be a piece we hold - it comes from the partner's hand, so the
   * hand check can't swallow the event):
   *  - we hold the piece: a told move ("If you tell me a move ... or a
   *    drop "Qh8" I will play it") - play it (doMove(), even pending
   *    until our turn - the piece stays in our hand until then).
   *  - we don't hold it and the partner is sitting/frozen: the
   *    wrapper-relayed sit-for-a-piece event - the piece simply
   *    appears (it was never in our hand) and the turn comes to us;
   *    without this we'd never get up from sitting => our clock runs
   *    to zero => "resign".  A relayed drop can only arrive while the
   *    partner is sitting (a sitting partner is frozen - only a relay,
   *    not a typed ptell, can then reach us).
   *  - we don't hold it and the partner isn't sitting: an impossible
   *    instruction (the partner typed a drop we can't play) - decline
   *    it; "applying" it would put a phantom piece on our board that
   *    icsdrone doesn't have, and our later moves would be rejected. */
  if(!toldMove.isBad() && (currentRules == BUGHOUSE) && (toldMove.from() == IN_HAND)
     && (gameBoard.pieceOnSquare(toldMove.to()) == NONE))
  {
  if (gameBoard.getPieceInHand(gameBoard.getDeepBugColor(), toldMove.moved()) > 0)
     doMove(toldMove);          /* told drop - play it */
  else if (partsitting)
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
  else
  {
  sprintf(buf, "tellics ptell sorry, I can't play %s - I don't have that piece\n", arg1);
  output(buf);
  return 1;
  }
  }
  else if(!toldMove.isBad()) doMove(toldMove);  
  else if(!strcasecmp(arg1, "sitting") || !strcasecmp(arg1, "frozen")) { partsitting = 1;  unsit();}
  /* A partner request to sit is obeyed: parttoldsit makes
   * findMove() sit - even if we aren't mated or aren't up on time
   * (even when we are not mated at all).  A "go"
   * (toldPartGoThisTurn) takes precedence and makes one move, and a
   * "go"/"cancel"/"x" drops the request; the request is
   * one-shot (consumed when we sit), so a partner that
   * wants a long sit simply says "sit" again.  We say nothing here
   * (no ptell flood); findMove() says "sitting" when it actually
   * sits. */
  else if(!strcasecmp(arg1, "sit")) { parttoldsit = 1; stopThought(); } 
  else if(!strcasecmp(arg1, "go")) { parttoldgo = 1; parttoldsit = 0; unsit(); stopThought(); } 
  else if(!strcasecmp(arg1, "flag")) output("tellics flag\n");
  /* "cancel"/"x": retract a pending sit request and reset ALL the piece
   * directives to default (no piece is "good"/"bad"/"to take" anymore).
   * These do NOT get us up from sitting (a "go" does) and leave the sit
   * bookkeeping (partsitting, toldpartisit) alone.  "nevermind" cancels
   * just the "+X" requests (partTake); "OK now" cancels just the "-X"
   * protections (partGuard) - neither retracts the sit or the trading
   * directive.  A mate flag is cleared only when the opposite side
   * (take vs guard) for that piece is also gone - otherwise it still
   * strengthens the surviving directive. */
  else if(!strcasecmp(arg1, "nevermind")) { { int di; for (di = 1; di <= 5; di++) if (partTake[di]) { partTake[di] = 0; if (!partGuard[di]) partMatesMe[di] = partMatesHim[di] = 0; } } return 1; }
  else if(!strcasecmp(arg1, "ok") && !strcasecmp(arg2, "now")) { { int di; for (di = 1; di <= 5; di++) if (partGuard[di]) { partGuard[di] = 0; if (!partTake[di]) partMatesMe[di] = partMatesHim[di] = 0; } } return 1; }
  else if(!strcasecmp(arg1, "cancel") || !strcasecmp(arg1, "x")) { parttoldsit = 0; { int di; for (di = 1; di <= 5; di++) partTake[di] = partGuard[di] = partMatesMe[di] = partMatesHim[di] = 0; } partTrade = 0; return 1; }
  else if(!strcasecmp(arg1, "abort")) output("tellics abort\n");
  else if(!strcasecmp(arg1, "hi") || !strcasecmp(arg1, "hello") ||
	  !strcasecmp(arg1, "hiya") || !strcasecmp(arg1, "hi!")) { output("tellics ptell hi!\n");
    return 1; } 
  
  else return -1;
  return 0;
}

