/* ***************************************************************************
 *                                Sunsetter                                  *
 *				 (c) Ben Dean-Kawamura, Georg v. Zimmermann                  *
 *   For license terms, see the file COPYING that came with this program.    *
 *                                                                           *
 *  Name: search.cc                                                          *
 *  Purpose: Has the functions to search the game tree                       *
 *                                                                           *
 *  Comments: search.cc has three main functions, findMove(), searchRoot ()  *
 * and search().  findMove() is called by the game loop to get the           *
 * (hopefully) best move in the position.  It sets up the search and calls   *
 * searchRoot() which for every move in the position calls search().         *
 * search() recursively searches and returns the value relating to how good  *
 * it thinks Sunsetter is doing in that position.  searchRoot() chooses the  *
 * move that causes search() to be the highest.                              *
 *                                                                           *
 * search() uses an Negamax search with alpha-beta pruning, NullMove Pruning *
 * plus other search extensions to find its evaluation of the position.   *
 *																			 *			
 * Because the Branching Factor in Crazyhouse and Bughouse is so big, and    *
 * deep mates so common we use a 4-steps PMG ( Plausible Move Generator )    *
 * compared to most chess programs which only do 2 steps, normal search and  *
 * quiesce search.															 *
 *																			 *
 *************************************************************************** */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "brain.h"
#include "bughouse.h"
#include "notation.h"
#include "interface.h"


PrincipalVariation pv;					/* The principal variation, there needs
                                         to be a separate one for each ply that
                                         is searched */

move searchMoves[DEPTH_LIMIT][MAX_MOVES]; /* Where to store the moves.  They
                                             used to be in a local array, but
                                             that blew up the stack */
boardStruct AIBoard;                 /* The board that the AI uses */
volatile int stopThinking;           /* If the search should be stopped */
volatile int reSearch;               /* If the search should be restarted */
volatile int forceMove;              /* Make a move, even if you get mated*/
double millisecondsPerMove;             /* How many millisecs to take on a move */
int    reSearchTimeBoost;              /* 1 if the next searchRoot (the
                                        * skip-ghosts re-search) gets 4x the
                                        * normal time budget */
int partGuardOrder;                 /* 1 if findMove wants searchRoot to order
                                     * the root moves by the partGuard penalty
                                     * (rootGuardPend, filled by guardRootCheck) */
static int rootGuardPend[MAX_MOVES];   /* Under-attacked flag per root move,
                                        * filled by guardRootCheck() */
int lastSearchScore;                     /* Score of the last findMove() search,
                                         from our perspective */
int stats_positionsSearched;               /* # of search() done */
int stats_quiescensePositionsSearched;     /* # of quieses() done */
int stats_transpositionHits;               /* # of success for transposition lookups*/
int stats_hashFillingUp; 
int stats_hashSize;


const int FractionalDeep[MAX_SEARCH_DEPTH + 1] = { 0, 0, ONE_PLY, ONE_PLY * 2, ONE_PLY * 3, ONE_PLY * 4, ONE_PLY * 5, ONE_PLY * 6, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 999 };
		/* The implementation allows to experiment with fractional deepening, for example smaller steps at higher depths*/



int initialTime;
int currentDepth; 
int movesSearched;




#ifdef DEBUG_STATS

int stats_forceext, stats_checkext, stats_capext;  
int stats_NullTries[DEPTH_LIMIT], stats_NullCuts[DEPTH_LIMIT]; 
int stats_RazorTries, stats_Razors; 
int stats_MakeUnmake[MOVEGEN_TYPES]; 

#endif

/* Time */
clock_t startClockply, endClockply, startClockAnalyze; 
clock_t startClockTime, endClockTime; 


#ifdef GAMETREE

	char filename[MAX_STRING][DEPTH_LIMIT]; FILE *fi[DEPTH_LIMIT];

#endif


move overideMove;						/* If legal, this move is played instead
                                        of what the search found */

int pondering;							/* If we're pondering */
int tryToPonder;						/* If we should be pondering */


/* Function: PrincipalVariation::save
 * Input:    A move and the old PV
 * Output:   None.
 * Purpose:  Used to store the PV.
 */


__forceinline void savePrincipalVar(move m, int ply) 
{
	
assert ( pv.depth[ply] <= DEPTH_LIMIT );


    pv.moves[ply-1][0] = m;
    for(int i = 0; i < pv.depth[ply]; i++) pv.moves[ply-1][i + 1] = pv.moves[ply][i];
    pv.depth[ply-1] = pv.depth[ply] + 1;

}




/* Function: PrincipalVariation::print
* Input:    The board that the principal variation is from, the depth searched and the value.
* Output:   None.
* Purpose:  Used to print out information about the principal variation.
*/

static void printPrincipalVar(int valueReached)
{
	int n, timeUsed;
	int variationLength = 0;
	char buf[MAX_STRING], emoticon[MAX_STRING];
	char pvtxt[MAX_STRING];
	

	timeUsed = (getSysMilliSecs() - startClockTime) / 10; // time in centiseconds 
	if ((timeUsed < 2) && !analyzeMode) return;
	
	// translate mate values to xboard standard
	
	if (valueReached > MATE)
	{
		valueReached -= MATE_IN_ONE;
		valueReached *=2;
		valueReached += 32768;
	}

	if (valueReached < -MATE)
	{
		valueReached += MATE_IN_ONE;
		valueReached *=2;
		valueReached -= 32769;
	}
	

	if ((gameBoard.getColorOnMove() == BLACK) && analyzeMode)
		valueReached = -valueReached; // kinda a kludge, but it works

	strcpy(pvtxt, "");

	for (n = 0; n < pv.depth[0]; n++)
	{
		assert(!AIBoard.badMove(pv.moves[0][n]));

		AIBoard.changeBoard(pv.moves[0][n]); 
		variationLength++;

		DBMoveToRawAlgebraicMove(pv.moves[0][n], buf);
		strcat(pvtxt, buf); strcat(pvtxt, " ");

		if (AIBoard.isNotRepDrawSearch()) // print the valuation only up to the first repetition
		{
			break;
		}
	}

	if (!xboardMode) 
	{
		sprintf(buf, "%3d  %6d  %5d %8d ", currentDepth, valueReached,
			timeUsed, stats_positionsSearched);

		for (n = variationLength; n < 9; n++)
		{
			strcat(pvtxt, "     ");
		}

		output(buf); output(pvtxt); output("\n");

		if ((analyzeMode) && (timeUsed <20)) output("\r");
		else output("\n");
	}
	else 
	{ // xboard mode, and not done searching yet
		if ((timeUsed>15) || (currentDepth > 6))
		{ // only output after first 0.15 seconds OR at least depth 6
			sprintf(buf, "%d %d %d %d ",
				currentDepth, valueReached,
				timeUsed, stats_positionsSearched);
			output(buf); output(pvtxt); output("\n");
		}
	}

	for (n = variationLength - 1; n >= 0; n--) 
	{
		AIBoard.unchangeBoard();
	}

	startClockply = getSysMilliSecs();
	startClockAnalyze = startClockply;
}

/* Function: analyzeUpdate
* Input:    None.
* Output:   None.
* Purpose:  If 4 secs or more are passed since we displayed any new
*			 info in analyze mode, do so now.
*/

void analyzeUpdate()

{
	char buf[MAX_STRING], buf2[MAX_STRING];

	if (xboardMode)
	{
		if (!searchMoves[0][movesSearched].isBad())
		{
			DBMoveToRawAlgebraicMove(searchMoves[0][movesSearched], buf2);
			// the 100 is in place of "total moves" because we don't have that here
			sprintf(buf, "stat01: %ld %d %d %d 100 %s \n",
				(getSysMilliSecs() - startClockTime) / 10,
				stats_positionsSearched,
				currentDepth,
				movesSearched, buf2);
			output(buf);
		}
	}
	else
		if (((getSysMilliSecs() - startClockAnalyze) > 4000) && ((startClockAnalyze - startClockTime) > 200))
		{
			if (!searchMoves[0][movesSearched].isBad())
			{
				DBMoveToRawAlgebraicMove(searchMoves[0][movesSearched], buf2);
				sprintf(buf,
					"             %5ld %8d  searching: %s ..   ( HT: %2d percent )\r",
					((getSysMilliSecs() - startClockply) / 10), stats_positionsSearched,
					buf2, (stats_hashFillingUp * 100 / stats_hashSize));
				output(buf);
			}
			startClockAnalyze = getSysMilliSecs();
		}
	return;
}


/* Function: calcTimeToSpend
 * Input:    What depth we're on currently
 * Output:   None,
 * Purpose:  Used by searchRoot to set millisecondsPerMove to the correct amount
 *           of time.
 */

void calcTimeToSpend()

{

	int mytime, opptime;

	mytime = AIBoard.getTime(AIBoard.getDeepBugColor());
	opptime = AIBoard.getTime(otherColor(AIBoard.getDeepBugColor()));

	if ((FIXED_DEPTH) || (analyzeMode))

	{
		millisecondsPerMove = 100000000; 
		return; 
	}

	if (currentRules == BUGHOUSE) 

	{
		if (opptime < mytime) mytime = opptime; // if opp is low on time, play fast too 
												// he and my part may have sat 
												// so maybe my part is low too 
		/* The clock ticks every 2ms (1000 ticks = 2s of thinking).
		 * Thinking a fixed fraction (1/18) of the *remaining* clock on
		 * every move can never flag on a +0 clock (the clock decays
		 * geometrically), and gives ~2s on a full 3-minute clock
		 * (mytime/180 was ~200ms, which stopped the search at depth 1). */
		millisecondsPerMove = mytime / 18;
		if (millisecondsPerMove > 1500) millisecondsPerMove = 1500;
		if (millisecondsPerMove < 300) millisecondsPerMove = 300;
		if (mytime <= 900) { millisecondsPerMove = mytime / 6; }
		/* "I dead" (partnerDead): the partner can't feed us any more,
		 * so push the win quickly - cap the thinking budget to play
		 * fast (a win that needs a drop can never materialize, so we
		 * search the real position, fast) */
		if (partnerDead && (millisecondsPerMove > 300)) millisecondsPerMove = 300;
	}

	else 
	{											   
		millisecondsPerMove = ( mytime / 25 ); 
		if (mytime <= 20000) { millisecondsPerMove = mytime / 40; }
		if (mytime <= 4000) { millisecondsPerMove = 100; }
		if (mytime <= 800) { millisecondsPerMove = 20; }
	
	}

	if (reSearchTimeBoost)

	{
		millisecondsPerMove *= 4;
		reSearchTimeBoost = 0;
	}

}


/* Function: searchFirstMove
 * Input:    What the first move is, how deep to search, and a guess at what
 *           the value for the position is.
 * Output:   That value of the position after the first move.
 * Purpose:  Used by searchRoot to find the value of the first move.  This
 *           is done differently then all of the other moves because it has
 *           to find and exact value.  searchFirstMove() sets alpha to be
 *           the guess and beta to be the guess + 1.  This will either fail
 *           high or low, then searchFirstMove() sees if the value of the
 *           position is a little away from the guess.  If the search fails
 *           high or low again then searchFirstMove() gets the exact value,
 *           no matter how high/low it is.
 */

