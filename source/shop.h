#pragma once
// The game's shops (ShopOwner, ShopItem, the Shop Menu prefab's shop_control): the stocks of text/shops.txt
// (tools/build_shops.py), their icons (ui/shop_items.sss). An NPC's script asks for its ShopObject and sends it
// SHOP UP: the menu opens over the top screen with the items still for sale (ShopItem.IsAvailable); a purchase takes
// the rosaries or shell shards and sets the item's PlayerData bools (ShopItem.SetPurchased). Closing it broadcasts
// SHOP CLOSED; with nothing left to sell, SHOP UP sends SHOP NO STOCK to the event register instead.
#include "common.h"

void shop_init(void);
// the stock of the room's object of that name (its ShopOwner), -1 if it has none
int shop_of(const char *scene, const char *object);
bool shop_has_stock(int stock, bool not_infinite);   // ShopMenuStock.StockLeft / StockLeftNotInfinite
// an event sent to the shop menu object by the script of node `owner` (SHOP UP opens it)
void shop_event(const char *ev, int owner);
void shop_select(int stock);          // GetShopObject: the stock the menu will show
bool shop_active(void);
void shop_update(u32 down, u32 held, float dt);
void shop_draw(void);                 // over the top screen
bool shop_was_purchased(void);        // ShopWasItemPurchased: something bought the last time it was open
// a PlayerDataTest as tools/build_shops.py writes it (groups split by '|', tests by ','): true when a group passes
bool pd_conditions(const char *tests);
