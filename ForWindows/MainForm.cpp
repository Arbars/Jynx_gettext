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
#include <assert.h>
#include "resource.h"
#include "mmsystem.h"
#include <iostream>
#include <fstream>
#include "MainForm.h"
#include "AboutBoxForm.h"
#include "WindowsFileOpener.h"

#include "../Portable/LynxHardware.h"
#include "../Portable/UIStrings.h"

#define TIMESLICE_PERIOD   16   // 16 bizarrely looks like 20 milliseconds (check the cursor flash rate).
#define WM_HI_RES_TIMER (WM_USER + 0x101)



// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//        HOST WINDOW
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


volatile HWND  g_hWndToPostMessage = NULL;

void CALLBACK MainFormTimerProcedure(
	UINT uTimerID,
	UINT uMsg,
	DWORD_PTR dwUser,
	DWORD_PTR dw1,
	DWORD_PTR dw2)
{
	if( g_hWndToPostMessage != NULL )
	{
		::PostMessage( g_hWndToPostMessage, WM_HI_RES_TIMER, 0, 0 );
	}
}




MainForm::MainForm( HWND hWndOwner, const wchar_t *settingsFilePath, const wchar_t *snapshotFilePath, bool gamesMode, const wchar_t *tapFilePath )
	: BaseForm( hWndOwner, MainForm::IDD )
	, _lynxUIModel( nullptr )
	, _guestScreenBitmap(NULL)
	, _timeBeginPeriodResult(0)
	, _timeSetEventResult(0)
	, _waveOutStream(nullptr)
	, _settingsFilePath(settingsFilePath)
	, _snapshotFilePath(snapshotFilePath)
	, _tapFilePath(tapFilePath)
	, _gamesMode(gamesMode)
	, _saveDC(0)
{
	//
	// Sound
	//

	uint32_t numSamplesPerBuffer = 882;

	_soundBuffer.reserve( numSamplesPerBuffer );

	for( uint32_t i=0; i < numSamplesPerBuffer; i++ )
	{
		_soundBuffer.push_back( 0 );
	}

	auto bufferSizeBytes = numSamplesPerBuffer * 2;
	_waveOutStream = new libWinApi::WaveOutputStream( 44100, 2, 1, 3, (int) bufferSizeBytes );

	//
	// Create frame buffer bitmap which emulator can directly draw on.
	//

	if( ! CreateDIBSectionFrameBuffer( LYNX_FRAMEBUF_WIDTH, LYNX_FRAMEBUF_HEIGHT, &_screenInfo, &_guestScreenBitmap ) )
	{
		throw std::runtime_error( "Windows cannot create the screen bitmap for the emulation.\nThe emulation cannot continue." );
	}

	//
	// Ask for high resolution timers.
	//

	_timeBeginPeriodResult = timeBeginPeriod(1);
	if( _timeBeginPeriodResult == TIMERR_NOCANDO )
	{
		MessageBox( *this, L"Windows has not permitted the emulator to use the desired timer frequency.\nThe emulation speed may be affected.", L"Note", MB_OK | MB_ICONINFORMATION );
	}

	_timeSetEventResult	= timeSetEvent( 20, 20, &MainFormTimerProcedure, (DWORD_PTR) GetHWND(), TIME_PERIODIC | TIME_CALLBACK_FUNCTION | TIME_KILL_SYNCHRONOUS );
	if( _timeSetEventResult == NULL )
	{
		throw std::runtime_error( "Windows cannot create a multimedia timer.  Emulation cannot continue." );
	}

	//
	// Create the model (this has the emulator inside, plus UI logic)
	//

	_lynxUIModel = std::unique_ptr<Jynx::LynxUserInterfaceModel>( new Jynx::LynxUserInterfaceModel(
		this,
		&_soundBuffer.front(),
		_soundBuffer.size(),
		"\r\n", _gamesMode ) );  // The preferred end of line sequence on the WINDOWS platform.  (Think: Notepad.exe!)
}