int searchFirstMove(move m, int depth, int guess)
{
  int learnValue; 
  int alpha, beta, value;

  alpha = guess ;
  beta = guess +1;

assert (depth < MAX_SEARCH_DEPTH * ONE_PLY);
assert (guess >= -INFINITY);
assert (guess <= INFINITY); 
assert (!AIBoard.badMove(m));

  AIBoard.changeBoard(m);

  learnValue = AIBoard.checkLearnTable(); 
  
  // the idea is to play faster or slower in the opening depending on
  // whether we did well or bad in the past  in this position
  
  millisecondsPerMove -= learnValue *4; 

#ifdef DEBUG_LEARN
  if ((learnValue) && (currentDepth == 4))
  {
	  char buf[MAX_STRING], buf2[MAX_STRING]; 
	  DBMoveToRawAlgebraicMove(m, buf);
	  sprintf (buf2,"Learn: %s has push Value of %d\n", buf, learnValue); 
	  output (buf2); 
  }
#endif

  
  value = learnValue -search(-beta+ learnValue, -alpha+ learnValue, depth - ONE_PLY, 1, 0);

   AIBoard.unchangeBoard();

   if(stopThinking) return -INFINITY;

   if(value >= beta) {   

    // The null window search failed high  

    alpha = value;
    beta = INFINITY;

assert (!AIBoard.badMove(m));    
	
	AIBoard.changeBoard(m);

	// not -beta + learnValue, since -beta = -INFINITY and we can't allow that to get smaller !
	value = learnValue -search(-beta, -alpha+learnValue, depth - ONE_PLY, 1, 0);

    AIBoard.unchangeBoard();

    if(stopThinking) return -INFINITY;

  
    else return value;
  } else {

    // The null window search failed low  

    beta = value;
    alpha = -INFINITY; 
    
assert (!AIBoard.badMove(m));	
	AIBoard.changeBoard(m);
	
	// not -alpha + learnValue
	value = learnValue -search(-beta+learnValue, -alpha, depth - ONE_PLY, 1, 0);

    AIBoard.unchangeBoard();

    if(stopThinking) return -INFINITY;
   
    else return value;
  }



}

/* Function: searchMove
 * Input:    What the move is, how deep to search, and alpha
 * Output:   That value of the position after the move.
 * Purpose:  Used by searchRoot to find the value of
 *           a move other then the first.  It first sees if the move
 *           is worse then the best one already found by setting beta
 *           to be alpha + 1.  If this move is better then it sees how
 *           much better it is, geting the exact value, no matter how high
 *           it is.  This is basically the same as searchFirstMove(),
 *           except if it fails low, then it doesn't search anymore,
 *           since it already knows that there is a better move.  
 */

int searchMove(move m, int depth, int alpha)
{

#ifdef GAMETREE
char buf[MAX_STRING];
char buf3[MAX_STRING];
#endif
	
  int learnValue, razor; 
  int beta, value;

  beta = alpha +1; 

assert (alpha >= -INFINITY);
assert (alpha <= INFINITY);
assert (depth <= MAX_SEARCH_DEPTH * ONE_PLY);  
assert (!AIBoard.badMove(m));  
  
  AIBoard.changeBoard(m);

  learnValue = AIBoard.checkLearnTable(); 
  millisecondsPerMove -= learnValue *4; 

#ifdef DEBUG_LEARN
  if ((learnValue) && (currentDepth == 4))
  {
	  char buf[MAX_STRING], buf2[MAX_STRING]; 
	  DBMoveToRawAlgebraicMove(m, buf);
	  sprintf (buf2,"Learn: %s has push Value of %d\n", buf, learnValue); 
	  output (buf2); 
  }
#endif

  
  // We don't razor if



	  if ( (AIBoard.isInCheck(AIBoard.getColorOnMove()))   
			// a) we are checking the opp			
			 ||  (AIBoard.highestAttacked(m.to()))
			// b) we are attacking something with our move thats worth more than or the same as our moved piece, or less defendet. 
			 ||  (AIBoard.escapingAttack(m.from(), m.to())) )
			// c) we are escaping with the piece that got attacked in the move before

		  // what about blocking attack ?
	  {
		  razor = 0; 

#ifdef GAMETREE
	  DBMoveToRawAlgebraicMove(m, buf);
	  sprintf (buf3,"<a href=\"=%s-%d.html\"><b>%s</b></a> <br>\n",buf, currentDepth,buf); 
	  fprintf (fi[0], buf3);
#endif
	  }
	  else
	  {
			  if (depth - ONE_PLY < 6 * ONE_PLY)
				  razor = -4;
			  else if (depth - ONE_PLY < 8 * ONE_PLY)
				  razor = -3;
			  else
				  razor = -2;	  

#ifdef GAMETREE
	  DBMoveToRawAlgebraicMove(m, buf);
	  sprintf (buf3,"<a href=\"=%s-%d.html\">%s</a> <br>\n",buf, currentDepth,buf); 
	  fprintf (fi[0], buf3);
#endif
	  }

 
  value = learnValue -search(-beta+learnValue, -alpha+learnValue, depth - ONE_PLY + razor , 1, 0);

  AIBoard.unchangeBoard();

  if(stopThinking) return -INFINITY;

  if(value < beta) return value;

  // This move is better then the best so far  

  alpha = value;
  beta = INFINITY;

assert (!AIBoard.badMove(m));
  AIBoard.changeBoard(m);

  // not -beta + learnValue, since -beta = -INFINITY and we can't allow that to get smaller !
  value = learnValue -search(-beta, -alpha+learnValue, depth - ONE_PLY + razor, 1, 0);

  AIBoard.unchangeBoard();

  if(stopThinking) return alpha;  


  return value;
}



/* Function: searchRoot
 * Input:    How many ply to search and a pointer to a move to fill with the
 *           found move.
 * Output:   None.
 * Purpose:  Uses an iterative alpha-beta search to assign a value to the
 *           position.  searchRoot() is what is called from the first ply.  It
 *           starts by doing a 1 ply search and then a 2 ply search up until it
 *           gets to the maximum depth.
 */

