/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2013, Jacek Sieka
  Copyright (C) 2026 iceman50

  SmartWin++

  Copyright (c) 2005 Thomas Hansen

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor SmartWin++ nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/** @file
 * This file contains all common DWT includes and also configures compiler-specific
 * quirks etc.
 */
#ifndef DWT_WindowsHeaders_h
#define DWT_WindowsHeaders_h

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifndef WINVER
#define WINVER 0x0601
#endif

#ifndef _WIN32_IE
#define _WIN32_IE 0x0A00
#endif

#if _WIN32_WINNT < 0x0601 || WINVER < 0x0601
#error _WIN32_WINNT / WINVER must require Windows 7 (0x0601)
#endif

#if _WIN32_IE < 0x0A00
#error _WIN32_IE must be at least 0x0A00
#endif

#include <cstdint>

// Windows API files...
#include <errno.h>
#include <windows.h>
#include <tchar.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <assert.h>

// GCC specific
#ifdef __GNUC__
#include "GCCHeaders.h"
#endif //! __GNUC__

// MSVC specific
#ifdef _MSC_VER
#include "VCDesktopHeaders.h"
#endif

// Standard headers that most classes need
#include <memory>
#include <vector>
#include <list>

// Other quirks

// Some MinGW headers omit this Windows SDK alias.
#ifndef LM_GETIDEALSIZE
#define LM_GETIDEALSIZE LM_GETIDEALHEIGHT
#endif

#ifndef WM_POINTERUPDATE
#define WM_POINTERUPDATE 0x0245
#define WM_POINTERDOWN 0x0246
#define WM_POINTERUP 0x0247
#define WM_POINTERENTER 0x0249
#define WM_POINTERLEAVE 0x024A
#define WM_POINTERCAPTURECHANGED 0x024C
#endif

#ifndef WM_POINTERWHEEL
#define WM_POINTERWHEEL 0x024E
#define WM_POINTERHWHEEL 0x024F
#endif

#ifndef POINTER_FLAG_NONE
typedef DWORD POINTER_INPUT_TYPE;
typedef UINT32 POINTER_FLAGS;
typedef enum tagPOINTER_BUTTON_CHANGE_TYPE {
	POINTER_CHANGE_NONE,
	POINTER_CHANGE_FIRSTBUTTON_DOWN,
	POINTER_CHANGE_FIRSTBUTTON_UP,
	POINTER_CHANGE_SECONDBUTTON_DOWN,
	POINTER_CHANGE_SECONDBUTTON_UP,
	POINTER_CHANGE_THIRDBUTTON_DOWN,
	POINTER_CHANGE_THIRDBUTTON_UP,
	POINTER_CHANGE_FOURTHBUTTON_DOWN,
	POINTER_CHANGE_FOURTHBUTTON_UP,
	POINTER_CHANGE_FIFTHBUTTON_DOWN,
	POINTER_CHANGE_FIFTHBUTTON_UP,
} POINTER_BUTTON_CHANGE_TYPE;

#define PT_POINTER 1
#define PT_TOUCH 2
#define PT_PEN 3
#define PT_MOUSE 4
#define PT_TOUCHPAD 5

#define POINTER_FLAG_NONE 0x00000000
#define POINTER_FLAG_NEW 0x00000001
#define POINTER_FLAG_INRANGE 0x00000002
#define POINTER_FLAG_INCONTACT 0x00000004
#define POINTER_FLAG_FIRSTBUTTON 0x00000010
#define POINTER_FLAG_SECONDBUTTON 0x00000020
#define POINTER_FLAG_THIRDBUTTON 0x00000040
#define POINTER_FLAG_FOURTHBUTTON 0x00000080
#define POINTER_FLAG_FIFTHBUTTON 0x00000100
#define POINTER_FLAG_PRIMARY 0x00002000
#define POINTER_FLAG_CONFIDENCE 0x00004000
#define POINTER_FLAG_CANCELED 0x00008000
#define POINTER_FLAG_DOWN 0x00010000
#define POINTER_FLAG_UPDATE 0x00020000
#define POINTER_FLAG_UP 0x00040000
#define POINTER_FLAG_WHEEL 0x00080000
#define POINTER_FLAG_HWHEEL 0x00100000
#define POINTER_FLAG_CAPTURECHANGED 0x00200000
#define POINTER_FLAG_HASTRANSFORM 0x00400000

