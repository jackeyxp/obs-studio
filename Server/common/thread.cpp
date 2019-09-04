
#include "server.h"
#include "thread.h"

CThread::CThread()
  : fStopRequested(false)
  , fJoined(false)
  , fThreadID(0)
{
}

CThread::~CThread()
{
  this->StopAndWaitForThread();
}

void CThread::StopAndWaitForThread()
{
	if( fThreadID <= 0 )
		return;
	fStopRequested = true;
	if( !fJoined )							// Have Exit?
	{
		fJoined = true;					// Set Flag.
    pthread_join(fThreadID, NULL);
	}
	fThreadID = 0;
}

void CThread::Start()
{
  if( fThreadID > 0 )
    return;
  int nErrCode = pthread_create(&fThreadID, NULL, _Entry, this);
  if( nErrCode < 0 ) {
    log_trace("[pthread_create] error code :%d", nErrCode);
  }
}

void * CThread::_Entry(void * inThread)
{
  CThread * theThread = (CThread*)inThread;
  theThread->Entry();
  
  pthread_exit(NULL);
  
  theThread->fStopRequested = false;
  theThread->fThreadID = 0;
  
  return NULL;
}