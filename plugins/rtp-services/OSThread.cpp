
#include "OSThread.h"
#include "process.h"
#include <time.h>

DWORD OSThread::sThreadStorageIndex = 0;

OSThread::OSThread()
  : fStopRequested(FALSE),
	fJoined(FALSE),
	fThreadID(NULL),
	fThreadMark(0)
{
}

OSThread::~OSThread()
{
	this->StopAndWaitForThread();
}
//
// OSThread Static Initialize 
//
void OSThread::Initialize()
{
	if(sThreadStorageIndex == 0)
	{
		sThreadStorageIndex = ::TlsAlloc();
	}
	ASSERT(sThreadStorageIndex >= 0);
}

void OSThread::UnInitialize()
{
	if(sThreadStorageIndex > 0)
	{
		if(::TlsFree(sThreadStorageIndex))
		{
			sThreadStorageIndex = 0;
		}
	}
}
//
// Get Current Thread Pointer.
//
OSThread * OSThread::GetCurrent()
{
	return (OSThread *)::TlsGetValue(sThreadStorageIndex);
}
//
// Thread Start Point.
//
void OSThread::Start()
{
	fThreadID = (HANDLE)_beginthreadex(	NULL, 			// Inherit security
										0,				// Inherit stack size
										_Entry,			// Entry function
										(void*)this,	// Entry arg
										0,				// Begin executing immediately
										&fThreadMark );	// We need to care about thread identify
	ASSERT(fThreadID != NULL);
	fStopRequested = FALSE;
}
//
// Stop And Wait For Thread Function Exit.
//
void OSThread::StopAndWaitForThread()
{
	if( fThreadID == NULL )					// Is not start?
		return;
	fStopRequested = TRUE;
	if( !fJoined )							// Have Exit?
	{
		fJoined = TRUE;						// Set Flag.
		DWORD theErr = ::WaitForSingleObject(fThreadID, INFINITE);
		(theErr != WAIT_OBJECT_0) ? MsgLogGM(theErr) : NULL;
		CloseHandle(fThreadID);
	}
	fThreadID = NULL;
}
//
// Static Thread Entry.
//
UINT WINAPI OSThread::_Entry(LPVOID inThread)
{
	OSThread * theThread = (OSThread*)inThread;		// OSThread Pointer.
	BOOL theErr = ::TlsSetValue(sThreadStorageIndex, theThread);
	ASSERT(theErr == TRUE);
	theThread->Entry();								// Run the thread.

	_endthread();
	theThread->fStopRequested = FALSE;				// Reset member
	theThread->fThreadMark = 0;
	theThread->fThreadID = NULL;
	theThread->fJoined = FALSE;
	return NULL;
}