MainForm::~MainForm()
{
	// THREADING NOTE:
	// - Must destroy _lynxUIModel FIRST - to clean up threads, before
	//   we destroy what the threads are using!
	_lynxUIModel = nullptr;

	//
	// Now the EMULATOR thread is gone, we can now clean up
	// everything that the EMULATOR thread was using:
	//

	g_hWndToPostMessage = NULL;

	if( _timeSetEventResult != NULL )
	{
		timeKillEvent( _timeSetEventResult );
		_timeSetEventResult = NULL;
	}

	if( _timeBeginPeriodResult != TIMERR_NOCANDO )
	{
		timeEndPeriod(1);
		_timeBeginPeriodResult = TIMERR_NOCANDO;
	}

	if( _guestScreenBitmap != NULL )
	{
		::DeleteObject(_guestScreenBitmap);
		_guestScreenBitmap = NULL;
	}

	if( _waveOutStream != nullptr )
	{
		delete _waveOutStream;
		_waveOutStream = nullptr;
	}
}






// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//     HOST KEY CODE to LYNX KEY INDEX translation
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


		// Key codes are in order of the ports Bits A11..A8, then in order bit D7..D0 :


#define NUMBER_OF_KEYS (8*11)

uint8_t  KeysInLynxKeyOrder[NUMBER_OF_KEYS] =
{
	VK_LSHIFT, VK_ESCAPE,   VK_DOWN,     VK_UP, VK_CAPITAL,    0,        0,        '1',
	0,         0,           'C',         'D',   'X',           'E',      '4',      '3',
	0,         VK_LCONTROL, 'A',         'S',   'Z',           'W',      'Q',      '2',
	0,         0,           'F',         'G',   'V',           'T',      'R',      '5',
	0,         0,           'B',         'N',   VK_SPACE,      'H',      'Y',      '6',
	0,         0,           'J',         0,     'M',           'U',      '8',      '7',
	0,         0,           'K',         0,     VK_OEM_COMMA,  'O',      'I',      '9',
	0,         0,           VK_OEM_1,    0,     VK_OEM_PERIOD, 'L',      'P',      '0',
	0,         0,           VK_OEM_PLUS, 0,     VK_OEM_2,      VK_OEM_4, VK_OEM_7, VK_OEM_MINUS,
	0,         0,           VK_RIGHT,    0,     VK_RETURN,     VK_LEFT,  VK_OEM_6, VK_BACK,
};


int32_t MicrosoftWindowsVkCodeToLynxKeyIndex( uint8_t keyVkCode )
{
	// Wire these keys to the ones that will be found in the table:

	if( keyVkCode == VK_SHIFT )      keyVkCode = VK_LSHIFT;
	else if( keyVkCode == VK_RSHIFT )     keyVkCode = VK_LSHIFT;
	else if( keyVkCode == VK_CONTROL )    keyVkCode = VK_LCONTROL;
	else if( keyVkCode == VK_RCONTROL )   keyVkCode = VK_LCONTROL;
	else if( keyVkCode == VK_HOME )       keyVkCode = VK_UP;   // Works well with Lynx line editor
	else if( keyVkCode == VK_END)         keyVkCode = VK_DOWN; // Works well with Lynx line editor

	for( int32_t n=0; n < NUMBER_OF_KEYS; ++n )
	{
		if( KeysInLynxKeyOrder[n] == keyVkCode ) return n;
	}

	return -1;
}




// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//     FRAMEWORK HANDLING
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool  MainForm::OnInitDialog()
{
	SetBigAndSmallIcons( IDR_MAINFRAME );

	g_hWndToPostMessage = GetHWND();

	// Centre window placement BEFORE calling model's OnInitDialog() as
	// that may cause go full screen as settings file is loaded!
	libWinApi::CenterWindowPercent( *this, 85, GetOwner() );

	_lynxUIModel->OnInitDialog();

	if( ! _snapshotFilePath.empty() )
	{
		// Load the snapshot file that the user specified on the command line:
		_lynxUIModel->ForceLoadSpecificSnapshot( &WindowsFileOpener( _snapshotFilePath.c_str() ) );
	}
	else if( ! _tapFilePath.empty() )
	{
		// Load the cassette file that the user specified on the command line:
		_lynxUIModel->ForceLoadSpecificTape( &WindowsFileOpener( _tapFilePath.c_str() ) );
	}

	auto result = BaseForm::OnInitDialog();
	return result;
}



void  MainForm::OnCancel()
{
	_lynxUIModel->OnExit();
}



