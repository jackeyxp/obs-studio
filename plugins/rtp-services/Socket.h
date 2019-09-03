
#pragma once

#include "HYDefine.h"

class Socket
{
public:
	enum { kBufferSize = 4096 };
protected:
	Socket();
	~Socket();
public:
	GM_Error 		Open(int theType);
	GM_Error		Close();
	
	SOCKET			GetSocket()			{ return m_hSocket; }
	UInt32			GetLocalAddrV4()	{ return ntohl(m_LocalAddrV4.sin_addr.s_addr); }
	UInt16			GetLocalPortV4()	{ return ntohs(m_LocalAddrV4.sin_port); }
	UInt32			GetRemoteAddrV4() { return ntohl(m_RemoteAddrV4.sin_addr.s_addr); }
	UInt16			GetRemotePortV4() { return ntohs(m_RemoteAddrV4.sin_port); }
	const char  *   GetRemoteAddrStrV4();

	void			Broadcast();
	void			SetBlocking();
	void 			NonBlocking();
	void			ReuseAddr();
	void			NoDelay();
	void			KeepAlive();
	void			Linger(int nTime);
	void            SetTTL(int timeToLive);
	void			SetSocketSendBufSize(UInt32 inNewSize);
	void			SetSocketRecvBufSize(UInt32 inNewSize);
	
	GM_Error 		Bind(UInt32 addr, UInt16 port);
	GM_Error		AsyncSelect(HWND hWnd, UINT uMsg, long lEvent=FD_READ|FD_CONNECT|FD_CLOSE);

	GM_Error 		Send(const char* inData, const UInt32 inLength, UInt32* outLengthSent);
	GM_Error 		Read(char *buffer, const UInt32 length, UInt32 *outRecvLenP);

	GM_Error		Send(const string & inStr, UInt32 * outLengthSent);
	GM_Error		Read(string & outStr, UInt32 * outRecvLenP);
public: /*-- For Event IO --*/
	WSAEVENT		GetWSAEvent()	{ return m_hEvent; }
	BOOL			IsUseEvent()	{ return (m_hEvent != NULL) ? TRUE : FALSE; }			// Query Socket Event is Used?
	GM_Error		CreateEvent(int inWorkID = FD_READ | FD_CLOSE);							// Create Socket binded event.
	GM_Error		ResetEvent(int inWorkID = FD_READ | FD_CLOSE);							// Resignal socket event sate.
	void			CloseEvent();															// Close Event.
	void			SignalEvent();															// Change socket event sate.
protected:
	WSAEVENT 		m_hEvent;
	SOCKET			m_hSocket;
	string			m_RemoteStrV4;			// 远程地址IPV4
	sockaddr_in 	m_RemoteAddrV4;			// 远程地址IPV4
	sockaddr_in		m_LocalAddrV4;			// 本地地址IPV4
};