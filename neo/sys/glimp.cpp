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

#include <SDL.h>
#include <SDL_syswm.h>

#include "sys/platform.h"
#include "framework/Licensee.h"
#include "idlib/containers/Sort.h"

#include "renderer/tr_local.h"
#if defined(_WIN32)
#include "sys/win32/win_local.h"
#endif

idCVar in_nograb("in_nograb", "0", CVAR_SYSTEM | CVAR_NOCHEAT, "prevents input grabbing");
idCVar r_useOpenGL32( "r_useOpenGL32", "1", CVAR_INTEGER, "0 = OpenGL 2.0, 1 = OpenGL 3.2 compatibility profile, 2 = OpenGL 3.2 core profile", 0, 2 );

static bool grabbed = false;

SDL_Window *SDL_window = NULL;
static SDL_GLContext *SDL_glContext = NULL;

/*
===================
GLimp_PreInit

 R_GetModeListForDisplay is called before GLimp_Init(), but SDL needs SDL_Init() first.
 So do that in GLimp_PreInit()
 Calling that function more than once doesn't make a difference
===================
*/
void GLimp_PreInit() {
	if( !SDL_WasInit( SDL_INIT_VIDEO ) ){
		if( SDL_Init( SDL_INIT_VIDEO ) )
			common->Error( "Error while initializing SDL: %s", SDL_GetError() );
	}
}


