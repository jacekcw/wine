/* -*- tab-width: 8; c-basic-offset: 4 -*- */

/*
 * MMSYTEM time functions
 *
 * Copyright 1993 Martin Ayotte
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "winbase.h"
#include "wine/winbase16.h" /* GetTaskDS */
#include "callback.h"
#include "mmsystem.h"
#include "xmalloc.h"
#include "services.h"
#include "options.h"
#include "debugtools.h"

DECLARE_DEBUG_CHANNEL(mmsys)
DECLARE_DEBUG_CHANNEL(mmtime)

static MMTIME16 mmSysTimeMS;
static MMTIME16 mmSysTimeSMPTE;

typedef struct tagTIMERENTRY {
    UINT			wDelay;
    UINT			wResol;
    FARPROC16 			lpFunc;
    HINSTANCE			hInstance;
    DWORD			dwUser;
    UINT			wFlags;
    UINT			wTimerID;
    UINT			wCurTime;
    UINT			isWin32;
    struct tagTIMERENTRY*	Next;
} TIMERENTRY, *LPTIMERENTRY;

static LPTIMERENTRY lpTimerList = NULL;
static HANDLE hMMTimer;

/*
 * FIXME
 * We're using "1" as the mininum resolution to the timer,
 * as Windows 95 does, according to the docs. Maybe it should
 * depend on the computers resources!
 */
#define MMSYSTIME_MININTERVAL /* (1) */ (10)
#define MMSYSTIME_MAXINTERVAL (65535)

static	void	TIME_TriggerCallBack(LPTIMERENTRY lpTimer, DWORD dwCurrent)
{
    lpTimer->wCurTime = lpTimer->wDelay;
    
    if (lpTimer->lpFunc != (FARPROC16) NULL) {
	TRACE_(mmtime)("before CallBack16 (%lu)!\n", dwCurrent);
	TRACE_(mmtime)("lpFunc=%p wTimerID=%04X dwUser=%08lX !\n",
	      lpTimer->lpFunc, lpTimer->wTimerID, lpTimer->dwUser);
	TRACE_(mmtime)("hInstance=%04X !\n", lpTimer->hInstance);
	
	
	/* - TimeProc callback that is called here is something strange, under Windows 3.1x it is called 
	 * 		during interrupt time,  is allowed to execute very limited number of API calls (like
	 *	    	PostMessage), and must reside in DLL (therefore uses stack of active application). So I 
	 *       guess current implementation via SetTimer has to be improved upon.		
	 */
	switch (lpTimer->wFlags & 0x30) {
	case TIME_CALLBACK_FUNCTION:
		if (lpTimer->isWin32)
		    lpTimer->lpFunc(lpTimer->wTimerID,0,lpTimer->dwUser,0,0);
		else
		    Callbacks->CallTimeFuncProc(lpTimer->lpFunc,
						lpTimer->wTimerID,0,
						lpTimer->dwUser,0,0);
		break;
	case TIME_CALLBACK_EVENT_SET:
		SetEvent((HANDLE)lpTimer->lpFunc);
		break;
	case TIME_CALLBACK_EVENT_PULSE:
		PulseEvent((HANDLE)lpTimer->lpFunc);
		break;
	default:
		FIXME_(mmtime)("Unknown callback type 0x%04x for mmtime callback (%p),ignored.\n",lpTimer->wFlags,lpTimer->lpFunc);
		break;
	}
	TRACE_(mmtime)("after CallBack16 !\n");
    }
    if (lpTimer->wFlags & TIME_ONESHOT)
	timeKillEvent(lpTimer->wTimerID);
}

/**************************************************************************
 *           TIME_MMSysTimeCallback
 */
static VOID CALLBACK TIME_MMSysTimeCallback( ULONG_PTR dummy )
{
    LPTIMERENTRY lpTimer;
    
    mmSysTimeMS.u.ms += MMSYSTIME_MININTERVAL;
    mmSysTimeSMPTE.u.smpte.frame++;
    
    for (lpTimer = lpTimerList; lpTimer != NULL; lpTimer = lpTimer->Next) {
	if (lpTimer->wCurTime < MMSYSTIME_MININTERVAL) {
	    TIME_TriggerCallBack(lpTimer, mmSysTimeMS.u.ms);
	} else {
	    lpTimer->wCurTime -= MMSYSTIME_MININTERVAL;
	}
    }
}

