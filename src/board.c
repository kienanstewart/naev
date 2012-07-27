/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file board.c
 *
 * @brief Deals with boarding ships.
 */

#include "board.h"

#include "naev.h"

#include "log.h"
#include "pilot.h"
#include "player.h"
#include "toolkit.h"
#include "space.h"
#include "rng.h"
#include "economy.h"
#include "hook.h"
#include "damagetype.h"
#include "nstring.h"
#include "tk/toolkit_priv.h"

#define BOARDING_WIDTH  400 /**< Boarding window width. */
#define BOARDING_HEIGHT 350 /**< Boarding window height. */

#define BUTTON_WIDTH     50 /**< Boarding button width. */
#define BUTTON_HEIGHT    30 /**< Boarding button height. */


static int board_stopboard = 0; /**< Whether or not to unboard. */
static int board_boarded   = 0;
static char** board_boarditems;
static int board_nboarditems = 0;

/*
 * prototypes
 */
static void board_stealCreds();
static void board_stealCargo();
static void board_stealFuel();
static void board_stealAmmo();
static int board_trySteal( Pilot *p );
static int board_fail();
static unsigned int board_createWindow( Pilot* boarder, Pilot* target );
static void board_onItemTakePressed( unsigned int wdw, char* wgtname );
static void board_onItemReturnPressed( unsigned int wdw, char* wgtname );
static void board_onAvailableItemSelected( unsigned int wdw, char* wgtname );
static void board_onTakenItemSelected( unsigned int wdw, char* wgtname );
static void board_onBoardingStartPressed( unsigned int wdw, char* wgtname );
static void board_cleanPlayerBoard();
static void board_playerBoardComplete( Pilot* p );
static void player_showBoardFailMessage( int reason );
double board_boardTime( Pilot *p, Pilot *target );

/**
 * @brief Gets if the player is boarded.
 */
int player_isBoarded (void)
{
   return board_boarded;
}


/**
 * @fn void player_board (void)
 *
 * @brief Attempt to board the player's target.
 *
 * Creates the window on success.
 */
void player_board (void)
{
   Pilot *p, *target;
   unsigned int wdw, canboard;
   char c;
   HookParam hparam[2];
   p = player.p;
   target = pilot_get(p->target);
   c = pilot_getFactionColourChar( target );
   if (pilot_isFlag(p, PILOT_BOARDING)) {
      player_message("\erYou are already boarding a target; can't board again right now!");
      return;
   }
   canboard = pilot_canBoard(p);
   if (canboard != BOARD_CANBOARD) {
      player_showBoardFailMessage(canboard);
      return;
   }
   /* We'll recover it if it's the pilot's ex-escort. */
   if (target->parent == PLAYER_ID) {
      /* Try to recover. */
      pilot_dock( target, p, 0 );
      if (pilot_isFlag(target, PILOT_DELETE )) { /* Hack to see if it boarded. */
         player_message("\epYou recover \eg%s\ep into your fighter bay.", target->name);
         return;
      }
   }
   board_boarditems = NULL;
   board_nboarditems = 0;
   /* Is boarded. */
   board_boarded = 1;
   /* Don't unboard. */
   board_stopboard = 0;
   /*
    * run hook if needed
    */
   hparam[0].type       = HOOK_PARAM_PILOT;
   hparam[0].u.lp.pilot = target->id;
   hparam[1].type       = HOOK_PARAM_SENTINEL;
   hooks_runParam( "board", hparam );
   pilot_runHook(target, PILOT_HOOK_BOARD);
   if (board_stopboard) {
      board_boarded = 0;
      return;
   }
   /* show window to give player a choice of what to take */
   player_message("\epBoarding ship \e%c%s\e0.", c, target->name);
   wdw = board_createWindow(p, target);
}


/**
 * @brief Shows a message in the player's flight log for why they cannot board a target
 */
