/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This is Brown, a simple go program.                           *
 *                                                               *
 * Copyright 2003 and 2004 by Gunnar Farneb�ck.                  *
 *                                                               *
 * Permission is hereby granted, free of charge, to any person   *
 * obtaining a copy of this file gtp.c, to deal in the Software  *
 * without restriction, including without limitation the rights  *
 * to use, copy, modify, merge, publish, distribute, and/or      *
 * sell copies of the Software, and to permit persons to whom    *
 * the Software is furnished to do so, provided that the above   *
 * copyright notice(s) and this permission notice appear in all  *
 * copies of the Software and that both the above copyright      *
 * notice(s) and this permission notice appear in supporting     *
 * documentation.                                                *
 *                                                               *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY     *
 * KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE    *
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR       *
 * PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN NO      *
 * EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS  *
 * NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR    *
 * CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING    *
 * FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF    *
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT    *
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS       *
 * SOFTWARE.                                                     *
 *                                                               *
 * Except as contained in this notice, the name of a copyright   *
 * holder shall not be used in advertising or otherwise to       *
 * promote the sale, use or other dealings in this Software      *
 * without prior written authorization of the copyright holder.  *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "brown.h"

/* The GTP specification leaves the initial board size and komi to the
 * discretion of the engine. We make the uncommon choices of 6x6 board
 * and komi -3.14.
 */
int board_size = 6;
int current_handicap = 0;
int current_move_num = 0;
int GNU_FIRST_MOVE = 0;
float komi = -3.14;

/* Board represented by a 1D array. The first board_size*board_size
 * elements are used. Vertices are indexed row by row, starting with 0
 * in the upper left corner.
 */
static int board[MAX_BOARD * MAX_BOARD];
static int game_moves[10*MAX_BOARD * MAX_BOARD];
static int game_moves_color[10*MAX_BOARD * MAX_BOARD];

/* Stones are linked together in a circular list for each string. */
static int next_stone[MAX_BOARD * MAX_BOARD];

/* Storage for final status computations. */
static int final_status[MAX_BOARD * MAX_BOARD];

/* Point which would be an illegal ko recapture. */
static int ko_i, ko_j;

/* Offsets for the four directly adjacent neighbors. Used for looping. */
static int deltai[4] = {-1, 1, 0, 0};
static int deltaj[4] = {0, 0, -1, 1};

/* Macros to convert between 1D and 2D coordinates. The 2D coordinate
 * (i, j) points to row i and column j, starting with (0,0) in the
 * upper left corner.
 */
#define POS(i, j) ((i) * board_size + (j))
#define I(pos) ((pos) / board_size)
#define J(pos) ((pos) % board_size)

/* Macro to find the opposite color. */
#define OTHER_COLOR(color) (WHITE + BLACK - (color))

void
init_brown()
{
  int k;
  int i, j;

  /* The GTP specification leaves the initial board configuration as
   * well as the board configuration after a boardsize command to the
   * discretion of the engine. We choose to start with up to 20 random
   * stones on the board.
   */
  clear_board();
  for (k = 0; k < 2; k++) {
	  
	//gtp_printf("init_brown %i\n",current_move_num);
    int color = rand() % 2 ? BLACK : WHITE;
    generate_move(&i, &j, color);
    play_move(i, j, color);
  }
}

void
clear_board()
{
  memset(board, 0, sizeof(board));
  memset(game_moves, 0, sizeof(game_moves));
  memset(game_moves_color, 0, sizeof(game_moves));
  current_move_num = 0;
  GNU_FIRST_MOVE = 0;
//  gtp_printf("clear_board\n");
}

void undo(){
	
	char before[5000] = "printf \"";
	char after[5000] = "printf \"";
	getGNUcmd(before,BLACK);
	//gtp_printf("undo BEFORE %i %s\n",current_move_num,before);
	if(current_move_num>0) {
		int new_last_move = current_move_num-1;
		current_move_num = 0;
		if(new_last_move < GNU_FIRST_MOVE) {
			GNU_FIRST_MOVE = 0;
		}
		memset(board, 0, sizeof(board));
		int cpt=0;
		for(cpt=0;cpt<new_last_move;cpt++){
			//gtp_printf("undo cpt %i\n",cpt);
			int color = game_moves_color[cpt];
			int move = game_moves[cpt];
			
			play_move(I(move), J(move), color);
		}
		
		getGNUcmd(after,BLACK);
		//gtp_printf("undo AFTER %i %s\n",current_move_num,after);
	}
}

int
board_empty()
{
  int i;
  for (i = 0; i < board_size * board_size; i++)
    if (board[i] != EMPTY)
      return 0;

  return 1;
}

int
get_board(int i, int j)
{
  return board[i * board_size + j];
}