void searchRoot(int depth, move *rightMove, int *bestValue )
{
    
  int n, done;				/* needed for sorting moves */
  int bestValueEver, count, startDepth, searchedFirstMove;
  int value = -INFINITY; 

  move tmp, bestMoveLastPly;
  int values[MAX_MOVES];
  
  char buf[MAX_STRING];
  

  transpositionEntry *te;

  startClockTime = getSysMilliSecs();
  startClockply = getSysMilliSecs();

  *bestValue = -INFINITY;
  bestValueEver = -INFINITY; 
  values[1] = -INFINITY;

  bestMoveLastPly.makeBad();
  
  searchedFirstMove = 0; 
  startDepth = 1;

  AIBoard.setCheckHistory(0);
  AIBoard.setBestCapture();
 
  count = AIBoard.moves(searchMoves[0]);  
  
				// if there is only 1 legal move play it
  if ((count == 1) && (currentRules == CRAZYHOUSE) &&  (!analyzeMode)) 
  {
    *rightMove = searchMoves[0][0];
    return;
  }
  if (count == 0) 
  {
	movesSearched = 0;
	searchMoves[0][0].makeBad(); 
	*rightMove = searchMoves[0][0];
	/* In Bughouse a position with no legal move is a sit for a piece:
	 * findMove() runs the sit protocol for us (bestValue -INFINITY
	 * is <= -MATE).  We must not block here: waiting here would
	 * freeze us forever instead of sitting. */
	*bestValue = -INFINITY; 
	if (currentRules != BUGHOUSE) 
	{
	/* True checkmate (Crazyhouse): the game is over, wait for the
	 * next command */
	output("  0 -32000       0        0  #-0   \n");
	waitForInput();
	}
	return; 
  }
 
  /* Partner "-X" directive: findMove() ran guardRootCheck() over exactly
   * these moves (same AIBoard state, same generation);  reorder the root
   * moves so the ones that leave a guarded piece under-attacked go last.
   * Stable sort: the engine's own ordering is kept within a penalty tier */
  if (partGuardOrder)
  {
  int pend[MAX_MOVES];
  int i2, j2;

  for (i2 = 0; i2 < count; i2++) pend[i2] = rootGuardPend[i2];
  for (j2 = 1; j2 < count; j2++)
  {
  int cp = pend[j2];
  move cur = searchMoves[0][j2];

  i2 = j2 - 1;
  while ((i2 >= 0) && (pend[i2] > cp))
  {
  searchMoves[0][i2 + 1] = searchMoves[0][i2];
  pend[i2 + 1] = pend[i2];
  i2--;
  }
  searchMoves[0][i2 + 1] = cur;
  pend[i2 + 1] = cp;
  }
  partGuardOrder = 0;
  }
 
  if((te = AIBoard.lookup()) != NULL) { // We've searched this position before
                                        // and can remember some information
                                        // about it 
    stats_transpositionHits++;


    if(te->type == EXACT && !te->hashMove.isBad()) {
   	  *rightMove = te->hashMove;
	  bestMoveLastPly = *rightMove;
      *bestValue = te->value;  // no adjustment for mate values needed, because this is ply 0.
	  value = *bestValue; 
	  pv.depth[0] = 0; pv.depth[1] = 0;
      n = 0;
	  
	  // these 4 lines sort the hash move to the top of moves to search
	  while(searchMoves[0][n] != te->hashMove) n++;
      tmp = searchMoves[0][n];
      searchMoves[0][n] = searchMoves[0][0];
      searchMoves[0][0] = tmp;
      
	  searchedFirstMove = 1;  
      startDepth = (te->depth / ONE_PLY) +1;

assert (n <= count); // this checks that the hash move we 
					 // got was in the moves we generated
      
	  sprintf(buf, "%3d  %6d      0%8d  ", startDepth, value, 0); 
	  output(buf);
	  DBMoveToRawAlgebraicMove(searchMoves[0][0], buf);
	  output(buf);
	  output(" <already searched>\n");		

    } else if(te->type != WORTHLESS) {
      *bestValue = te->value;	  
    }
  } 
  

  calcTimeToSpend();

  // do the searches with increasing ply 

  for(currentDepth = startDepth;
      currentDepth < depth || sitting; currentDepth++) {

  movesSearched = 0; 

#ifdef GAMETREE
  char buf3[MAX_STRING];

  sprintf(filename[0],"Start");
  sprintf(buf3,"treemoves/%s-%d.html",filename[0],currentDepth);
  fi[0] = fopen(buf3,"wt");
  fprintf(fi[0],"<link rel=\"stylesheet\" href=\"../style/style.css\" type=\"text/css\">\n");
  
  
  fprintf(fi[0],"<html><table cellpadding=10><tr><td valign=top>\n"); 
  
  printHtmlBoard(fi[0]); 
  fprintf(fi[0],"</td><td valign=top>\n");
  
#endif

  if ((currentRules == CRAZYHOUSE) && (*bestValue <= -EXTREME_EVAL) && (currentDepth > 5))

  {	  
	  millisecondsPerMove *= 2; 
  }


  if (((gameBoard.timeToMove() && (currentDepth >= 2)) || ((FIXED_DEPTH) && (currentDepth >= FIXED_DEPTH))) && (!sitting)) 
  { 
	  stopThought(); 	  
	  break;
  }

  
  if(!searchedFirstMove) 
  {	
	  
	  value = searchFirstMove(searchMoves[0][0], FractionalDeep[currentDepth], *bestValue);

	  /* Partner "Mates Him": sacrifice any material to take the
	   * requested piece - a hard push on the ROOT selection (the
	   * capture is already ordered first by orderCaptures' bonus
	   * and guaranteed to be searched; without this the eval would
	   * still prefer a quiet move over a costly take) */
	  { piece vc = AIBoard.pieceOnSquare(searchMoves[0][0].to());

	  if ((vc != NONE) && (partMatesHim[vc])) {
	    value += 2000;

	    /* "Mates him" = sacrifice ANY material: floor the requested
	       * capture just above the current best, so a losing capture
	       * (protected piece, recapture of any value) still gets played
	       * -- an actual mate (MATE and up) always beats it. */
	    if (((*bestValue + 1) < MATE) && (value < (*bestValue + 1)))
	       value = (*bestValue + 1);
}}
	

#ifdef GAMETREE
	  DBMoveToRawAlgebraicMove(searchMoves[0][0], buf);
	  sprintf (buf3,"<b><a href=\"=%s-%d.html\">%s</a></b>  Return Value: %d <br>\n",buf, currentDepth,buf,value); 
	  fprintf (fi[0], buf3);
#endif
	
    
  } 
  else 
  {			
	   searchedFirstMove = 0;
  } 
	 
  if(stopThinking) break;

  *bestValue = values[0] = value;
  *rightMove = searchMoves[0][0];

  if (value > bestValueEver)
  {
	  bestMoveLastPly = *rightMove;
	  bestValueEver = value; 
  }
  

  savePrincipalVar(*rightMove, 1);    
  printPrincipalVar(*bestValue);

  /* The following doesnt do what I thought it does, but it gives good results anyway */
  
  if ((currentRules == CRAZYHOUSE) 
	  && ((value +40) < values[1]) 
	  && (currentDepth > 7) 
	  && (millisecondsPerMove * 8 < gameBoard.getTime(AIBoard.getColorOnMove())) )
  {
	  millisecondsPerMove = (millisecondsPerMove /2) * 3;
  }   
  
  while(movesSearched < (count-1)) 
  
  {	
	  movesSearched++;
	  
	  value =  searchMove(searchMoves[0][movesSearched], FractionalDeep[currentDepth], *bestValue );  	  

	  /* Partner "Mates Him": see searchFirstMove above - hard push
	   * on the root selection of a take of the requested piece */
	  { piece vc = AIBoard.pieceOnSquare(searchMoves[0][movesSearched].to());

	  if ((vc != NONE) && (partMatesHim[vc])) {
	    value += 2000;
	    /* "Mates him" = any-material floor, see searchFirstMove above */
	    if (((*bestValue + 1) < MATE) && (value < (*bestValue + 1)))
	       value = (*bestValue + 1);
}}
      values[movesSearched] = value;

	  // we are out of time, and are not failing high  

	  if ((stopThinking) && (values[movesSearched]) == -INFINITY)
		  break;

	  //  We have a new best move, let's save it 
	  
	  if(value > (*bestValue)) 
	  {
			*rightMove = searchMoves[0][movesSearched];
			*bestValue = value ; 
			savePrincipalVar(*rightMove, 1);
			printPrincipalVar(*bestValue);	  
	  }
	  // use this to print a variation for each move tried.
	  // savePrincipalVar(searchMoves[0][movesSearched], 1);
	  // printPrincipalVar(value);	  

	  // we are out of time, but the last move from searchMoves() 
	  // is already failing high. Play that even though we dont know 
	  // just how good it is

	  if (stopThinking)
		  break;
  
  }	

    // Sort the moves based on the new values 

    do {
      done = 1;
      for(n = 0; n < count - 1; n++) {
        if(values[n + 1] > values[n]) {
          tmp = searchMoves[0][n];
          searchMoves[0][n] = searchMoves[0][n + 1];
          searchMoves[0][n + 1] = tmp;
          value = values[n];
          values[n] = values[n + 1];
          values[n + 1] = value;
          done = 0;
        }
      }
    } while(!done);

  /* values[1] is only valid when there is more than one legal move;
   * with a single move it is stale (it once held a -INFINITY from
   * the skip-ghosts re-search, which killed that re-search after
   * one ply and hid mate-in-1 positions).
   *
   * We stop on a *winning* mate (bestValue > MATE: we found the mate we're
   * delivering, no need to search deeper) or when the 2nd-best move is already
   * a fast mate (nothing better is possible).  We deliberately do NOT stop on a
   * *losing* mate (bestValue < -MATE): when we are being checkmated we must keep
   * searching to find the move that DELAYS the mate (a quiet move that buys a
   * tempo is worth far more than stopping at the first proven mate).  Stopping
   * here (the old behaviour) made the engine take an immediate mate over a
   * delaying one (e.g. gxh6 Q@g7# instead of Kh8 ... Nxf7#).  The cost is a
   * slightly longer search in mated positions; the benefit is delaying the mate. */
  if (((*bestValue > MATE) || ((count > 1) && (values[1] < -MATE_IN_ONE + 3))) && (!analyzeMode)) stopThought();

  // if we mate, are mated or have only 1 move to escape a short mate, then move now in zh.

  if (stopThinking) break;


#ifdef GAMETREE
  fprintf(fi[0],"<br><br>Return: end of searchRoot<br></td></tr></table></html>\n");
  fclose(fi[0]); 
#endif

} // end of iterative deepening

if ((*bestValue <= -EXTREME_EVAL) && (! bestMoveLastPly.isBad()) && (!analyzeMode))
 
{	// Entering swindle mode ... ;)
 
		*rightMove = bestMoveLastPly;

}

 

  if(!reSearch && !analyzeMode && !forceMode) {

   printPrincipalVar(*bestValue);   // whisper the last ply searched 
   endClockTime = getSysMilliSecs();

	output("\n");
    output("Found move: ");
    DBMoveToRawAlgebraicMove(*rightMove, buf);
    output(buf);
    sprintf(buf," %+d fply: %d  searches: %d quiesces: %d \n            T-hits: %d T-full: %d (percent)\n", *bestValue, currentDepth - 1, stats_positionsSearched, stats_quiescensePositionsSearched, stats_transpositionHits, (stats_hashFillingUp * 100 / stats_hashSize) );
    output(buf);

#ifdef DEBUG_STATS

    sprintf(buf,"Extensions: C-ext: %d F-ext: %d X-ext: %d (in fractional ply)\n", stats_checkext,stats_forceext,stats_capext);
	output(buf);
	sprintf(buf,"NullCuts  : depth-1: %d d-2: %d d-3: %d d-4: %d d-5: %d d-6: %d (percent)\n", (stats_NullCuts[CC_DEPTH+1] * 100 / (stats_NullTries[CC_DEPTH+1] +1)), (stats_NullCuts[CC_DEPTH+2] * 100 / (stats_NullTries[CC_DEPTH+2] +1)),(stats_NullCuts[CC_DEPTH+3] * 100 / (stats_NullTries[CC_DEPTH+3] +1)),(stats_NullCuts[CC_DEPTH+4] * 100 / (stats_NullTries[CC_DEPTH+4] +1)),(stats_NullCuts[CC_DEPTH+5] * 100 / (stats_NullTries[CC_DEPTH+5] +1)),(stats_NullCuts[CC_DEPTH+6] * 100 / (stats_NullTries[CC_DEPTH+6] +1)));
	output(buf);
	sprintf(buf,"Razor     : Tries: %d Success: %d\n",stats_RazorTries,stats_Razors);
	output(buf);
	sprintf(buf,"Make/Unm  : Hash: %d  All-Captures: %d Winning-Captures: %d \n            MateTries: %d Full: %d \n", (stats_MakeUnmake[HASH_MOVE]), (stats_MakeUnmake[ALL_CAP]), (stats_MakeUnmake[WINNING_CAP]), (stats_MakeUnmake[MATE_TRIES]), (stats_MakeUnmake[ALL_NON_CAP]) );
	output(buf);

#endif

	sprintf(buf,"Time      : Time Alloc: %d Clock Ticks Used (in Thousands): %d\n", (int)millisecondsPerMove, (int)(endClockTime - startClockTime));    
	output(buf); 
	
#ifdef DEBUG_HASH
	
	sprintf(buf,"Debug	  : Hash Collisions: %d\n", debug_allcoll); 
	output(buf); 
	assert (debug_allcoll == 0);

#endif
	
	output("\n\n"); 
 	stats_overallticks += (int) (endClockTime - startClockTime); 	
  }  

  if ((analyzeMode || forceMode) && ((currentDepth >= MAX_SEARCH_DEPTH) || ((FIXED_DEPTH) && (currentDepth >= FIXED_DEPTH))))
  {
    waitForInput();
  }

  return;

}

/* Function: ponder
 * Input:    None.
 * Output:   None.
 * Purpose:  Called when it's the opponents move.  It just searches
 *           until the opponents move.  This doesn't generate a move, but it
 *           puts information in the transposition tables.
 *			 Only used in Crazyhouse. 
 */