void player_showBoardFailMessage(int reason)
{
   if (reason == BOARD_NOTARGET)
      player_message("\erNo target selected.");
   else if (reason == BOARD_NOBOARD)
      player_message("\erTarget is not boardable.");
   else if (reason == BOARD_BOARDED)
      player_message("\erTarget has already been boarded.");
   else if (reason == BOARD_NOTDISABLED)
      player_message("\erTarget is not disabled.");
   else if (reason == BOARD_DISTANCE)
      player_message("\erToo far away from target to board it.");
   else if (reason == BOARD_SPEED)
      player_message("\erGoing too fast to board target.");
   else if (reason == BOARD_BOARDING)
      player_message("\erTarget is already being boarded.");
   else
      WARN("Unknown reason to not board target: %d", reason);
}


/**
 * @brief Creates the boarding window
 *
 *     @param Pilot* boarder, the boarding pilot (unused for the moment)
 *     @param Pilot* target, the pilot being boarded
 *     @@return int, the id of the window created
 */
static unsigned int board_createWindow( Pilot* boarder, Pilot* target )
{
   (void)boarder;
   unsigned int wdw;
   int lootcount;
   char** lootable_items = pilot_getLootableItems(target, &lootcount);
   wdw = window_create("Boarding", -1, -1, BOARDING_WIDTH, BOARDING_HEIGHT);
   window_addList(wdw, 20, -40, 150, 250, "AvailableItems", lootable_items, 
                  lootcount, 0, board_onAvailableItemSelected);
   window_addButton(wdw, 185, -120, 30, 30, "TakeItemButton",
                    ">", board_onItemTakePressed);
   window_addButton(wdw, 185, -165, 30, 30, "ReturnItemButton",
                    "<", board_onItemReturnPressed);
   window_disableButton(wdw, "ReturnItemButton");
   window_addList(wdw, 230, -40, 150, 250, "TakenItems", NULL,
                  0, -1, board_onTakenItemSelected);
   window_addButton(wdw, 20, -300, 130, 30, "BoardStart", 
                    "Start Boarding", board_onBoardingStartPressed);
   window_addButton(wdw, BOARDING_WIDTH-80, -300, 60, 30,
                    "BoardCancel", "Cancel", board_exit);
   return wdw;
}

void board_onAvailableItemSelected( unsigned int wdw, char* wgtname )
{
   /* @todo: display more info about selected stuff? */
   (int)wdw;
   (void) wgtname;
}


void board_onTakenItemSelected( unsigned int wdw, char* wgtname )
{
   (int)wdw;
   (void) wgtname;
}


/**
 * @brief Callback that starts the player's boarding process
 *
 *    @param unsigned int wdw, the id of the window
 *    @param char* wgtname, the name of the wiget which called this (unused)
 */
void board_onBoardingStartPressed( unsigned int wdw, char* wgtname )
{
   (void) wgtname;
   Pilot *p, *target;
   Widget *takenItemsList;
   int i, itemlength;
   p = player.p;
   target = pilot_get(p->target);
   if (target == NULL) {
      window_destroy(wdw);
      return;
   }
   if (board_stopboard) {
      board_boarded = 0;
      window_destroy(wdw);
      return;
   }
   takenItemsList = window_getwgt(wdw, "TakenItems");
   if (takenItemsList != NULL) {
      board_nboarditems = takenItemsList->dat.lst.noptions;
      if (board_nboarditems > 0) {
         board_boarditems = malloc(board_nboarditems*sizeof(char*));
         for (i=0; i<board_nboarditems; i++) {
            itemlength = strlen(takenItemsList->dat.lst.options[i]);
            board_boarditems[i] = calloc((itemlength+2), sizeof(char));
            nsnprintf(board_boarditems[i], itemlength+1, "%s", takenItemsList->dat.lst.options[i]);
         }
      }
      else {
         board_nboarditems = 0;
         player_message("\erNo items selected to loot from boarding.");
      }
   }
   /* start the pilot boarding timer */
   if (pilot_board(p))
      player_message("Boarding started! Time left: %f", p->ptimer);
   window_destroy(wdw);
}


/**
 * @brief Callback for when an available item is selected by the player to be taken
 *
 *    @param unsigned int wdw, the id of the window from whence the callback came
 *    @param char* wgtname, the name of the widget that called the callback (unused)
 */