/* Get the stones of a string. stonei and stonej must point to arrays
 * sufficiently large to hold any string on the board. The number of
 * stones in the string is returned.
 */
int
get_string(int i, int j, int *stonei, int *stonej)
{
  int num_stones = 0;
  int pos = POS(i, j);
  do {
    stonei[num_stones] = I(pos);
    stonej[num_stones] = J(pos);
    num_stones++;
    pos = next_stone[pos];
  } while (pos != POS(i, j));

  return num_stones;
}

static int
pass_move(int i, int j)
{
  return i == -1 && j == -1;
}

static int
on_board(int i, int j)
{
  return i >= 0 && i < board_size && j >= 0 && j < board_size;
}

int
legal_move(int i, int j, int color)
{
  int other = OTHER_COLOR(color);
  
  /* Pass is always legal. */
  if (pass_move(i, j))
    return 1;

  /* Already occupied. */
  if (get_board(i, j) != EMPTY)
    return 0;

  /* Illegal ko recapture. It is not illegal to fill the ko so we must
   * check the color of at least one neighbor.
   */
  if (i == ko_i && j == ko_j
      && ((on_board(i - 1, j) && get_board(i - 1, j) == other)
	  || (on_board(i + 1, j) && get_board(i + 1, j) == other)))
    return 0;

  return 1;
}

/* Does the string at (i, j) have any more liberty than the one at
 * (libi, libj)?
 */
static int
has_additional_liberty(int i, int j, int libi, int libj)
{
  int pos = POS(i, j);
  do {
    int ai = I(pos);
    int aj = J(pos);
    int k;
    for (k = 0; k < 4; k++) {
      int bi = ai + deltai[k];
      int bj = aj + deltaj[k];
      if (on_board(bi, bj) && get_board(bi, bj) == EMPTY
	  && (bi != libi || bj != libj))
      return 1;
    }

    pos = next_stone[pos];
  } while (pos != POS(i, j));

  return 0;
}

/* Does (ai, aj) provide a liberty for a stone at (i, j)? */
static int
provides_liberty(int ai, int aj, int i, int j, int color)
{
  /* A vertex off the board does not provide a liberty. */
  if (!on_board(ai, aj))
    return 0;

  /* An empty vertex IS a liberty. */
  if (get_board(ai, aj) == EMPTY)
    return 1;

  /* A friendly string provides a liberty to (i, j) if it currently
   * has more liberties than the one at (i, j).
   */
  if (get_board(ai, aj) == color)
    return has_additional_liberty(ai, aj, i, j);

  /* An unfriendly string provides a liberty if and only if it is
   * captured, i.e. if it currently only has the liberty at (i, j).
   */
  return !has_additional_liberty(ai, aj, i, j);
}

/* Is a move at (i, j) suicide for color? */
static int
suicide(int i, int j, int color)
{
  int k;
  for (k = 0; k < 4; k++)
    if (provides_liberty(i + deltai[k], j + deltaj[k], i, j, color))
      return 0;

  return 1;
}

/* Remove a string from the board array. There is no need to modify
 * the next_stone array since this only matters where there are
 * stones present and the entire string is removed.
 */
static int
remove_string(int i, int j)
{
  int pos = POS(i, j);
  int removed = 0;
  do {
    board[pos] = EMPTY;
    removed++;
    if(pos == POS(9, 9)) { // if the central stone is captured, we stop the mirror
		GNU_FIRST_MOVE=current_move_num;
    }
    pos = next_stone[pos];
  } while (pos != POS(i, j));

  return removed;
}

/* Do two vertices belong to the same string. It is required that both
 * pos1 and pos2 point to vertices with stones.
 */
static int
same_string(int pos1, int pos2)
{
  int pos = pos1;
  do {
    if (pos == pos2)
      return 1;
    pos = next_stone[pos];
  } while (pos != pos1);
  
  return 0;
}

/* Play at (i, j) for color. No legality check is done here. We need
 * to properly update the board array, the next_stone array, and the
 * ko point.
 */