void ponder()
{

  move m[MAX_MOVES], tmp;

  int values[MAX_MOVES],  n, count, value, done;
  int extensions = 0;
  char buf[MAX_STRING], buf2[MAX_STRING];

  pondering = 1;

  gameBoard.copy(&AIBoard);
  AIBoard.setCheckHistory(0);
  AIBoard.setBestCapture();

  stopThinking = 0;
  currentDepth = 1;
  stats_hashFillingUp = stats_positionsSearched = 0;
  millisecondsPerMove = 100000000;
  count = AIBoard.moves(m);
  memset(values, 0, sizeof(values));
  startClockAnalyze = getSysMilliSecs();

  while(!stopThinking && currentDepth < MAX_SEARCH_DEPTH) 
  {

	  if (currentDepth > 4 && !xboardMode)
	  {
		  DBMoveToRawAlgebraicMove(m[0], buf2);
		  sprintf(buf, "pondering %s %6d [%2d-%2d]\n", buf2, -values[0],
			  currentDepth, currentDepth + (extensions / ONE_PLY));
		  output(buf);
	  }
	  else if (currentDepth > 4) 
	  {
		  DBMoveToRawAlgebraicMove(m[0], buf);
		  sprintf(buf2, "%d %d %ld %d pondering %s(hashfill %%%5.2f)\n",
			  currentDepth, -values[0],
			  (getSysMilliSecs() - startClockAnalyze) / 10,
			  stats_positionsSearched,
			  buf, stats_hashFillingUp * 50.0 / stats_hashSize);
		  output(buf2);
	  }

	  extensions = 0; 

	  for(n = 0; n < count; n++) 
	  {

		// Old version 
		// This was (actually intentionally :) ) comparing the current move to the one before. If there was a 0.8 pawn gap
		// in valuation, the current move was searched less deep. I guess extensive tests would be needed here (georg)
		// if (n && (currentDepth > 3) && (values[n-1] > values[n]+80)) extensions -= ONE_PLY;
	  
		// Angrims version
		// moves that are 0.8 pawns worse than the best get searched less
		// and if they are 1.5 pawns worse, they get even less
		
		if (n && (currentDepth > 3)) {
			if (values[0] > values[n] + 80)
				extensions = -ONE_PLY;
			else if (values[0] > values[n] + 150)
				extensions = -ONE_PLY * 2;
		}
		

		AIBoard.changeBoard(m[n]);

		values[n] = -search(-INFINITY, +INFINITY, FractionalDeep[currentDepth - 1] + extensions, 1, 1);

		AIBoard.unchangeBoard();

		if(stopThinking) break;

	  }


    
  if(stopThinking) break;

    /* Sort the moves based on the new values */

    do {
      done = 1;
      for(n = 0; n < count - 1; n++) {
        if(values[n + 1] > values[n]) {
          tmp = m[n];
          m[n] = m[n + 1];
          m[n + 1] = tmp;
          value = values[n];
          values[n] = values[n + 1];
          values[n + 1] = value;
          done = 0;
        }
      }
    } while(!done);
    currentDepth++;

  }
  
  pondering = 0;

  output("\n");
  if(currentDepth >= MAX_SEARCH_DEPTH) {
    /* we've gone as far as we can, wait for it to be our move */
    while(gameBoard.getColorOnMove() != gameBoard.getDeepBugColor())
    waitForInput();
  }
  return;
}

/* |-------------| ==> |------------------|
 * |             |     | searchFirstMove  | ==> |--------|      |------------------------|
 * |             |     |------------------|     |        | ==>  | recursiveCheckEvasion  |
 * | searchRoot  |                              | search | ==>  | recursiveFullSearch ...|
 * |             | ==> |------------------|     |        | ==>  |------------------------|
 * |             | ==> | searchMove       | ==> |--------|          ||
 * |-------------| ... |------------------|         ^=================
 */                                                 


/* Function: recursiveCheckEvasion()
 *
 *
 */

inline void recursiveCheckEvasion(int *alpha, int *beta,int *bestValue, move *bestMove,int depthWithExtensions,int ply,move hashMove)

{

	int n, value;
	int realcount;
	int count = 0; 
	move *m; 
	m = searchMoves[ply]; 


	if (!hashMove.isBad())
	{
			m[0] = hashMove; 
			count = 1;
	}
	
	count += AIBoard.checkEvasionCaptures(m+count); 		
	count += AIBoard.checkEvasionOthers(m+count);			

	realcount = count;
	if (!hashMove.isBad()) realcount--;

	// if there is only 1 legal move 
	if (realcount == 1)
	{
		depthWithExtensions += FORCING_EXTENSION; 
		
		#ifdef DEBUG_STATS
		stats_forceext+= FORCING_EXTENSION; 
		#endif
	}
	else if ((realcount > 1) && (realcount < 4))
	{
		depthWithExtensions += 1;
	}
	// testing is unclear about this, keeping it because it seems right.
	else if (realcount > 8)
	{
		depthWithExtensions -= 1;
	}




	for(n = 0; n < count; n++) 
	
	{
		if ((m[n] == hashMove) && (n != 0) && (!hashMove.isBad())) continue; 


		assert (!AIBoard.badMove(m[n]));
		AIBoard.changeBoard(m[n]);
	
		// Recursive Search call 
	
		value = -search(-(*beta), -(*alpha), depthWithExtensions,  ply + 1, 0);

		AIBoard.unchangeBoard();	
	
	
#ifdef GAMETREE
		
		char buf[MAX_STRING], buf2[MAX_STRING], buf3[MAX_STRING];  
		
		if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
		{
		DBMoveToRawAlgebraicMove(m[n], buf);
		strcpy(buf2, filename[ply]); strcat(buf2, buf);
		sprintf (buf3,"<a href=\"%s-%d.html\"><b>%s</b></a>  Return Value: %d<br>\n",buf2,currentDepth, buf,value); 		
		
		fprintf (fi[ply], buf3); 
		}
#endif			
	

		
		if (value > *bestValue) 
		{	
			*bestValue = value;  
			*bestMove = m[n];
			savePrincipalVar(*bestMove,ply + 1);

		}
	
		if (*bestValue > *alpha) *alpha = *bestValue; 
	
		if (*bestValue >= *beta) return; 	
	
	}

}

/* Function: recursiveFullSearch()
 *
 *
 */

inline int recursiveFullSearch(int *alpha, int *beta, int *bestValue, move *bestMove, int depthWithExtensions, int  ply, move hashMove)

{
		int n, value, count; 


#ifdef GAMETREE
		char buf[MAX_STRING], buf2[MAX_STRING], buf3[MAX_STRING];  
		
		if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
		{ 
			fprintf(fi[ply],"<br>Full Search <a href=\"start.html#razor\">(*)</a>:<br><br>\n");
		}
#endif 
		

		move *m; 

		m = searchMoves[ply]; 
		
		count = AIBoard.aiMoves(m); 

		
		for ( n = 0; n < count; n++)
		{
			
			if ((m[n] == hashMove) && (!hashMove.isBad())) continue; 

	assert (!AIBoard.badMove(m[n]));

			AIBoard.changeBoard(m[n]);	

			// We don't razor if

			#ifdef DEBUG_STATS
			stats_RazorTries++;
			stats_MakeUnmake[ALL_NON_CAP]++;
			#endif
		
			if ( (AIBoard.isInCheck(AIBoard.getColorOnMove()))   
				// a) we are checking the opp
				 ||  (AIBoard.highestAttacked(m[n].to())) 
				// b) we are attacking something with our move thats worth more than or the same as our moved piece, or less defended. 
				 ||  (AIBoard.escapingAttack(m[n].from(), m[n].to())) )
				// c) we are escaping with the piece that got attacked in the move before
			

			{			

				// Recursive Search call 
				value = -search(-(*beta), -(*alpha), depthWithExtensions ,  ply + 1, 0);		
				AIBoard.unchangeBoard();

	#ifdef GAMETREE			
		
				if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
				{								
					DBMoveToRawAlgebraicMove(m[n], buf);
					strcpy(buf2, filename[ply]); strcat(buf2, buf);
					sprintf (buf3,"<a href=\"%s-%d.html\"><b>%s</b></a>  Return Value: %d<br>\n",buf2,currentDepth, buf,value); 				
					fprintf (fi[ply], buf3); 
				}
	#endif	


		}

		else	// razor


		{			
			#ifdef DEBUG_STATS
			stats_Razors++;
			#endif


			if (depthWithExtensions < 6 * ONE_PLY )
				value = -search(-(*beta), -(*alpha), depthWithExtensions - 4, ply + 1, 0);
			else if (depthWithExtensions < 8 * ONE_PLY)
				value = -search(-(*beta), -(*alpha), depthWithExtensions - 3, ply + 1, 0);
			else
				value = -search(-(*beta), -(*alpha), depthWithExtensions - 2, ply + 1, 0);

			

			AIBoard.unchangeBoard();

			#ifdef GAMETREE					
			if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
			{								
				DBMoveToRawAlgebraicMove(m[n], buf);
				strcpy(buf2, filename[ply]); strcat(buf2, buf);
				sprintf (buf3,"<a href=\"%s-%d.html\">%s</a> Return Value: %d<br>\n",buf2,currentDepth, buf,value); 				
				fprintf (fi[ply], buf3); 
			}
			#endif	
		}
			
	
			if (value > *bestValue) 
			{		
				*bestValue = value;  
				*bestMove = m[n];							
				savePrincipalVar(*bestMove, ply + 1);
			}						
			
			if (*bestValue > *alpha) *alpha = *bestValue; 
			

			if (*bestValue >= *beta) return 1;			
			
			
		}		

return 0; 
}

//  searching all checks
int recursiveChecks(int *alpha, int *beta, int *bestValue, move *bestMove, int depthWithExtensions, int ply, move hashMove)

{

	int n, value, count, captureGain;
	move *m;
	m = searchMoves[ply];
	
	value = -INFINITY;

	count = AIBoard.aiMoves(m);



	for (n = 0; n < count; n++)

	{
		if ((m[n] == hashMove) && (!hashMove.isBad())) continue;


		assert(!AIBoard.badMove(m[n]));


		AIBoard.changeBoard(m[n]);

		if (AIBoard.isInCheck(AIBoard.getColorOnMove()))
		{
			value = -search(-(*beta), -(*alpha), depthWithExtensions, ply + 1, 0);
		}
		AIBoard.unchangeBoard();

		if (value > *bestValue)
		{
			*bestValue = value;
			*bestMove = m[n];
			savePrincipalVar(*bestMove, ply + 1);

		}

		if (*bestValue > *alpha) *alpha = *bestValue;

		if (*bestValue >= *beta) return 1;

	}
	return 0;
}

/* Function: recursiveSearch()
 *
 *
 */

int recursiveSearch(int *alpha, int *beta,int *bestValue, move *bestMove,int depthWithExtensions,int ply,move hashMove,int  searchType)

{

	int n, value, count; 
	move *m; 
	m = searchMoves[ply]; 


#ifdef GAMETREE
		if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
		{ 
				switch (searchType)
				{
					case WINNING_CAP: 
						fprintf(fi[ply],"<br>Winning Captures:<br><br>\n");
						break;
					case ALL_CAP:
						fprintf(fi[ply],"<br>All Captures:<br><br>\n");
						break;
					case MATE_TRIES:
						fprintf(fi[ply],"<br>Mate Tries:<br><br>\n");
						break;
				}
		}
			
#endif 

	switch (searchType)
	{
	case WINNING_CAP: 
		count = AIBoard.captureMoves(m);		
		count = AIBoard.orderCaptures(m)- m;    
		break;
	case ALL_CAP:
		count = AIBoard.captureMoves(m);		
		AIBoard.orderCaptures(m);
		break;
	case MATE_TRIES:
		count = AIBoard.mateTries(m); 
		break;
	default:
		count = 0; 
		assert (0);
		break; 
	}

	
	for(n = 0; n < count; n++) 
	
	{
		if ((m[n] == hashMove) && (!hashMove.isBad())) continue; 


assert (!AIBoard.badMove(m[n]));
		AIBoard.changeBoard(m[n]);
	
		#ifdef DEBUG_STATS
		stats_MakeUnmake[searchType]++;
		#endif

		// Recursive Search call 
	
		value = -search(-(*beta), -(*alpha), depthWithExtensions,  ply + 1, 0);

		AIBoard.unchangeBoard();	
	
	
#ifdef GAMETREE
		
		char buf[MAX_STRING], buf2[MAX_STRING], buf3[MAX_STRING];  
		
		if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
		{
		DBMoveToRawAlgebraicMove(m[n], buf);
		strcpy(buf2, filename[ply]); strcat(buf2, buf);
		sprintf (buf3,"<a href=\"%s-%d.html\"><b>%s</b></a>  Return Value: %d<br>\n",buf2,currentDepth, buf,value); 		
		
		fprintf (fi[ply], buf3); 
		}
#endif			
	

		
		if (value > *bestValue) 
		{	
			*bestValue = value;  
			*bestMove = m[n];
			savePrincipalVar(*bestMove,ply + 1);

		}
	
		if (*bestValue > *alpha) *alpha = *bestValue; 
	
		if (*bestValue >= *beta) return 1; 	
	
	}
	return 0; 
}