void board_onItemTakePressed( unsigned int wdw, char* wgtname )
{
   (void) wgtname;
   Widget* availableListWidget, *takenListWidget;
   char* selectedItem;
   int selectedItemIndex;
   availableListWidget = window_getwgt(wdw, "AvailableItems");
   if (availableListWidget == NULL)
      return;
   takenListWidget = window_getwgt(wdw, "TakenItems");
   if (takenListWidget == NULL)
      return;
   selectedItemIndex = availableListWidget->dat.lst.selected;
   if (selectedItemIndex == -1)
      return;
   selectedItem = lst_remove(wdw, "AvailableItems", selectedItemIndex);
   if (selectedItem == NULL)
      return;
   lst_add(wdw, "TakenItems", selectedItem);
   /* if there are no more items available to take, disable the button */
   if (availableListWidget->dat.lst.noptions <= 0)
      window_disableButton(wdw, "TakeItemButton");
   /* make sure the GiveButton is enabled again */
   window_enableButton(wdw, "ReturnItemButton");
}


/**
 * @brief Callback for returning an taken item to available pool.
 *
 *    @param unsigned int wdw, the id of the window where the callback was triggered from
 *    @param char* wgtname, the name of the widget that caused the callback (unused)
 */
void board_onItemReturnPressed( unsigned int wdw, char* wgtname )
{
   (void) wgtname;
   Widget* availableListWidget, *takenListWidget;
   char* selectedItem;
   int selectedItemIndex;
   takenListWidget = window_getwgt(wdw, "TakenItems");
   if (takenListWidget == NULL)
      return;
   availableListWidget = window_getwgt(wdw, "AvailableItems");
   if (availableListWidget == NULL)
      return;
   selectedItemIndex = takenListWidget->dat.lst.selected;
   if (selectedItemIndex == -1)
      return;
   selectedItem = lst_remove(wdw, "TakenItems", selectedItemIndex);
   if (selectedItem == NULL)
      return;
   lst_add(wdw, "AvailableItems", selectedItem);
   if (takenListWidget->dat.lst.noptions <= 0)
      window_disableButton(wdw, "ReturnItemButton");
   window_enableButton(wdw, "TakeItemButton");
}


/**
 * @brief Forces unboarding of the pilot.
 */
void board_unboard (void)
{
   board_stopboard = 1;
}


/**
 * @brief Closes the boarding window.
 *
 *    @param wdw Window triggering the function.
 *    @param str Unused.
 */
void board_exit( unsigned int wdw, char* str )
{
   (void) str;
   window_destroy( wdw );

   /* Is not boarded. */
   board_boarded = 0;
}


/**
 * @brief Steal the boarded ship's credits, if able.
 */
static void board_stealCreds()
{
   Pilot* p;
   p = pilot_get(player.p->target);
   if (p->credits==0) { /* you can't steal from the poor */
      player_message("\epThe ship has no credits.");
      return;
   }

   player_modCredits( p->credits );
   p->credits = 0;
   player_message("\epYou manage to steal the ship's credits.");
}


/**
 * @brief Steal the boarded ship's cargo, if able.
 */
static void board_stealCargo()
{
   int q;
   Pilot* p;
   p = pilot_get(player.p->target);
   if (p->ncommodities==0) { /* no cargo */
      player_message("\epThe ship has no cargo.");
      return;
   }
   else if (pilot_cargoFree(player.p) <= 0) {
      player_message("\erYou have no room for the ship's cargo.");
      return;
   }

   /** steal as much as possible until full - @todo let player choose */
   q = 1;
   while ((p->ncommodities > 0) && (q!=0)) {
      q = pilot_cargoAdd( player.p, p->commodities[0].commodity,
            p->commodities[0].quantity );
      pilot_cargoRm( p, p->commodities[0].commodity, q );
   }

   player_message("\epYou manage to steal the ship's cargo.");
}


/**
 * @brief Steal the boarded ship's fuel, if able.
 */