/**************************************************************************
 * 				StartMMTime			[internal]
 */
static void StartMMTime()
{
    static BOOL 	mmTimeStarted = FALSE;
    
    if (!mmTimeStarted) {
	mmTimeStarted = TRUE;
	mmSysTimeMS.wType = TIME_MS;
	mmSysTimeMS.u.ms = GetTickCount();
	mmSysTimeSMPTE.wType = TIME_SMPTE;
	mmSysTimeSMPTE.u.smpte.hour = 0;
	mmSysTimeSMPTE.u.smpte.min = 0;
	mmSysTimeSMPTE.u.smpte.sec = 0;
	mmSysTimeSMPTE.u.smpte.frame = 0;
	mmSysTimeSMPTE.u.smpte.fps = 0;
	mmSysTimeSMPTE.u.smpte.dummy = 0;
	hMMTimer = SERVICE_AddTimer( MMSYSTIME_MININTERVAL*1000L, TIME_MMSysTimeCallback, 0 );
    }
}

/**************************************************************************
 * 				timeGetSystemTime	[WINMM.140]
 */
MMRESULT WINAPI timeGetSystemTime(LPMMTIME lpTime, UINT wSize)
{
    TRACE_(mmsys)("(%p, %u);\n", lpTime, wSize);
    StartMMTime();
    lpTime->wType = TIME_MS;
    lpTime->u.ms = mmSysTimeMS.u.ms;
    return 0;
}

/**************************************************************************
 * 				timeGetSystemTime	[MMSYSTEM.601]
 */
MMRESULT16 WINAPI timeGetSystemTime16(LPMMTIME16 lpTime, UINT16 wSize)
{
    TRACE_(mmsys)("(%p, %u);\n", lpTime, wSize);
    StartMMTime();
    lpTime->wType = TIME_MS;
    lpTime->u.ms = mmSysTimeMS.u.ms;
    return 0;
}

static	WORD	timeSetEventInternal(UINT wDelay,UINT wResol,
				     FARPROC16 lpFunc,DWORD dwUser,
				     UINT wFlags, UINT16 isWin32)
{
    WORD 		wNewID = 0;
    LPTIMERENTRY	lpNewTimer;
    LPTIMERENTRY	lpTimer = lpTimerList;
    
    TRACE_(mmtime)("(%u, %u, %p, %08lX, %04X);\n",
	  wDelay, wResol, lpFunc, dwUser, wFlags);
    StartMMTime();
    lpNewTimer = (LPTIMERENTRY)xmalloc(sizeof(TIMERENTRY));
    if (lpNewTimer == NULL)
	return 0;
    while (lpTimer != NULL) {
	wNewID = MAX(wNewID, lpTimer->wTimerID);
	lpTimer = lpTimer->Next;
    }
    
    lpNewTimer->Next = lpTimerList;
    lpTimerList = lpNewTimer;
    lpNewTimer->wTimerID = wNewID + 1;
    lpNewTimer->wCurTime = wDelay;
    lpNewTimer->wDelay = wDelay;
    lpNewTimer->wResol = wResol;
    lpNewTimer->lpFunc = lpFunc;
    lpNewTimer->isWin32 = isWin32;
    lpNewTimer->hInstance = GetTaskDS16();
    TRACE_(mmtime)("hInstance=%04X !\n", lpNewTimer->hInstance);
    TRACE_(mmtime)("lpFunc=0x%08lx !\n", (DWORD)lpFunc );
    lpNewTimer->dwUser = dwUser;
    lpNewTimer->wFlags = wFlags;
    return lpNewTimer->wTimerID;
}

/**************************************************************************
 * 				timeSetEvent		[MMSYSTEM.602]
 */
MMRESULT WINAPI timeSetEvent(UINT wDelay,UINT wResol,
				 LPTIMECALLBACK lpFunc,DWORD dwUser,
				 UINT wFlags)
{
    return timeSetEventInternal(wDelay, wResol, (FARPROC16)lpFunc, 
				dwUser, wFlags, 1);
}

/**************************************************************************
 * 				timeSetEvent		[MMSYSTEM.602]
 */
