
#include "Socket.h"
#include <mstcpip.h>

Socket::Socket()
  : m_hEvent(NULL)
  , m_hSocket(INVALID_SOCKET)
{
	memset(&m_RemoteAddrV4, 0, sizeof(m_RemoteAddrV4));
	memset(&m_LocalAddrV4, 0, sizeof(m_LocalAddrV4));
}

Socket::~Socket()
{
	if( m_hSocket != INVALID_SOCKET ) {
		shutdown(m_hSocket, SD_BOTH);
		closesocket(m_hSocket);
		m_hSocket = INVALID_SOCKET;
	}
	this->CloseEvent();
}

const char * Socket::GetRemoteAddrStrV4()
{
	if( m_RemoteStrV4.size() <= 0 && m_RemoteAddrV4.sin_addr.S_un.S_addr > 0 ) {
		char* addr = inet_ntoa(m_RemoteAddrV4.sin_addr);
		m_RemoteStrV4.assign(addr, strlen(addr));
	}
	return m_RemoteStrV4.c_str();
}

GM_Error Socket::AsyncSelect(HWND hWnd, UINT uMsg, long lEvent/*=FD_READ|FD_CONNECT|FD_CLOSE*/)
{
	ASSERT( hWnd != NULL && uMsg > 0 );
	ASSERT( m_hSocket != INVALID_SOCKET );
	int err = ::WSAAsyncSelect(m_hSocket, hWnd, uMsg, lEvent);
	return ((err == -1) ? GetLastError() : GM_NoErr);
}

GM_Error Socket::Bind(UInt32 addr, UInt16 port)
{
	GM_Error  theErr = GM_NoErr;
	ASSERT(m_hSocket != INVALID_SOCKET);
	::memset(&m_LocalAddrV4, 0, sizeof(m_LocalAddrV4));
	m_LocalAddrV4.sin_family = AF_INET;
	m_LocalAddrV4.sin_port = htons(port);
	m_LocalAddrV4.sin_addr.s_addr = htonl(addr);

	int err = ::bind(m_hSocket, (sockaddr *)&m_LocalAddrV4, sizeof(m_LocalAddrV4));

	if( err == -1 ) {
		::memset(&m_LocalAddrV4, 0, sizeof(m_LocalAddrV4));
		theErr = ::GetLastError();
		MsgLogGM(theErr);
		return theErr;
	} else {	// get the kernel to fill in unspecified values
		int len = sizeof(m_LocalAddrV4);
		::getsockname(m_hSocket, (sockaddr *)&m_LocalAddrV4, &len); 
	}
	return GM_NoErr;
}

GM_Error Socket::Open(int theType)
{
	GM_Error theErr = GM_NoErr;
	ASSERT( m_hSocket == INVALID_SOCKET );
	m_hSocket = ::socket(PF_INET, theType, 0);
	if( m_hSocket == INVALID_SOCKET ) {
		theErr = WSAGetLastError();
		MsgLogGM(theErr);
		return theErr;
	}
	return GM_NoErr;
}

GM_Error Socket::Close()
{
	GM_Error theErr = GM_NoErr;
	if( closesocket(m_hSocket) == SOCKET_ERROR ) {
		theErr = WSAGetLastError();
	}
	m_hSocket = INVALID_SOCKET;
	this->CloseEvent();
	return theErr;
}

void Socket::SetSocketSendBufSize(UInt32 inNewSize)
{
	int bufSize = inNewSize;
	int err = ::setsockopt(m_hSocket, SOL_SOCKET, SO_SNDBUF, (char*)&bufSize, sizeof(int));
	ASSERT(err == 0);
}

void Socket::SetSocketRecvBufSize(UInt32 inNewSize)
{
	int bufSize = inNewSize;
	int err = ::setsockopt(m_hSocket, SOL_SOCKET, SO_RCVBUF, (char*)&bufSize, sizeof(int));
	ASSERT(err == 0);
}

