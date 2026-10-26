/* ***************************************************************************
 *                                Sunsetter                                  *
 *				 (c) Ben Dean-Kawamura, Georg v. Zimmermann                  *
 *   For license terms, see the file COPYING that came with this program.    *
 *                                                                           *
 *  Name: quienscense.cc                                                     *
 *  Purpose: Has quiesce().  quiesce() inputs a position and outputs the     *
 * value that Sunsetter thinks represents it.  quiesce() searchs only        *
 * capture and promotion moves. The idea is to prevent the horizon effect(ie *
 * when the computer thinks it's  doing well because it's reached its        *
 * maximum depth and is up a pawn, but its queen is hanging).  It uses an    *
 * alpha-beta search, it is pretty much identical to search() except it only *
 * looks for captures and pawn promotions.                                   *
 *                                                                           *
 *************************************************************************** */


#include "board.h"
#include "brain.h"
#include "interface.h"
#include "variables.h"

#ifdef GAMETREE
#include <string.h>
#include "notation.h"
#endif




/* Stuff from search.cpp */

#ifdef GAMETREE
	extern char filename[MAX_STRING][DEPTH_LIMIT]; 
	extern FILE *fi[DEPTH_LIMIT];
	extern int currentDepth;
#endif

extern int stats_quiescensePositionsSearched;  
extern volatile int stopThinking;        


/* Get a place to store the moves.  They used to 
   be in a local array, but that blew up the stack */


extern move searchMoves[DEPTH_LIMIT][MAX_MOVES]; 


/*
 * Function: quiesce
 * Input:    alpha, beta and the current ply
 * Output:   None
 * Purpose:  Uses an recursive alpha-beta quiescense search to assign a value 
 *           to the position.  For each position it uses either the current
 *           evaluation or the evaluation after a capture has been made,
 *           whichever is better for the player to move.  It doesn't search
 *           deeper than MAX_QUIESCE_SEARCH_DEPTH
 */

