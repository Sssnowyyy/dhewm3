/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

// Wii U (Cafe OS, built with devkitPPC + wut) system layer.
// Replaces sys/posix/posix_main.cpp and sys/linux/main.cpp, which depend on
// fork/exec, signals, termios and dlopen - none of which exist on the Wii U.

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <malloc.h>

#include <coreinit/time.h>
#include <coreinit/thread.h>
#include <coreinit/memory.h>
#include <sysapp/launch.h>
#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>

#include "sys/platform.h"
#include "idlib/containers/StrList.h"
#include "framework/FileSystem.h"
#include "framework/Licensee.h"
#include "sys/sys_local.h"
#include "sys/sys_sdl.h"

// game data, config and savegames all live here on the SD card:
//   sd:/wiiu/apps/dhewm3/base/pak000.pk4 ...
#define WIIU_DATA_PATH "fs:/vol/external01/wiiu/apps/dhewm3"

idCVar com_pid( "com_pid", "0", CVAR_INTEGER | CVAR_INIT | CVAR_SYSTEM, "process id" );

static FILE* consoleLog = NULL;
static OSTime lastLogSync = 0;
static OSThread* engineThread = NULL;
static bool procExiting = false; // ProcUI told us to exit (HOME menu "close software")

static void Wiiu_SyncLog() {
	if ( consoleLog != NULL ) {
		fflush( consoleLog );
		fsync( fileno( consoleLog ) );
		lastLogSync = OSGetSystemTime();
	}
}

/*
================
Wiiu_Exit

Cafe OS apps can't just exit() while ProcUI is running: unless ProcUI already
told us to exit, ask the system to go back to the menu and keep processing
ProcUI messages until it releases us.
The engine runs on its own thread (see main()), so end that thread and let
main() return instead of calling exit() from it.
================
*/
static void Wiiu_Exit( int ret ) {
	if ( consoleLog != NULL ) {
		fclose( consoleLog );
		consoleLog = NULL;
	}

	// common->Shutdown() already did this unless we're here because of Sys_Error()
	SDL_Quit();

	if ( !procExiting ) {
		SYSLaunchMenu();
		while ( WHBProcIsRunning() ) {
			OSSleepTicks( OSMillisecondsToTicks( 10 ) );
		}
	}
	WHBProcShutdown();

	if ( OSGetCurrentThread() == engineThread ) {
		OSExitThread( ret );
	}
	exit( ret );
}

void idSysLocal::StartProcess( const char *exeName, bool quit ) {
	common->DPrintf( "Sys_StartProcess %s - not supported on Wii U\n", exeName );
	if ( quit ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
	}
}

void idSysLocal::OpenURL( const char *url, bool quit ) {
	common->Printf( "OpenURL %s - not supported on Wii U\n", url );
	if ( quit ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
	}
}

void Sys_Quit( void ) {
	Wiiu_Exit( EXIT_SUCCESS );
}

void Sys_Mkdir( const char *path ) {
	mkdir( path, 0777 );
}

int Sys_ListFiles( const char *directory, const char *extension, idStrList &list ) {
	struct dirent *d;
	DIR *fdir;
	bool dironly = false;
	char search[MAX_OSPATH];
	struct stat st;
	bool debug;

	list.Clear();

	debug = cvarSystem->GetCVarBool( "fs_debug" );

	if ( !extension )
		extension = "";

	// passing a slash as extension will find directories
	if ( extension[0] == '/' && extension[1] == 0 ) {
		extension = "";
		dironly = true;
	}

	if ( ( fdir = opendir( directory ) ) == NULL ) {
		if ( debug ) {
			common->Printf( "Sys_ListFiles: opendir %s failed\n", directory );
		}
		return -1;
	}

	while ( ( d = readdir( fdir ) ) != NULL ) {
		idStr::snPrintf( search, sizeof( search ), "%s/%s", directory, d->d_name );
		if ( stat( search, &st ) == -1 )
			continue;
		if ( !dironly ) {
			idStr look( search );
			idStr ext;
			look.ExtractFileExtension( ext );
			if ( extension[0] != '\0' && ext.Icmp( &extension[1] ) != 0 ) {
				continue;
			}
		}
		if ( ( dironly && !S_ISDIR( st.st_mode ) ) ||
			( !dironly && S_ISDIR( st.st_mode ) ) )
			continue;

		list.Append( d->d_name );
	}

	closedir( fdir );

	if ( debug ) {
		common->Printf( "Sys_ListFiles: %d entries in %s\n", list.Num(), directory );
	}

	return list.Num();
}

