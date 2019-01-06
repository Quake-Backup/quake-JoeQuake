/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// vid_wgl.c -- Windows 9x/NT OpenGL driver

#include "quakedef.h"
#include "winquake.h"
#include "resource.h"
#include <commctrl.h>

#define MAX_MODE_LIST	128
#define WARP_WIDTH		320
#define WARP_HEIGHT		200
#define MAXWIDTH		10000
#define MAXHEIGHT		10000
#define BASEWIDTH		320
#define BASEHEIGHT		200

#define MODE_WINDOWED	0
#define NO_MODE			(MODE_WINDOWED - 1)
#define MODE_FULLSCREEN_DEFAULT	(MODE_WINDOWED + 1)

qboolean gl_have_stencil = false;

typedef struct
{
	modestate_t	type;
	int		width;
	int		height;
	int		modenum;
	int		dib;
	int		fullscreen;
	int		bpp;
	int		halfscreen;
	char	modedesc[17];
} vmode_t;

typedef struct
{
	int		width;
	int		height;
} lmode_t;

lmode_t	lowresmodes[] =
{
	{320, 200},
	{320, 240},
	{400, 300},
	{512, 384},
};

qboolean	DDActive;
qboolean	scr_skipupdate;

static	vmode_t	modelist[MAX_MODE_LIST];
static	int	nummodes;
static	vmode_t	*pcurrentmode;
static	vmode_t	badmode;

static DEVMODE	gdevmode;
static qboolean	vid_initialized = false;
static qboolean	windowed, leavecurrentmode;
static qboolean vid_canalttab = false;
static qboolean vid_wassuspended = false;
static qboolean windowed_mouse = true;
extern qboolean	mouseactive;	// from in_win.c
static HICON	hIcon;

DWORD	WindowStyle, ExWindowStyle;

HWND	mainwindow, dibwindow;

int		vid_modenum = NO_MODE;
int		vid_default = MODE_WINDOWED;
static int windowed_default;
unsigned char vid_curpal[256*3];
qboolean fullsbardraw = false;

HGLRC	baseRC;
HDC		maindc;

glvert_t glv;

unsigned short	*currentgammaramp = NULL;
static unsigned short systemgammaramp[3][256];

qboolean	vid_gammaworks = false;
qboolean	vid_hwgamma_enabled = false;
qboolean	customgamma = false;

void RestoreHWGamma (void);

HWND WINAPI InitializeWindow (HINSTANCE hInstance, int nCmdShow);

modestate_t	modestate = MS_UNINIT;

int menu_display_freq;
void VID_MenuDraw (void);
void VID_MenuKey (int key);

LONG WINAPI MainWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void AppActivate (BOOL fActive, BOOL minimize);
char *VID_GetModeDescription (int mode);
void ClearAllStates (void);
void VID_UpdateWindowStatus (void);
void GL_Init (void);

qboolean OnChange_vid_mode(cvar_t *var, char *string);
cvar_t		vid_mode = {"vid_mode", "-1", 0, OnChange_vid_mode };

//cvar_t		_vid_default_mode = {"_vid_default_mode", "0", CVAR_ARCHIVE};
//cvar_t		_vid_default_mode_win = {"_vid_default_mode_win", "3", CVAR_ARCHIVE};
//cvar_t		vid_config_x = {"vid_config_x", "800", CVAR_ARCHIVE};
//cvar_t		vid_config_y = {"vid_config_y", "600", CVAR_ARCHIVE};
cvar_t		_windowed_mouse = {"_windowed_mouse", "1", CVAR_ARCHIVE};

qboolean OnChange_vid_displayfrequency(cvar_t *var, char *string); 
cvar_t		vid_displayfrequency = {"vid_displayfrequency", "60", 0, OnChange_vid_displayfrequency };
cvar_t		vid_hwgammacontrol = {"vid_hwgammacontrol", "1"};

qboolean OnChange_vid_con_xxx(cvar_t *var, char *string);
cvar_t      vid_conwidth = { "vid_conwidth", "640", 0, OnChange_vid_con_xxx };
cvar_t      vid_conheight = { "vid_conheight", "0", 0, OnChange_vid_con_xxx }; // default is 0, so i can sort out is user specify conheight on cmd line or something 

// VVD: din't restore gamma after ALT+TAB on some ATI video cards (or drivers?...) 
// HACK!!! FIXME { 
cvar_t		vid_forcerestoregamma = { "vid_forcerestoregamma", "0" };
int restore_gamma = 0;
// }

typedef BOOL (APIENTRY *SWAPINTERVALFUNCPTR)(int);
SWAPINTERVALFUNCPTR wglSwapIntervalEXT = NULL;
static qboolean update_vsync = false;
qboolean OnChange_vid_vsync (cvar_t *var, char *string);
cvar_t		vid_vsync = {"vid_vsync", "0", 0, OnChange_vid_vsync};

int		window_center_x, window_center_y, window_x, window_y, window_width, window_height;
RECT	window_rect;

void GL_WGL_CheckExtensions(void)
{
	if (!COM_CheckParm("-noswapctrl") && CheckExtension("WGL_EXT_swap_control"))
	{
		if ((wglSwapIntervalEXT = (void *)wglGetProcAddress("wglSwapIntervalEXT")))
		{
			Con_Printf("Vsync control extensions found\n");
			Cvar_Register (&vid_vsync);
			update_vsync = true;	// force to update vsync after startup
		}
	}
}

qboolean OnChange_vid_vsync (cvar_t *var, char *string)
{
	update_vsync = true;
	return false;
}

void GL_Init_Win(void) 
{
	GL_WGL_CheckExtensions();
}

// direct draw software compatability stuff

void VID_LockBuffer (void)
{
}

void VID_UnlockBuffer (void)
{
}

void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
}

void D_EndDirectRect (int x, int y, int width, int height)
{
}

void CenterWindow (HWND hWndCenter, int width, int height, BOOL lefttopjustify)
{
	int     CenterX, CenterY;

	CenterX = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
	CenterY = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
	if (CenterX > CenterY * 2)
		CenterX >>= 1;	// dual screens
	CenterX = (CenterX < 0) ? 0 : CenterX;
	CenterY = (CenterY < 0) ? 0 : CenterY;
	SetWindowPos (hWndCenter, NULL, CenterX, CenterY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_DRAWFRAME);
}