void Socket::SetBlocking()
{
	u_long	one	= 0;
	int		err = ::ioctlsocket(m_hSocket, FIONBIO, &one);
	ASSERT(err == 0);
}

void Socket::NonBlocking()
{
	u_long	one	= 1;
	int		err = ::ioctlsocket(m_hSocket, FIONBIO, &one);
	ASSERT(err == 0);
}

void Socket::ReuseAddr()
{
	int one = 1;
	int err = ::setsockopt(m_hSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(int));
	ASSERT(err == 0);
}

void Socket::Broadcast()
{
	BOOL	bBroadcast = true;
	int		err = ::setsockopt(m_hSocket, SOL_SOCKET, SO_BROADCAST, (char*)&bBroadcast, sizeof(BOOL));
	ASSERT(err == 0);
}

void Socket::NoDelay()
{
	int one = 1;
	int err = ::setsockopt(m_hSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(int));
	ASSERT(err == 0);	
}

void Socket::KeepAlive()
{
	int one = 1;
	int err = ::setsockopt(m_hSocket, SOL_SOCKET, SO_KEEPALIVE, (char*)&one, sizeof(int));
	ASSERT(err == 0);
	// 2017.07.28 - by jackey => 加入保持连接时间间隔设置...
	tcp_keepalive alive_in = {0};
	tcp_keepalive alive_out = {0};
	alive_in.keepalivetime  = 5000;  // 5s
	alive_in.keepaliveinterval  = 5000; // 5s
	alive_in.onoff = true;
	unsigned long ulBytesReturn = 0;
	err = WSAIoctl(m_hSocket, SIO_KEEPALIVE_VALS, &alive_in, sizeof(alive_in), &alive_out, sizeof(alive_out), &ulBytesReturn, NULL, NULL);
	if( err == SOCKET_ERROR ) {
		GM_Error theErr = WSAGetLastError();;
		MsgLogGM(theErr);
	}
}

void Socket::Linger(int nTime)
{
	linger tLinger;
	tLinger.l_linger = nTime;
	tLinger.l_onoff	 = 1;
	int err = ::setsockopt(m_hSocket, SOL_SOCKET, SO_LINGER, (char*)&tLinger, sizeof(linger));
	/*int one = 1;
	int err = ::setsockopt(m_hSocket,SOL_SOCKET,SO_DONTLINGER,(const char*)&one,sizeof(one));*/
	ASSERT(err == 0);	
}

void Socket::SetTTL(int timeToLive)
{
	u_char	nOptVal = (u_char)timeToLive;
	int err = setsockopt(m_hSocket, IPPROTO_IP, IP_TTL, (char*)&nOptVal, sizeof(nOptVal));
	ASSERT(err == 0);
}

GM_Error Socket::Send(const char* inData, const UInt32 inLength, UInt32* outLengthSent)
{
	ASSERT(inData != NULL);
	GM_Error theErr = GM_NoErr;
	int	 err = -1;
    do {
       err = ::send(m_hSocket, inData, inLength, 0); //flags??
	} while((err == -1) && (GetLastError() == WSAEINTR));
	if( err == -1 ) {
		theErr = GetLastError();
		return theErr;
	}
	*outLengthSent = err;
	return GM_NoErr;
}

GM_Error Socket::Send(const string & inStr, UInt32 * outLengthSent)
{
	ASSERT(inStr.size() > 0 && outLengthSent != NULL);
	GM_Error theErr = GM_NoErr;
	int err = 0;
	do {
		err = ::send(m_hSocket, inStr.c_str(), inStr.size(), 0); //flags??
	} while((err == -1) && (GetLastError() == WSAEINTR));
	if( err == -1 ) {
		theErr = GetLastError();
		return theErr;
	}
	*outLengthSent = err;
	return GM_NoErr;
}