bool Sys_GetPath( sysPath_t type, idStr &path ) {
	switch ( type ) {
	case PATH_BASE:
	case PATH_CONFIG:
	case PATH_SAVE:
		path = WIIU_DATA_PATH;
		return true;
	case PATH_EXE:
		path = WIIU_DATA_PATH "/dhewm3.rpx";
		return true;
	}

	path.Clear();
	return false;
}

void Sys_Init( void ) {
	if ( consoleLog != NULL )
		common->Printf( "Logging console output to " WIIU_DATA_PATH "/dhewm3log.txt\n" );

	com_pid.SetInteger( 0 );
	common->Printf( "%d MB System Memory\n", Sys_GetSystemRam() );
}

void Sys_Shutdown( void ) {
}

// returns in megabytes
int Sys_GetSystemRam( void ) {
	// wut hands the whole MEM2 heap to malloc() at startup, so asking the heap
	// how much is free returns 0 - report the size of the app's MEM2 area instead
	uint32_t addr = 0, size = 0;
	if ( OSGetMemBound( OS_MEM2, &addr, &size ) != 0 ) {
		return 512;
	}
	int mb = (int)( size / ( 1024 * 1024 ) );
	// round to the nearest 16Mb
	return ( mb + 8 ) & ~15;
}

// everything is hardlinked (HARDLINK_GAME), there are no game DLLs to load
uintptr_t Sys_DLL_Load( const char *path ) {
	return 0;
}

void* Sys_DLL_GetProcAddress( uintptr_t handle, const char *sym ) {
	return NULL;
}

void Sys_DLL_Unload( uintptr_t handle ) {
}

void Sys_ShowConsole( int visLevel, bool quitOnClose ) { }

ID_TIME_T Sys_FileTimeStamp( FILE * fp ) {
	struct stat st;
	if ( fstat( fileno( fp ), &st ) != 0 )
		return 0;
	return st.st_mtime;
}

// no clipboard on the Wii U
char *Sys_GetClipboardData( void ) {
	return NULL;
}

void Sys_FreeClipboardData( char* data ) {
}

void Sys_SetClipboardData( const char *string ) {
}

bool Sys_LockMemory( void *ptr, int bytes ) {
	return true;
}

bool Sys_UnlockMemory( void *ptr, int bytes ) {
	return true;
}

void Sys_SetPhysicalWorkMemory( int minBytes, int maxBytes ) {
}

int Sys_GetDriveFreeSpace( const char *path ) {
	return 1000 * 1024;
}

char *Sys_ConsoleInput( void ) {
	return NULL;
}

// ---------- Time Stuff -------------

static OSTime firstTicks;

static void Wiiu_InitTime() {
	// set back first by 1.5ms so neither Sys_MillisecondsPrecise() nor Sys_Milliseconds()
	// will ever return 0 or even a negative value
	firstTicks = OSGetSystemTime() - OSMicrosecondsToTicks( 1500 );
}

double Sys_MillisecondsPrecise() {
	OSTime diff = OSGetSystemTime() - firstTicks;
	return (double)diff * 1000.0 / (double)OSTimerClockSpeed;
}

void Sys_SleepUntilPrecise( double targetTimeMS ) {
	double msec = targetTimeMS - Sys_MillisecondsPrecise();
	if ( msec < 0.01 ) // don't bother for less than 10usec
		return;

	// sleep for everything but the last 100usec, busy-wait the rest
	static const double sleepThreshold = 0.100;
	if ( msec > sleepThreshold ) {
		OSSleepTicks( OSMicrosecondsToTicks( (int64_t)( ( msec - sleepThreshold ) * 1000.0 ) ) );
	}

	do {
		msec = targetTimeMS - Sys_MillisecondsPrecise();
	} while ( msec >= 0.01 );
}

/*
===============
low level output
===============
*/

void Sys_VPrintf( const char *msg, va_list arg ) {
	char buf[4096];
	idStr::vsnPrintf( buf, sizeof( buf ), msg, arg );

	WHBLogWrite( buf );

	if ( consoleLog != NULL ) {
		fputs( buf, consoleLog );
		// a crash would lose whatever is still buffered, but syncing the SD card
		// for every line slows down loading, so do it at most once per second
		// (Cemu doesn't even write anything to the host file before that)
		if ( OSGetSystemTime() - lastLogSync > OSSecondsToTicks( 1 ) ) {
			Wiiu_SyncLog();
		}
	}
}

void Sys_DebugPrintf( const char *fmt, ... ) {
	va_list argptr;
	va_start( argptr, fmt );
	Sys_VPrintf( fmt, argptr );
	va_end( argptr );
}

void Sys_DebugVPrintf( const char *fmt, va_list arg ) {
	Sys_VPrintf( fmt, arg );
}

void Sys_Printf( const char *msg, ... ) {
	va_list argptr;
	va_start( argptr, msg );
	Sys_VPrintf( msg, argptr );
	va_end( argptr );
}