int GetBestFreq(int w, int h, int bpp) 
{
	int freq = 0;
	DEVMODE	testMode;

	memset((void*)&testMode, 0, sizeof(testMode));
	testMode.dmSize = sizeof(testMode);

	testMode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
	testMode.dmPelsWidth = w; // here we must pass right value if modelist[vid_modenum].halfscreen
	testMode.dmPelsHeight = h;
	testMode.dmBitsPerPel = bpp;

	for (freq = 301; freq >= 0; freq--)
	{
		testMode.dmDisplayFrequency = freq;
		if (ChangeDisplaySettings(&testMode, CDS_FULLSCREEN | CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
			continue; // mode can't be set

		break; // wow, we found something
	}

	return max(0, freq);
}

int display_freq_modes[20];
int display_freq_modes_num;

void VID_ShowFreq_f(void) 
{
	int freq;
	DEVMODE	testMode;

	if (!vid_initialized || vid_modenum < 0 || vid_modenum >= MAX_MODE_LIST)
		return;

	memset((void*)&testMode, 0, sizeof(testMode));
	testMode.dmSize = sizeof(testMode);

	Con_Printf("Possible display frequency:");

	testMode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
	testMode.dmPelsWidth = modelist[vid_modenum].width << modelist[vid_modenum].halfscreen;
	testMode.dmPelsHeight = modelist[vid_modenum].height;
	testMode.dmBitsPerPel = modelist[vid_modenum].bpp;

	memset(display_freq_modes, 0, sizeof(display_freq_modes));
	display_freq_modes_num = 0;

	for (freq = 1; freq < 301; freq++)
	{
		testMode.dmDisplayFrequency = freq;
		if (ChangeDisplaySettings(&testMode, CDS_FULLSCREEN | CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
			continue; // mode can't be set

		Con_Printf(" %d", freq);
		
		display_freq_modes[display_freq_modes_num] = freq;
		display_freq_modes_num++;
	}

	Con_Printf("%s\n", display_freq_modes_num ? "" : " none");
}

int GetCurrentFreq(void) 
{
	DEVMODE	testMode;

	memset((void*)&testMode, 0, sizeof(testMode));
	testMode.dmSize = sizeof(testMode);
	return EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &testMode) ? testMode.dmDisplayFrequency : 0;
}

qboolean ChangeFreq(int freq) 
{
	DWORD oldFields = gdevmode.dmFields, oldFreq = gdevmode.dmDisplayFrequency; // so we can return old values if we failed

	if (!vid_initialized || !host_initialized)
		return true; // hm, -freq xxx or +set vid_displayfrequency xxx cmdline params? allow then

	if (!ActiveApp || Minimized || !vid_canalttab || vid_wassuspended) 
	{
		Con_Printf("Can't switch display frequency while minimized\n");
		return false;
	}

	if (modestate != MS_FULLDIB) 
	{
		Con_Printf("Can't switch display frequency in non full screen mode\n");
		return false;
	}

	if (GetCurrentFreq() == freq) 
	{
		Con_Printf("Display frequency %d already set\n", freq);
		return false;
	}

	gdevmode.dmDisplayFrequency = freq;
	gdevmode.dmFields |= DM_DISPLAYFREQUENCY;

	if (ChangeDisplaySettings(&gdevmode, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL) 
	{
		Con_Printf("Can't switch display frequency to %d\n", freq);
		gdevmode.dmDisplayFrequency = oldFreq;
		gdevmode.dmFields = oldFields;
		return false;
	}

	Con_Printf("Switching display frequency to %d\n", freq);

	return true;
}

qboolean OnChange_vid_displayfrequency(cvar_t *var, char *string) 
{
	return !ChangeFreq(Q_atoi(string));
}

void SetWidthWithSbarScale(void)
{
	float sbar_scale_amount;
	extern cvar_t scr_sbarscale_amount;

	sbar_scale_amount = bound(1, scr_sbarscale_amount.value, 4);
	vid.width = vid.conwidth / sbar_scale_amount;
}

void SetHeightWithSbarScale(void)
{
	float sbar_scale_amount;
	extern cvar_t scr_sbarscale_amount;

	sbar_scale_amount = bound(1, scr_sbarscale_amount.value, 4);
	vid.height = vid.conheight / sbar_scale_amount;
}

qboolean OnChange_vid_con_xxx(cvar_t *var, char *string) 
{
	// this is safe but do not allow set this variables from cmd line
	//	if (!vid_initialized || !host_initialized || vid_modenum < 0 || vid_modenum >= nummodes)
	//		return true;

	if (var == &vid_conwidth) 
	{
		int width = Q_atoi(string);

		width = max(320, width);
		width &= 0xfff8; // make it a multiple of eight

		vid.conwidth = width;
		SetWidthWithSbarScale();

		Cvar_SetValue(var, (float)width);

		Draw_AdjustConback();

		vid.recalc_refdef = 1;

		return true;
	}

	if (var == &vid_conheight) 
	{
		int height = Q_atoi(string);

		height = max(200, height);
//		height &= 0xfff8; // make it a multiple of eight

		vid.conheight = height;
		SetHeightWithSbarScale();

		Cvar_SetValue(var, (float)height);

		Draw_AdjustConback();

		vid.recalc_refdef = 1;

		return true;
	}

	return true;
}

qboolean VID_SetWindowedMode (int modenum)
{
	HDC	hdc;
	int	lastmodestate, width, height;
	RECT	rect;

	lastmodestate = modestate;

	rect.top = rect.left = 0;
	rect.right = modelist[modenum].width;
	rect.bottom = modelist[modenum].height;

	window_width = modelist[modenum].width;
	window_height = modelist[modenum].height;

	WindowStyle = WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	ExWindowStyle = 0;

	AdjustWindowRectEx (&rect, WindowStyle, FALSE, 0);

	width = rect.right - rect.left;
	height = rect.bottom - rect.top;

	// Create the DIB window
	if (!dibwindow) // create window, if not exist yet 
		dibwindow = CreateWindowEx (
			 ExWindowStyle,
			 "JoeQuake",
			 "JoeQuake",
			 WindowStyle,
			 rect.left, rect.top,
			 width,
			 height,
			 NULL,
			 NULL,
			 global_hInstance,
			 NULL);
	else // just update size
		SetWindowPos(dibwindow, NULL, 0, 0, width, height, 0);

	if (!dibwindow)
		Sys_Error ("Couldn't create DIB window");

	// Center and show the DIB window
	CenterWindow (dibwindow, modelist[modenum].width, modelist[modenum].height, false);

	ShowWindow (dibwindow, SW_SHOWDEFAULT);
	UpdateWindow (dibwindow);

	modestate = MS_WINDOWED;

// because we have set the background brush for the window to NULL
// (to avoid flickering when re-sizing the window on the desktop),
// we clear the window to black when created, otherwise it will be
// empty while Quake starts up.
	hdc = GetDC (dibwindow);
	PatBlt (hdc, 0, 0, modelist[modenum].width, modelist[modenum].height, BLACKNESS);
	ReleaseDC (dibwindow, hdc);

	vid.conwidth = modelist[modenum].width;
	SetWidthWithSbarScale();
	vid.conheight = modelist[modenum].height;
	SetHeightWithSbarScale();

	vid.numpages = 2;

	mainwindow = dibwindow;

	SendMessage (mainwindow, WM_SETICON, (WPARAM)TRUE, (LPARAM)hIcon);
	SendMessage (mainwindow, WM_SETICON, (WPARAM)FALSE, (LPARAM)hIcon);

	return true;
}

qboolean VID_SetFullDIBMode (int modenum)
{
	int	lastmodestate, width, height;
	HDC	hdc;
	RECT	rect;

	if (!leavecurrentmode)
	{
		gdevmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
		gdevmode.dmBitsPerPel = modelist[modenum].bpp;
		gdevmode.dmPelsWidth = modelist[modenum].width << modelist[modenum].halfscreen;
		gdevmode.dmPelsHeight = modelist[modenum].height;
		gdevmode.dmSize = sizeof (gdevmode);

		if (vid_displayfrequency.value) // freq was somehow specified, use it
			gdevmode.dmDisplayFrequency = vid_displayfrequency.value;
		else // guess best possible freq
			gdevmode.dmDisplayFrequency = GetBestFreq(gdevmode.dmPelsWidth, gdevmode.dmPelsHeight, gdevmode.dmBitsPerPel);
		gdevmode.dmFields |= DM_DISPLAYFREQUENCY;

		if (ChangeDisplaySettings(&gdevmode, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL) 
		{
			gdevmode.dmFields &= ~DM_DISPLAYFREQUENCY; // okay trying default refresh rate (60hz?) then
			if (ChangeDisplaySettings(&gdevmode, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
				Sys_Error("Couldn't set fullscreen DIB mode"); // failed for default refresh rate too, bad luck :E
		}

		gdevmode.dmDisplayFrequency = GetCurrentFreq();
		Cvar_SetValue(&vid_displayfrequency, (float)(int)gdevmode.dmDisplayFrequency); // so variable will we set to actual value (sometimes this fail, but does't cause any damage)
	}

	lastmodestate = modestate;
	modestate = MS_FULLDIB;

	rect.top = rect.left = 0;
	rect.right = modelist[modenum].width;
	rect.bottom = modelist[modenum].height;

	window_width = modelist[modenum].width;
	window_height = modelist[modenum].height;

	WindowStyle = WS_POPUP;
	ExWindowStyle = 0;

	AdjustWindowRectEx (&rect, WindowStyle, FALSE, 0);

	width = rect.right - rect.left;
	height = rect.bottom - rect.top;

	// Create the DIB window
	if (!dibwindow) // create window, if not exist yet 
		dibwindow = CreateWindowEx (
			 ExWindowStyle,
			 "JoeQuake",
			 "JoeQuake",
			 WindowStyle,
			 rect.left, rect.top,
			 width,
			 height,
			 NULL,
			 NULL,
			 global_hInstance,
			 NULL);
	else // just update size
		SetWindowPos(dibwindow, NULL, 0, 0, width, height, 0);

	if (!dibwindow)
		Sys_Error ("Couldn't create DIB window");

	ShowWindow (dibwindow, SW_SHOWDEFAULT);
	UpdateWindow (dibwindow);

	// Because we have set the background brush for the window to NULL
	// (to avoid flickering when re-sizing the window on the desktop),
	// we clear the window to black when created, otherwise it will be
	// empty while Quake starts up.
	hdc = GetDC (dibwindow);
	PatBlt (hdc, 0, 0, modelist[modenum].width, modelist[modenum].height, BLACKNESS);
	ReleaseDC (dibwindow, hdc);

	vid.conwidth = modelist[modenum].width;
	SetWidthWithSbarScale();
	vid.conheight = modelist[modenum].height;
	SetHeightWithSbarScale();

	vid.numpages = 2;

// needed because we're not getting WM_MOVE messages fullscreen on NT
	window_x = 0;
	window_y = 0;

	mainwindow = dibwindow;

	SendMessage (mainwindow, WM_SETICON, (WPARAM)TRUE, (LPARAM)hIcon);
	SendMessage (mainwindow, WM_SETICON, (WPARAM)FALSE, (LPARAM)hIcon);

	return true;
}

int VID_SetMode (int modenum, unsigned char *palette)
{
	int			temp;
	qboolean	stat;

	//if ((windowed && modenum) || (!windowed && (modenum < 1)) || (!windowed && (modenum >= nummodes)))
	if (modenum < 0 || modenum >= nummodes)
		Sys_Error ("Bad video mode");

	// so Con_Printfs don't mess us up by forcing vid and snd updates
	temp = scr_disabled_for_loading;
	scr_disabled_for_loading = true;

	CDAudio_Pause ();

	// Set either the fullscreen or windowed mode
	if (modelist[modenum].type == MS_WINDOWED)
	{
		if (_windowed_mouse.value && key_dest == key_game)
		{
			stat = VID_SetWindowedMode (modenum);
			IN_ActivateMouse ();
			IN_HideMouse ();
		}
		else
		{
			IN_DeactivateMouse ();
			IN_ShowMouse ();
			stat = VID_SetWindowedMode (modenum);
		}
	}
	else if (modelist[modenum].type == MS_FULLDIB)
	{
		stat = VID_SetFullDIBMode (modenum);
		IN_ActivateMouse ();
		IN_HideMouse ();
	}
	else
	{
		Sys_Error ("VID_SetMode: Bad mode type in modelist");
	}

	VID_UpdateWindowStatus ();

	CDAudio_Resume ();
	scr_disabled_for_loading = temp;

	if (!stat)
		Sys_Error ("Couldn't set video mode");

// now we try to make sure we get the focus on the mode switch, because
// sometimes in some systems we don't. We grab the foreground, then
// finish setting up, pump all our messages, and sleep for a little while
// to let messages finish bouncing around the system, then we put
// ourselves at the top of the z order, then grab the foreground again,
// Who knows if it helps, but it probably doesn't hurt
	SetForegroundWindow (mainwindow);
	//VID_SetPalette (palette);
	vid_modenum = modenum;
	Cvar_SetValue (&vid_mode, (float)vid_modenum);

// { after vid_modenum set we can safe do this
	Cvar_SetValue(&vid_conwidth, (float)modelist[vid_modenum].width);
	Cvar_SetValue(&vid_conheight, (float)modelist[vid_modenum].height);
	Draw_AdjustConback(); // need this even vid_conwidth have callback which leads to call this
// }

	//while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	//{
	//	TranslateMessage (&msg);
	//	DispatchMessage (&msg);
	//}

	//Sleep (100);

	SetWindowPos (mainwindow, HWND_TOP, 0, 0, 0, 0, SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
	SetForegroundWindow (mainwindow);

// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

//joe	if (!msg_suppress_1)
	Con_Printf ("Video mode %s initialized\n", VID_GetModeDescription(vid_modenum));

	//VID_SetPalette (palette);

	vid.recalc_refdef = 1;

	return true;
}

/*
================
VID_UpdateWindowStatus
================
*/
void VID_UpdateWindowStatus (void)
{
	window_rect.left = window_x;
	window_rect.top = window_y;
	window_rect.right = window_x + window_width;
	window_rect.bottom = window_y + window_height;
	window_center_x = (window_rect.left + window_rect.right) / 2;
	window_center_y = (window_rect.top + window_rect.bottom) / 2;

	IN_UpdateClipCursor ();
}

//=================================================================

/*
=================
GL_BeginRendering
=================
*/
void GL_BeginRendering (int *x, int *y, int *width, int *height)
{
	*x = *y = 0;
	*width = window_width;
	*height = window_height;
}

/*
=================
GL_EndRendering
=================
*/
void GL_EndRendering (void)
{
	static qboolean	old_hwgamma_enabled;

	vid_hwgamma_enabled = vid_hwgammacontrol.value && vid_gammaworks && ActiveApp && !Minimized;
	vid_hwgamma_enabled = vid_hwgamma_enabled && (modestate == MS_FULLDIB || vid_hwgammacontrol.value == 2);
	if (vid_hwgamma_enabled != old_hwgamma_enabled)
	{
		old_hwgamma_enabled = vid_hwgamma_enabled;
		if (vid_hwgamma_enabled && currentgammaramp)
			VID_SetDeviceGammaRamp (currentgammaramp);
		else
			RestoreHWGamma ();
	}

	if (!scr_skipupdate || block_drawing)
	{
		if (wglSwapIntervalEXT && update_vsync && vid_vsync.string[0])
			wglSwapIntervalEXT (vid_vsync.value ? 1 : 0);
		update_vsync = false;

#ifdef USEFAKEGL
		FakeSwapBuffers ();
#else
		SwapBuffers (maindc);
#endif
	}

	// handle the mouse state when windowed if that's changed
	if (modestate == MS_WINDOWED)
	{
		if (!_windowed_mouse.value)
		{
			if (windowed_mouse)
			{
				IN_DeactivateMouse ();
				IN_ShowMouse ();
				windowed_mouse = false;
			}
		}
		else
		{
			windowed_mouse = true;
			if (key_dest == key_game && !mouseactive && ActiveApp)
			{
				IN_ActivateMouse ();
				IN_HideMouse ();
			}
			else if (mouseactive && key_dest != key_game)
			{
				IN_DeactivateMouse ();
				IN_ShowMouse ();
			}
		}
	}

	if (fullsbardraw)
		Sbar_Changed ();
}

void VID_SetDefaultMode (void)
{
	IN_DeactivateMouse ();
}

void VID_ShiftPalette (unsigned char *palette)
{
}

/*
======================
VID_SetDeviceGammaRamp

Note: ramps must point to a static array
======================
*/
void VID_SetDeviceGammaRamp (unsigned short *ramps)
{
	if (vid_gammaworks)
	{
		currentgammaramp = ramps;
		if (vid_hwgamma_enabled)
		{
			SetDeviceGammaRamp (maindc, ramps);
			customgamma = true;
		}
	}
}

void InitHWGamma (void)
{
	if (COM_CheckParm("-nohwgamma"))
		return;

	vid_gammaworks = GetDeviceGammaRamp (maindc, systemgammaramp);
	if (vid_gammaworks && !COM_CheckParm("-nogammareset"))
	{
		int i, j;
		for (i = 0; i < 3; i++)
			for (j = 0; j < 256; j++)
				systemgammaramp[i][j] = (j << 8);
	}
}

void RestoreHWGamma (void)
{
	if (vid_gammaworks && customgamma)
	{
		customgamma = false;
		SetDeviceGammaRamp (maindc, systemgammaramp);
	}
}

//=================================================================

void VID_Shutdown (void)
{
   	HGLRC	hRC;
   	HDC		hDC;

	if (vid_initialized)
	{
		RestoreHWGamma ();

		vid_canalttab = false;
		hRC = wglGetCurrentContext ();
		hDC = wglGetCurrentDC ();

		wglMakeCurrent (NULL, NULL);

		if (hRC)
			wglDeleteContext (hRC);

		if (hDC && dibwindow)
			ReleaseDC (dibwindow, hDC);

		if (modestate == MS_FULLDIB)
			ChangeDisplaySettings (NULL, 0);

		if (maindc && dibwindow)
			ReleaseDC (dibwindow, maindc);

		AppActivate (false, false);
	}
}

int bChosePixelFormat(HDC hDC, PIXELFORMATDESCRIPTOR *pfd, PIXELFORMATDESCRIPTOR *retpfd)
{
	int	pixelformat;

	if (!(pixelformat = ChoosePixelFormat(hDC, pfd)))
	{
		MessageBox (NULL, "ChoosePixelFormat failed", "Error", MB_OK);
		return 0;
	}

	if (!(DescribePixelFormat(hDC, pixelformat, sizeof(PIXELFORMATDESCRIPTOR), retpfd)))
	{
		MessageBox(NULL, "DescribePixelFormat failed", "Error", MB_OK);
		return 0;
	}

	return pixelformat;
}

qboolean OnChange_vid_mode(cvar_t *var, char *string)
{
	int modenum;

	if (!vid_initialized || !host_initialized)
		return false; // set from cmd line or from VID_Init(), allow change but do not issue callback

	if (leavecurrentmode) 
	{
		Con_Printf("Can't switch vid mode when using -current cmd line parammeter\n");
		return true;
	}

	if (!ActiveApp || Minimized || !vid_canalttab || vid_wassuspended) 
	{
		Con_Printf("Can't switch vid mode while minimized\n");
		return true;
	}

	modenum = Q_atoi(string);

	if (host_initialized) // exec or cfg_load or may be from console, prevent set same mode again, no hurt but less annoying
	{ 
		if (modenum == vid_mode.value) 
		{
			Con_Printf("Vid mode %d alredy set\n", modenum);
			return true;
		}
	}

	if (modenum < 0 || modenum >= nummodes
		|| (windowed && modelist[modenum].type != MS_WINDOWED)
		|| (!windowed && modelist[modenum].type != MS_FULLDIB))
	{
		Con_Printf("Invalid vid mode %d\n", modenum);
		return true;
	}

	// we call a few Cvar_SetValues in VID_SetMode and in deeper functions but their callbacks will not be triggered
	VID_SetMode(modenum, host_basepal);

	Cbuf_AddText("v_cshift 0 0 0 1\n");	//FIXME

	return true;
}

BOOL bSetupPixelFormat (HDC hDC)
{
	int	pixelformat;
	PIXELFORMATDESCRIPTOR retpfd, pfd = {
		sizeof(PIXELFORMATDESCRIPTOR),	// size of this pfd
		1,						// version number
		PFD_DRAW_TO_WINDOW 		// support window
		| PFD_SUPPORT_OPENGL 	// support OpenGL
		| PFD_DOUBLEBUFFER,		// double buffered
		PFD_TYPE_RGBA,			// RGBA type
		24,						// 24-bit color depth
		0, 0, 0, 0, 0, 0,		// color bits ignored
		0,						// no alpha buffer
		0,						// shift bit ignored
		0,						// no accumulation buffer
		0, 0, 0, 0, 			// accum bits ignored
		24,						// 24-bit z-buffer	
		8,						// 8-bit stencil buffer
		0,						// no auxiliary buffer
		PFD_MAIN_PLANE,			// main layer
		0,						// reserved
		0, 0, 0					// layer masks ignored
	};

	if (!(pixelformat = bChosePixelFormat(hDC, &pfd, &retpfd)))
		return FALSE;

	if (retpfd.cDepthBits < 24)
	{
		pfd.cDepthBits = 24;
		if (!(pixelformat = bChosePixelFormat(hDC, &pfd, &retpfd)))
			return FALSE;
	}

	if (!SetPixelFormat(hDC, pixelformat, &retpfd))
	{
		MessageBox(NULL, "SetPixelFormat failed", "Error", MB_OK);
		return FALSE;
	}

	if (retpfd.cDepthBits < 24)
		gl_allow_ztrick = false;

	gl_have_stencil = true;
	return TRUE;
}

/*
===================================================================

MAIN WINDOW

===================================================================
*/

/*
================
ClearAllStates
================
*/
void ClearAllStates (void)
{
	int	i;
	
// send an up event for each key, to make sure the server clears them all
	for (i=0 ; i<256 ; i++)
	{
		if (keydown[i])
			Key_Event (i, false);
	}

	Key_ClearStates ();
	IN_ClearStates ();
}

void AppActivate (BOOL fActive, BOOL minimize)
/****************************************************************************
*
* Function:     AppActivate
* Parameters:   fActive - True if app is activating
*
* Description:  If the application is activating, then swap the system
*               into SYSPAL_NOSTATIC mode so that our palettes will display
*               correctly.
*
****************************************************************************/
{
	static BOOL	sound_active;

	ActiveApp = fActive;
	Minimized = minimize;

// enable/disable sound on focus gain/loss
	if (!ActiveApp && sound_active)
	{
		S_BlockSound ();
		sound_active = false;
	}
	else if (ActiveApp && !sound_active)
	{
		S_UnblockSound ();
		sound_active = true;
	}

	if (fActive)
	{
		if (modestate == MS_FULLDIB)
		{
			IN_ActivateMouse ();
			IN_HideMouse ();

			if (vid_canalttab && vid_wassuspended)
			{
				vid_wassuspended = false;
				if (ChangeDisplaySettings (&gdevmode, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
					Sys_Error ("Couldn't set fullscreen DIB mode");
				ShowWindow (mainwindow, SW_SHOWNORMAL);

				// Fix for alt-tab bug in NVidia drivers
				MoveWindow (mainwindow, 0, 0, gdevmode.dmPelsWidth, gdevmode.dmPelsHeight, false);
				
				// scr_fullupdate = 0;
				Sbar_Changed ();
			}
		}
		else if (modestate == MS_WINDOWED && Minimized)
			ShowWindow (mainwindow, SW_RESTORE);
		else if ((modestate == MS_WINDOWED) && _windowed_mouse.value && key_dest == key_game)
		{
			IN_ActivateMouse ();
			IN_HideMouse ();
		}

		if ((vid_canalttab && !Minimized) && currentgammaramp) 
		{
			VID_SetDeviceGammaRamp(currentgammaramp);
			// VVD: din't restore gamma after ALT+TAB on some ATI video cards (or drivers?...)
			// HACK!!! FIXME {
			if (restore_gamma == 0 && (int)vid_forcerestoregamma.value)
				restore_gamma = 1;
			// }
		}
	}
	else
	{
		RestoreHWGamma();
		if (modestate == MS_FULLDIB)
		{
			IN_DeactivateMouse ();
			IN_ShowMouse ();
			if (vid_canalttab) 
			{ 
				ChangeDisplaySettings (NULL, 0);
				vid_wassuspended = true;
			}
		}
		else if ((modestate == MS_WINDOWED) && _windowed_mouse.value)
		{
			IN_DeactivateMouse ();
			IN_ShowMouse ();
		}
	}
}

LONG CDAudio_MessageHandler (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
int IN_MapKey (int key);
void MW_Hook_Message (long buttons);

#define WM_MWHOOK (WM_USER + 1)

/*
=============
Main Window procedure
=============
*/
LONG WINAPI MainWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	int	fActive, fMinimized, temp;
	LONG	lRet = 1;
	extern	unsigned int	uiWheelMessage;
	extern	cvar_t		cl_confirmquit;

	// VVD: din't restore gamma after ALT+TAB on some ATI video cards (or drivers?...)
	// HACK!!! FIXME {
	static time_t time_old;
	if (restore_gamma == 2 && currentgammaramp) 
	{
		if (time(NULL) - time_old > 0) 
		{
			VID_SetDeviceGammaRamp(currentgammaramp);
			restore_gamma = 0;
		}
	}
	// }

	if (uMsg == uiWheelMessage)
	{
		uMsg = WM_MOUSEWHEEL;
		wParam <<= 16;
	}

	switch (uMsg)
	{
	case WM_KILLFOCUS:
		if (modestate == MS_FULLDIB)
			ShowWindow(mainwindow, SW_SHOWMINNOACTIVE);
		break;

	case WM_CREATE:
		break;

	case WM_MOVE:
		window_x = (int) LOWORD(lParam);
		window_y = (int) HIWORD(lParam);
		VID_UpdateWindowStatus ();
		break;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		Key_Event (IN_MapKey(lParam), true);
		break;
			
	case WM_KEYUP:
	case WM_SYSKEYUP:
		Key_Event (IN_MapKey(lParam), false);
		break;

	case WM_SYSCHAR:
	// keep Alt-Space from happening
		break;

// this is complicated because Win32 seems to pack multiple mouse events into
// one update sometimes, so we always check all states and look for events
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
		temp = 0;

		if (wParam & MK_LBUTTON)
			temp |= 1;

		if (wParam & MK_RBUTTON)
			temp |= 2;

		if (wParam & MK_MBUTTON)
			temp |= 4;

		IN_MouseEvent (temp);

		break;

	// JACK: This is the mouse wheel with the Intellimouse
	// Its delta is either positive or neg, and we generate the proper
	// Event.
	case WM_MOUSEWHEEL: 
		if ((short)HIWORD(wParam) > 0)
		{
			Key_Event (K_MWHEELUP, true);
			Key_Event (K_MWHEELUP, false);
		}
		else
		{
			Key_Event (K_MWHEELDOWN, true);
			Key_Event (K_MWHEELDOWN, false);
		}
		break;

	case WM_MWHOOK:
		MW_Hook_Message (lParam);
		break;

	case WM_SIZE:
		break;

	case WM_CLOSE:
		if (!cl_confirmquit.value || 
		    MessageBox(mainwindow, "Are you sure you want to quit?", "Confirm Exit", MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION) == IDYES)
			Sys_Quit ();
	        break;

	case WM_ACTIVATE:
		fActive = LOWORD(wParam);
		fMinimized = (BOOL)HIWORD(wParam);
		AppActivate (!(fActive == WA_INACTIVE), fMinimized);

		// VVD: din't restore gamma after ALT+TAB on some ATI video cards (or drivers?...)
		// HACK!!! FIXME {
		if (restore_gamma == 1) 
		{
			time_old = time(NULL);
			restore_gamma = 2;
		}
		// }

		// fix the leftover Alt from any Alt-Tab or the like that switched us away
		ClearAllStates ();

		break;

	case WM_DESTROY:
		if (dibwindow)
			DestroyWindow (dibwindow);
		PostQuitMessage (0);
		break;

	case MM_MCINOTIFY:
		lRet = CDAudio_MessageHandler (hWnd, uMsg, wParam, lParam);
		break;

	default:
	// pass all unhandled messages to DefWindowProc
		lRet = DefWindowProc (hWnd, uMsg, wParam, lParam);
		break;
	}

	// return 1 if handled message, 0 if not
	return lRet;
}

/*
=================
VID_NumModes
=================
*/
int VID_NumModes (void)
{
	return nummodes;
}
	
/*
=================
VID_GetModePtr
=================
*/
vmode_t *VID_GetModePtr (int modenum)
{
	if ((modenum >= 0) && (modenum < nummodes))
		return &modelist[modenum];

	return &badmode;
}

/*
=================
VID_GetModeDescription
=================
*/
char *VID_GetModeDescription (int mode)
{
	char		*pinfo;
	vmode_t		*pv;
	static char temp[100];

	if ((mode < 0) || (mode >= nummodes))
		return NULL;

	if (!leavecurrentmode)
	{
		pv = VID_GetModePtr (mode);
		pinfo = pv->modedesc;
	}
	else
	{
		sprintf (temp, "Desktop resolution (%dx%d)", modelist[MODE_FULLSCREEN_DEFAULT].width, modelist[MODE_FULLSCREEN_DEFAULT].height);
		pinfo = temp;
	}

	return pinfo;
}

// KJB: Added this to return the mode driver name in description for console

char *VID_GetExtModeDescription (int mode)
{
	static char pinfo[40];
	vmode_t		*pv;

	if ((mode < 0) || (mode >= nummodes))
		return NULL;

	pv = VID_GetModePtr (mode);
	if (modelist[mode].type == MS_FULLDIB)
	{
		if (!leavecurrentmode)
			sprintf(pinfo, "%12s fullscreen", pv->modedesc); // "%dx%dx%d" worse is WWWWxHHHHxBB
		else
			sprintf (pinfo, "Desktop resolution (%dx%d)", modelist[MODE_FULLSCREEN_DEFAULT].width, modelist[MODE_FULLSCREEN_DEFAULT].height);
	}
	else
	{
		if (modestate == MS_WINDOWED)
			sprintf(pinfo, "%12s windowed", pv->modedesc); //  "%dx%d" worse is WWWWxHHHH 
		else
			sprintf (pinfo, "windowed");
	}

	return pinfo;
}

void VID_ModeList_f(void) 
{
	int i, lnummodes, t, width = -1, height = -1, bpp = -1;
	char *pinfo;
	vmode_t *pv;

	if ((i = Cmd_CheckParm("-w")) && i + 1 < Cmd_Argc())
		width = Q_atoi(Cmd_Argv(i + 1));

	if ((i = Cmd_CheckParm("-h")) && i + 1 < Cmd_Argc())
		height = Q_atoi(Cmd_Argv(i + 1));

	if ((i = Cmd_CheckParm("-b")) && i + 1 < Cmd_Argc())
		bpp = Q_atoi(Cmd_Argv(i + 1));

	if ((i = Cmd_CheckParm("b32")))
		bpp = 32;

	if ((i = Cmd_CheckParm("b16")))
		bpp = 16;

	lnummodes = VID_NumModes();

	t = leavecurrentmode;
	leavecurrentmode = 0;

	for (i = 1; i < lnummodes; i++) {
		if (width != -1 && modelist[i].width != width)
			continue;

		if (height != -1 && modelist[i].height != height)
			continue;

		if (bpp != -1 && modelist[i].bpp != bpp)
			continue;

		pv = VID_GetModePtr(i);
		pinfo = VID_GetExtModeDescription(i);
		Con_Printf("%3d: %s\n", i, pinfo);
	}

	leavecurrentmode = t;
}

/*
=================
VID_DescribeCurrentMode_f
=================
*/
void VID_DescribeCurrentMode_f (void)
{
	Con_Printf ("%s\n", VID_GetExtModeDescription(vid_modenum));
}

/*
=================
VID_NumModes_f
=================
*/
void VID_NumModes_f (void)
{
	Con_Printf ("Number of available video modes: %d\n", nummodes);
}

/*
=================
VID_DescribeMode_f
=================
*/
void VID_DescribeMode_f (void)
{
	int	t, modenum;

	modenum = Q_atoi(Cmd_Argv(1));

	t = leavecurrentmode;
	leavecurrentmode = 0;

	Con_Printf ("%s\n", VID_GetExtModeDescription(modenum));

	leavecurrentmode = t;
}

/*
=================
VID_DescribeModes_f
=================
*/
void VID_DescribeModes_f (void)
{
	int	i, lnummodes, t;
	char	*pinfo;

	lnummodes = VID_NumModes ();

	t = leavecurrentmode;
	leavecurrentmode = 0;

	for (i=1 ; i<lnummodes ; i++)
	{
		pinfo = VID_GetExtModeDescription (i);
		Con_Printf ("%2d: %s\n", i, pinfo);
	}

	leavecurrentmode = t;
}

void VID_InitDIB (HINSTANCE hInstance)
{
	int		i;
	WNDCLASS	wc;

	/* Register the frame class */
	wc.style         = 0;
	wc.lpfnWndProc   = (WNDPROC)MainWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = 0;
	wc.hCursor       = LoadCursor (NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName  = 0;
	wc.lpszClassName = "JoeQuake";

	if (!RegisterClass(&wc))
		Sys_Error ("Couldn't register window class");

	modelist[0].type = MS_WINDOWED;

	if ((i = COM_CheckParm("-width")) && i + 1 < com_argc)
		modelist[0].width = Q_atoi(com_argv[i+1]);
	else
		modelist[0].width = 640;

	if (modelist[0].width < 320)
		modelist[0].width = 320;

	if ((i = COM_CheckParm("-height")) && i + 1 < com_argc)
		modelist[0].height= Q_atoi(com_argv[i+1]);

	if (modelist[0].height < 240)
		modelist[0].height = 240;

	sprintf (modelist[0].modedesc, "%dx%d", modelist[0].width, modelist[0].height);

	modelist[0].modenum = MODE_WINDOWED;
	modelist[0].dib = 1;
	modelist[0].fullscreen = 0;
	modelist[0].halfscreen = 0;
	modelist[0].bpp = 0;

	nummodes = 1;
}

/*
=================
VID_InitFullDIB
=================
*/
void VID_InitFullDIB (HINSTANCE hInstance)
{
	DEVMODE	devmode;
	int	i, j, bpp, done, modenum, originalnummodes, existingmode, numlowresmodes;
	BOOL	stat;

// enumerate >8 bpp modes
	originalnummodes = nummodes;
	modenum = 0;

	do {
		stat = EnumDisplaySettings (NULL, modenum, &devmode);

		if ((devmode.dmBitsPerPel >= 15) &&
		    (devmode.dmPelsWidth <= MAXWIDTH) &&
		    (devmode.dmPelsHeight <= MAXHEIGHT) &&
		    (nummodes < MAX_MODE_LIST))
		{
			devmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

			if (ChangeDisplaySettings(&devmode, CDS_TEST | (windowed ? 0 : CDS_FULLSCREEN)) == DISP_CHANGE_SUCCESSFUL)
			{
				modelist[nummodes].type = (windowed ? MS_WINDOWED : MS_FULLDIB);
				modelist[nummodes].width = devmode.dmPelsWidth;
				modelist[nummodes].height = devmode.dmPelsHeight;
				modelist[nummodes].modenum = 0;
				modelist[nummodes].halfscreen = 0;
				modelist[nummodes].dib = 1;
				modelist[nummodes].fullscreen = (windowed ? 0 : 1);
				modelist[nummodes].bpp = (windowed ? 0 : devmode.dmBitsPerPel);

				if (windowed)
					sprintf(modelist[nummodes].modedesc, "%dx%d", devmode.dmPelsWidth, devmode.dmPelsHeight);
				else
					sprintf(modelist[nummodes].modedesc, "%dx%dx%d", devmode.dmPelsWidth, devmode.dmPelsHeight, devmode.dmBitsPerPel);

			// if the width is more than twice the height, reduce it by half because this
			// is probably a dual-screen monitor
				if (!COM_CheckParm("-noadjustaspect"))
				{
					if (modelist[nummodes].width > (modelist[nummodes].height << 1))
					{
						modelist[nummodes].width >>= 1;
						modelist[nummodes].halfscreen = 1;
						sprintf (modelist[nummodes].modedesc, "%dx%dx%d", modelist[nummodes].width, modelist[nummodes].height, modelist[nummodes].bpp);
					}
				}

				for (i = originalnummodes, existingmode = 0 ; i < nummodes ; i++)
				{
					if ((modelist[nummodes].width == modelist[i].width) &&
					    (modelist[nummodes].height == modelist[i].height) &&
					    (modelist[nummodes].bpp == modelist[i].bpp))
					{
						existingmode = 1;
						break;
					}
				}

				if (!existingmode)
					nummodes++;
			}
		}

		modenum++;
	} while (stat);

// see if there are any low-res modes that aren't being reported
	numlowresmodes = sizeof(lowresmodes) / sizeof(lowresmodes[0]);
	bpp = 16;
	done = 0;

	do {
		for (j = 0 ; j < numlowresmodes && nummodes < MAX_MODE_LIST ; j++)
		{
			devmode.dmBitsPerPel = bpp;
			devmode.dmPelsWidth = lowresmodes[j].width;
			devmode.dmPelsHeight = lowresmodes[j].height;
			devmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

			if (ChangeDisplaySettings(&devmode, CDS_TEST | (windowed ? 0 : CDS_FULLSCREEN)) == DISP_CHANGE_SUCCESSFUL)
			{
				modelist[nummodes].type = (windowed ? MS_WINDOWED : MS_FULLDIB);
				modelist[nummodes].width = devmode.dmPelsWidth;
				modelist[nummodes].height = devmode.dmPelsHeight;
				modelist[nummodes].modenum = 0;
				modelist[nummodes].halfscreen = 0;
				modelist[nummodes].dib = 1;
				modelist[nummodes].fullscreen = (windowed ? 0 : 1);
				modelist[nummodes].bpp = (windowed ? 0 : devmode.dmBitsPerPel);

				if (windowed)
					sprintf(modelist[nummodes].modedesc, "%dx%d", devmode.dmPelsWidth, devmode.dmPelsHeight);
				else
					sprintf(modelist[nummodes].modedesc, "%dx%dx%d", devmode.dmPelsWidth, devmode.dmPelsHeight, devmode.dmBitsPerPel);

				for (i=originalnummodes, existingmode = 0 ; i<nummodes ; i++)
				{
					if ((modelist[nummodes].width == modelist[i].width) &&
					    (modelist[nummodes].height == modelist[i].height) &&
					    (modelist[nummodes].bpp == modelist[i].bpp))
					{
						existingmode = 1;
						break;
					}
				}

				if (!existingmode)
					nummodes++;
			}
		}

		switch (bpp)
		{
			case 16:
				bpp = 32;
				break;

			case 32:
				bpp = 24;
				break;

			case 24:
				done = 1;
				break;
		}
	} while (!done);

	if (nummodes == originalnummodes)
		Con_Printf ("No fullscreen DIB modes found\n");
}

void VID_Restart_f(void);

/*
===================
VID_Init
===================
*/
void VID_Init (unsigned char *palette)
{
	int		i, temp, basenummodes, width, height, bpp, findbpp, done;
	HDC		hdc;
	DEVMODE	devmode;
	float	aspect;

	if (COM_CheckParm("-window"))
		windowed = true;

	memset (&devmode, 0, sizeof(devmode));

	Cvar_Register (&vid_mode);
	//Cvar_Register (&_vid_default_mode);
	//Cvar_Register (&_vid_default_mode_win);
	//Cvar_Register (&vid_config_x);
	//Cvar_Register (&vid_config_y);
	Cvar_Register(&vid_conwidth);
	Cvar_Register(&vid_conheight);
	Cvar_Register(&vid_displayfrequency);
	Cvar_Register(&vid_hwgammacontrol);
	Cvar_Register(&vid_forcerestoregamma);

	Cvar_Register(&_windowed_mouse);

	Cmd_AddCommand ("vid_nummodes", VID_NumModes_f);
	Cmd_AddCommand ("vid_describecurrentmode", VID_DescribeCurrentMode_f);
	Cmd_AddCommand ("vid_describemode", VID_DescribeMode_f);
	Cmd_AddCommand ("vid_describemodes", VID_DescribeModes_f);

	Cmd_AddCommand("vid_modelist", VID_ModeList_f); 
	Cmd_AddCommand ("vid_restart", VID_Restart_f);

	hIcon = LoadIcon (global_hInstance, MAKEINTRESOURCE (IDI_ICON2));

	VID_InitDIB (global_hInstance);
	basenummodes = nummodes;

	VID_InitFullDIB (global_hInstance);

	if (windowed)
	{
		hdc = GetDC (NULL);

		if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE)
			Sys_Error ("Can't run in non-RGB mode");

		ReleaseDC (NULL, hdc);

		if ((temp = COM_CheckParm("-mode")) && temp + 1 < com_argc)
			vid_default = Q_atoi(com_argv[temp+1]);
		else if (vid_mode.value != NO_MODE) // serve +set vid_mode
			vid_default = vid_mode.value;
		else 
		{
			vid_default = NO_MODE;

			width = modelist[0].width;
			height = modelist[0].height;

			for (i = 1; i < nummodes; i++)
			{
				if (modelist[i].width == width && (!height || modelist[i].height == height))
				{
					vid_default = i;
					break;
				}
			}

			vid_default = (vid_default == NO_MODE ? MODE_WINDOWED : vid_default);
		}
	}
	else
	{
		Cmd_AddCommand("vid_showfreq", VID_ShowFreq_f);

		if (nummodes == 1)
			Sys_Error ("No RGB fullscreen modes available");

		windowed = false;

		if ((i = COM_CheckParm("-mode")) && i + 1 < com_argc)
		{
			vid_default = Q_atoi(com_argv[i+1]);
		}
		else if (vid_mode.value != NO_MODE) // serve +set vid_mode
		{ 
			vid_default = vid_mode.value;
		}
		else if (COM_CheckParm("-current"))
		{
			modelist[MODE_FULLSCREEN_DEFAULT].width = GetSystemMetrics (SM_CXSCREEN);
			modelist[MODE_FULLSCREEN_DEFAULT].height = GetSystemMetrics (SM_CYSCREEN);
			vid_default = MODE_FULLSCREEN_DEFAULT;
			leavecurrentmode = 1;
		}
		else
		{
			if ((i = COM_CheckParm("-width")) && i + 1 < com_argc)
				width = Q_atoi(com_argv[i+1]);
			else
				width = 640;

			if ((i = COM_CheckParm("-bpp")) && i + 1 < com_argc)
			{
				bpp = Q_atoi(com_argv[i+1]);
				findbpp = 0;
			}
			else
			{
				bpp = 15;
				findbpp = 1;
			}

			if ((i = COM_CheckParm("-height")) && i + 1 < com_argc)
				height = Q_atoi(com_argv[i+1]);

			// if they want to force it, add the specified mode to the list
			if (COM_CheckParm("-force") && nummodes < MAX_MODE_LIST)
			{
				modelist[nummodes].type = MS_FULLDIB;
				modelist[nummodes].width = width;
				modelist[nummodes].height = height;
				modelist[nummodes].modenum = 0;
				modelist[nummodes].halfscreen = 0;
				modelist[nummodes].dib = 1;
				modelist[nummodes].fullscreen = 1;
				modelist[nummodes].bpp = bpp;
				sprintf (modelist[nummodes].modedesc, "%dx%dx%d", devmode.dmPelsWidth, devmode.dmPelsHeight, devmode.dmBitsPerPel);

				for (i = nummodes ; i < nummodes ; i++)
				{
					if ((modelist[nummodes].width == modelist[i].width) && 
					    (modelist[nummodes].height == modelist[i].height) && 
					    (modelist[nummodes].bpp == modelist[i].bpp))
						break;
				}

				if (i == nummodes)
					nummodes++;
			}

			done = 0;

			do {
				height = 0;
				if ((i = COM_CheckParm("-height")) && i + 1 < com_argc)
					height = Q_atoi(com_argv[i+1]);
				else
					height = 0;

				for (i = 1, vid_default = 0 ; i < nummodes ; i++)
				{
					if (modelist[i].width == width && (!height || modelist[i].height == height) && modelist[i].bpp == bpp)
					{
						vid_default = i;
						done = 1;
						break;
					}
				}

				if (findbpp && !done)
				{
					switch (bpp)
					{
					case 15:
						bpp = 16;
						break;

					case 16:
						bpp = 32;
						break;

					case 32:
						bpp = 24;
						break;

					case 24:
						done = 1;
						break;
					}
				}
				else
				{
					done = 1;
				}
			} while (!done);

			if (!vid_default)
				Sys_Error ("Specified video mode not available");
		}
	}

	if ((i = COM_CheckParm("-freq")) && i + 1 < com_argc)
		Cvar_Set(&vid_displayfrequency, com_argv[i+1]);

	vid_initialized = true;

	if ((i = COM_CheckParm("-conwidth")) && i + 1 < com_argc)
		Cvar_SetValue(&vid_conwidth, (float)Q_atoi(com_argv[i+1]));
	else // this is ether +set vid_con... or just default value which we select in cvar initialization
		Cvar_SetValue(&vid_conwidth, vid_conwidth.value); // must trigger callback which validate value 

	// set console aspect using video frame's aspect
	aspect = (float)modelist[vid_default].height / (float)modelist[vid_default].width;

	if ((i = COM_CheckParm("-conheight")) && i + 1 < com_argc)
		Cvar_SetValue(&vid_conheight, (float)Q_atoi(com_argv[i+1]));
	else // this is ether +set vid_con... or just default value which we select in cvar initialization
		 // also select vid_conheight with proper aspect ratio if user omit it
		Cvar_SetValue(&vid_conheight, vid_conheight.value ? vid_conheight.value : vid_conwidth.value * aspect); // must trigger callback which validate value 

	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

	Check_Gamma (palette);
	VID_SetPalette (palette);

	VID_SetMode (vid_default, palette);

	maindc = GetDC (mainwindow);
	if (!bSetupPixelFormat(maindc))
		Sys_Error ("bSetupPixelFormat failed");

	InitHWGamma ();

	if (!(baseRC = wglCreateContext(maindc)))
		Sys_Error ("Could not initialize GL (wglCreateContext failed).\n\nMake sure you in are 65535 color mode, and try running -window.");

	if (!wglMakeCurrent(maindc, baseRC))
		Sys_Error ("wglMakeCurrent failed");

	GL_Init ();
	GL_Init_Win();

	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;

	strcpy (badmode.modedesc, "Bad mode");
	vid_canalttab = true;

	if (COM_CheckParm("-fullsbar"))
		fullsbardraw = true;

	menu_display_freq = (int)vid_displayfrequency.value;
	VID_ShowFreq_f(); // query possible display frequencies for the menu
}

void VID_Restart()
{
	// TODO: de-init more things, and re-init it

	if (baseRC)
		wglDeleteContext(baseRC);

	baseRC = NULL;

	VID_SetMode(vid_mode.value, host_basepal);

	baseRC = wglCreateContext(maindc);
	if (!baseRC)
		Sys_Error("Could not initialize GL (wglCreateContext failed).\n\nMake sure you in are 65535 color mode, and try running -window.");

	if (!wglMakeCurrent(maindc, baseRC))
		Sys_Error("wglMakeCurrent failed");
}

void VID_Restart_f(void)
{
	extern void GFX_Init(void);

	if (!host_initialized) // sanity
	{
		Con_Printf("Can't do %s yet\n", Cmd_Argv(0));
		return;
	}

	VID_Restart();

	GL_Init();
#ifdef WIN32
	GL_Init_Win();
#else
	// TODO: some *nix related here
#endif

	// force models to reload (just flush, no actual loading code here)
	Cache_Flush();

	// reload 2D textures, particles textures, some other textures and gfx.wad
	GFX_Init();

	// we need done something like for map reloading, for example reload textures for brush models
	R_NewMap(true);

	// force all cached models to be loaded, so no short HDD lag then u walk over level and discover new model
	Mod_TouchModels();
}

//========================================================
// Video menu stuff
//========================================================

#define	VIDEO_ITEMS	6

int	video_cursor_row = 0;
int	video_cursor_column = 0;
int video_mode_rows = 0;

extern	void M_Menu_Options_f (void);
extern	void M_Print (int cx, int cy, char *str);
extern	void M_PrintWhite (int cx, int cy, char *str);
extern	void M_DrawCharacter (int cx, int line, int num);
extern	void M_DrawTransPic (int x, int y, mpic_t *pic);
extern	void M_DrawPic (int x, int y, mpic_t *pic);
extern	void M_DrawCheckbox(int x, int y, int on);

static	int	vid_line, vid_wmodes;

typedef struct
{
	int	modenum;
	char	*desc;
	int	iscur;
} modedesc_t;

#define VID_ROW_SIZE		3
#define MAX_COLUMN_SIZE		13
#define MAX_MODEDESCS		(MAX_COLUMN_SIZE * VID_ROW_SIZE)

static	modedesc_t	modedescs[MAX_MODEDESCS];

/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw (void)
{
	mpic_t	*p;
	char	*ptr, display_freq[10];
	int	lnummodes, i, k, column, row;
	vmode_t	*pv;

	p = Draw_CachePic ("gfx/vidmodes.lmp");
	M_DrawPic ((320-p->width)/2, 4, p);

	vid_wmodes = 0;
	lnummodes = VID_NumModes ();
	
	for (i = 1; i < lnummodes && vid_wmodes < MAX_MODEDESCS; i++)
	{
		ptr = VID_GetModeDescription (i);
		pv = VID_GetModePtr (i);

		k = vid_wmodes;

		modedescs[k].modenum = i;
		modedescs[k].desc = ptr;
		modedescs[k].iscur = 0;

		if (i == vid_modenum)
			modedescs[k].iscur = 1;

		vid_wmodes++;
	}

	M_Print(16, 32, "        Fullscreen");
	M_DrawCheckbox(188, 32, !windowed);

	M_Print(16, 40, "      Refresh rate");
	sprintf(display_freq, "%i Hz", menu_display_freq);
	M_Print(188, 40, display_freq);

	M_Print(16, 48, "     Vertical sync");
	M_DrawCheckbox(188, 48, vid_vsync.value);

	M_PrintWhite(16, 64, "     Apply changes");

	column = 0;
	row = 32 + VIDEO_ITEMS * 8;

	video_mode_rows = 1;
	for (i = 0 ; i < vid_wmodes ; i++)
	{
		if (modedescs[i].iscur)
			M_PrintWhite (column, row, modedescs[i].desc);
		else
			M_Print (column, row, modedescs[i].desc);

		column += 14 * 8;

		if ((i % VID_ROW_SIZE) == (VID_ROW_SIZE - 1))
		{
			column = 0;
			row += 8;
			video_mode_rows++;
		}
	}

	// cursor
	if (video_cursor_row < VIDEO_ITEMS)
		M_DrawCharacter(168, 32 + video_cursor_row * 8, 12 + ((int)(realtime * 4) & 1));
	else // we are in the resolutions region
		M_DrawCharacter(-8 + video_cursor_column * 14 * 8, 32 + video_cursor_row * 8, 12 + ((int)(realtime * 4) & 1));

	M_Print(8 * 8, row + 8, "Press enter to set mode");
	M_Print(6 * 8, row + 8 * 3, "T to test mode for 5 seconds");
}

/*
================
VID_MenuKey
================
*/
void VID_MenuKey (int key)
{
	int i, selected_modenum;

	switch (key)
	{
	case K_ESCAPE:
		M_Menu_Options_f ();
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		video_cursor_row--;
		if (video_cursor_row < 0)
			video_cursor_row = (VIDEO_ITEMS + video_mode_rows) - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		video_cursor_row++;
		if (video_cursor_row >= (VIDEO_ITEMS + video_mode_rows))
			video_cursor_row = 0;
		break;

	case K_HOME:
	case K_PGUP:
		S_LocalSound("misc/menu1.wav");
		video_cursor_row = 0;
		break;

	case K_END:
	case K_PGDN:
		S_LocalSound("misc/menu1.wav");
		video_cursor_row = (VIDEO_ITEMS  + video_mode_rows) - 1;
		break;

	case K_LEFTARROW:
		if (video_cursor_row >= VIDEO_ITEMS)
		{ 
			video_cursor_column--;
			if (video_cursor_column < 0)
			{
				if (video_cursor_row >= ((VIDEO_ITEMS + video_mode_rows) - 1)) // if we stand on the last row, check how many items we have
				{
					if (vid_wmodes % VID_ROW_SIZE == 1)
						video_cursor_column = 0;
					else if (vid_wmodes % VID_ROW_SIZE == 2)
						video_cursor_column = 1;
					else
						video_cursor_column = 2;
				}
				else
				{
					video_cursor_column = VID_ROW_SIZE - 1;
				}
			}
		}
		break;

	case K_RIGHTARROW:
		if (video_cursor_row >= VIDEO_ITEMS)
		{
			video_cursor_column++;
			if (video_cursor_column >= VID_ROW_SIZE || ((video_cursor_row - VIDEO_ITEMS) * VID_ROW_SIZE + (video_cursor_column + 1)) > vid_wmodes)
				video_cursor_column = 0;
		}
		break;

	case K_ENTER:
		S_LocalSound("misc/menu2.wav");
		switch (video_cursor_row)
		{
		case 0:
			//FIXME when switching windowed/fullscreen mode is supported
			break;

		case 1:
			for (i = 0; i < display_freq_modes_num; i++)
				if (display_freq_modes[i] == menu_display_freq)
					break;
			if (i >= (display_freq_modes_num - 1))
				i = -1;
			menu_display_freq = display_freq_modes[i + 1];
			break;

		case 2:
			Cvar_SetValue(&vid_vsync, !vid_vsync.value);
			break;

		case 4:
			Cvar_SetValue(&vid_displayfrequency, menu_display_freq);
			break;

		default:
			selected_modenum = (video_cursor_row - VIDEO_ITEMS) * VID_ROW_SIZE + (video_cursor_column + 1);
			Cvar_SetValue(&vid_mode, (float)selected_modenum);
			VID_ShowFreq_f(); // refresh possible display frequencies after a resolution change
			break;
		}
	}

	if (key == K_UPARROW && (video_cursor_row == 3 || video_cursor_row == 5))
		video_cursor_row--;
	else if (key == K_DOWNARROW && (video_cursor_row == 3 || video_cursor_row == 5))
		video_cursor_row++;
}