GM_Error Socket::Read(string & outStr, UInt32 * outRecvLenP)
{
	ASSERT( outRecvLenP != NULL );
	GM_Error theErr = GM_NoErr;
	int		 theRecvLen = 0;
	char 	 buffer[kBufferSize] = {0};
	outStr.resize(0);
	while( TRUE ) {
		memset(buffer, 0, kBufferSize);
		theRecvLen = ::recv(m_hSocket, buffer, kBufferSize, 0);
		if( theRecvLen <= 0 ) break;
		ASSERT( theRecvLen <= kBufferSize );
		outStr.append(buffer, theRecvLen);
	}
	if( (theRecvLen == -1) && (outStr.size() <= 0) ) {
		theErr = GetLastError();
		return theErr;
	}
	if( outStr.size() <= 0 ) {
		return (GM_Error)WSAENOTCONN;
	}
	ASSERT( outStr.size() > 0 );
	*outRecvLenP = (UInt32)outStr.size();
	return GM_NoErr;
}

GM_Error Socket::Read(char *buffer, const UInt32 length, UInt32 *outRecvLenP)
{
	ASSERT(outRecvLenP != NULL);
	ASSERT(buffer != NULL);
	GM_Error theErr = GM_NoErr;
	int theRecvLen = 0;
	int	theCurLen = 0;
	while( TRUE ) {
		theCurLen = ::recv(m_hSocket, &buffer[theRecvLen], length-theRecvLen, 0);
		if( theCurLen <= 0 ) break;
		theRecvLen += theCurLen;
		if( theRecvLen >= (int)length ) break;
	}
	if((theCurLen == -1) && (theRecvLen <= 0) )	{
		theErr = GetLastError();
		return theErr;
	}
	if( theRecvLen <= 0 ) {
		return (GM_Error)WSAENOTCONN;
	}
	ASSERT(theRecvLen > 0);
	*outRecvLenP = (UInt32)theRecvLen;
	return GM_NoErr;
}
//---------------------------------------------------
// For Event-IO Method...
//---------------------------------------------------
GM_Error Socket::CreateEvent(int inWorkID/* = FD_READ | FD_CLOSE*/)
{
	inWorkID |= FD_CONNECT;
	ASSERT(m_hSocket != INVALID_SOCKET);
	if( m_hSocket == INVALID_SOCKET ) {
		MsgLogGM(WSAENOTSOCK);
		return WSAENOTSOCK;
	}
	if( m_hEvent != NULL ) {
		this->CloseEvent();
		m_hEvent = NULL;
	}
	m_hEvent = WSACreateEvent();
	ASSERT( m_hEvent != NULL );
	int iRet = WSAEventSelect(m_hSocket, m_hEvent, inWorkID);
	if((iRet == SOCKET_ERROR) && (WSAGetLastError() != ERROR_IO_PENDING))
		return WSAGetLastError();
	return GM_NoErr;
}
//
// 这里需要对事件设置信号，让事件线程立即退出...
void Socket::CloseEvent()
{
	BOOL bReturn = false;
	if( m_hEvent != NULL ) {
		bReturn = WSASetEvent(m_hEvent);
		bReturn = WSACloseEvent(m_hEvent);
		m_hEvent = NULL;
	}
}

void Socket::SignalEvent()
{
	if( m_hEvent != NULL ) {
		WSASetEvent(m_hEvent);
	}
}

GM_Error Socket::ResetEvent(int inWorkID/* = FD_READ | FD_CLOSE*/)
{
	ASSERT(m_hSocket != INVALID_SOCKET);
	if( m_hSocket == INVALID_SOCKET )
		return WSAENOTSOCK;
	if( m_hEvent != NULL ) {
		WSAResetEvent(m_hEvent);
		int iRet = WSAEventSelect(m_hSocket, m_hEvent, inWorkID);
		if((iRet == SOCKET_ERROR) && (WSAGetLastError() != ERROR_IO_PENDING))
			return WSAGetLastError();
	}
	return GM_NoErr;
}