void MainForm::WindowProc( libWinApi::WindowProcArgs &e )
{
	try
	{
		uint16_t  menuCommand = 0;

		//
		// WM_HI_RES_TIMER:
		// Periodic timer for the model to perform tasks.
		//

		if( e.message == WM_HI_RES_TIMER )
		{
			_lynxUIModel->OnTimer();
			e.Result = 0;
			return;
		}

		if( e.IsPaint() )
		{
			libWinApi::WmPaintHandler ph( *this );
			if( ! ph.AreaIsEmpty() )
			{
				if( ph.ClipToUpdateRect() )
				{
					_dc = ph.dc;
					_lynxUIModel->OnPaint();
					_dc = NULL;
				}
			}
			e.Result = 1;
			return;
		}

		if( e.IsErase() )
		{
			e.Result = 1;
			return;
		}

		if( e.IsMenuCommand( &menuCommand ) )
		{
			if( ! _lynxUIModel->DispatchMenuComment( menuCommand ) )
			{
				switch( menuCommand )
				{
					case ID_HELP_ABOUT:               OnAbout(); break; // not handled by the model
					default:                          return BaseForm::WindowProc( e );
				}
			}
			e.Result = 1;
			return;  // Processed.
		}

		if( e.IsDeactivated() )
		{
			_lynxUIModel->NotifyAllKeysUp();   // Let's make sure we don't have stuck keys, switching away from the app!
		}

		if( e.message == WM_LBUTTONDBLCLK )
		{
			_lynxUIModel->OnEnableDisableFullScreen();
		}

		//
		// WM_WINDOWPOSCHANGED
		//

		if( e.message == WM_WINDOWPOSCHANGED )
		{
			auto pWP = reinterpret_cast<WINDOWPOS *>(e.lParam);
			if( pWP )
			{
				int Mask = SWP_NOSIZE; // irritating negative logic here
				if( (pWP->flags & Mask) != Mask )
				{
					// Called because of size of this window.
					RECT r;
					if( GetClientRect( *this, &r ) )
					{
						InvalidateRect( *this, &r, FALSE );
					}
				}
			}

			// Let caller see this message too.
		}

	}
	catch( const UserInterfaceException &nonFatalError )  // reminder: catches stream errors too.  TODO: This might be controversial, and a "UserInterfaceException" better.
	{
		MessageBoxA( *this, nonFatalError.what(), "Error", MB_OK | MB_ICONERROR );
	}

	return BaseForm::WindowProc( e );
}










bool  MainForm::PreProcessMessage( libWinApi::Message *pMsg )
{
	if( _lynxUIModel != nullptr )
	{
		uint32_t  keyCode = 0;

		if( pMsg->IsKeyDown( &keyCode ) )
		{
			auto lynxKeyIndex = MicrosoftWindowsVkCodeToLynxKeyIndex( keyCode );
			if( lynxKeyIndex != -1 )
			{
				_lynxUIModel->NotifyKeyDown( lynxKeyIndex );
				return true;
			}
		}
		else if( pMsg->IsKeyUp( &keyCode ) )
		{
			auto lynxKeyIndex = MicrosoftWindowsVkCodeToLynxKeyIndex( keyCode );
			if( lynxKeyIndex != -1 )
			{
				_lynxUIModel->NotifyKeyUp( lynxKeyIndex );
				return true;
			}
		}
	}

	return false;
}






// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//     UI ELEMENT EVENT HANDLERS
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void MainForm::OnAbout()
{
	AboutBoxForm  aboutForm( *this );
	aboutForm.DoModal();
}








// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//        VIEW SERVICES TO MODEL  (IHostServicesForLynxUserInterfaceModel)
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void MainForm::CloseDownNow()
{
	// Just exit with no questions asked -- the Model has done all checks + saves.
	BaseForm::OnCancel();
}



std::shared_ptr<Jynx::IFileOpener> MainForm::ShowOpenFileDialog( Jynx::LoadableFileTypes::Enum fileType )
{
	std::wstring  filePathChosen;

	if( libWinApi::ShowOpenFileDialog( *this, OpenFileDialogTitles[fileType], OpenFileDialogSpecs[fileType], &filePathChosen ) )
	{
		return std::make_shared<WindowsFileOpener>( filePathChosen ); // throws
	}
	return nullptr;  // User cancelled
}



std::shared_ptr<Jynx::IFileOpener> MainForm::ShowSaveFileDialog( Jynx::SaveableFileTypes::Enum fileType )
{
	std::wstring  filePathChosen;

	if( libWinApi::ShowSaveFileDialog( *this, SaveFileDialogTitles[fileType], SaveFileDialogSpecs[fileType], SaveFileDialogExtns[fileType], &filePathChosen ) )
	{
		return std::make_shared<WindowsFileOpener>( filePathChosen ); // throws
	}
	return nullptr;  // User cancelled
}



