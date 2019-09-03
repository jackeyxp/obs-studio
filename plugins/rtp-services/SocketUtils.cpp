
#include "SocketUtils.h"

UINT							SocketUtils::sNumIPAddrs = 0;
SocketUtils::IPAddrInfo *		SocketUtils::sIPAddrInfoArray = NULL;

void SocketUtils::Initialize(Bool16 lookupDNSName /* = false */)
{
	SocketUtils::GetIPV4List(lookupDNSName);
}

Bool16 SocketUtils::IsMulticastIPAddr(UINT inAddress)
{
	return ((inAddress>>8) & 0x00f00000) == 0x00e00000;	//	multicast addresses == "class D" == 0xExxxxxxx == 1,1,1,0,<28 bits>
}

Bool16 SocketUtils::IsLocalIPAddr(UINT inAddress)
{
	for (UINT x = 0; x < sNumIPAddrs; x++)
	{
		if (sIPAddrInfoArray[x].fIPAddr == inAddress)
			return TRUE;
	}
	return FALSE;
}

Bool16 SocketUtils::IsLocalIPAddr(const char * lpszAddr)
{
	if( lpszAddr == NULL )
		return FALSE;
	for( UINT x = 0; x < sNumIPAddrs; x++ )
	{
		if( sIPAddrInfoArray[x].fIPAddrStr.EqualIgnoreCase(lpszAddr, strlen(lpszAddr)) )
			return TRUE;
	}
	return FALSE;
}

//  Input the string of address , IPV4 or IPV6 => µÃµ½×Ö·û´®IP
int  SocketUtils::FormatAddress(SOCKADDR *sa, int salen, char * addrbuf,int addrbuflen, int * iport )
{
	//	ASSERT( sa != NULL && addrbuf != NULL && iport != NULL );
	char    host[NI_MAXHOST],		serv[NI_MAXSERV];
	int     hostlen = NI_MAXHOST,	servlen = NI_MAXSERV,	err = NO_ERROR;

	err = getnameinfo(
		sa,
		salen,
		host,
		hostlen,
		serv,
		servlen,
		NI_NUMERICHOST | NI_NUMERICSERV
		);
	if (err != NO_ERROR){		
		return err;
	}
	if( (strlen(host) + strlen(serv) + 1) > (unsigned)addrbuflen)
		return WSAEFAULT;
	if (sa->sa_family == AF_INET || sa->sa_family == AF_INET6 ){
		sprintf(addrbuf, "%s", host);
		*iport = atoi(serv);
	}
	else
		addrbuf[0] = '\0';
	return NO_ERROR;
}

void SocketUtils::ConvertAddrToString(const struct in_addr& theAddr, StrPtrLen* ioStr)
{
	char* addr = inet_ntoa(theAddr);
	strcpy(ioStr->Ptr, addr);
	ioStr->Len = ::strlen(ioStr->Ptr);
}

string SocketUtils::ConvertAddrToString(DWORD dwHostAddr)
{
	struct sockaddr_in dAddr;
	memset((void *)&dAddr, 0, sizeof(dAddr));
	dAddr.sin_addr.s_addr = htonl(dwHostAddr);
	return inet_ntoa(dAddr.sin_addr);
}

UINT SocketUtils::ConvertStringToAddr(char* inAddrStr)
{
	return ntohl(::inet_addr(inAddrStr));
}

void SocketUtils::UnInitialize()
{
	SocketUtils::ReleaseIPV4Ary();
}

void SocketUtils::ReleaseIPV4Ary()
{
	if(sIPAddrInfoArray == NULL)
		return;
	for(UInt32 i = 0; i < sNumIPAddrs; i++)
	{
		if(sIPAddrInfoArray[i].fIPAddrStr.Ptr != NULL)
		{
			delete [] sIPAddrInfoArray[i].fIPAddrStr.Ptr;
			sIPAddrInfoArray[i].fIPAddrStr.Ptr = NULL;
		}
		if(sIPAddrInfoArray[i].fDNSNameStr.Ptr != NULL)
		{
			delete [] sIPAddrInfoArray[i].fDNSNameStr.Ptr;
			sIPAddrInfoArray[i].fDNSNameStr.Ptr = NULL;
		}
	}
	delete [] (UInt8*)sIPAddrInfoArray;
	sIPAddrInfoArray = NULL;
	sNumIPAddrs = 0;
}