void play_move(int i, int j, int color)
{
  int pos = POS(i, j);
  int captured_stones = 0;
  int k;

  /* Reset the ko point. */
  ko_i = -1;
  ko_j = -1;
//  gtp_printf("play_move %i i=%i j=%i\n",current_move_num,i,j);
  game_moves_color[current_move_num] = color;
  game_moves[current_move_num++] = pos;
  /* Nothing more happens if the move was a pass. */
  if (pass_move(i, j))
    return;
//  gtp_printf("play_move not pass");

  /* If the move is a suicide we only need to remove the adjacent
   * friendly stones.
   */
  if (suicide(i, j, color)) {
    for (k = 0; k < 4; k++) {
      int ai = i + deltai[k];
      int aj = j + deltaj[k];
      if (on_board(ai, aj)
	  && get_board(ai, aj) == color)
	remove_string(ai, aj);
    }
    return;
  }

//  gtp_printf("play_move not suicide");
  /* Not suicide. Remove captured opponent strings. */
  for (k = 0; k < 4; k++) {
    int ai = i + deltai[k];
    int aj = j + deltaj[k];
    if (on_board(ai, aj)
	&& get_board(ai, aj) == OTHER_COLOR(color)
	&& !has_additional_liberty(ai, aj, i, j))
      captured_stones += remove_string(ai, aj);
  }

  /* Put down the new stone. Initially build a single stone string by
   * setting next_stone[pos] pointing to itself.
   */
  board[pos] = color;
  next_stone[pos] = pos;

  /* If we have friendly neighbor strings we need to link the strings
   * together.
   */
  for (k = 0; k < 4; k++) {
    int ai = i + deltai[k];
    int aj = j + deltaj[k];
    int pos2 = POS(ai, aj);
    /* Make sure that the stones are not already linked together. This
     * may happen if the same string neighbors the new stone in more
     * than one direction.
     */
    if (on_board(ai, aj) && board[pos2] == color && !same_string(pos, pos2)) {
      /* The strings are linked together simply by swapping the the
       * next_stone pointers.
       */
      int tmp = next_stone[pos2];
      next_stone[pos2] = next_stone[pos];
      next_stone[pos] = tmp;
    }
  }
//  gtp_printf("play_move check ko");

  /* If we have captured exactly one stone and the new string is a
   * single stone it may have been a ko capture.
   */
  if (captured_stones == 1 && next_stone[pos] == pos) {
    int ai, aj;
    /* Check whether the new string has exactly one liberty. If so it
     * would be an illegal ko capture to play there immediately. We
     * know that there must be a liberty immediately adjacent to the
     * new stone since we captured one stone.
     */
    for (k = 0; k < 4; k++) {
      ai = i + deltai[k];
      aj = j + deltaj[k];
      if (on_board(ai, aj) && get_board(ai, aj) == EMPTY)
	break;
    }
    
    if (!has_additional_liberty(i, j, ai, aj)) {
      ko_i = ai;
      ko_j = aj;
    }
  }
}

/* Generate a move. */
void generate_move(int *i, int *j, int color)
{
  int moves[MAX_BOARD * MAX_BOARD];
  int num_moves = 0;
  int move;
  int ai, aj;
  int previ, prevj;
  int k;
  int GNUi, GNUj;

  memset(moves, 0, sizeof(moves));
  for (ai = 0; ai < board_size; ai++)
    for (aj = 0; aj < board_size; aj++) {
      /* Consider moving at (ai, aj) if it is legal and not suicide. */
      if (legal_move(ai, aj, color)
	  && !suicide(ai, aj, color)) {
	/* Further require the move not to be suicide for the opponent... */
	if (!suicide(ai, aj, OTHER_COLOR(color)))
	  moves[num_moves++] = POS(ai, aj);
	else {
	  /* ...however, if the move captures at least one stone,
           * consider it anyway.
	   */
	  for (k = 0; k < 4; k++) {
	    int bi = ai + deltai[k];
	    int bj = aj + deltaj[k];
	    if (on_board(bi, bj) && get_board(bi, bj) == OTHER_COLOR(color)) {
	      moves[num_moves++] = POS(ai, aj);
	      break;
	    }
	  }
	}
      }
    }

  /* Choose one of the considered moves randomly with uniform
   * distribution. (Strictly speaking the moves with smaller 1D
   * coordinates tend to have a very slightly higher probability to be
   * chosen, but for all practical purposes we get a uniform
   * distribution.)
   */
  if (num_moves > 0) {
	//askGGNU(&GNUi,&GNUj,color);
	if(current_move_num == 0) {
		move = POS(9,9);
		GNU_FIRST_MOVE = 0;
	} else {
		previ = I(game_moves[current_move_num-1]);
		prevj = J(game_moves[current_move_num-1]);
		previ = board_size-1-previ;
		prevj = board_size-1-prevj;
		if (GNU_FIRST_MOVE == 0 && legal_move(previ, prevj, color)
			&& !suicide(previ, prevj, color)){
			move = POS(previ,prevj);
		} else {
			if(GNU_FIRST_MOVE == 0 && current_move_num>=current_handicap){
				
				//gtp_printf("OK end of the mirror, ");
				//gtp_printf("GNU takes over %i \n",current_move_num);
				GNU_FIRST_MOVE=current_move_num;
			}
			askGGNU(&GNUi,&GNUj,color);
			
			//gtp_printf("\ncGENAAA %i %i %i\n", color, GNUi, GNUj);
			if(GNUi>=-1){
				move = POS(GNUi,GNUj);
			}
		}
	}
	if(GNUi>=-1){
		*i = I(move);
		*j = J(move);
	} else {	
		*i = GNUi;
		*j = GNUj;
	}
    
    
	//gtp_printf("\ncGENBBB %i %i %i\n", color, i, j);
  }
  else {
    /* But pass if no move was considered. */
    *i = -1;
    *j = -1;
  }
}