static void board_stealFuel()
{
   Pilot* p;
   p = pilot_get(player.p->target);
   if (p->fuel <= 0.) { /* no fuel. */
      player_message("\epThe ship has no fuel.");
      return;
   }
   else if (player.p->fuel == player.p->fuel_max) {
      player_message("\erYour ship is at maximum fuel capacity.");
      return;
   }

   /* Steal fuel. */
   player.p->fuel += p->fuel;
   p->fuel = 0.;

   /* Make sure doesn't overflow. */
   if (player.p->fuel > player.p->fuel_max) {
      p->fuel      = player.p->fuel - player.p->fuel_max;
      player.p->fuel = player.p->fuel_max;
   }

   player_message("\epYou manage to steal the ship's fuel.");
}


/**
 * @brief Steal the ships ammo, if able
 */
static void board_stealAmmo()
{
     Pilot* p;
     int nreloaded, i, nammo, x;
     PilotOutfitSlot *target_outfit_slot, *player_outfit_slot;
     Outfit *target_outfit, *ammo, *player_outfit;
     nreloaded = 0;
     p = pilot_get(player.p->target);
     /* Target has no ammo */
     if (pilot_countAmmo(p) <= 0) {
        player_message("\erThe ship has no ammo.");
        return; 
     }
     /* Player is already at max ammo */
     if (pilot_countAmmo(player.p) >= pilot_maxAmmo(player.p)) {
        player_message("\erYou are already at max ammo.");
        return;
     }
     /* Steal the ammo */
     for (i=0; i<p->noutfits; i++) {
        target_outfit_slot = p->outfits[i];
        if (target_outfit_slot == NULL)
           continue;
        target_outfit = target_outfit_slot->outfit;
        if (target_outfit == NULL)
           continue;
        /* outfit isn't a launcher */
        if (!outfit_isLauncher(target_outfit)) {
           continue;
        }
        nammo = target_outfit_slot->u.ammo.quantity;
        ammo = target_outfit_slot->u.ammo.outfit;
        /* launcher has no ammo */
        if (ammo == NULL)
           continue;
        if (nammo <= 0) {
           continue;
        }
        for (x=0; x<player.p->noutfits; x++) {
           int nadded = 0;
           player_outfit_slot = player.p->outfits[x];
           if (player_outfit_slot == NULL)
              continue;
           player_outfit = player_outfit_slot->outfit;
           if (player_outfit == NULL)
              continue;
           if (!outfit_isLauncher(player_outfit)) {
              continue;
           }
           if (strcmp(ammo->name, player_outfit_slot->u.ammo.outfit->name) != 0) {
              continue;
           }
           /* outfit's ammo matches; try to add to player and remove from target */
           nadded = pilot_addAmmo(player.p, player_outfit_slot, ammo, nammo);
           nammo -= nadded;
           pilot_rmAmmo(p, target_outfit_slot, nadded);
           nreloaded += nadded;
           if (nadded > 0)
              player_message("\epYou looted %d %s(s)", nadded, ammo->name);
           if (nammo <= 0) {
              break;
           }
        }
        if (nammo <= 0) {
           continue;
        }
     }
     if (nreloaded <= 0)
        player_message("\erThere is no ammo compatible with your launchers on board.");
     pilot_updateMass(player.p);
     pilot_weaponSane(player.p);
     pilot_updateMass(p);
     pilot_weaponSane(p);
}


/**
 * @brief Checks to see if the pilot can steal from its target.
 *
 *    @param p Pilot stealing from its target.
 *    @return 0 if successful, 1 if fails, -1 if fails and kills target.
 */
static int board_trySteal( Pilot *p )
{
   Pilot *target;
   Damage dmg;

   /* Get the target. */
   target = pilot_get(p->target);
   if (target == NULL)
      return 1;

   /* See if was successful. */
   if (RNGF() > (0.5 * (10. + target->crew)/(10. + p->crew)))
      return 0;

   /* Triggered self destruct. */
   if (RNGF() < 0.4) {
      /* Don't actually kill. */
      target->shield = 0.;
      target->armour = 1.;
      /* This will make the boarding ship take the possible faction hit. */
      dmg.type        = dtype_get("normal");
      dmg.damage      = 100.;
      dmg.penetration = 1.;
      dmg.disable     = 0.;
      pilot_hit( target, NULL, p->id, &dmg );
      /* Return ship dead. */
      return -1;
   }

   return 1;
}


/**
 * @brief Checks to see if the hijack attempt failed.
 *
 *    @return 1 on failure to board, otherwise 0.
 */