/*
===================
GLimp_Init
===================
*/
bool GLimp_Init(glimpParms_t parms) {
	common->Printf( "Initializing OpenGL subsystem\n" );

	GLimp_PreInit();

	Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

	if (parms.fullScreen)
		flags |= SDL_WINDOW_FULLSCREEN;

	SDL_GL_SetAttribute (SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 8);

	SDL_GL_SetAttribute(SDL_GL_STEREO, parms.stereo ? 1 : 0);

	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, parms.multiSamples ? 1 : 0);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, parms.multiSamples);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	if( r_useOpenGL32.GetInteger() > 0 ) {
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 2 );
		if( r_useOpenGL32.GetInteger() > 1 ) {
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
		} else {
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY );
		}
			
		if( r_debugContext.GetBool() ) {
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG );
		}
	}

	int windowPos = SDL_WINDOWPOS_UNDEFINED;
	if( parms.fullScreen > 0 ) {
		if( parms.fullScreen > SDL_GetNumVideoDisplays() ) {
			common->Warning( "Couldn't set display to num %i because we only have %i displays",
							 parms.fullScreen, SDL_GetNumVideoDisplays() );
		} else {
			// -1 because SDL starts counting displays at 0, while parms.fullScreen starts at 1
			windowPos = SDL_WINDOWPOS_UNDEFINED_DISPLAY( ( parms.fullScreen - 1 ) );
		}
	}
	// TODO: if parms.fullScreen == -1 there should be a borderless window spanning multiple displays
	/*
	 * NOTE that this implicitly handles parms.fullScreen == -2 (from r_fullscreen -2) meaning
	 * "do fullscreen, but I don't care on what monitor", at least on my box it's the monitor with
	 * the mouse cursor.
	 */
	// Destroy existing state if it exists
	if( SDL_glContext != NULL )	{
		SDL_GL_DeleteContext( SDL_glContext );
		SDL_glContext = NULL;
	}

	if( SDL_window != NULL ) {
		SDL_GetWindowPosition( SDL_window, &windowPos, &windowPos );
		common->DPrintf( "Existing window at %dx%d before being destroyed\n", windowPos, windowPos );
		SDL_DestroyWindow( SDL_window );
		SDL_window = NULL;
	}

	if( ( SDL_window = SDL_CreateWindow( GAME_NAME, windowPos, windowPos, parms.width, parms.height, flags ) ) == 0 ) {
		common->DPrintf( "SDL_CreateWindow failed: %s\n", SDL_GetError( ) );
		return false;
	}

	if( ( SDL_glContext = (SDL_GLContext *)SDL_GL_CreateContext( SDL_window ) ) == NULL ) {
		common->DPrintf( "SDL_GL_CreateContext failed: %s\n", SDL_GetError( ) );
		return false;
	}

	if( SDL_GL_MakeCurrent( SDL_window, SDL_glContext ) < 0 ) {
		common->DPrintf( "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError( ) );
		return false;
	}

	if (SDL_GL_SetSwapInterval(r_swapInterval.GetInteger()) < 0)
		common->Warning("SDL_GL_SWAP_CONTROL not supported");

	SDL_GetWindowSize( SDL_window, &glConfig.vidWidth, &glConfig.vidHeight );

	glConfig.isFullscreen = (SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN;
	
	glConfig.colorBits = 24;
	glConfig.depthBits = 24;
	glConfig.stencilBits = 8;
	
	glConfig.displayFrequency = 60;

	// make sure cursor is not visible and grab window focus
	SDL_ShowCursor( SDL_DISABLE );
	SDL_SetWindowGrab( SDL_window, SDL_TRUE );

	return true;
}


// SDL1 doesn't support multiple displays, so the source is much shorter and doesn't need seperate functions
// makes sure the window will be full-screened on the right display and returns the SDL display index
static int ScreenParmsHandleDisplayIndex( glimpParms_t parms )
{
	int displayIdx;
	if( parms.fullScreen > 0 )
	{
		displayIdx = parms.fullScreen - 1; // first display for SDL is 0, in parms it's 1
	}
	else // -2 == use current display
	{
		displayIdx = SDL_GetWindowDisplayIndex( SDL_window );
		if( displayIdx < 0 ) // for some reason the display for the window couldn't be detected
			displayIdx = 0;
	}
	
	if( parms.fullScreen > SDL_GetNumVideoDisplays() )
	{
		common->Warning( "Can't set fullscreen mode to display number %i, because SDL2 only knows about %i displays!",
						 parms.fullScreen, SDL_GetNumVideoDisplays() );
		return -1;
	}
	
	if( parms.fullScreen != glConfig.isFullscreen )
	{
		// we have to switch to another display
		if( glConfig.isFullscreen )
		{
			// if we're already in fullscreen mode but want to switch to another monitor
			// we have to go to windowed mode first to move the window.. SDL-oddity.
			SDL_SetWindowFullscreen( SDL_window, SDL_FALSE );
		}
		// select display ; SDL_WINDOWPOS_UNDEFINED_DISPLAY() doesn't work.
		int x = SDL_WINDOWPOS_CENTERED_DISPLAY( displayIdx );
		// move window to the center of selected display
		SDL_SetWindowPosition( SDL_window, x, x );
	}
	return displayIdx;
}

static bool SetScreenParmsFullscreen( glimpParms_t parms )
{
	SDL_DisplayMode m = {0};
	int displayIdx = ScreenParmsHandleDisplayIndex( parms );
	if( displayIdx < 0 )
		return false;
		
	// get current mode of display the window should be full-screened on
	SDL_GetCurrentDisplayMode( displayIdx, &m );
	
	// change settings in that display mode according to parms
	// FIXME: check if refreshrate, width and height are supported?
	// m.refresh_rate = parms.displayHz;
	m.w = parms.width;
	m.h = parms.height;
	
	// set that displaymode
	if( SDL_SetWindowDisplayMode( SDL_window, &m ) < 0 )
	{
		common->Warning( "Couldn't set window mode for fullscreen, reason: %s", SDL_GetError() );
		return false;
	}
	
	// if we're currently not in fullscreen mode, we need to switch to fullscreen
	if( !( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_FULLSCREEN ) )
	{
		if( SDL_SetWindowFullscreen( SDL_window, SDL_TRUE ) < 0 )
		{
			common->Warning( "Couldn't switch to fullscreen mode, reason: %s!", SDL_GetError() );
			return false;
		}
	}
	return true;
}

static bool SetScreenParmsWindowed( glimpParms_t parms )
{
	SDL_SetWindowSize( SDL_window, parms.width, parms.height );
	SDL_SetWindowPosition( SDL_window, parms.x, parms.y );
	
	// if we're currently in fullscreen mode, we need to disable that
	if( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_FULLSCREEN )
	{
		if( SDL_SetWindowFullscreen( SDL_window, SDL_FALSE ) < 0 )
		{
			common->Warning( "Couldn't switch to windowed mode, reason: %s!", SDL_GetError() );
			return false;
		}
	}
	return true;
}

/*
===================
GLimp_SetScreenParms
===================
*/
bool GLimp_SetScreenParms( glimpParms_t parms ) {
	if( parms.fullScreen > 0 || parms.fullScreen == -2 )
	{
		if( !SetScreenParmsFullscreen( parms ) )
			return false;
	}
	else if( parms.fullScreen == 0 ) // windowed mode
	{
		if( !SetScreenParmsWindowed( parms ) )
			return false;
	}
	else
	{
		common->Warning( "GLimp_SetScreenParms: fullScreen -1 (borderless window for multiple displays) currently unsupported!" );
		return false;
	}

	SDL_GL_SetAttribute( SDL_GL_STEREO, parms.stereo ? 1 : 0 );
	
	SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, parms.multiSamples ? 1 : 0 );
	SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, parms.multiSamples );
	
	glConfig.isFullscreen = parms.fullScreen;
	glConfig.vidWidth = parms.width;
	glConfig.vidHeight = parms.height;
	glConfig.displayFrequency = parms.displayHz;
	
	return true;
}

/*
===================
GLimp_Shutdown
===================
*/
void GLimp_Shutdown() {
	common->Printf("Shutting down OpenGL subsystem\n");

	if ( SDL_glContext ) {
		SDL_GL_DeleteContext( SDL_glContext );
		SDL_glContext = NULL;
	}

	if ( SDL_window ) {
		SDL_DestroyWindow( SDL_window );
		SDL_window = NULL;
	}
}

/*
===================
GLimp_SwapBuffers
===================
*/
void GLimp_SwapBuffers() {
	SDL_GL_SwapWindow( SDL_window );
}