/* Set a final status value for an entire string. */
static void
set_final_status_string(int pos, int status)
{
  int pos2 = pos;
  do {
    final_status[pos2] = status;
    pos2 = next_stone[pos2];
  } while (pos2 != pos);
}

/* Compute final status. This function is only valid to call in a
 * position where generate_move() would return pass for at least one
 * color.
 *
 * Due to the nature of the move generation algorithm, the final
 * status of stones can be determined by a very simple algorithm:
 *
 * 1. Stones with two or more liberties are alive with territory.
 * 2. Stones in atari are dead.
 *
 * Moreover alive stones are unconditionally alive even if the
 * opponent is allowed an arbitrary number of consecutive moves.
 * Similarly dead stones cannot be brought alive even by an arbitrary
 * number of consecutive moves.
 *
 * Seki is not an option. The move generation algorithm would never
 * leave a seki on the board.
 *
 * Comment: This algorithm doesn't work properly if the game ends with
 *          an unfilled ko. If three passes are required for game end,
 *          that will not happen.
 */
void
compute_final_status(void)
{
  int i, j;
  int pos;
  int k;

  for (pos = 0; pos < board_size * board_size; pos++)
    final_status[pos] = UNKNOWN;
  
  for (i = 0; i < board_size; i++)
    for (j = 0; j < board_size; j++)
      if (get_board(i, j) == EMPTY)
	for (k = 0; k < 4; k++) {
	  int ai = i + deltai[k];
	  int aj = j + deltaj[k];
	  if (!on_board(ai, aj))
	    continue;
	  /* When the game is finished, we know for sure that (ai, aj)
           * contains a stone. The move generation algorithm would
           * never leave two adjacent empty vertices. Check the number
           * of liberties to decide its status, unless it's known
           * already.
	   *
	   * If we should be called in a non-final position, just make
	   * sure we don't call set_final_status_string() on an empty
	   * vertex.
	   */
	  pos = POS(ai, aj);
	  if (final_status[pos] == UNKNOWN) {
	    if (get_board(ai, aj) != EMPTY) {
	      if (has_additional_liberty(ai, aj, i, j))
		set_final_status_string(pos, ALIVE);
	      else
		set_final_status_string(pos, DEAD);
	    }
	  }
	  /* Set the final status of the (i, j) vertex to either black
           * or white territory.
	   */
	  if (final_status[POS(i, j)] == UNKNOWN) {
	    if ((final_status[pos] == ALIVE) ^ (get_board(ai, aj) == WHITE))
	      final_status[POS(i, j)] = BLACK_TERRITORY;
	    else
	      final_status[POS(i, j)] = WHITE_TERRITORY;
	  }
	}
}

int
get_final_status(int i, int j)
{
  return final_status[POS(i, j)];
}

void
set_final_status(int i, int j, int status)
{
  final_status[POS(i, j)] = status;
}

/* Valid number of stones for fixed placement handicaps. These are
 * compatible with the GTP fixed handicap placement rules.
 */
int
valid_fixed_handicap(int handicap)
{
  if (handicap < 2 || handicap > 9)
    return 0;
  if (board_size % 2 == 0 && handicap > 4)
    return 0;
  if (board_size == 7 && handicap > 4)
    return 0;
  if (board_size < 7 && handicap > 0)
    return 0;
  
  return 1;
}

/* Put fixed placement handicap stones on the board. The placement is
 * compatible with the GTP fixed handicap placement rules.
 */
void
place_fixed_handicap(int handicap)
{
	current_handicap = handicap;
  int low = board_size >= 13 ? 3 : 2;
  int mid = board_size / 2;
  int high = board_size - 1 - low;
  
  if (handicap >= 2) {
    play_move(high, low, BLACK);   /* bottom left corner */
    play_move(low, high, BLACK);   /* top right corner */
  }
  
  if (handicap >= 3)
    play_move(low, low, BLACK);    /* top left corner */
  
  if (handicap >= 4)
    play_move(high, high, BLACK);  /* bottom right corner */
  
  if (handicap >= 5 && handicap % 2 == 1)
    play_move(mid, mid, BLACK);    /* tengen */
  
  if (handicap >= 6) {
    play_move(mid, low, BLACK);    /* left edge */
    play_move(mid, high, BLACK);   /* right edge */
  }
  
  if (handicap >= 8) {
    play_move(low, mid, BLACK);    /* top edge */
    play_move(high, mid, BLACK);   /* bottom edge */
  }
}