static int board_fail()
{
   int ret;

   ret = board_trySteal( player.p );

   if (ret == 0)
      return 0;
   else if (ret < 0) /* killed ship. */
      player_message("\epYou have tripped the ship's self-destruct mechanism!");
   else /* you just got locked out */
      player_message("\epThe ship's security system locks %s out.",
            (player.p->ship->crew > 0) ? "your crew" : "you" );

   return 1;
}


/**
 * @brief Clears up the static boarding information for a player who has been boarding
 * @note This frees board items.
 */
static void board_cleanPlayerBoard()
{
   int i;
   board_boarded = 0;
   for (i=0; i<board_nboarditems; i++) 
      free(board_boarditems[i]);
   board_nboarditems = 0;
   free(board_boarditems);
   board_boarditems = NULL;
}


/**
 * @brief completes the boarding process for pilot p
 * @note This is where stuff is stolen and the attempt to steal is made (and can fail).
 * @note Behaviour differs between depending whether or not the pilot is a player
 *
 *    @param The pointer to the pilot doing the boarding
 */
void board_playerBoardComplete( Pilot* p )
{
   Pilot* target;
   int i;
   char * current_item;
   target = pilot_get(p->target);
   if (target == NULL)
      return;
   pilot_setFlag(target, PILOT_BOARDED);
   if(!board_fail()) {
      for (i=0; i<board_nboarditems; i++) {
         current_item = board_boarditems[i];
         if (strcmp(current_item, "Ammo") == 0)
            board_stealAmmo();
         else if (strcmp(current_item, "Commodities") == 0)
            board_stealCargo();
         else if (strcmp(current_item, "Fuel") == 0)
            board_stealFuel();
         else if (strcmp(current_item, "Credits") == 0)
            board_stealCreds();
         else
            WARN("Unknown boarding item: %s", current_item);
      }
   }
   board_cleanPlayerBoard();
}


/**
 * @brief Has a pilot attempt to board another pilot.
 *
 *    @param p Pilot doing the boarding.
 *    @return 1 if target was boarded.
 */
int pilot_board( Pilot *p )
{
   if (pilot_canBoard(p) != BOARD_CANBOARD)
      return 0;
   if (pilot_isFlag(p, PILOT_BOARDING))
       return 0;
   pilot_setFlag(p, PILOT_BOARDING);
   /* Set time it takes to board. */
   p->ptimer = board_boardTime(p, pilot_get(p->target));
   return 1;
}


/**
 * @brief Checks the board conditions for a boarding pilot. This function should be called on every pilot update
 *    while the pilot is boarding. Boarding will be cancelled if the reason returned is not BOARD_CANBOARD
 *
 *    @param p, the pointer to the Pilot performing the boarding
 */
void pilot_boardUpdate( Pilot *p )
{
   int failReason = pilot_canBoard(p);
   if (failReason != BOARD_CANBOARD)
      pilot_boardCancel(p, failReason);
}


/**
 * @brief Cancels an ongoing board process. If the reason is BOARD_NOTDISABLED, the pilot is stunned for 1s.
 * @note If the pilot is the player a message is shown in the flight log and the boarding information cleared
 *
 *    @param p, a pointer to the pilot (boarder) whose action is being cancelled
 *    @param reason, an int which should be from the BOARD_ enum.
 */
void pilot_boardCancel( Pilot* p, int reason )
{
   if (reason == BOARD_CANBOARD)
      return;
   if (reason == BOARD_NOTDISABLED) {
      /* target ship recovered, stun the current pilot for 1s */
      pilot_setFlag(p, PILOT_DISABLED);
      p->dtimer = 1.;
      p->dtimer_accum = 0.;
   }
   p->ptimer = 0.;
   pilot_rmFlag(p, PILOT_BOARDING);
   if (pilot_isPlayer(p)) {
      if (reason == BOARD_NOTARGET)
         player_message("\erThere is not longer a selected target.");
      else if (reason == BOARD_NOBOARD)
         player_message("\erThe current target is no longer boardable.");
      else if (reason == BOARD_BOARDED)
         player_message("\erThe current target has now been boarded (by someone else perhaps).");
      else if (reason == BOARD_NOTDISABLED)
         player_message("\erThe target is no longer disabled, blows the boarding tubes, and flies off, stunning you in the process.");
      else if (reason == BOARD_DISTANCE)
         player_message("\erYou are now too far away to continue boarding.");
      else if (reason == BOARD_SPEED)
         player_message("\erYou are now going too fast to continue boarding.");
      else if (reason == BOARD_COOLDOWNSTART)
         player_message("\erStarting the active cooldown interrupted the boarding process.");
      else
         player_message("\erUnknown reason for boarding termination: %d", reason);
      board_cleanPlayerBoard();
   }
}


