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

// Wii U networking: loopback only for now (enough for single player).
// TODO: real UDP/TCP via wut's nsysnet BSD sockets for multiplayer.

#include "sys/platform.h"
#include "framework/Common.h"
#include "framework/CVarSystem.h"
#include "sys/sys_public.h"

idPort clientPort, serverPort;

idCVar net_ip( "net_ip", "localhost", CVAR_SYSTEM, "local IP address" );
idCVar net_port( "net_port", "", CVAR_SYSTEM | CVAR_INTEGER, "local IP port number" );

bool Sys_StringToNetAdr( const char *s, netadr_t *a, bool doDNSResolve ) {
	if ( idStr::Icmpn( s, "localhost", 9 ) == 0 ) {
		memset( a, 0, sizeof( *a ) );
		a->type = NA_LOOPBACK;
		const char *colon = strchr( s, ':' );
		if ( colon ) {
			a->port = atoi( colon + 1 );
		}
		return true;
	}
	return false;
}

const char *Sys_NetAdrToString( const netadr_t a ) {
	static char s[64];

	s[0] = '\0';
	if ( a.type == NA_LOOPBACK ) {
		if ( a.port ) {
			idStr::snPrintf( s, sizeof(s), "localhost:%i", a.port );
		} else {
			idStr::snPrintf( s, sizeof(s), "localhost" );
		}
	} else if ( a.type == NA_IP ) {
		idStr::snPrintf( s, sizeof(s), "%i.%i.%i.%i:%i",
			a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port );
	}
	return s;
}

bool Sys_IsLANAddress( const netadr_t adr ) {
	return adr.type == NA_LOOPBACK;
}

bool Sys_CompareNetAdrBase( const netadr_t a, const netadr_t b ) {
	if ( a.type != b.type ) {
		return false;
	}
	if ( a.type == NA_LOOPBACK ) {
		return true;
	}
	if ( a.type == NA_IP ) {
		return a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] && a.ip[2] == b.ip[2] && a.ip[3] == b.ip[3];
	}
	return false;
}

void Sys_InitNetworking( void ) {
	common->Printf( "Networking is not implemented on Wii U yet, only loopback is available\n" );
}

void Sys_ShutdownNetworking( void ) {
}

idPort::idPort() {
	netSocket = 0;
	memset( &bound_to, 0, sizeof( bound_to ) );
}

idPort::~idPort() {
	Close();
}

void idPort::Close() {
	netSocket = 0;
	memset( &bound_to, 0, sizeof( bound_to ) );
}

bool idPort::InitForPort( int portNumber ) {
	return false;
}

bool idPort::GetPacket( netadr_t &from, void *data, int &size, int maxSize ) {
	return false;
}

bool idPort::GetPacketBlocking( netadr_t &from, void *data, int &size, int maxSize, int timeout ) {
	return false;
}

void idPort::SendPacket( const netadr_t to, const void *data, int size ) {
}

idTCP::idTCP() {
	fd = 0;
	memset( &address, 0, sizeof( address ) );
}

idTCP::~idTCP() {
	Close();
}

bool idTCP::Init( const char *host, short port ) {
	return false;
}

void idTCP::Close() {
	fd = 0;
}

int idTCP::Read( void *data, int size ) {
	return -1;
}

int idTCP::Write( void *data, int size ) {
	return -1;
}
