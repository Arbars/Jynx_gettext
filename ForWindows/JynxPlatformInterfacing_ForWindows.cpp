//
// Jynx - Jonathan's Lynx Emulator (Camputers Lynx 48K/96K models).
// Copyright (C) 2014  Jonathan Markland
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// 
//		jynx_emulator {at} yahoo {dot} com
// 

#include "stdafx.h"
#include "../Portable/JynxPlatformInterfacing.h"
#include <stdexcept>

namespace Jynx
{
	Waitable::Waitable()
	{
		// This synchronisation object is constructed in the "non-signalled" state.
		_platformSpecificImplementation = ::CreateEvent( NULL, FALSE, FALSE, NULL );
		if( _platformSpecificImplementation == NULL )
		{
			throw std::exception( "Cannot create Event object -- program cannot run." );
		}
	}



	void Waitable::Reset()
	{
		// Set this synchronisation object to the "non-signalled" state.
		// Then, anyone calling Wait() will block.
		if( ::ResetEvent( (HANDLE) _platformSpecificImplementation ) != 0 ) return;
		throw std::exception( "ResetEvent() call failed unexpectedly." );
	}



	void Waitable::Signal()
	{
		// Set this synchronisation object to the "signalled" state.
		// - Then, anyone calling Wait() will not block.
		// - Then, anyone who has already called Wait() will be released.
		if( ::SetEvent( (HANDLE) _platformSpecificImplementation ) != 0 ) return;
		throw std::exception( "SetEvent() call failed unexpectedly." );
	}



	void Waitable::Wait() const
	{
		// Block calling thread until the object is "signalled".
		// No-operation if this object is already signalled.
		if( ::WaitForSingleObject( (HANDLE) _platformSpecificImplementation, INFINITE ) == WAIT_OBJECT_0 ) return;
		throw std::exception( "Unexpected result from WaitForSingleObject() during Wait()." );
	}



	bool Waitable::IsSignalled() const
	{
		// Determines whether this object is signalled, without blocking caller.
		auto result = ::WaitForSingleObject( (HANDLE) _platformSpecificImplementation, 0 );  // tests state without wait
		if( result == WAIT_OBJECT_0 ) return true;
		if( result == WAIT_TIMEOUT ) return false;
		throw std::exception( "Unexpected result from WaitForSingleObject() while testing IsSignalled()." );
	}



	void Waitable::CleanupResources()
	{
		::CloseHandle( (HANDLE) _platformSpecificImplementation );
		_platformSpecificImplementation = NULL;
	}

}