/* Put free placement handicap stones on the board. We do this simply
 * by generating successive black moves.
 */
void
place_free_handicap(int handicap)
{
  int k;
  int i, j;
  current_handicap = handicap;
  for (k = 0; k < handicap; k++) {
    generate_move(&i, &j, BLACK);
    play_move(i, j, BLACK);
  }
}

//strcat
//printf "komi 6.5\nboardsize 19\nclear_board\nplay B D4\nplay W Q16\nplay B D17\nplay W Q3\nplay B R5\nplay W C15\ngenmove B\n" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep "^= [a-zA-Z]" | cat

//printf "komi 6.5\nboardsize 19\nclear_board\nplay B D4\nplay W Q16\nplay B D17\nplay W Q3\nplay B R5\nplay W C15\ngenmove B\nquit\n" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep "^= [a-zA-Z]" | cat
//printf "komi 6.5\nboardsize 19\nclear_board\nplay B D4\nplay W Q16\nplay B D17\nplay W Q3\nplay B R5\nplay W C15\ngenmove B\nquit\n" | /home/jeff/Documents/go/leela_zero_latest/build/leelaz -g --quiet --noponder -w /home/jeff/Documents/go/leela_zero_latest/LeelaMaster_GXAA.txt --resignpct 1 | grep "^= [a-zA-Z]" | cat

void getGNUcmd(char *s, int genmove_color){
	int cpt=0;
	
	int color = BLACK;
	int move;
	int i;
	int j;
	int ri;
	int rj;
	
	char str1[5000] = "B ";
	char komistr[15] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" ;
	sprintf(komistr,"komi %3.1f\\n",komi);
	char boardsizestr[16] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" ;
	sprintf(boardsizestr,"boardsize %i\\n",board_size);
	//char cmd[5000] = "komi 6.5\\nboardsize 19\\nclear_board\\nplay B D4\\nplay W Q16\\nplay B D17\\nplay W Q3\\nplay B R5\\ngenmove W\\nquit\\n\" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep \"^= [a-zA-Z]\" | cat";
	//char gamemovesSTR[5000] = "play B D4\\nplay W Q16\\nplay B D17\\nplay W Q3\\nplay B R5\\n";
	char cmd[5000] = "quit\\n\" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep \"^= [a-zA-Z]\" | cat";

	strcat(s,komistr);
	strcat(s,boardsizestr);
	strcat(s,"clear_board\\n");
	for(cpt=0;cpt<current_move_num;cpt++){
		color = game_moves_color[cpt];
		move = game_moves[cpt];
		i = I(move);
		j = J(move);
		if(!pass_move(i,j)) {
			
			
			/*if (k > 0)
			  gtp_printf(" ");
			if (i == -1 && j == -1)
			  gtp_printf("PASS");
			else*/ 
			if (i < 0 || i >= board_size
				 || j < 0 || j >= board_size) {
			  //gtp_printf("??");
			  color = OTHER_COLOR(color);
			  color = OTHER_COLOR(color);
			} else {
				/*if (vertex_transform_output_hook != NULL) {
					(*vertex_transform_output_hook)(i, j, &ri, &rj);
				} else {*/
					ri = i;
					rj = j;
				//}
				strcat(s,"play ");
				// TODO read the color from the board? what if captures happened?
				if(color == BLACK){
					strcat(s,"B ");
				} else {
					strcat(s,"W ");
				}
				char oneMove[15] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" ;
				sprintf(oneMove,"%c%d", 'A' + rj + (rj >= 8), board_size - ri);
			  
				strcat(s,oneMove);
				strcat(s,"\\n");
			}
		}
		//color = current_move_num>=current_handicap ? OTHER_COLOR(color) : BLACK;
	}
	//strcat(s,gamemovesSTR);
	strcat(s,"genmove ");
	if(genmove_color == BLACK){
		strcat(s,"B\\n");
	} else {
		strcat(s,"W\\n");
	}
	strcat(s,cmd);
}