void MainForm::TellUser( const char *messageText, const char *captionText )
{
	::MessageBoxA( *this, messageText, captionText, MB_OK | MB_ICONINFORMATION );
}



bool MainForm::AskYesNoQuestion( const char *questionText, const char *captionText )
{
	return ::MessageBoxA( *this, questionText, captionText, MB_YESNO | MB_ICONQUESTION ) == IDYES;
}



void MainForm::SetTickBoxState( Jynx::TickableInterfaceElements::Enum itemToSet, bool tickState )
{
	assert( itemToSet >= 0 && itemToSet <= Jynx::TickableInterfaceElements::Count );
	assert( sizeof(MainFormTickableItems) == (sizeof(UINT) * Jynx::TickableInterfaceElements::Count) );

	auto hMenu = ::GetMenu( *this );
	if( hMenu )
	{
		CheckMenuItem( hMenu, MainFormTickableItems[itemToSet], tickState ? MF_CHECKED : 0 );
	}

	if( itemToSet == Jynx::TickableInterfaceElements::ShowFullScreen )
	{
		auto previouslyFullScreen = libWinApi::IsWindowFullScreen( *this );
		if( tickState != previouslyFullScreen )
		{
			if( tickState )
			{
				_restorationAfterFullScreen = libWinApi::GoFullScreen( *this );
			}
			else
			{
				_restorationAfterFullScreen.Restore();
			}
		}
	}
}



void MainForm::SetEnabledState( Jynx::ButtonInterfaceElements::Enum itemToSet, bool enableState )
{
	assert( itemToSet >= 0 && itemToSet <= Jynx::ButtonInterfaceElements::Count );
	assert( sizeof(MainFormGreyableItems) == (sizeof(UINT) * Jynx::ButtonInterfaceElements::Count) );

	auto hMenu = ::GetMenu( *this );
	if( hMenu )
	{
		EnableMenuItem( hMenu, MainFormGreyableItems[itemToSet], enableState ? MF_GRAYED  : MF_ENABLED );
	}
}



Jynx::LynxRectangle  MainForm::GetClientRectangle()
{
	Jynx::LynxRectangle  area;

	RECT r;
	::GetClientRect( *this, &r );

	area.left   = r.left;
	area.top    = r.top;
	area.right  = r.right;
	area.bottom = r.bottom;

	return area;
}



void MainForm::SetViewport( int left, int top, int width, int height )
{
	if( _dc != NULL ) // If this is NULL, program will be terminating because of posted quit message, anyway!
	{
		assert( _saveDC == 0 );
		_saveDC = ::SaveDC( _dc );
		::IntersectClipRect( _dc, left, top, left+width, top+height );
	}
}



void MainForm::CancelViewport()
{
	if( _dc != NULL && _saveDC != 0 )
	{
		::RestoreDC( _dc, _saveDC );
		_saveDC = 0;
	}
}



void MainForm::StretchBlitTheGuestScreen( int left, int top, int width, int height )
{
	if( _dc != NULL && _guestScreenBitmap != NULL ) // If this is NULL, program will be terminating because of posted quit message, anyway!
	{
		auto bitmapDC = ::CreateCompatibleDC( _dc );
		if( bitmapDC != NULL )
		{
			auto previousBitmapHandle = (HANDLE) ::SelectObject( bitmapDC, _guestScreenBitmap );

			::StretchBlt(
				_dc,
				left, top, width, height,
				bitmapDC,
				0, 0, LYNX_FRAMEBUF_WIDTH, LYNX_FRAMEBUF_HEIGHT,
				SRCCOPY );

			::SelectObject( bitmapDC, previousBitmapHandle );
			::DeleteDC( bitmapDC );
		}
	}
}



void MainForm::FillBlackRectangle( int left, int top, int width, int height )
{
	if( _dc != NULL )
	{
		::BitBlt( _dc, left, top, width, height, NULL, 0, 0, BLACKNESS );
	}
}



void MainForm::InvalidateAreaOfHostScreen( const Jynx::LynxRectangle &area )
{
	RECT r;
	r.left   = area.left;
	r.top    = area.top;
	r.right  = area.right;
	r.bottom = area.bottom;
	::InvalidateRect( *this, &r, FALSE );
}