/* Function: recursiveHash()
 *
 * @georg: Better describe what this is doing with the drawing above, also clarify what "isBad" does in Relation to the hash move.
 */

inline int recursiveHash(int *alpha,int *beta, int *bestValue , move *bestMove,int depthWithExtensions,int ply,move  hashMove)

{
	int value; 


	if (!hashMove.isBad())
	
	{

		#ifdef GAMETREE
		if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
		{ 
			fprintf(fi[ply],"<br>Hash Move:<br><br>\n");
		}
		#endif 


assert (!AIBoard.badMove(hashMove));
		AIBoard.changeBoard(hashMove);


		#ifdef DEBUG_STATS
		stats_MakeUnmake[HASH_MOVE]++;
		#endif
	
		// Recursive Search call 
	
		value = -search(-(*beta), -(*alpha), depthWithExtensions,  ply + 1, 0);

		AIBoard.unchangeBoard();	
	
	
#ifdef GAMETREE
		if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
		{
		char buf[MAX_STRING], buf2[MAX_STRING], buf3[MAX_STRING];  

		DBMoveToRawAlgebraicMove(hashMove, buf);
		strcpy(buf2, filename[ply]); strcat(buf2, buf);
		sprintf (buf3,"<a href=\"%s-%d.html\">%s</a>  Return Value: %d<br>\n",buf2,currentDepth, buf,value); 		
		fprintf (fi[ply], buf3); 
		}
#endif			
	

		
		if (value > *bestValue) 
		{	
	
			*bestValue = value;  
			*bestMove = hashMove;
			savePrincipalVar(*bestMove, ply + 1);

		}
	
		if (*bestValue > *alpha) *alpha = *bestValue; 
	
		
	
	}

	if (*bestValue >= *beta) return 1; 

return 0; 
}


/* Function: search
 * Input:    alpha, beta,how far to search+ search extensions are left 
 *			 and how far we've searched currently            
 * Output:   None
 * Purpose:  Uses an recursive alpha-beta search to assign a value to the 
 *           position.
 */
 
int search(int alpha, int beta, int depth, int ply, int wasNullMove)
{
  int orgBeta, orgAlpha;	//  set to alpha and beta since those will be adjusted 
  int extensions = -ONE_PLY; 
  int bestValue = -INFINITY; 
  int HashValue;
  int NullValue;            //  NullValue of the position 
  int Currenteval;          //  Current Static evaluation      



  move bestMove, hashMove;
  
  transpositionEntry *te;

  
assert (alpha <= INFINITY);
assert (beta <= INFINITY);
assert (alpha >= -INFINITY);
assert (beta >= -INFINITY);
	
assert (ply <= DEPTH_LIMIT);
  
  
#ifdef GAMETREE

  
  char buf [MAX_STRING] ; 
  char buf2 [MAX_STRING] ; 
  char buf3 [MAX_STRING] ;
  int n;
  

  if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH -1) )
  {
  tree_positionsSaved++;
  sprintf(filename[ply],"=");
  sprintf(buf2,"Moves:");
  for (n=0; n<ply; n++) 
  {
	  DBMoveToRawAlgebraicMove(AIBoard.getMoveHistory(n), buf); 
	  strcat(filename[ply],buf);
	  strcat(buf2," "); strcat (buf2, buf); 
  }
  sprintf(buf3,"treemoves/%s-%d.html",filename[ply],currentDepth);
  fi[ply] = fopen(buf3,"wt");

  fprintf(fi[ply],"<link rel=\"stylesheet\" href=\"../style/style.css\" type=\"text/css\">\n");
  fprintf(fi[ply],"<html><table cellpadding=10><tr><td valign=top>\n"); 
  
  printHtmlBoard(fi[ply]); 
  fprintf(fi[ply],"</td><td valign=top>\n"); 
  
  fprintf(fi[ply],"%s <br><br><hr><br>", buf2);
  fprintf(fi[ply],"<img src=\"../images/alpha.gif\" width=\"11\" height=\"9\" border=\"0\" alt=\"alpha\" hspace=\"10\"> : <b>%d</b> <img src=\"../images/beta.gif\" width=\"8\" height=\"18\" border=\"0\" alt=\"beta\" hspace=\"10\"> : <b>%d</b> <br><br>\n",alpha, beta);
  }

#endif  

  pollForInput();
  
  stats_positionsSearched++;

  if (FIXED_NODES && (stats_positionsSearched + stats_quiescensePositionsSearched > FIXED_NODES))
  {
	  stopThought();
  }

assert ( stats_positionsSearched < 1000000000 );  // hoping for the day when 
												  // this one fails :)

  pv.depth[ply] = 0;

  if(stopThinking) { 
					 #ifdef GAMETREE
					 if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
					 {
						fprintf(fi[ply],"<br>Return: out of time<br></td></tr></table></html>\n");
						fclose(fi[ply]); 
					 }
					 #endif   
					// @georg testing needed
					// the return value here should be irrelevant, because in the search on root level "stopThinking" is checked too
					// and the tree currently searched is not used.
					return -INFINITY; }

  if(AIBoard.isInCheck(AIBoard.getColorOffMove())) { 
													#ifdef GAMETREE
													if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
														{	
														fprintf(fi[ply],"<br>Return: Illegal Position: other side in check<br></td></tr></table></html>\n");
														fclose(fi[ply]); 
														}
													#endif 

													return INFINITY; }

  

  bestMove.makeBad();

  orgAlpha = alpha;
  orgBeta = beta;
  
  if (AIBoard.isNotRepDrawSearch())
  {
	  // Todo: GAMETREE_END("Draw")
	  AIBoard.store((max(depth, 0)), bestMove, 0, orgAlpha, orgBeta, ply);

	  return 0; // Repetition
  }

  if ((te = AIBoard.lookup()) != NULL) 
  
  { 
					// We've searched this position before
					// and can remember some information
					// about it.
  
	  stats_transpositionHits++;

    /* If we searched to the same depth before as we're aiming at now, then
       we can use the value we got last time.

       If the search returned an exact value (higher than alpha and lower
       then beta) then we can return that value here too.

       If the search failed high (The position was at least as good as beta)
       then if the current beta isn't higher then the value returned last time
       we return the value, otherwise we set best and alpha to the value
       returned last time since we know we can get at least that. 
       
       If the search failed low (All of it's decendants were at least as bad as
       alpha)then if the current alpha isn't lower than the value returned last
       time we return the value, otherwise we set beta to the value returned
       since we know we can't get better then that. */

    if(te->depth >= depth) 
	{
		HashValue = te->value;

		if (HashValue >= MATE)
			HashValue -= ply;
		else if (HashValue <= -MATE)
			HashValue += ply;

		if(te->type == EXACT) 
		{ 
								#ifdef GAMETREE
								if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
									{
									fprintf(fi[ply],"<br><br>Return: exact hash value: %d<br></td></tr></table></html>\n", HashValue);
									fclose(fi[ply]); }
								#endif 

								return HashValue;
		}
      else if(te->type == FAIL_HIGH) 
	  {
			if(beta <= HashValue) {
								  #ifdef GAMETREE
									if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
									{	
									fprintf(fi[ply],"<br><br>Return: fail high hash value<br></td></tr></table></html>\n");
									fclose(fi[ply]); }
								  #endif

								  return HashValue; }
			if(HashValue > alpha)		
			{
				bestValue = alpha = HashValue;
			}
									}
			else if(te->type == FAIL_LOW) 
			{
				if(HashValue <= alpha) 
				{
								   #ifdef GAMETREE
										if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
										{
										fprintf(fi[ply],"<br><br>Return: fail low hash value<br></td></tr></table></html>\n");
										fclose(fi[ply]);  }
								   #endif 

					return HashValue;
				}
				if(beta > HashValue)		
				{
					beta = HashValue;
				}	
			}
		}


  /* If we had remembered this position from before then try to 
     use the best move we found last time and look at it first. */
  
    hashMove = te->hashMove;
	

  } else hashMove.makeBad();


 
#ifdef GAMETREE
	if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
	{	
	fprintf(fi[ply]," depth to horizon: <font size=+2>%d</font> (%d ply) at depth %d<br><br>\n",depth + extensions, (depth + extensions) / ONE_PLY, ply); 	
	}
#endif 
  

  /* Capture extensions. 
   * Conditions: 
   * Last move was a capture of the piece that just moved and it was the only way to capture it
   */
  
	if (!wasNullMove)
	{
		if (AIBoard.captureExtensionCondition())

		{
		extensions += CAPTURE_EXTENSION;
		/*
		// Tried the following 2 versions, both worse (= unchanged)

		if (depth >= (FractionalDeep[currentDepth] ) - (ONE_PLY *2))
		{
			extensions += 4;
		}
	
		if (ply < currentDepth / 2)
		    extensions += 2;
		*/

#ifdef DEBUG_STATS
		stats_capext += CAPTURE_EXTENSION;
#endif

#ifdef GAMETREE
		if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1))
		{
			fprintf(fi[ply], "Capture extension: depth+ %d<br>\n", CAPTURE_EXTENSION);
		}