//printf "komi 6.5\nboardsize 19\nclear_board\nplay B A1\ntop_moves_black\nquit\n" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep "^= [a-zA-Z]" | cat
// GNU top_moves_black responds (% are for BLACK only !!!)
// = R3 25.01 P3 25.00 Q16 25.00 B2 19.11
void getGNUTopMovecmd(char *s, int genmove_color){
	int cpt=0;

	int color = BLACK;
	int move;
	int i;
	int j;
	int ri;
	int rj;

	char str1[5000] = "B ";
	char komistr[15] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" ;
	sprintf(komistr,"komi %3.1f\\n",komi);
	char boardsizestr[16] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" ;
	sprintf(boardsizestr,"boardsize %i\\n",board_size);
	//char cmd[5000] = "komi 6.5\nboardsize 19\nclear_board\nplay B A1\ntop_moves_black\nquit\n" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep "^= [a-zA-Z]" | cat
	//char gamemovesSTR[5000] = "play B D4\\nplay W Q16\\nplay B D17\\nplay W Q3\\nplay B R5\\n";
	//char cmd[5000] = "quit\\n\" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep \"^= [a-zA-Z]\" | cat";
	char cmd[5000] = "quit\\n\" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep \"^= [a-zA-Z]\" | cat";

	strcat(s,komistr);
	strcat(s,boardsizestr);
	strcat(s,"clear_board\\n");
	for(cpt=0;cpt<current_move_num;cpt++){
		color = game_moves_color[cpt];
		move = game_moves[cpt];
		i = I(move);
		j = J(move);
		if(!pass_move(i,j)) {


			/*if (k > 0)
			  gtp_printf(" ");
			if (i == -1 && j == -1)
			  gtp_printf("PASS");
			else*/
			if (i < 0 || i >= board_size
				 || j < 0 || j >= board_size) {
			  //gtp_printf("??");
			  color = OTHER_COLOR(color);
			  color = OTHER_COLOR(color);
			} else {
				/*if (vertex_transform_output_hook != NULL) {
					(*vertex_transform_output_hook)(i, j, &ri, &rj);
				} else {*/
					ri = i;
					rj = j;
				//}
				strcat(s,"play ");
				// TODO read the color from the board? what if captures happened?
				if(color == BLACK){
					strcat(s,"B ");
				} else {
					strcat(s,"W ");
				}
				char oneMove[15] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" ;
				sprintf(oneMove,"%c%d", 'A' + rj + (rj >= 8), board_size - ri);

				strcat(s,oneMove);
				strcat(s,"\\n");
			}
		}
		//color = current_move_num>=current_handicap ? OTHER_COLOR(color) : BLACK;
	}
	//strcat(s,gamemovesSTR);
	strcat(s,"top_moves_");
	if(genmove_color == BLACK){
		strcat(s,"black\\n");
	} else {
		strcat(s,"white\\n");
	}
	strcat(s,cmd);
}