void  MainForm::OpenChipFileStream_OnMainThread( std::ifstream &streamToBeOpened, std::ios_base::openmode openModeRequired, Jynx::LynxRoms::Enum romRequired )
{
	// (Reminder - called on the MAIN thread only).

	assert( uint32_t(romRequired) < Jynx::LynxRoms::Count ); // Should be

	// Determine path name, ROMS are in same folder as the Windows EXE:
	auto wideLeafFileName = std::wstring( g_RomFileNames[ romRequired ] );
	auto folderPath = libWinApi::GetMyExeFolderPath();
	auto filePath = folderPath + wideLeafFileName;

	// Open the stream for the emulator:
	streamToBeOpened.exceptions( std::ifstream::failbit | std::ifstream::badbit | std::ifstream::eofbit );
	streamToBeOpened.open( filePath.c_str(), openModeRequired );
}



template<typename PIXEL_TYPE>
inline PIXEL_TYPE *CalcFrameBufferPixelAddress( PIXEL_TYPE *frameBufferTopLeftAddress, int32_t frameBufferRowStepBytes, int32_t pixelX, int32_t pixelY )
{
	auto pixelAddress = ((intptr_t) frameBufferTopLeftAddress) + (pixelY * frameBufferRowStepBytes) + (pixelX * sizeof(PIXEL_TYPE));
	return (PIXEL_TYPE *) pixelAddress;
}




void MainForm::TranslateRGBXColourPaletteToHostValues( const uint32_t *eightEntryColourPalette, uint32_t *eightEntryTranslatedValues )
{
	// Nothing to do here, the format this host requires is the same as the emulator uses.
}



void  MainForm::PaintPixelsOnHostBitmap_OnEmulatorThread( uint32_t addressOffset, const uint32_t *eightPixelsData )
{
	// (WARNING - Called on the EMULATOR thread, NOT the MAIN thread)

	// Multithreading note:  In theory, we may get "tearing" with this being unsynchronised.
	// This is deemed not to matter.  The EMULATOR thread CANNOT BE HELD UP without risking sound suffering!

	int32_t  destX = (addressOffset & 0x1F) << 3;
	int32_t  destY = (addressOffset >> 5);
	auto destinationPixelAddress = CalcFrameBufferPixelAddress( (uint32_t *) _screenInfo.BaseAddress, _screenInfo.BytesPerScanLine, destX, destY );
	auto endPixelAddress = destinationPixelAddress + 8;
	std::copy_n( eightPixelsData, 8, destinationPixelAddress );
}



std::wstring  GetJynxAppDataPath()
{
	auto pathRoot = libWinApi::GetUserAppDataPath();
	if( ! pathRoot.empty() )
	{
		auto jynxFolderPath = pathRoot + L"JynxEmulator";
		if ( CreateDirectory( jynxFolderPath.c_str(), NULL) || ERROR_ALREADY_EXISTS == GetLastError() )
		{
			return jynxFolderPath;
		}
	}
	return std::wstring();
}



std::shared_ptr<Jynx::IFileOpener>  MainForm::GetUserSettingsFilePath()
{
	if( _settingsFilePath.empty() )
	{
		// The user did not specify a settings file path on the command line.
		// Use the platform preferred location for settings files:

		auto jynxAppDataPath = GetJynxAppDataPath();
		if( ! jynxAppDataPath.empty() )
		{
			auto fullPath = jynxAppDataPath + L"\\JynxEmulatorSettings.config";
			return std::make_shared<WindowsFileOpener>( fullPath );
		}
	}
	else
	{
		// Use the path the user specified on the command line:
		return std::make_shared<WindowsFileOpener>( _settingsFilePath );
	}

	return nullptr;
}







void MainForm::WriteSoundBufferToSoundCardOrSleep_OnEmulatorThread()
{
	// (Called on the EMULATOR thread, NOT the MAIN thread)

	if( _lynxUIModel->IsSoundEnabled() )
	{
		// NOTE: The sound card "forces" us back until it's ready.
		// This gives us a 20ms timer, on which the emulation is synchronised, when sound is ON.
		_waveOutStream->Write( &(*_soundBuffer.begin()), (int) _soundBuffer.size() * 2 );
	}
	else
	{
		// Sound is OFF, so we have to sleep for the 20 milliseconds instead.
		// The emulation burst processing is usually very small on a modern CPU
		// so this will suffice.  I don't care so much about realtime accuracy with
		// sound OFF.
		::Sleep(20);
	}
}

