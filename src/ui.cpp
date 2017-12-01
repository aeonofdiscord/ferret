#include "ui.h"

void MenuItem::addMenu(Menu* menu)
{
	widgets.push_back(menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(handle), menu->handle);
}