int askGGNU(int *i, int *j, int color) {
    /* 1) ask for a GNU move
       2) if pass or resign => return
       3) ask GNU for move suggestions
       4) ask leela for move suggestions
       5) take GNU's best suggestion that is in leela scope

       if no match between GNU and Leela
       6) ask leela opinion on GNU's suggestions
       7a) take GNU's best suggestion
       7b) take leela's suggestion that is the closest and above GNU's best suggestion

       other ideas:

    */
	FILE *fp;
	FILE *fp2;
	FILE *fp3;
	int status;
	int status2;
	int status3;
	char gnuSuggestions[5000]          = "= R3 25.01 P3 25.00 Q16 25.00 B2 19.11";
    int PATH_MAX = 200;
    int MAX_SUGGESTIONS = 10;
    int gnuSuggestionsCpt = 0;
	char gnuMoves[20][5] = {"\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0","\0\0\0\0\0"};
	char path0[PATH_MAX];
	char path1[PATH_MAX];
	char path2[PATH_MAX];
	char path3[PATH_MAX];
	char path4[PATH_MAX];
	char path5[PATH_MAX];
	char path6[PATH_MAX];
	char str1[5000] = "";
	char cmd[5000]          = "printf \"";
	getGNUcmd(cmd,color);
	char cmdGNUIdeas[5000]  = "printf \""; //printf "komi 6.5\nboardsize 19\nclear_board\nplay B A1\ntop_moves_black\nquit\n" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep "^= [a-zA-Z]" | cat

	// GNU top_moves_black responds (% are for BLACK only !!!)
	// = R3 25.01 P3 25.00 Q16 25.00 B2 19.11
	char cmdLeelaOpinions[5000] = "printf \""; // printf "komi 6.5\nboardsize 19\nclear_board\nplay B D4\nplay W Q16\nplay B D17\nplay W Q3\nplay B R5\nplay W C15\nplay B O4\nlz-analyze W\nquit\n" | (/home/jeff/Documents/go/leela_zero_latest/build/leelaz -g --noponder -w /home/jeff/Documents/go/leela_zero_latest/LeelaMaster_GXAA.txt --resignpct 1 --playouts 4  3>&1 1>&2- 2>&3- ) | grep "[A-Z][1-9][ 1-9]" | cat
	// lz-analyze responds (% are for the asked color)
	/*
	 C11 ->       3 (V: 51.93%) (LCB:  0.00%) (N: 26.53%) PV: C11 C6
     D11 ->       2 (V: 53.69%) (LCB:  0.00%) (N:  2.30%) PV: D11 C13
      O4 ->       2 (V: 53.40%) (LCB:  0.00%) (N:  3.98%) PV: O4 Q5
      O3 ->       2 (V: 52.31%) (LCB:  0.00%) (N:  2.44%) PV: O3 P4
     D13 ->       2 (V: 52.22%) (LCB:  0.00%) (N:  7.35%) PV: D13 E15
     C10 ->       2 (V: 51.94%) (LCB:  0.00%) (N:  3.27%) PV: C10 B17
     D12 ->       2 (V: 50.96%) (LCB:  0.00%) (N: 24.27%) PV: D12 E16
     R14 ->       2 (V: 47.88%) (LCB:  0.00%) (N: 13.96%) PV: R14 O17
      N4 ->       1 (V: 51.64%) (LCB:  0.00%) (N:  2.40%) PV: N4
     E16 ->       1 (V: 45.91%) (LCB:  0.00%) (N:  2.66%) PV: E16
	*/
	getGNUcmd(cmd,color);

//    :2
	
	
	//fp = popen("printf \"komi 6.5\\nboardsize 19\\nclear_board\\nplay B D4\\nplay W Q16\\nplay B D17\\nplay W Q3\\nplay B R5\\ngenmove W\\nquit\\n\" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep \"^= [a-zA-Z]\" | cat", "r");
	
	//gtp_printf("\nREF 'printf \"komi 6.5\\nboardsize 19\\nclear_board\\nplay B D4\\nplay W Q16\\nplay B D17\\nplay W Q3\\nplay B R5\\ngenmove W\\nquit\\n\" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep \"^= [a-zA-Z]\" | cat'");
	//gtp_printf("\ncmd '%s'\n", cmd);
	fp = popen(cmd, "r");
	if (fp == NULL)
		/* Handle error */;


	while (fgets(path0, PATH_MAX, fp) != NULL) {
		//printf("answers %s", path);
		//gtp_printf("coucou %s", path);
		strncpy (path1, path0 + 2, strlen(path0)-2); // remove the "= " from the answer "= B A1"
		//gtp_printf("answers '%s'", path1);
		if (strcmp(path1, "pass")==0 || strcmp(path1, "pass\n")==0 || strcmp(path1, "PASS")==0 || strcmp(path1, "PASS\n")==0){
			//gtp_printf("GNU wants to pass");
			//gtp_success(path1);
			//gtp_success(path1);
			*i = -1;
			*j = -1;
			return 0;
		}
		if (strcmp(path1, "resign") ==0 || strcmp(path1, "resign\n") ==0){
			//gtp_printf("GNU wants to resign");
			//gtp_success(path1);
			//gtp_success(path1);
			*i = -2;
			*j = -2;
			return 0;
		}
		//
		if(color == BLACK){
			strcat(str1,"B ");
		} else {
			strcat(str1,"W ");
		}
		strcat(str1,path1);
		//gtp_printf("coucou-3 '%s'", str1);

		// 3) ask GNU for move suggestions
		/*getGNUTopMovecmd(cmdGNUIdeas,color);
		fp2 = popen(cmdGNUIdeas, "r");


        while (fgets(path2, PATH_MAX, fp2) != NULL) { // expected result is "= R3 25.01 P3 25.00 Q16 25.00 B2 19.11"
            strncpy (path3, path2 + 2, strlen(path2)-2); // remove the "= " from the answer -> "R3 25.01 P3 25.00 Q16 25.00 B2 19.11"
            bool isContinue = true;
            while (isContinue && gnuSuggestionsCpt < MAX_SUGGESTIONS) {
                //strncpy(gnuMoves[gnuSuggestionsCpt] , "\0",1);
                //strncpy(gnuMoves[gnuSuggestionsCpt+1] , "\0",1);
                strncpy (gnuMoves[gnuSuggestionsCpt], gnuSuggestions , 4);
                char *spaceCharInMove = strchr(gnuMoves[gnuSuggestionsCpt], ' ');
                if(NULL != spaceCharInMove){
                    *spaceCharInMove = '\0';
                    strncpy (gnuSuggestions, gnuSuggestions + 4, strlen(gnuSuggestions)+1-4);

                    // TODO replace space by \0 in gnuMoves[gnuSuggestionsCpt]
                    //strncpy (gnuMoves[gnuSuggestionsCpt], gnuMoves[gnuSuggestionsCpt], 5-strlen(strchr(gnuMoves[gnuSuggestionsCpt], ' ')));

                    //gtp_printf("gnuMove");gtp_printf("\n");
                    //gtp_printf(gnuMoves[gnuSuggestionsCpt]);gtp_printf("\n");
                    //strlen(gnuMoves[gnuSuggestionsCpt]);
                    //printf(strlen(gnuMoves[gnuSuggestionsCpt]));printf("\n");
                    //printf(1);printf("\n");
                    //gtp_printf("gnuSuggestions");gtp_printf("\n");
                    //gtp_printf(gnuSuggestions);gtp_printf("\n");

                    gnuSuggestionsCpt ++; // validates the current move as being coordinates

                    if(NULL != strchr(gnuSuggestions, ' ')){
                       strncpy (gnuSuggestions, strchr(gnuSuggestions, ' ')+1, strlen(strchr(gnuSuggestions, ' ')));
                    } else {
                        isContinue = false;
                        //gtp_printf("no more GNU suggestions");gtp_printf("\n");
                    }

                } else {
                    isContinue = false;

                    //gtp_printf("not coords");gtp_printf("\n");
                }
                isContinue = isContinue && strlen(gnuSuggestions) > 4;
            }
        }
        status2 = pclose(fp2);

            return 1;
        }
        // 4) ask leela for move suggestions
        // 5) take GNU's best suggestion that is in leela scope

        // if no match between GNU and Leela
        // 6) ask leela opinion on GNU's suggestions
        // 7a) take GNU's best suggestion
        // 7b) take leela's suggestion that is the closest and above GNU's best suggestion
*/
		// str1 contains the chosen move like "B A1"
		gtp_decode_move(str1, &color, i, j);
		//gtp_printf("\ncouc %i %i %i\n", color, *i, *j);
	}
	/*if (!legal_move(i, j, color))
		return gtp_failure("illegal move");
*/
	//play_move(i, j, color);

	status = pclose(fp);
	if (status == -1) {
		/* Error reported by pclose() */
		
	} else {
		/* Use macros described under wait() to inspect `status' in order
		   to determine success/failure of command executed by popen() */
		return 0;
	}
	return 1;
}