#endif	 
		}

	}

  /* In check.  */

  if(AIBoard.isInCheck(AIBoard.getColorOnMove())) 
  {   
	AIBoard.setCheckHistory(1);

	if (depth > (5 * ONE_PLY))
	{
		extensions += 3;
	}
	else if (depth > (3 * ONE_PLY))
	{
		extensions += 2;
	}
	else
	{
		extensions += 1;
	}


		#ifdef DEBUG_STATS
		stats_checkext+= CHECK_EXTENSION; 
		#endif
	 	
		#ifdef GAMETREE
		if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1))  { fprintf(fi[ply],"Check extension: depth + %d<br><br><hr><br>\n", CHECK_EXTENSION); }
		#endif	 
	

	recursiveCheckEvasion(&alpha, &beta,&bestValue, &bestMove, depth+extensions, ply, hashMove); 				 
  } 
  
  /* Not in Check */

  else  
  {	  	 
	 AIBoard.setCheckHistory(0);
	 AIBoard.setBestCapture(); // this is probably not needed, since before this point orderCaptures has already been called. But just to be sure.
	 
	 if ((depth < ONE_PLY) || (ply>MAX_SEARCH_DEPTH)) 

	 {
	
#ifdef GAMETREE
		if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
		{ 
			fprintf (fi[ply]," <br><br> Return: Quiesce = %d <br></td></tr></table></html>\n", bestValue); 
			fclose(fi[ply]); 
		}
#endif 
		
		
		
		bestValue = quiesce(alpha, beta, ply); 

		
		AIBoard.store(ONE_PLY-1, bestMove, bestValue, orgAlpha, orgBeta, ply);

		return bestValue; 
		
	    }
	
	
	if (depth > CC_DEPTH * ONE_PLY)
		
	{
	
	 /* NullMove : passing should be worse than any other move. */
	
	if ((!wasNullMove) && (NULL_REDUCTION))
		
	  //  no Null moves for black + white after another.
	  //  no Null move try if depth allows standpat
	{
	 
		//TODO: do we really need to try nullmove if eval() is already worse than i.e. alpha?

		AIBoard.makeNullMove();

		NullValue =  -search(-beta, -beta+1, depth - ((NULL_REDUCTION +1) * ONE_PLY), ply + 1, 1);	
		
		/*
		
		if (NullValue < beta)
		{
			NullValue = -search(-beta, -beta + 1, depth - ((NULL_REDUCTION + 1) * ONE_PLY), ply + 1, 1);
		}
		*/
		AIBoard.unmakeNullMove(); 

		#ifdef GAMETREE
		if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
		{fprintf (fi[ply]," Null Move Value : %d <br><br>\n", NullValue); }
		#endif
		
		#ifdef DEBUG_STATS
		stats_NullTries[depth/ONE_PLY]++;   	
		#endif
	

		if (NullValue >= beta) //	fail high even without making a move, this must
						   //	be a very good position
		{			                     
       
			#ifdef GAMETREE
			if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
			{ fprintf(fi[ply],"<br><br>Return: Null Move Cut<br></td></td></tr></table></html>\n");
			fclose(fi[ply]); }
			#endif
	 
			#ifdef DEBUG_STATS
			stats_NullCuts[depth/ONE_PLY]++;		
			#endif
			
			AIBoard.store((max (depth, 0)), bestMove, NullValue, orgAlpha, orgBeta, ply);

			return NullValue;

		}	// End of successful NullMove try

	
	}	// End of NullMove try

#ifdef GAMETREE
	if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
	{ fprintf(fi[ply],"<br><hr><br>\n"); }
#endif


	if ( (! recursiveHash(&alpha, &beta,&bestValue, &bestMove, depth+extensions, ply, hashMove)) && 
		 (! recursiveSearch(&alpha, &beta,&bestValue, &bestMove, depth+extensions, ply, hashMove, ALL_CAP)) )
		 recursiveFullSearch(&alpha, &beta,&bestValue, &bestMove, depth+extensions, ply, hashMove); 
	


	} // End of > depth CC_DEPTH left

		
	else 
	{ 			
			/* Stand pat 
			 * Conditions : the last move was not a sack 
			 * ( piece we last move did capture one worth as much as itself or
			 * it was not taken )
			 */

			// This needs further testing
			// if ((wasNullMove) || (AIBoard.standpatCondition()))
			// {
				
				Currenteval = AIBoard.eval() ; 
				bestValue = Currenteval; 
				if (bestValue > alpha) alpha = bestValue; 

#ifdef GAMETREE
				if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
				{ 
					fprintf (fi[ply],"Standpat allowed<a href=\"start.html#standpat\">(*)</a> : bestValue  = %d <br><br><hr><br>\n", bestValue); 
				}
#endif 
				
				if (Currenteval >= beta) 
				{
				
					#ifdef GAMETREE
					if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
					{ fprintf(fi[ply],"<br><br><hr><br>Return:  bestValue > beta<br></td></td></tr></table></html>\n");
					fclose(fi[ply]); }
					#endif

					AIBoard.store((max (depth, 0)), bestMove, bestValue, orgAlpha, orgBeta, ply);
										

					return bestValue; 
				}
			/*
			}

			else 
			{
			// if we are not allowed to standpat (like in quiesce) or search all moves 
			// (like in full width) it can happen that we find no best move even when not
			// mated. That position will be terrible, but still don't think its mate !
			
			bestValue = -EXTREME_EVAL; 


#ifdef GAMETREE
		if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
		{ 
			fprintf (fi[ply],"Standpat not allowed<a href=\"start.html#standpat\">(*)</a> : bestValue  = %d <br><br><hr><br>\n", bestValue); 
		}
#endif 
			
			
			}
			*/

		
			if ((!recursiveHash(&alpha, &beta, &bestValue, &bestMove, depth + extensions, ply, hashMove)) &&
				(!recursiveSearch(&alpha, &beta, &bestValue, &bestMove, depth + extensions, ply, hashMove, WINNING_CAP)))
				recursiveSearch(&alpha, &beta, &bestValue, &bestMove, depth + extensions, ply, hashMove, MATE_TRIES);
				
		} // End of <= depth * CC_DEPTH left


	} // End of in check/ not in check branch 	

  if (bestValue == -INFINITY) 
	{  
	 
	 // None of the moves were legal.  See if it's checkmate or the person has to sit 
    

	 if (currentRules == CRAZYHOUSE)  
	 {
		 bestValue = -MATE_IN_ONE + ply / 2 + 1;
     } 
	 else // current rules = bughouse
	 {
		 if (AIBoard.cantBlock())
		 {
			/*
			 * Add the moves-to-mate term so a losing engine DELAYS the checkmate:
			 * it prefers the move whose mate is found at the largest ply (most
			 * moves to mate).  This term must DOMINATE bughouseMateEval(), which
			 * ranges 0-492 and -- crucially -- DEPENDS ON OUR HOLDING: a capturing
			 * move adds the piece to holding, lowering bughouseMateEval by ~192-492
			 * vs a quiet move.  If the delay term were small (e.g. 4*ply = 8cp per
			 * move-to-mate), the holding swing would override it and the engine
			 * would take the capturing move (immediate mate) over the delaying move
			 * (e.g. gxh6 Q@g7# vs Kh8 ... Nxf7#).  So we use 512 per ply (1024cp per
			 * move-to-mate, > the 492cp max holding swing): delay is primary, the
			 * holding (bughouseMateEval) is a secondary tiebreaker for equal mates.
			 * Capped at 8000 so the score stays below -MATE (a true mate, not
			 * "losing but not mated") even at deep ply.  Via negation the mating
			 * side still prefers fast mate.
			 */
			 bestValue = -MATE_IN_ONE + AIBoard.bughouseMateEval() + min(512 * ply, 8000);
		 }
		 else
		 {
			 bestValue = -ALMOST_MATE;
		 }
	 }
  }
 


  /* Now that the search is over, save information to transposition tables */

   
  if (!stopThinking)
  {
	
	AIBoard.store((max (depth, 0)), bestMove, bestValue, orgAlpha, orgBeta, ply);	

	if (! bestMove.isBad())
	{
	
		if  (AIBoard.pieceOnSquare(bestMove.to()) == NONE) 
		{
			updateHistory(bestMove, (max (depth, 0))); 
		}
	}

  } 
#ifdef GAMETREE
  if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
  {				
	fprintf(fi[ply],"<br><br><hr><br>Return: end of search<br></td></tr></table></html>\n");
	fclose(fi[ply]); 
  }
#endif
  
  return bestValue;
}


/* Function: guardRootCheck
 * Input:    The search board, our color, the guarded piece type
 * Output:   1 if no move saves the piece
 * Purpose:  Partner "-X" directive (protect my X).  Runs through every
 *           legal root move and records, for each one, whether at least
 *           one of our X pieces is left under-attacked (attacker >
 *           defender, boardStruct::attacks[]) after it; 1 is recorded in
 *           rootGuardPend[i] (searchRoot() sorts the root moves by it,
 *           so protective moves come first).  Returns 1 if EVERY move
 *           leaves an X hanging: we can't save it, the piece is coming
 *           (the opponent takes it, or drops one on it) - findMove() sits
 *           and tells the partner. */

static int guardRootCheck(boardStruct *b, color us, piece type)
{
  move mv[MAX_MOVES];
  int n, i, unsavable;

  n = b->moves(mv);
  if (n == 0) return 0;   /* no legal move: the mated-sit branch handles it */

  for (i = 0; i < n; i++)
  {
    b->changeBoard(mv[i]);
    rootGuardPend[i] = b->guardPieceHang(us, type);
    b->unchangeBoard();
  }

  unsavable = 1;
  for (i = 0; i < n; i++) if (!rootGuardPend[i]) { unsavable = 0; break; }
  return unsavable;
}

/* Function: findMove
 * Input:    A pointer to move
 * Output:   None
 * Purpose:  Fills rightMove with the move that Sunsetter thinks is best
 */

/* A told move (doMove) is consumed by findMove() when it is legal
 * - but isLegal() (validate.cpp) can never say a drop is legal:
 * it is a lookup in moves(), and moves() generates no drops (only
 * aimoves() does, for the search).  A partner-told drop ("n@b2")
 * would be dropped as "stale" and the search's move played instead.
 * A drop is playable iff we hold the piece and the target is
 * empty (a drop never gives check and is never pinned; both
 * bughouse and crazyhouse). */
static int toldMoveIsLegal(move m)
{
   if (m.isBad())
      return 0;
   if (m.from() == IN_HAND)
      return ((gameBoard.getPieceInHand(gameBoard.getDeepBugColor(), m.moved()) > 0)
             && (gameBoard.pieceOnSquare(m.to()) == NONE));
   return gameBoard.isLegal(m);
}


/* ---- Dead-board announcements to the partner (bughouse) ----
 *
 * If our opponent is mated soon (bestValue >= +MATE) we say "Opp dead"
 * (their board is dying).  Sent once per game (toldOppDead, reset in
 * resetAI).  We say "I dead" from findMove's sit block and its
 * mated-move branch (tied to being mated: sent once, re-armed when
 * the mate is avoided).  Sitting is now clock-dependent (we sit only
 * while ahead over the partner's opponent), so "I dead" must track
 * the mate, not just one sit.
 *
 * We do NOT report a moves-to-checkmate count: in bughouse the terminal
 * mate score is material-based (-MATE_IN_ONE + bughouseMateEval(), see the
 * end of search()) plus an additive moves-to-mate term (min(512*ply, 8000)) to delay
 * checkmate, not crazyhouse's pure count-encoding.  So the main search never proves a specific count;
 * probing for one (a separate forced search) is slow and, for the common
 * beyond-horizon case, only yields an imprecise "N+".  We just report
 * that the board is dead - the partner's own search gives the count when
 * it matters. */

