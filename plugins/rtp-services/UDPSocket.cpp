
#include "UDPSocket.h"

UDPSocket::UDPSocket()
{
	memset(&m_MultiAddr, 0, sizeof(m_MultiAddr));
}

UDPSocket::~UDPSocket()
{
	// 如果配置了组播地址接口，需要退出组播成员...
	if (m_MultiAddr.imr_multiaddr.s_addr > 0) {
		this->LeaveMulticastMember();
	}
}

// 组播远程地址也通过这个接口来设置...
void UDPSocket::SetRemoteAddr(const char * lpAddr, int nPort)
{
	ASSERT( lpAddr != NULL);
	sockaddr_in	sRmtAdd;
	sRmtAdd.sin_addr.s_addr = inet_addr(lpAddr);
	sRmtAdd.sin_port = htons(nPort);
	sRmtAdd.sin_family = AF_INET;
	memcpy(&m_RemoteAddrV4, &sRmtAdd, sizeof(sockaddr_in));
}

GM_Error UDPSocket::SendTo(UInt32 inRemoteAddr, UInt16 inRemotePort, void* inBuffer, UInt32 inLength)
{
	ASSERT(inBuffer != NULL);
	sockaddr_in theRemoteAddr = {0};
	theRemoteAddr.sin_family = AF_INET;
	theRemoteAddr.sin_port	 = htons(inRemotePort);
	theRemoteAddr.sin_addr.s_addr = htonl(inRemoteAddr);
	// Win32 says that inBuffer is a char*
	GM_Error theErr = GM_NoErr;
	int eErr = ::sendto(m_hSocket, (char*)inBuffer, inLength, 0, (sockaddr*)&theRemoteAddr, sizeof(theRemoteAddr));
	if( eErr == -1 ) {
		theErr = GetLastError();
		return theErr;
	}
	return GM_NoErr;
}

GM_Error UDPSocket::SendTo(void * inBuffer, UInt32 inLength)
{
	ASSERT(inBuffer != NULL);
	// Win32 says that inBuffer is a char*
	GM_Error theErr = GM_NoErr;
	int eErr = ::sendto( m_hSocket, (char*)inBuffer, inLength, 0, (sockaddr*)&m_RemoteAddrV4, sizeof(m_RemoteAddrV4));
	if( eErr == -1 ) {
		theErr = GetLastError();
		return theErr;
	}
	return GM_NoErr;
}

GM_Error UDPSocket::RecvFrom(UInt32* outRemoteAddr, UInt16* outRemotePort, void* ioBuffer, UInt32 inBufLen, UInt32* outRecvLen)
{
	ASSERT(outRecvLen != NULL);
	ASSERT(outRemoteAddr != NULL);
	ASSERT(outRemotePort != NULL);

	GM_Error theErr = GM_NoErr;
	sockaddr_in theMsgAddr = { 0 };
	int addrLen = sizeof(theMsgAddr);
	// Win32 says that ioBuffer is a char*
	SInt32 theRecvLen = ::recvfrom(m_hSocket, (char*)ioBuffer, inBufLen, 0, (sockaddr*)&theMsgAddr, &addrLen);
	if( theRecvLen == -1 ) {
		theErr = GetLastError();
		return theErr;
	}
	*outRemoteAddr = ntohl(theMsgAddr.sin_addr.s_addr);
	*outRemotePort = ntohs(theMsgAddr.sin_port);
	ASSERT(theRecvLen >= 0);
	*outRecvLen = (UInt32)theRecvLen;
	return GM_NoErr;		
}

// 设置组播数据本机能否接收到，通常要禁止本机接收...
GM_Error UDPSocket::SetMulticastLoop(BOOL bLoopFlag)
{
	u_char	nOptVal = (u_char)bLoopFlag;
	int err = setsockopt(m_hSocket, IPPROTO_IP, IP_MULTICAST_LOOP, (char*)&nOptVal, sizeof(nOptVal));
	return ((err == SOCKET_ERROR) ? GetLastError() : GM_NoErr);
}

// 设置组播能够分发的网络数，默认1，只能在本网组播...
GM_Error UDPSocket::SetMulticastTTL(UInt16 timeToLive)
{
	u_char	nOptVal = (u_char)timeToLive;
	int err = setsockopt(m_hSocket, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&nOptVal, sizeof(nOptVal));
	return ((err == SOCKET_ERROR) ? GetLastError() : GM_NoErr);
}

// 修改组播发送数据或接收数据的本地接口，即指定网卡接收数据或发送数据...
GM_Error UDPSocket::ModMulticastInterface(UINT inInterAddr)
{
	// 注意：这里保存了新的本地接口地址...
	ASSERT(m_hSocket != INVALID_SOCKET);
	m_MultiAddr.imr_interface.s_addr = inInterAddr;
	int err = setsockopt(m_hSocket, IPPROTO_IP, IP_MULTICAST_IF, (char *)&inInterAddr, sizeof(DWORD));
	return ((err == SOCKET_ERROR) ? GetLastError() : GM_NoErr);
}

// 加入组播成员，需要指定组播地址和本机的网卡地址，默认时本机网卡地址为0...
GM_Error UDPSocket::JoinMulticastMember(UINT inMultiAddr, UINT inInterAddr)
{
	ASSERT(m_hSocket != INVALID_SOCKET);
	m_MultiAddr.imr_multiaddr.s_addr = inMultiAddr;
	m_MultiAddr.imr_interface.s_addr = inInterAddr;
	int	err = setsockopt(m_hSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&m_MultiAddr, sizeof(m_MultiAddr));
	return ((err == SOCKET_ERROR) ? GetLastError() : GM_NoErr);
}

// 退出组播成员，直接使用加入组播时的成员参数信息...
GM_Error UDPSocket::LeaveMulticastMember()
{
	ASSERT(m_hSocket != INVALID_SOCKET);
	int err = setsockopt(m_hSocket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&m_MultiAddr, sizeof(m_MultiAddr));
	return ((err == -1) ? GetLastError() : GM_NoErr);
}
