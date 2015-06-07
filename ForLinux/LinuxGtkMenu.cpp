
#include <algorithm>
#include <assert.h>
#include "LinuxGtkMenu.h"

// TODO: Raise exception on allocation failures.
// TODO: Consider gtk_check_menu_item_get_draw_as_radio()


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//    MENU BAR
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


        // A menu bar contains menus.



LinuxGtkMenuBar::LinuxGtkMenuBar()
{
    _menuBar = gtk_menu_bar_new();
}



std::shared_ptr<LinuxGtkMenu>  LinuxGtkMenuBar::CreateMenu( const char *optionText, InterfaceForMenuItemClickHandling *eventHandler )
{
    auto newSubMenu = std::make_shared<LinuxGtkMenu>( _menuBar, optionText, eventHandler );
    _subMenus.push_back( newSubMenu );
    return newSubMenu;
}



void LinuxGtkMenuBar::EnableMenuItem( uint32_t menuID, bool isEnabled )
{
    for( auto &thisSubMenu : _subMenus )
    {
        thisSubMenu->EnableMenuItem( menuID, isEnabled );
    }
}



void LinuxGtkMenuBar::CheckMenuItem(  uint32_t menuID, bool isChecked )
{
    for( auto &thisSubMenu : _subMenus )
    {
        thisSubMenu->CheckMenuItem( menuID, isChecked );
    }
}





// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//    MENU
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

LinuxGtkMenu::LinuxGtkMenu( GtkWidget *menubar, const char *optionText, InterfaceForMenuItemClickHandling *eventHandler )
{
    _onClickEventHandler = eventHandler;
    _menuObject = gtk_menu_new();
    _barButton  = gtk_menu_item_new_with_label( StripAccelerators(optionText).c_str() );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM(_barButton), _menuObject );
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), _barButton);
}



void LinuxGtkMenu::AddItem( const char *optionText, uint32_t menuID )
{
    auto newItemWidget = gtk_menu_item_new_with_label( StripAccelerators(optionText).c_str() );
    AppendItem( newItemWidget, menuID );
}



void LinuxGtkMenu::AddTick( const char *optionText, uint32_t menuID )
{
    auto newItemWidget = gtk_check_menu_item_new_with_label( StripAccelerators(optionText).c_str() );
    AppendItem( newItemWidget, menuID );
}



void LinuxGtkMenu::AppendItem( GtkWidget *menuItemWidget, uint32_t menuID )
{
    gtk_menu_shell_append( GTK_MENU_SHELL(_menuObject), menuItemWidget );
    g_signal_connect( G_OBJECT(menuItemWidget), "activate", G_CALLBACK(&LinuxGtkMenu::GenericMenuItemHandler), this );
    _widgetToMenuItemIdMap.insert( std::make_pair(menuItemWidget, menuID) );
    _menuItemIdMapToWidget.insert( std::make_pair(menuID, menuItemWidget) );
}



void LinuxGtkMenu::AddSeparator()
{
    auto newSeparatorWidget = gtk_separator_menu_item_new();
    gtk_menu_shell_append( GTK_MENU_SHELL(_menuObject), newSeparatorWidget );
}



std::string  LinuxGtkMenu::StripAccelerators( const char *optionText )
{
    // Strip Microsoft Window accelerator characters from the menus.
    // TODO: Make install these accelerators in GTK
    std::string  text( optionText );
    text.erase(
        std::remove_if( text.begin(), text.end(), []( char ch ){ return ch == '&'; } ),
        text.end() );
    return text;
}



void LinuxGtkMenu::GenericMenuItemHandler( GtkWidget *menuItemWidget, gpointer thisMenuObject ) // static member
{
    auto thisObject = (LinuxGtkMenu *) thisMenuObject;
    thisObject->MenuItemClicked( menuItemWidget );
}



void LinuxGtkMenu::MenuItemClicked( GtkWidget *menuItemWidget )
{
    // Look it up in the map to get the Microsoft-style ID number of the menu item.
    // The delegate the call to the external handler.

    auto foundAt = _widgetToMenuItemIdMap.find( menuItemWidget );
    if( foundAt != _widgetToMenuItemIdMap.end() )
    {
        _onClickEventHandler->NotifyMenuItemClicked( foundAt->second );
    }
    else assert(false);
}



GtkWidget *LinuxGtkMenu::GetWidgetByID( uint32_t menuID ) const
{
    auto foundAt = _menuItemIdMapToWidget.find( menuID );
    if( foundAt != _menuItemIdMapToWidget.end() )
    {
        return foundAt->second;
    }

    assert(false);
    return nullptr; // ID not known.
}



void LinuxGtkMenu::EnableMenuItem( uint32_t menuID, bool isEnabled )
{
    auto newItemWidget = GetWidgetByID( menuID );
    if( newItemWidget != nullptr )
    {
        gtk_widget_set_sensitive( newItemWidget, isEnabled ? TRUE : FALSE );
    }
}



void LinuxGtkMenu::CheckMenuItem( uint32_t menuID, bool isChecked )
{
    auto newItemWidget = GetWidgetByID( menuID );
    if( newItemWidget != nullptr )
    {
        gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM(newItemWidget), isChecked ? TRUE : FALSE );
    }
}