MMRESULT16 WINAPI timeSetEvent16(UINT16 wDelay, UINT16 wResol,
				 LPTIMECALLBACK16 lpFunc,DWORD dwUser,
				 UINT16 wFlags)
{
    return timeSetEventInternal(wDelay, wResol, (FARPROC16)lpFunc, 
				dwUser, wFlags, 0);
}

/**************************************************************************
 * 				timeKillEvent		[WINMM.142]
 */
MMRESULT WINAPI timeKillEvent(UINT wID)
{
    LPTIMERENTRY*	lpTimer;
    
    for (lpTimer = &lpTimerList; *lpTimer; lpTimer = &((*lpTimer)->Next)) {
	if (wID == (*lpTimer)->wTimerID) {
	    LPTIMERENTRY xlptimer = (*lpTimer)->Next;
	    
	    free(*lpTimer);
	    *lpTimer = xlptimer;
	    return TRUE;
	}
    }
    return 0;
}

/**************************************************************************
 * 				timeKillEvent		[MMSYSTEM.603]
 */
MMRESULT16 WINAPI timeKillEvent16(UINT16 wID)
{
    return timeKillEvent(wID);
}

/**************************************************************************
 * 				timeGetDevCaps		[WINMM.139]
 */
MMRESULT WINAPI timeGetDevCaps(LPTIMECAPS lpCaps,UINT wSize)
{
    TRACE_(mmtime)("(%p, %u) !\n", lpCaps, wSize);
    StartMMTime();
    lpCaps->wPeriodMin = MMSYSTIME_MININTERVAL;
    lpCaps->wPeriodMax = MMSYSTIME_MAXINTERVAL;
    return 0;
}

/**************************************************************************
 * 				timeGetDevCaps		[MMSYSTEM.604]
 */
MMRESULT16 WINAPI timeGetDevCaps16(LPTIMECAPS16 lpCaps, UINT16 wSize)
{
    TRACE_(mmtime)("(%p, %u) !\n", lpCaps, wSize);
    StartMMTime();
    lpCaps->wPeriodMin = MMSYSTIME_MININTERVAL;
    lpCaps->wPeriodMax = MMSYSTIME_MAXINTERVAL;
    return 0;
}

/**************************************************************************
 * 				timeBeginPeriod		[WINMM.137]
 */
MMRESULT WINAPI timeBeginPeriod(UINT wPeriod)
{
    TRACE_(mmtime)("(%u) !\n", wPeriod);
    StartMMTime();
    if (wPeriod < MMSYSTIME_MININTERVAL || wPeriod > MMSYSTIME_MAXINTERVAL) 
	return TIMERR_NOCANDO;
    return 0;
}
/**************************************************************************
 * 				timeBeginPeriod		[MMSYSTEM.605]
 */
MMRESULT16 WINAPI timeBeginPeriod16(UINT16 wPeriod)
{
    TRACE_(mmtime)("(%u) !\n", wPeriod);
    StartMMTime();
    if (wPeriod < MMSYSTIME_MININTERVAL || wPeriod > MMSYSTIME_MAXINTERVAL) 
	return TIMERR_NOCANDO;
    return 0;
}

/**************************************************************************
 * 				timeEndPeriod		[WINMM.138]
 */
MMRESULT WINAPI timeEndPeriod(UINT wPeriod)
{
    TRACE_(mmtime)("(%u) !\n", wPeriod);
    StartMMTime();
    if (wPeriod < MMSYSTIME_MININTERVAL || wPeriod > MMSYSTIME_MAXINTERVAL) 
	return TIMERR_NOCANDO;
    return 0;
}

/**************************************************************************
 * 				timeEndPeriod		[MMSYSTEM.606]
 */
MMRESULT16 WINAPI timeEndPeriod16(UINT16 wPeriod)
{
    TRACE_(mmtime)("(%u) !\n", wPeriod);
    if (wPeriod < MMSYSTIME_MININTERVAL || wPeriod > MMSYSTIME_MAXINTERVAL) 
	return TIMERR_NOCANDO;
    return 0;
}

/**************************************************************************
 * 				timeGetTime    [MMSYSTEM.607][WINMM.141]
 */
DWORD WINAPI timeGetTime()
{
    StartMMTime();
    return mmSysTimeMS.u.ms;
}