static int toldOppDead = 0;      /* sent "Opp dead" this game */
static int toldIDead = 0;        /* sent "I dead" (re-armed when we are no longer mated; sent on a mated sit or a mated move) */

static void declareBoardDead(int bestValue)
   {
   if (currentRules != BUGHOUSE) return;

   if ((bestValue >= MATE) && (!toldOppDead))
   {
   toldOppDead = 1;
   output("tellics ptell Opp dead\n");
   }
   /* "I dead" is sent from findMove's sit block (tied to the actual sit) */
   }

void findMove(move *rightMove)
{

  int bestValue; 
  
  int skipGhosts;   /* 1 if we must search without ghost pieces (the
                   * last search wanted a ghost drop, which we can't
                   * wait for) */
  int toldPartGoThisTurn;   /* 1 if the partner told us to "go": this
                           * turn makes one move even when mated (a
                           * "go" lasts exactly one move) */
  
  char buf[MAX_STRING];
  char buf2[MAX_STRING];

  pondering = 0;


  
  stats_overallsearches += stats_positionsSearched; stats_overallqsearches += stats_quiescensePositionsSearched;
  stats_hashFillingUp = stats_transpositionHits = stats_quiescensePositionsSearched = stats_positionsSearched = 0; 
  
#ifdef DEBUG_STATS
  stats_checkext = stats_forceext =  stats_capext = stats_RazorTries = stats_Razors =  0;

  int i; 
  
  for (i = 0; i<MOVEGEN_TYPES; i++) { stats_MakeUnmake [i] = 0; }
  for (i = 0; i<DEPTH_LIMIT; i++) { stats_NullTries [i] = stats_NullCuts [i] = 0; }

#endif

#ifdef DEBUG_HASH

  debug_allcoll = 0; 
  
#endif 

#ifdef GAMETREE

  tree_positionsSaved = 0; 

#endif

    if (gameBoard.getMoveNum() < 3) 
  {
		initialTime =  gameBoard.getTime(BLACK);
  }	

  skipGhosts = 0;

  do {
	/* A move told by the partner (doMove() via a ptell move, e.g.
	 * "Qf3"): play it unconditionally - even if it loses, even if
	 * we're mated soon - skip this pass' search.  A stale tell (no
	 * longer legal, e.g. a drop landed on its target square) is
	 * dropped and we search normally.  A tell that arrives
	 * mid-search (stopThought abort) or mid-sit (unsit) is caught
	 * by the post-loop check below instead. */
	if (toldMoveIsLegal(overideMove))
	{
	*rightMove = overideMove;
	overideMove.makeBad();
	stopThinking = reSearch = forceMove = 0;
	parttoldgo = 0;	/* the told move is this turn's move (consumes a "go") */
	parttoldsit = 0;	/* we move, not sit (the sit request is dropped) */
	goto endOfFindMove;
	}

		gameBoard.copy(&AIBoard);
   		overideMove.makeBad();
	

	/* We search with "ghost pieces" in Bughouse: pieces our
	 * partner might drop for us.  Ghost pieces are only searched
	 * while canSitByClock (we're up on time, and the board isn't
	 * stalled): when we have to keep moving to catch up on time, a
	 * win that only exists with a dropped piece is no good.  If
	 * rightMove turns out to be a ghost drop, we search again
	 * without ghosts (skipGhosts) and play the best real move, see
	 * below */

	if ((currentRules == BUGHOUSE) && (!partsitting) && (!parttoldgo)
	   && (!skipGhosts) && canSitByClock())
	
	{

	AIBoard.addPieceToHand(WHITE, ROOK, 1);
	AIBoard.addPieceToHand(BLACK, ROOK, 1);

    AIBoard.addPieceToHand(WHITE, KNIGHT, 1);
	AIBoard.addPieceToHand(BLACK, KNIGHT, 1);
	
	AIBoard.addPieceToHand(WHITE, PAWN, 1);
	AIBoard.addPieceToHand(BLACK, PAWN, 1);
	
	}
	
	stopThinking = reSearch = forceMove = 0;
	/* if this is the skip-ghosts re-search, its time budget
	 * starts right here (the first pass used its own up);
	 * reSearchTimeBoost gives it 4x the normal amount */
	if (skipGhosts) gameBoard.setLastMoveNow();
    toldPartGoThisTurn = parttoldgo;
    parttoldgo = 0; // he tells us to go always for 1 move only


	/* Partner "-X" directive (protect my X, partGuard) and "Mates Me"
	 * (partMatesMe: protect the X to the max).  For BOTH, order the root
	 * moves so the protective ones come first (move X to safety / refuse
	 * to trade it - rootGuardPend, filled by guardRootCheck, sorted in
	 * searchRoot).  For "Mates Me" only: if every legal move leaves one of
	 * our X pieces under-attacked we can't save it - the piece is coming
	 * (opponent takes it, or drops one on it) - we sit for a piece and
	 * tell the partner, who can "go" to make us move anyway (the do-while
	 * re-runs with toldPartGoThisTurn) or a drop for us wakes us
	 * (startSearchOver).  Runs before searchRoot so we don't spend a full
	 * search on a position we sit on.  One-time (1 sign) resets in the main
	 * loop / when the piece is lost; standing (2 signs) re-triggers until
	 * nevermind/x/cancel. */

	int guardSkipSearch = 0;

	if ((currentRules == BUGHOUSE) && (!sitting) && (!toldPartGoThisTurn))
	{
	color us = AIBoard.getColorOnMove();
	piece gtype;
	char pn[2];

	partGuardOrder = 0;
	for (gtype = PAWN; (gtype <= QUEEN) && (!sitting); gtype++)
	{
	if ((partGuard[gtype]) || (partMatesMe[gtype])) if (AIBoard.havePieceOf(us, gtype))
	{
	int unsavable;

	partGuardOrder = 1;   /* order the root moves (protect X first) for
	                     * both "-x" alone and "-x" + Mates Me */
	unsavable = guardRootCheck(&AIBoard, us, gtype);
	if ((unsavable) && (partMatesMe[gtype]))   /* sit only for "Mates Me"
	                                        * ("-x" alone just moves to
	                                        * safety / refuses to trade) */
	{
	int pn2 = gtype - PAWN;
	pn[0] = "PRNBQ"[pn2]; pn[1] = '\0';
	output ("tellics ptell I sit (the ");
	output (pn);
	if (partMatesMe[gtype]) output (" is about to be taken, tell me \"go\" to make moves)\n");
	else output (" is coming, tell me \"go\" to make moves)\n");
	sitting = 1;
	while ((sitting) && (!reSearch)) pollForInput();
	guardSkipSearch = 1;   /* the wait ended ("go" or a drop): don't run
	                         * this searchRoot, re-run the do-while - it
	                         * re-consumes parttoldgo / the new position */
	reSearch = 1;
	partGuardOrder = 0;   /* rootGuardPend is for the old position */
	}
	}
	}
	}

    if (!guardSkipSearch) searchRoot(MAX_SEARCH_DEPTH, rightMove, &bestValue);

	if (!guardSkipSearch) lastSearchScore = bestValue;

	/* Announce "Opp dead" (we're going to mate our opponent) or "I dead"
	 * (we're going to get mated), from the proven mate score.  Must run
	 * BEFORE the sit below: when mated we sit and block in pollForInput(),
	 * so anything after the sit never runs.  Skipped on guardSkipSearch
	 * (the "Mates Me" directive sit, which sits before searchRoot -
	 * bestValue is stale there, and a directive is not a proven mate).
	 * Fires once per game (told flags). */
	if (!guardSkipSearch) declareBoardDead(bestValue);

	/* Only for Bughouse */
	if ((currentRules == BUGHOUSE) && (!guardSkipSearch)) 
		
	{

	/* A bad rightMove is a no-legal-move position; the mated branch
	 * below handles it (sit for a piece) */
	/* If rightMove is a ghost drop (we're only ok because of a piece
	 * our partner might drop for us), search again without ghosts to
	 * get our true value and best real move.  Ghost pieces weren't
	 * searched when canSitByClock() is false, so rightMove is legal
	 * then.  The re-search gets its own time budget (the first pass
	 * used it up); if that true value says we're mated, the mated
	 * branch below sits and waits for the piece (we're up on time,
	 * that's why the ghost was searched in the first place) */

	if ((!rightMove->isBad()) && (!gameBoard.isLegal(*rightMove)))

	{
		skipGhosts = 1;
		reSearch = 1;
		/* the first pass used up its whole time budget; the
		 * re-search gets a fresh, boosted one (see above)
		 * so it can actually find mate if there is one */
		reSearchTimeBoost = 1;
	}

	// the move is not legal => we tried to drop a ghost-piece => sit for it
	
	if ((bestValue > -MATE) && (toldparttosit) && ((bestValue >= 0) || (partnerDead) || partnerDownOnTime())) 
	
		{ 
		output ("tellics ptell go\n"); 
		toldparttosit = 0; 
		}

	/* We told our part to sit, its the next move and now we are not
	 * mated anymore => he can go again - but NOT while we are still
	 * in danger (losing, bestValue < 0, not mated, not attacking):
	 * keep him sitting (his drops keep feeding us) until we are
	 * attacking (bestValue >= MATE), safe (bestValue >= 0) or have
	 * neutralized the threat (bestValue >= 0 = not losing) - all
	 * three are bestValue >= 0, and bestValue is fresh from the
	 * search; this is re-checked every turn, so the "go" goes out
	 * on the first safe/attacking turn.  A DEAD partner (partnerDead)
	 * can't keep sitting (checkmated board) - release the flag then
	 * (the "go" is a no-op for a checkmated board).  A partner that
	 * is DOWN on time (partnerDownOnTime) is released too, while we
	 * are still in danger: its clock is stopped while it sits but
	 * partOppTime keeps running, so the gap grows the longer it sits
	 * - releasing it (go) stops the bleed */
		
	/* We are mated soon (mate in 1 included).  With no legal move
	 * left (checkmated, bad rightMove) we always sit for a piece:
	 * that's the only way to continue, and the server (e.g.
	 * Lichess) auto-sits a checkmated player, so our clock
	 * correctly stops.  With legal moves left we sit for the mate
	 * only while we are AHEAD on time over our partner's opponent
	 * (matedSitClockOK, re-checked every turn): we bank time and
	 * tell the partner "I dead" so the (winning) partner finishes
	 * the game; when our clock falls to or below the partner's
	 * opponent we MOVE instead (delay our own mate, our captures
	 * feed the partner).  A partner "go" no longer forces a move
	 * while we are ahead on time (toldPartGoThisTurn is ignored
	 * here - we bank time and sit; it only gets us moving when we
	 * are BEHIND on time, to feed the partner).  Bot-vs-bot
	 * exception
	 * (partnerSittingLong): if our partner is ANOTHER Sunsetter
	 * (identified itself in the intro ptell), is sitting on his
	 * own for an extended period and is up on time, we do NOT
	 * sit - two Sunsetters sitting at once is a double stall
	 * (neither board moves, neither board gets fed, the game
	 * can sit out to a time result): we move instead (our
	 * captures feed him), sit on the next turn, and he gets up
	 * with a dropped piece.  A partner's explicit "sit"
	 * request (parttoldsit) is always obeyed while it is pending,
	 * even when we are not mated at all.  We never resign, we
	 * just play out to the end (see partnerSittingLong for
	 * the bot-vs-bot move-instead-of-sit exception) */

	int obeyPartSit = parttoldsit && !toldPartGoThisTurn;

	/* Bot-vs-bot coordination (see above): the partner is another
	 * Sunsetter sitting on his own for a while, up on time - move
	 * instead of sitting (only for legal-moves-left; the fully
	 * checkmated first branch sits anyway - there is nothing
	 * else to do) */
	int keepToMove = partnerSittingLong();

	/* "Opp dead" (the partner's opponent is dead): sit (and report
	 * "I sit") only when we are dead soon ourselves (proven mate,
	 * bestValue <= -MATE) and ahead on time over it, else move until
	 * we are (re-checked every turn, see partnerOppDeadSit) */
	int wantSit = partnerOppDeadSit() && (bestValue <= -MATE);

	/* Mated with legal moves left: sit (bank time, "I dead") only
	 * while ahead on time over the partner's opponent, else move
	 * (delay the mate, feed pieces); re-checked every turn */
	int matedSit = matedSitClockOK();

	if (((!sitting)
	  && ((rightMove->isBad() && (bestValue <= -MATE))
	      || ((bestValue <= -MATE)
	          && (!keepToMove)
	          && (matedSit))
	      || obeyPartSit
	      || wantSit))
	  && (currentRules == BUGHOUSE)) 

	{ 
	/* If the forced mate sequence costs us material (we lose more
	 * piece value to the opponent's captures than we gain from
	 * ours), say so: the partner decides when to "go" */
	int losingMaterial = 0;

	if (!rightMove->isBad()) 
	{
	int i, nMade = 0, usGain = 0, themGain = 0;
	move m;

	for (i = 0; (i < movesSearched) && (i < 32); i++) 
	{
		m = searchMoves[0][i];
		if (m.isBad()) break;
		{
		piece vic = AIBoard.pieceOnSquare(m.to());
		AIBoard.changeBoard(m);
		nMade++;
		if ((i % 2) == 0) 
		{
			if (m.from() == IN_HAND) usGain += pValue[m.moved()]; 
			else if (vic != NONE) usGain += pValue[vic]; 
		}
		else 
		{
			if (m.from() == IN_HAND) themGain += pValue[m.moved()]; 
			else if (vic != NONE) themGain += pValue[vic]; 
		}
		}
	}
	for (; nMade > 0; nMade--) AIBoard.unchangeBoard();
	if (themGain > usGain) losingMaterial = 1;
	}

	if (rightMove->isBad()) 
	{
		output ("tellics ptell I sit (I am mated)\ntellics ptell go\n"); 
		output ("tellics ptell I sit (I am mated)\ntellics ptell go\n"); 
	}
	else if (wantSit) 
	{
		output ("tellics ptell I sit (my opponent is dead, I'm up on time)\n"); 
	}
	else if (obeyPartSit && (bestValue > -MATE)) 
	{
		output ("tellics ptell I sit (partner told me to sit)\n"); 
	}
	else if (losingMaterial) 
	{
		output ("tellics ptell I am dead soon, I will lose material, tell me \"go\" to make a few moves\ntellics ptell I sit\n"); 
	}
	else
	{
		output ("tellics ptell I am dead soon, tell me \"go\" to make a few moves\ntellics ptell I sit\n"); 
	}
	/* We are sitting and mated: tell the partner "I dead" (no more
	 * pieces come from our board, play fast to win).  Sent on every
	 * sit after a move; toldIDead re-arms when the mate is avoided
	 * (see the move branch below), so the partner tracks our
	 * dead<->alive state. */
	if ((bestValue <= -MATE) && (!toldIDead)) 
	{
		toldIDead = 1;
		output("tellics ptell I dead\n");
	}
	sitting =1; 
	parttoldsit = 0; /* one-shot request, consumed: a partner that
	                    wants a long sit says "sit" again; a "go"
	                    arriving here (or a drop) ends the sit */
	while ((sitting) && (!reSearch)) {pollForInput();} 
	}

    else 

	{ 
		/* Mated while moving (behind on time over the partner's
		 * opponent, or the bot-vs-bot feed): we are actually dying -
		 * also tell the partner "I dead" (they stop counting on
		 * drops, play fast; our last captures still reach them).
		 * Sent once: toldIDead re-arms when the mate is avoided
		 * (bestValue > -MATE, below).  A partner "go" only gets us
		 * here when we are BEHIND on time (when we're ahead we sit
		 * above, regardless of the "go"). */
		if ((currentRules == BUGHOUSE) && (bestValue <= -MATE)) 
		{
		if (!toldIDead) 
		{
		toldIDead = 1;
		output("tellics ptell I dead\n");
		}
		}
		else toldIDead = 0;   /* mate avoided (or not bughouse): re-arm */
		/* Mated soon but keeping to move: we are behind on time over
		 * the partner's opponent (catch up on the clock),
		 * or a bot partner (another Sunsetter) is sitting on his
		 * own, up on time (feed him with captures, sit on the
		 * next turn).  Tell our partner to sit (so we get pieces)
		 * and re-search, maybe it helps.  In the bot case the
		 * partner is already sitting (partsitting), so nothing is
		 * said - just play the delaying move we just searched.
		 * Never while the partner itself is DOWN on time against
		 * its opponent (partnerDownOnTime): its drops on our board
		 * would cost it time it can't spare - re-checked every turn,
		 * so the request re-arms the turn it gets up on time again */
		if ((bestValue <= -MATE) && (!partsitting) && (!partnerDownOnTime())) 
					{ 
					reSearch =1; 
					gameBoard.setLastMoveNow();
					output ("tellics ptell sit (I am in trouble)\n"); 
					partsitting =1; toldparttosit =1; 
					}  
		}

	/* (Mated soon but keeping to move: see the else above.  Otherwise
	 * just play - a "go" turn makes the mate line, a behind-on-time
	 * turn makes a real move to catch up on clock) */
    
	} /* End of Only for Bughouse */ 

  } while(reSearch && !toldMoveIsLegal(overideMove) && !forceMove );
  
  /* A move told by the partner arrived mid-search (stopThought
   * abort) or mid-sit (unsit): it takes precedence over the
   * aborted search result.  (A tell that arrived before the search
   * was already consumed at the top of the pass; there
   * parttoldgo is consumed too, so only parttoldsit is left to
   * drop here - we move, not sit.) */
  /* Drain the input buffer (the late-tell window below, and the
   * told-move check after it): our search may end a fraction of a
   * second before a just-typed partner move reaches us - icsdrone
   * relays it within ~100 ms - and without this the tell lands one
   * turn late (a pending overideMove, played on our NEXT turn, or
   * never if the game ends).  The 200 ms window (non-blocking
   * polls) only runs with a partner, so plain xboard/crazyhouse
   * games pay nothing.  A told move in the window wins over the
   * finished search; a holding/drop relay that re-searches (see
   * startSearchOver in interface.cpp/partner.cpp) arrives as
   * reSearch and gets one more pass at the top of the loop next -
   * harmless here (its overideMove is bad, the told-move check
   * below misses it, and the main loop's own checkInput() picks up
   * the rest). */
  checkInput();
  if (partner && !analyzeMode)
  {
  long wStart = getSysMilliSecs();
  while ((getSysMilliSecs() - wStart < 200) && overideMove.isBad())
     checkInput();
  }
  if (toldMoveIsLegal(overideMove))
  {
  *rightMove = overideMove;
	overideMove.makeBad();
	parttoldsit = 0;
  }

endOfFindMove:
  
  /* Only for Bughouse */
  if (currentRules == BUGHOUSE)

  {

  /* Our part is sitting on his own.  If he does that for more than
   * 5 moves I suggest to go (psittinglong keeps counting, so it
   * stays >= 5 for the whole long sit; canSitByClock uses that).
   * Never for a DEAD partner (partnerDead, "I dead"): we want the
   * dead partner to keep sitting (bank time, so we can win) - a
   * "go" would get him up (break his sit, delay his mate) */
  if (partsitting && !toldparttosit && !partnerDead) { psittinglong ++; if ((psittinglong == 6) && !toldpartisit) output ("tellics ptell go (watch our time)\n"); }
  else { psittinglong = 0; } 	
  
  if (toldpartisit) {unsit ();}

  // we were waiting for a piece. We got that now. Or our partner told us to go anyway.

  if (toldparttosit) {partsitting = 0;}

  // we told part to sit. Don't expect him to sit more than 1 move     
  
  } /* End of only for Bughouse */

  makeHistoryOld(); 

  

  return;
}