#define POINTER_MESSAGE_FLAG_NEW 0x00000001
#define POINTER_MESSAGE_FLAG_INRANGE 0x00000002
#define POINTER_MESSAGE_FLAG_INCONTACT 0x00000004
#define POINTER_MESSAGE_FLAG_FIRSTBUTTON 0x00000010
#define POINTER_MESSAGE_FLAG_SECONDBUTTON 0x00000020
#define POINTER_MESSAGE_FLAG_THIRDBUTTON 0x00000040
#define POINTER_MESSAGE_FLAG_FOURTHBUTTON 0x00000080
#define POINTER_MESSAGE_FLAG_FIFTHBUTTON 0x00000100
#define POINTER_MESSAGE_FLAG_PRIMARY 0x00002000
#define POINTER_MESSAGE_FLAG_CONFIDENCE 0x00004000
#define POINTER_MESSAGE_FLAG_CANCELED 0x00008000

#define POINTER_MOD_SHIFT 0x0004
#define POINTER_MOD_CTRL 0x0008

#define GET_POINTERID_WPARAM(wParam) (LOWORD(wParam))

typedef struct tagPOINTER_INFO {
	POINTER_INPUT_TYPE pointerType;
	UINT32 pointerId;
	UINT32 frameId;
	POINTER_FLAGS pointerFlags;
	HANDLE sourceDevice;
	HWND hwndTarget;
	POINT ptPixelLocation;
	POINT ptHimetricLocation;
	POINT ptPixelLocationRaw;
	POINT ptHimetricLocationRaw;
	DWORD dwTime;
	UINT32 historyCount;
	INT32 InputData;
	DWORD dwKeyStates;
	UINT64 PerformanceCount;
	POINTER_BUTTON_CHANGE_TYPE ButtonChangeType;
} POINTER_INFO;

typedef UINT32 TOUCH_FLAGS;
typedef UINT32 TOUCH_MASK;
#define TOUCH_FLAG_NONE 0x00000000
#define TOUCH_MASK_NONE 0x00000000
#define TOUCH_MASK_CONTACTAREA 0x00000001
#define TOUCH_MASK_ORIENTATION 0x00000002
#define TOUCH_MASK_PRESSURE 0x00000004

typedef struct tagPOINTER_TOUCH_INFO {
	POINTER_INFO pointerInfo;
	TOUCH_FLAGS touchFlags;
	TOUCH_MASK touchMask;
	RECT rcContact;
	RECT rcContactRaw;
	UINT32 orientation;
	UINT32 pressure;
} POINTER_TOUCH_INFO;

typedef UINT32 PEN_FLAGS;
typedef UINT32 PEN_MASK;
#define PEN_FLAG_NONE 0x00000000
#define PEN_FLAG_BARREL 0x00000001
#define PEN_FLAG_INVERTED 0x00000002
#define PEN_FLAG_ERASER 0x00000004
#define PEN_MASK_NONE 0x00000000
#define PEN_MASK_PRESSURE 0x00000001
#define PEN_MASK_ROTATION 0x00000002
#define PEN_MASK_TILT_X 0x00000004
#define PEN_MASK_TILT_Y 0x00000008

typedef struct tagPOINTER_PEN_INFO {
	POINTER_INFO pointerInfo;
	PEN_FLAGS penFlags;
	PEN_MASK penMask;
	UINT32 pressure;
	UINT32 rotation;
	INT32 tiltX;
	INT32 tiltY;
} POINTER_PEN_INFO;
#endif

#ifndef WM_DPICHANGED_BEFOREPARENT
#define WM_DPICHANGED_BEFOREPARENT 0x02E2
#endif

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif

#endif // !WindowsHeaders_h