/*
=================
GLimp_SetGamma
=================
*/
void GLimp_SetGamma(unsigned short red[256], unsigned short green[256], unsigned short blue[256]) {
	if ( !SDL_window ) {
		common->Warning("GLimp_SetGamma called without window");
		return;
	}

	if (SDL_SetWindowGammaRamp( SDL_window, red, green, blue ))
		common->Warning("Couldn't set gamma ramp: %s", SDL_GetError());
}

/*
===================
GLimp_ExtensionPointer
===================
*/
GLExtension_t GLimp_ExtensionPointer(const char *name) {
	assert(SDL_WasInit(SDL_INIT_VIDEO));

	return (GLExtension_t)SDL_GL_GetProcAddress(name);
}

void GLimp_GrabInput(int flags) {
	bool grab = flags & GRAB_ENABLE;

	if ( grab && (flags & GRAB_REENABLE) )
		grab = false;

	if ( flags & GRAB_SETSTATE )
		grabbed = grab;

	if ( in_nograb.GetBool() )
		grab = false;

	if ( !SDL_window ) {
		common->Warning("GLimp_GrabInput called without window");
		return;
	}

	SDL_SetRelativeMouseMode( flags & GRAB_ENABLE ? SDL_TRUE : SDL_FALSE );
	SDL_SetWindowGrab( SDL_window, grab ? SDL_TRUE : SDL_FALSE );
}

class idSort_VidMode : public idSort_Quick< vidMode_t, idSort_VidMode > {
public:
	int Compare( const vidMode_t& a, const vidMode_t& b ) const {
		int wd = a.width - b.width;
		int hd = a.height - b.height;
		int fd = a.displayHz - b.displayHz;
		return ( hd != 0 ) ? hd : ( wd != 0 ) ? wd : fd;
	}
};

// RB: resolutions supported by XreaL
static void FillStaticVidModes( idList<vidMode_t>& modeList ) {
	modeList.AddUnique( vidMode_t( 320,   240, 60 ) );
	modeList.AddUnique( vidMode_t( 400,   300, 60 ) );
	modeList.AddUnique( vidMode_t( 512,   384, 60 ) );
	modeList.AddUnique( vidMode_t( 640,   480, 60 ) );
	modeList.AddUnique( vidMode_t( 800,   600, 60 ) );
	modeList.AddUnique( vidMode_t( 960,   720, 60 ) );
	modeList.AddUnique( vidMode_t( 1024,  768, 60 ) );
	modeList.AddUnique( vidMode_t( 1152,  864, 60 ) );
	modeList.AddUnique( vidMode_t( 1280,  720, 60 ) );
	modeList.AddUnique( vidMode_t( 1280,  768, 60 ) );
	modeList.AddUnique( vidMode_t( 1280,  800, 60 ) );
	modeList.AddUnique( vidMode_t( 1280, 1024, 60 ) );
	modeList.AddUnique( vidMode_t( 1360,  768, 60 ) );
	modeList.AddUnique( vidMode_t( 1440,  900, 60 ) );
	modeList.AddUnique( vidMode_t( 1680, 1050, 60 ) );
	modeList.AddUnique( vidMode_t( 1600, 1200, 60 ) );
	modeList.AddUnique( vidMode_t( 1920, 1080, 60 ) );
	modeList.AddUnique( vidMode_t( 1920, 1200, 60 ) );
	modeList.AddUnique( vidMode_t( 2048, 1536, 60 ) );
	modeList.AddUnique( vidMode_t( 2560, 1600, 60 ) );
	
	modeList.SortWithTemplate( idSort_VidMode() );
}

/*
====================
R_GetModeListForDisplay
====================
*/
bool R_GetModeListForDisplay( const int requestedDisplayNum, idList<vidMode_t>& modeList ) {
	assert( requestedDisplayNum >= 0 );
	
	modeList.Clear();

	if( requestedDisplayNum >= SDL_GetNumVideoDisplays() ) {
		// requested invalid displaynum
		return false;
	}
	
	int numModes = SDL_GetNumDisplayModes( requestedDisplayNum );
	if( numModes > 0 ) {
		for( int i = 0; i < numModes; i++ ) {
			SDL_DisplayMode m;
			int ret = SDL_GetDisplayMode( requestedDisplayNum, i, &m );
			if( ret != 0 ) {
				common->Warning( "Can't get video mode no %i, because of %s\n", i, SDL_GetError() );
				continue;
			}
			
			vidMode_t mode;
			mode.width = m.w;
			mode.height = m.h;
			mode.displayHz = m.refresh_rate ? m.refresh_rate : 60; // default to 60 if unknown (0)
			modeList.AddUnique( mode );
		}
		
		if( modeList.Num() < 1 ) {
			common->Warning( "Couldn't get a single video mode for display %i, using default ones..!\n", requestedDisplayNum );
			FillStaticVidModes( modeList );
		}
		
		// sort with lowest resolution first
		modeList.SortWithTemplate( idSort_VidMode() );
	} else {
		common->Warning( "Can't get Video Info, using default modes...\n" );
		if( numModes < 0 ) {
			common->Warning( "Reason was: %s\n", SDL_GetError() );
		}
		FillStaticVidModes( modeList );
	}
	
	return true;
}