int askGGNU2(int *i, int *j, int color) {
	FILE *fp;
	int status;
	int PATH_MAX = 200;
	char path[PATH_MAX];
	char path2[PATH_MAX];
	char str1[5000] = "";
	char cmd[5000] = "printf \"";
	getGNUcmd(cmd,color);

//    :2
	
	
	//fp = popen("printf \"komi 6.5\\nboardsize 19\\nclear_board\\nplay B D4\\nplay W Q16\\nplay B D17\\nplay W Q3\\nplay B R5\\ngenmove W\\nquit\\n\" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep \"^= [a-zA-Z]\" | cat", "r");
	
	//gtp_printf("\nREF 'printf \"komi 6.5\\nboardsize 19\\nclear_board\\nplay B D4\\nplay W Q16\\nplay B D17\\nplay W Q3\\nplay B R5\\ngenmove W\\nquit\\n\" | /home/jeff/Documents/go/gnugo/interface/gnugo --mode gtp --quiet | grep \"^= [a-zA-Z]\" | cat'");
	//gtp_printf("\ncmd '%s'\n", cmd);
	fp = popen(cmd, "r");
	if (fp == NULL)
		/* Handle error */;


	while (fgets(path, PATH_MAX, fp) != NULL) {
		//printf("answers %s", path);
		//gtp_printf("coucou %s", path);
		strncpy (path2, path + 2, strlen(path)-2);
		//gtp_printf("answers '%s'", path2);
		if (strcmp(path2, "pass")==0 || strcmp(path2, "pass\n")==0 || strcmp(path2, "PASS")==0 || strcmp(path2, "PASS\n")==0){
			//gtp_printf("GNU wants to pass");
			//gtp_success(path2);
			//gtp_success(path2);
			*i = -1;
			*j = -1;
			return 0;
		}
		if (strcmp(path2, "resign") ==0 || strcmp(path2, "resign\n") ==0){
			//gtp_printf("GNU wants to resign");
			//gtp_success(path2);
			//gtp_success(path2);
			*i = -2;
			*j = -2;
			return 0;
		}
		if(color == BLACK){
			strcat(str1,"B ");
		} else {
			strcat(str1,"W ");
		}
		strcat(str1,path2);
		//gtp_printf("coucou-3 '%s'", str1);
		gtp_decode_move(str1, &color, i, j);
		//gtp_printf("\ncouc %i %i %i\n", color, *i, *j);
	}
	/*if (!legal_move(i, j, color))
		return gtp_failure("illegal move");
*/
	//play_move(i, j, color);

	status = pclose(fp);
	if (status == -1) {
		/* Error reported by pclose() */
		
	} else {
		/* Use macros described under wait() to inspect `status' in order
		   to determine success/failure of command executed by popen() */
		return 0;
	}
	return 1;
}

/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
