/*
 * See Licensing and Copyright notice in naev.h
 */


#ifndef BOARD_H
#  define BOARD_H


#include "pilot.h"

enum {
   BOARD_CANBOARD, /* can board */
   BOARD_NOTARGET, /* Nothing targeted */
   BOARD_NOBOARD, /* Target is not boardabled */
   BOARD_BOARDED, /* Target has been boarded already */
   BOARD_NOTDISABLED, /* Target is not disabled */
   BOARD_DISTANCE, /* Target is too far away */
   BOARD_SPEED, /* Going too fast */
   BOARD_BOARDING /* Already boarding a target */
};

int player_isBoarded (void);
void player_board (void);
void board_unboard (void);
int pilot_board( Pilot *p );
int pilot_canBoard( Pilot *p);
void pilot_boardUpdate( Pilot *p );
void pilot_boardComplete( Pilot *p );
void pilot_boardCancel( Pilot *p, int reason );
void board_exit( unsigned int wdw, char* str );

#endif