/* Function: stopThought
 * Input:    None.
 * Output:   None.
 * Purpose:  If Sunsetter is thinking it makes it stop and make a move.  It
 *           doesn't do anything if Sunsetter is sitting while thinking about
 *           it's move to play.
 */

void stopThought()
{
 if(!sitting || pondering ) stopThinking = 1;
}

/* Function: startSearchOver
 * Input:    None.
 * Output:   None.
 * Purpose:  Used to make Sunsetter start the search again from scratch.  This
 *           should be used when a value that affects the search is changed,
 *           like the piece values or new piece in the hand.
 */

void startSearchOver()
{
  stopThinking = 1;
  reSearch = 1;

  /* Somethings changed the position by a lot,
     the old hash values are no good */

  zapHashValues();
 

}

/* Function: sit
 * Input:    None.
 * Output:   None.
 * Purpose:  Used to make Sunsetter sit
 */

void sit()
{
  sitting = 1;
  output("tellics ptell I sit\n");
}

/* Function: unsit
 * Input:    None.
 * Output:   None.
 * Purpose:  Used to make Sunsetter stop sitting
 */

void unsit()
{
  sitting = 0;
  toldpartisit = 0; 
}

/* Function: doMove
 * Input:    A move.
 * Output:   None.
 * Purpose:  Used to make Sunsetter play a move instead of what the search
 *           finds.
 */

void doMove(move m)
{
  overideMove = m;
  forceDeepBugToMove();
  unsit();
  stopThought();
}

/* Function: forceDeepBugMove
 * Input:    None.
 * Output:   None.
 * Purpose:  Used to make Sunsetter play a move even if it thinks it gets it
 *           mated
 */

void forceDeepBugToMove()
{
  forceMove = 1;
}



/* Function: resetAI
 * Input:    None.
 * Output:   None.
 * Purpose:  Used to make Sunsetter stop all trains of thought, good for things
 *           like ending and begining a game
 */

void resetAI()
{
  sitting = 0;
  toldpartisit = 0;
  stopThought();
  forceDeepBugToMove();
  reSearch = 0;
  toldOppDead = toldIDead = 0;   /* new game: our boards aren't dead yet */
}