void Sys_Error( const char *error, ... ) {
	va_list argptr;

	Sys_Printf( "Sys_Error: " );
	va_start( argptr, error );
	Sys_DebugVPrintf( error, argptr );
	va_end( argptr );
	Sys_Printf( "\n" );
	Wiiu_SyncLog();

	Wiiu_Exit( EXIT_FAILURE );
}

/*
===============
main
===============
*/

// Doom 3 _alloca()s up to ID_MAX_ALLOCA_SIZE (1MB) at once, but the RPX main
// thread only gets elf2rpl's default 64KB stack - so run the engine on a thread
// with a big stack instead.
#define ENGINE_STACK_SIZE ( 8 * 1024 * 1024 )

static int EngineThreadMain( int argc, const char **argv ) {
	// ProcUI has to be handled on the main core, which this thread runs on
	WHBProcInit();

	// Cafe OS doesn't pass useful arguments to homebrew, so these are the defaults
	// for the Wii U: render at the TV's 720p, the GamePad gets a scaled copy
	static const char *defaultArgs[] = {
		"+set", "r_mode", "-1",
		"+set", "r_customWidth", "1280",
		"+set", "r_customHeight", "720",
		"+set", "r_fullscreen", "1",
	};
	static char *args[64];
	int numArgs = 0;
	for ( size_t i = 0; i < sizeof( defaultArgs ) / sizeof( defaultArgs[0] ); i++ ) {
		args[numArgs++] = (char *)defaultArgs[i];
	}

	// the GamePad is the only input, but dhewm3 has no gamepad bindings by default:
	// on the first start (nothing saved yet) use the ones from base/gamepad.cfg.
	// SDL thinks the GamePad is XBox-like, but its buttons are laid out like Nintendo's
	struct stat st;
	if ( stat( WIIU_DATA_PATH "/" BASE_GAMEDIR "/" CONFIG_FILE, &st ) != 0 ) {
		static const char *firstStartArgs[] = { "+exec", "gamepad.cfg", "+set", "joy_gamepadLayout", "1" };
		for ( size_t i = 0; i < sizeof( firstStartArgs ) / sizeof( firstStartArgs[0] ); i++ ) {
			args[numArgs++] = (char *)firstStartArgs[i];
		}
	}

	// more arguments (like "+map game/mars_city1") can be put into args.txt
	static char extraArgs[1024];
	FILE *f = fopen( WIIU_DATA_PATH "/args.txt", "r" );
	if ( f ) {
		size_t len = fread( extraArgs, 1, sizeof( extraArgs ) - 1, f );
		fclose( f );
		extraArgs[len] = '\0';
		char *tok = strtok( extraArgs, " \t\r\n" );
		while ( tok && numArgs < (int)( sizeof( args ) / sizeof( args[0] ) ) ) {
			args[numArgs++] = tok;
			tok = strtok( NULL, " \t\r\n" );
		}
	}

	common->Init( numArgs, args );

	while ( WHBProcIsRunning() ) {
		common->Frame();
	}

	// the user closed us from the HOME menu
	procExiting = true;
	common->Quit(); // ends in Sys_Quit(), which ends this thread
	return 0;
}

int main( int argc, char **argv ) {
	WHBLogCafeInit();
	WHBLogUdpInit();

	mkdir( WIIU_DATA_PATH, 0777 );
	consoleLog = fopen( WIIU_DATA_PATH "/dhewm3log.txt", "w" );

	Wiiu_InitTime();

	OSThread *mainThread = OSGetCurrentThread();
	engineThread = (OSThread *)memalign( 16, sizeof( OSThread ) );
	uint8_t *stack = (uint8_t *)memalign( 16, ENGINE_STACK_SIZE );
	if ( engineThread == NULL || stack == NULL ) {
		WHBLogPrint( "dhewm3: couldn't allocate the engine thread" );
		return EXIT_FAILURE;
	}

	// stay on the main core (CPU1): ProcUI has to be handled there
	if ( !OSCreateThread( engineThread, EngineThreadMain, 0, NULL, stack + ENGINE_STACK_SIZE,
			ENGINE_STACK_SIZE, OSGetThreadPriority( mainThread ), OS_THREAD_ATTRIB_AFFINITY_CPU1 ) ) {
		WHBLogPrint( "dhewm3: couldn't create the engine thread" );
		return EXIT_FAILURE;
	}
	OSSetThreadName( engineThread, "dhewm3 engine" );
	OSResumeThread( engineThread );

	int ret = EXIT_SUCCESS;
	OSJoinThread( engineThread, &ret );

	WHBLogUdpDeinit();
	WHBLogCafeDeinit();
	return ret;
}