int quiesce(int alpha, int beta, int ply)
   {
   int best, value;
   move *end, *current, *m;   

assert (ply <= DEPTH_LIMIT); 


#ifdef GAMETREE

  
  char buf [MAX_STRING] ; 
  char buf2 [MAX_STRING] ;
  char buf3 [MAX_STRING] ;
  int n;
  

  if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
  {
  tree_positionsSaved++;
  sprintf(filename[ply],"=");
  sprintf(buf2,"Moves:");
  for (n=0; n<ply; n++) 
  {
	  DBMoveToRawAlgebraicMove(AIBoard.getMoveHistory(n), buf); 
	  strcat(filename[ply],buf);
	  strcat(buf2," "); strcat(buf2,buf);  

  }
  sprintf(buf3,"treemoves/%s-%d.html",filename[ply],currentDepth);
  fi[ply] = fopen(buf3,"wt");

  fprintf(fi[ply],"<link rel=\"stylesheet\" href=\"../style/style.css\" type=\"text/css\">\n");
  fprintf(fi[ply],"<html><table cellpadding=10><tr><td valign=top>\n"); 

  
  printHtmlBoard(fi[ply]); 
  fprintf(fi[ply],"</td><td valign=top>\n"); 
  
  fprintf(fi[ply],"%s <br><br><hr><br>", buf2);
  fprintf(fi[ply],"<img src=\"../images/alpha.gif\" width=\"11\" height=\"9\" border=\"0\" alt=\"\" hspace=\"10\"> : <b>%d</b> <img src=\"../images/beta.gif\" width=\"8\" height=\"18\" border=\"0\" alt=\"\" hspace=\"10\"> : <b>%d</b> <br><br>\n",alpha, beta);
  }

#endif  

   if (stopThinking)
   {
      
#ifdef GAMETREE
	 if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
	 {
		fprintf(fi[ply],"<br>Return: out of time<br>\n");
		fclose(fi[ply]); 
	 }
#endif
	 return AIBoard.eval();
   }


   if (AIBoard.isInCheck(AIBoard.getColorOffMove()))
   {
#ifdef GAMETREE
	if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
	{	
		fprintf(fi[ply],"<br>Return: Illegal Position: other side in check<br></td></tr></table></html>\n");
		fclose(fi[ply]); 
	}
#endif        
	return INFINITY;

   }

#ifdef GAMETREE
	if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
	{	
	fprintf(fi[ply]," quiesce at depth %d<br><br><hr><br><br>Winning Captures:<br><br>\n", ply); 	
	}
#endif 



   m = searchMoves[ply];
  
   if (ply >= MAX_QUIESCE_SEARCH_DEPTH)
   {
#ifdef GAMETREE
	if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
	{
		fprintf(fi[ply],"<br>Return: max depth<br></td></tr></table></html>\n");
		fclose(fi[ply]); }
#endif 
      return AIBoard.eval();
   }

   /* captures (bad sentinel left at m[ncap]) */
   int ncap = AIBoard.captureMoves(m);

   /* order the captures NOW, before the sentinel at m[ncap] is
    * overwritten by the check-drops below; orderCaptures() counts up
    * to the makeBad() sentinel, so it only touches the captures */
   end = AIBoard.orderCaptures(m);

   /* dropped pieces that give check (contact checks from the hand) are
    * searched too: a drop-mate (e.g. Q@c2#) is invisible to a
    * captures-only quiesce, so a losing move that walks into one would
    * otherwise be rated as a win.  Appended after the captures
    * (overwrites the sentinel at m[ncap], new sentinel at m[ncap+nchk]) */
   int nchk = AIBoard.checkDropMoves(m + ncap);

   if ((ncap + nchk) == 0)
   {
#ifdef GAMETREE
	if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
	{
		fprintf(fi[ply],"<br>Return: no more captures or check-drops<br></td></tr></table></html>\n");
		fclose(fi[ply]); }
#endif 
      return AIBoard.eval();
   }

   stats_quiescensePositionsSearched++;
      
   best = AIBoard.eval(); 		

   for (current = m; current < end; current++)
      {

	   if (best >= beta) break;

	   if (best > alpha) alpha = best;

assert (!AIBoard.badMove(*current));  

		AIBoard.changeBoard(*current);
		value = -quiesce(-beta, -alpha, ply + 1);
		AIBoard.unchangeBoard();	

assert (value >= -INFINITY);
assert (value <= INFINITY); 


#ifdef GAMETREE
			char buf[MAX_STRING], buf2[MAX_STRING], buf3[MAX_STRING];  
		
			if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
			{								
				DBMoveToRawAlgebraicMove(*current, buf);
				strcpy(buf2, filename[ply]); strcat(buf2, buf);
				sprintf (buf3,"<a href=\"%s-%d.html\">%s</a>  Return Value: %d<br>\n",buf2,currentDepth, buf,value); 				
				fprintf (fi[ply], buf3); 
			}
#endif		

		if (value > best) 
		{
			best = value;
		}
   
		}
		

   
   /* search the check-drops (appended after the captures, [m+ncap, m+ncap+nchk)) */
   for (current = m + ncap; current < m + ncap + nchk; current++)
      {
	   if (best >= beta) break;
	   if (best > alpha) alpha = best;
		AIBoard.changeBoard(*current);
		value = -quiesce(-beta, -alpha, ply + 1);
		AIBoard.unchangeBoard();	
		if (value > best) 
		{
			best = value;
		}
      }

   
#ifdef GAMETREE
	if ((tree_positionsSaved < GAMETREE) && (currentDepth == FIXED_DEPTH - 1)) 
	{
		fprintf(fi[ply],"<br><br><hr><br>Return: end of quiesce<br></td></tr></table></html>\n");
		fclose(fi[ply]); 
	}
#endif    

   return best;
   }

