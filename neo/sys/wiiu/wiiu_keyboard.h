// The Wii U's software keyboard (nn::swkbd), for GUI edit fields like the
// savegame name, see idEditWindow

#ifndef __WIIU_KEYBOARD_H__
#define __WIIU_KEYBOARD_H__

class idStr;

// shows the keyboard for the edit field "owner" (unless it's already open)
void	WiiU_KeyboardOpen( const void *owner, const char *text, int maxChars, bool numeric );
// true once if the keyboard opened for owner was closed with OK, text is what was entered
bool	WiiU_KeyboardPoll( const void *owner, idStr &text );

// for gx2_glimp.cpp, every frame: handle input, then draw it if it's visible
void	WiiU_KeyboardUpdate( void );
bool	WiiU_KeyboardVisible( void );
void	WiiU_KeyboardDrawTV( void );
void	WiiU_KeyboardDrawDRC( void );
void	WiiU_KeyboardShutdown( void );

#endif
