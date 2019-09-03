
#include "UDPSocket.h"

UDPSocket::UDPSocket()
{
	memset(&m_MultiAddr, 0, sizeof(m_MultiAddr));
}

UDPSocket::~UDPSocket()
{
	// ����������鲥��ַ�ӿڣ���Ҫ�˳��鲥��Ա...
	if (m_MultiAddr.imr_multiaddr.s_addr > 0) {
		this->LeaveMulticastMember();
	}
}

// �鲥Զ�̵�ַҲͨ������ӿ�������...
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

// �����鲥���ݱ����ܷ���յ���ͨ��Ҫ��ֹ��������...
GM_Error UDPSocket::SetMulticastLoop(BOOL bLoopFlag)
{
	u_char	nOptVal = (u_char)bLoopFlag;
	int err = setsockopt(m_hSocket, IPPROTO_IP, IP_MULTICAST_LOOP, (char*)&nOptVal, sizeof(nOptVal));
	return ((err == SOCKET_ERROR) ? GetLastError() : GM_NoErr);
}

// �����鲥�ܹ��ַ�����������Ĭ��1��ֻ���ڱ����鲥...
GM_Error UDPSocket::SetMulticastTTL(UInt16 timeToLive)
{
	u_char	nOptVal = (u_char)timeToLive;
	int err = setsockopt(m_hSocket, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&nOptVal, sizeof(nOptVal));
	return ((err == SOCKET_ERROR) ? GetLastError() : GM_NoErr);
}

// �޸��鲥�������ݻ�������ݵı��ؽӿڣ���ָ�������������ݻ�������...
GM_Error UDPSocket::ModMulticastInterface(UINT inInterAddr)
{
	// ע�⣺���ﱣ�����µı��ؽӿڵ�ַ...
	ASSERT(m_hSocket != INVALID_SOCKET);
	m_MultiAddr.imr_interface.s_addr = inInterAddr;
	int err = setsockopt(m_hSocket, IPPROTO_IP, IP_MULTICAST_IF, (char *)&inInterAddr, sizeof(DWORD));
	return ((err == SOCKET_ERROR) ? GetLastError() : GM_NoErr);
}

// �����鲥��Ա����Ҫָ���鲥��ַ�ͱ�����������ַ��Ĭ��ʱ����������ַΪ0...
GM_Error UDPSocket::JoinMulticastMember(UINT inMultiAddr, UINT inInterAddr)
{
	ASSERT(m_hSocket != INVALID_SOCKET);
	m_MultiAddr.imr_multiaddr.s_addr = inMultiAddr;
	m_MultiAddr.imr_interface.s_addr = inInterAddr;
	int	err = setsockopt(m_hSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&m_MultiAddr, sizeof(m_MultiAddr));
	return ((err == SOCKET_ERROR) ? GetLastError() : GM_NoErr);
}

// �˳��鲥��Ա��ֱ��ʹ�ü����鲥ʱ�ĳ�Ա������Ϣ...
GM_Error UDPSocket::LeaveMulticastMember()
{
	ASSERT(m_hSocket != INVALID_SOCKET);
	int err = setsockopt(m_hSocket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&m_MultiAddr, sizeof(m_MultiAddr));
	return ((err == -1) ? GetLastError() : GM_NoErr);
}
