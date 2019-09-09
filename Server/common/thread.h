
#pragma once

#include <pthread.h>

class OSMutexLocker
{
public:
	OSMutexLocker(pthread_mutex_t * lpMutex);
	~OSMutexLocker();
private:
	pthread_mutex_t * m_lpMutex;
};

class CThread
{
public:
  CThread();
  virtual ~CThread();
public:
	virtual void		Entry() = 0;
          void 		Start();
          void		StopAndWaitForThread();
          void		SendStopRequest()	{ fStopRequested = ((fThreadID > 0) ? true : false); }
          bool		IsStopRequested()	{ return fStopRequested; }
private:
  static  void *  _Entry(void * inThread);
private:
  bool		        fStopRequested;
  bool        		fJoined;
  pthread_t       fThreadID;
};