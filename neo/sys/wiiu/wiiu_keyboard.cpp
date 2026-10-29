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

// The Wii U's software keyboard (nn::swkbd) for GUI edit fields.
// It's driven from the frame loop in gx2_glimp.cpp: WiiU_KeyboardUpdate() feeds it
// the GamePad's input, the Draw functions draw it on top of the game (TV) and on
// its own (GamePad, where it's used by touch). While it's open the game ignores
// the gamepad, so pressing buttons in the keyboard doesn't also do things in menus.

#include <malloc.h>

#include <coreinit/filesystem.h>
#include <nn/swkbd.h>
#include <vpad/input.h>

#include "sys/platform.h"
#include "framework/Common.h"
#include "framework/CVarSystem.h"

#include "sys/wiiu/wiiu_keyboard.h"

static bool			created;
static bool			createFailed;
static void *		workMemory;
static FSClient *	fsClient;

static const void *	owner;			// edit field the keyboard is open for
static bool			resultReady;
static bool			resultOk;
static idStr		resultText;
static bool			restoreGamepad;

static char16_t		initialText[256];

static bool Create( void ) {
	if ( created || createFailed ) {
		return created;
	}

	FSInit();
	fsClient = (FSClient *)memalign( 0x40, sizeof( FSClient ) );
	FSAddClient( fsClient, FS_ERROR_FLAG_ALL );

	workMemory = memalign( 0x40, nn::swkbd::GetWorkMemorySize( 0 ) );

	nn::swkbd::CreateArg createArg;
	createArg.regionType = nn::swkbd::RegionType::USA;
	createArg.workMemory = workMemory;
	createArg.fsClient = fsClient;
	if ( !nn::swkbd::Create( createArg ) ) {
		common->Warning( "Couldn't create the Wii U software keyboard" );
		createFailed = true;
		return false;
	}
	created = true;
	return true;
}

void WiiU_KeyboardShutdown( void ) {
	if ( created ) {
		nn::swkbd::Destroy();
		created = false;
	}
	if ( fsClient ) {
		FSDelClient( fsClient, FS_ERROR_FLAG_ALL );
		free( fsClient );
		fsClient = NULL;
	}
	free( workMemory );
	workMemory = NULL;
}

void WiiU_KeyboardOpen( const void *_owner, const char *text, int maxChars, bool numeric ) {
	if ( !Create() || nn::swkbd::GetStateInputForm() != nn::swkbd::State::Hidden ) {
		return;
	}

	// Doom 3 text is Latin-1
	int len = 0;
	for ( ; text[len] && len < (int)( sizeof( initialText ) / sizeof( initialText[0] ) ) - 1; len++ ) {
		initialText[len] = (unsigned char)text[len];
	}
	initialText[len] = 0;

	nn::swkbd::AppearArg appearArg;
	appearArg.keyboardArg.configArg.languageType = nn::swkbd::LanguageType::English;
	appearArg.keyboardArg.configArg.keyboardMode = numeric ? nn::swkbd::KeyboardMode::Numpad : nn::swkbd::KeyboardMode::Full;
	appearArg.inputFormArg.initialText = initialText;
	appearArg.inputFormArg.maxTextLength = maxChars > 0 ? maxChars : -1;
	if ( !nn::swkbd::AppearInputForm( appearArg ) ) {
		return;
	}

	owner = _owner;
	resultReady = false;

	// don't let the game see the button presses meant for the keyboard
	idCVar *useGamepad = cvarSystem->Find( "in_useGamepad" );
	restoreGamepad = useGamepad && useGamepad->GetBool();
	if ( restoreGamepad ) {
		useGamepad->SetBool( false );
	}
}

bool WiiU_KeyboardPoll( const void *_owner, idStr &text ) {
	if ( !resultReady || owner != _owner ) {
		return false;
	}
	resultReady = false;
	owner = NULL;
	if ( resultOk ) {
		text = resultText;
	}
	return resultOk;
}

static void Close( bool ok ) {
	if ( ok ) {
		resultText.Clear();
		const char16_t *str = nn::swkbd::GetInputFormString();
		for ( int i = 0; str && str[i]; i++ ) {
			resultText.Append( str[i] < 256 ? (char)str[i] : '?' );
		}
	}
	resultOk = ok;
	resultReady = true;
	nn::swkbd::DisappearInputForm();

	if ( restoreGamepad ) {
		cvarSystem->SetCVarBool( "in_useGamepad", true );
		restoreGamepad = false;
	}
}

bool WiiU_KeyboardVisible( void ) {
	return created && nn::swkbd::GetStateInputForm() != nn::swkbd::State::Hidden;
}

void WiiU_KeyboardUpdate( void ) {
	if ( !WiiU_KeyboardVisible() ) {
		return;
	}

	VPADStatus vpad;
	VPADReadError error;
	memset( &vpad, 0, sizeof( vpad ) );
	VPADRead( VPAD_CHAN_0, &vpad, 1, &error );
	VPADGetTPCalibratedPoint( VPAD_CHAN_0, &vpad.tpNormal, &vpad.tpNormal );

	nn::swkbd::ControllerInfo controllerInfo;
	controllerInfo.vpad = ( error == VPAD_READ_SUCCESS ) ? &vpad : NULL;
	nn::swkbd::Calc( controllerInfo );

	if ( nn::swkbd::IsNeedCalcSubThreadFont() ) {
		nn::swkbd::CalcSubThreadFont();
	}
	if ( nn::swkbd::IsNeedCalcSubThreadPredict() ) {
		nn::swkbd::CalcSubThreadPredict();
	}

	if ( nn::swkbd::GetStateInputForm() == nn::swkbd::State::Visible ) {
		if ( nn::swkbd::IsDecideOkButton( NULL ) ) {
			Close( true );
		} else if ( nn::swkbd::IsDecideCancelButton( NULL ) ) {
			Close( false );
		}
	}
}

void WiiU_KeyboardDrawTV( void ) {
	nn::swkbd::DrawTV();
}

void WiiU_KeyboardDrawDRC( void ) {
	nn::swkbd::DrawDRC();
}