/**
 * @brief Checks if the pilot, p, can board it's current target
 * @note does not check if the pilot is currently boarding
 *
 *    @param p, the pointer to the pilot
 *    @@return int, the code for the reason why, BOARD_CANBOARD indicates it is possible
 */
int pilot_canBoard( Pilot *p )
{
   Pilot *target = pilot_get(p->target);
   if (target == NULL)
      return BOARD_NOTARGET;
   else if (p == player.p && target->id == PLAYER_ID)
      return BOARD_NOTARGET;
   else if (pilot_isFlag(target, PILOT_NOBOARD))
      return BOARD_NOBOARD;
   else if (pilot_isFlag(target, PILOT_BOARDED))
      return BOARD_BOARDED;
   else if (!pilot_isDisabled(target) && !pilot_isFlag(target, PILOT_BOARDABLE))
      return BOARD_NOTDISABLED;
   else if (vect_dist(&p->solid->pos, &target->solid->pos) >
       p->ship->gfx_space->sw * PILOT_SIZE_APROX) {
      return BOARD_DISTANCE;
   }
   else if ((pow2(VX(p->solid->vel)-VX(target->solid->vel)) +
             pow2(VY(p->solid->vel)-VY(target->solid->vel))) >
         (double)pow2(MAX_HYPERSPACE_VEL)) {
      return BOARD_SPEED;
   }
   return BOARD_CANBOARD;
}


/**
 * @brief Finishes the boarding.
 *
 *    @param p Pilot to finish the boarding.
 */
void pilot_boardComplete( Pilot *p )
{
   Pilot *target;
   credits_t worth;
   char creds[ ECON_CRED_STRLEN ];

   /* Finish the boarding. */
   pilot_rmFlag(p, PILOT_BOARDING);

   /* Make sure target is sane. */
   target = pilot_get(p->target);
   if (target == NULL)
      return;
   /* In the case of the player take fewer credits. Also if an NPC boards another NPC */
   if (pilot_isPlayer(target) || (!pilot_isPlayer(p) && !pilot_isPlayer(target))) {
      worth = MIN( 0.1*pilot_worth(target), target->credits );
      p->credits       += worth;
      target->credits  -= worth;
      credits2str( creds, worth, 2 );
      if (pilot_isPlayer(target)) {
         player_message( "\e%c%s\e0 has plundered %s credits from your ship!",
                         pilot_getFactionColourChar(p), p->name, creds );
      }
   }
   else {
      board_playerBoardComplete(p);
   }
}


/**
 * @brief Gets the time it should take to board another ship.
 * @note The time taken is relative to the crew sizes between the ships. 
      A small ship boarding a large ship will take a long time, and 
      a large ship boarding a small ship will take a short time.
 *     
 *    @param p Pilot that is doing the boarding
 *    @param target Pilot that is being boarded
 *    @return double, the time it will take. If the time is < 0.0f then it should be considered an error
 * 
 */
double board_boardTime( Pilot *p, Pilot *target )
{
   double boardtime; 
   if (p == NULL)
      return -1.;
   if (target == NULL)
      return -1.;
   boardtime = exp(target->crew / p->crew);
   /* No longer than BOARD_MAXTIME */
   boardtime = (boardtime > BOARD_MAXTIME) ? BOARD_MAXTIME : boardtime;
   /* No shorter than BOARD_MINTIME */
   boardtime = (boardtime < BOARD_MINTIME) ? BOARD_MINTIME : boardtime;
   return boardtime;
}
