
#pragma once

#include "Socket.h"

class UDPSocket : public Socket
{
public:
	UDPSocket();
	~UDPSocket();
public:
	GM_Error 	Open() { return Socket::Open( SOCK_DGRAM ); }

	void		SetRemoteAddr(const char * lpAddr, int nPort);
	GM_Error	RecvFrom(UInt32* outRemoteAddr, UInt16* outRemotePort, void* ioBuffer, UInt32 inBufLen, UInt32* outRecvLen);
	GM_Error	SendTo(UInt32 inRemoteAddr, UInt16 inRemotePort, void* inBuffer, UInt32 inLength);
	GM_Error	SendTo(void* inBuffer, UInt32 inLength);

	GM_Error    SetMulticastLoop(BOOL bLoopFlag);
	GM_Error    SetMulticastTTL(UInt16 timeToLive);
	GM_Error    ModMulticastInterface(UINT inInterAddr);
	GM_Error    JoinMulticastMember(UINT inMultiAddr, UINT inInterAddr);
	GM_Error    LeaveMulticastMember();
private:
	ip_mreq     m_MultiAddr;
};