void SocketUtils::GetIPV4List(Bool16 lookupDNSName /* = false */)
{
	if( sNumIPAddrs > 0 )
		return;
	ASSERT( sNumIPAddrs <= 0 );
	int tempSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (tempSocket == -1)
		return;

	static const UINT kMaxAddrBufferSize = 2048;
	char	inBuffer[kMaxAddrBufferSize] = {0};
	char	outBuffer[kMaxAddrBufferSize] = {0};
	ULONG	theReturnedSize = 0;
	
	//
	// Use the WSAIoctl function call to get a list of IP addresses
	//
	int theErr = ::WSAIoctl(	tempSocket, SIO_GET_INTERFACE_LIST, 
								inBuffer, kMaxAddrBufferSize,
								outBuffer, kMaxAddrBufferSize,
								&theReturnedSize,
								NULL,
								NULL);
	ASSERT(theErr == 0);
	if (theErr != 0){
		int ierr = GetLastError();
		return;
	}
	
	ASSERT((theReturnedSize % sizeof(INTERFACE_INFO)) == 0);	
	LPINTERFACE_INFO addrListP = (LPINTERFACE_INFO)&outBuffer[0];
	
	sNumIPAddrs = theReturnedSize / sizeof(INTERFACE_INFO);
	//
	// allocate the IPAddrInfo array. Unfortunately we can't allocate this
	// array the proper way due to a GCC bug
	//
	BYTE * addrInfoMem = new BYTE[sizeof(IPAddrInfo) * sNumIPAddrs];
	::memset(addrInfoMem, 0, sizeof(IPAddrInfo) * sNumIPAddrs);
	sIPAddrInfoArray = (IPAddrInfo*)addrInfoMem;

	UINT currentIndex = 0;
	for (UINT theIfCount = sNumIPAddrs, addrCount = 0;
		 addrCount < theIfCount; addrCount++)
	{
		struct sockaddr_in* theAddr = (struct sockaddr_in*)&addrListP[addrCount].iiAddress;

		char* theAddrStr = ::inet_ntoa(theAddr->sin_addr);

		//store the IP addr
		sIPAddrInfoArray[currentIndex].fIPAddr = ntohl(theAddr->sin_addr.s_addr);
		
		//store the IP addr as a string
		sIPAddrInfoArray[currentIndex].fIPAddrStr.Len = ::strlen(theAddrStr);
		sIPAddrInfoArray[currentIndex].fIPAddrStr.Ptr = new char[sIPAddrInfoArray[currentIndex].fIPAddrStr.Len + 2];
		::strcpy(sIPAddrInfoArray[currentIndex].fIPAddrStr.Ptr, theAddrStr);

		struct hostent* theDNSName = NULL;
		if (lookupDNSName) //convert this addr to a dns name, and store it
		{
			theDNSName = ::gethostbyaddr((char *)&theAddr->sin_addr, sizeof(theAddr->sin_addr), AF_INET);
		}
		
		if (theDNSName != NULL)
		{
			sIPAddrInfoArray[currentIndex].fDNSNameStr.Len = ::strlen(theDNSName->h_name);
			sIPAddrInfoArray[currentIndex].fDNSNameStr.Ptr = new char[sIPAddrInfoArray[currentIndex].fDNSNameStr.Len + 2];
			::strcpy(sIPAddrInfoArray[currentIndex].fDNSNameStr.Ptr, theDNSName->h_name);
		}
		else
		{
			//if we failed to look up the DNS name, just store the IP addr as a string
			sIPAddrInfoArray[currentIndex].fDNSNameStr.Len = sIPAddrInfoArray[currentIndex].fIPAddrStr.Len;
			sIPAddrInfoArray[currentIndex].fDNSNameStr.Ptr = new char[sIPAddrInfoArray[currentIndex].fDNSNameStr.Len + 2];
			::strcpy(sIPAddrInfoArray[currentIndex].fDNSNameStr.Ptr, sIPAddrInfoArray[currentIndex].fIPAddrStr.Ptr);
		}
		//move onto the next array index
		currentIndex++;
		
	}
 	::closesocket(tempSocket);
	//
	// If LocalHost is the first element in the array, switch it to be the second.
	// The first element is supposed to be the "default" interface on the machine,
	// which should really always be en0.
	if ((sNumIPAddrs > 1) && (::strcmp(sIPAddrInfoArray[0].fIPAddrStr.Ptr, "127.0.0.1") == 0))
	{
		UINT tempIP = sIPAddrInfoArray[1].fIPAddr;
		sIPAddrInfoArray[1].fIPAddr = sIPAddrInfoArray[0].fIPAddr;
		sIPAddrInfoArray[0].fIPAddr = tempIP;
		StrPtrLen tempIPStr(sIPAddrInfoArray[1].fIPAddrStr);
		sIPAddrInfoArray[1].fIPAddrStr = sIPAddrInfoArray[0].fIPAddrStr;
		sIPAddrInfoArray[0].fIPAddrStr = tempIPStr;
		StrPtrLen tempDNSStr(sIPAddrInfoArray[1].fDNSNameStr);
		sIPAddrInfoArray[1].fDNSNameStr = sIPAddrInfoArray[0].fDNSNameStr;
		sIPAddrInfoArray[0].fDNSNameStr = tempDNSStr;
	}
}
