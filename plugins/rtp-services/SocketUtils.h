
#pragma once

#include "StrPtrLen.h"

class SocketUtils
{
public:
	
	// Call initialize before using any socket functions.
	// (pass true for lookupDNSName if you want the hostname
	// looked up via DNS during initialization -- %%sfu)
	static void Initialize(Bool16 lookupDNSName = false);
	static void UnInitialize();
	
	//static utility routines
	static Bool16	IsMulticastIPAddr(UINT inAddress);
	static Bool16	IsLocalIPAddr(UINT inAddress);
	static Bool16	IsLocalIPAddr(const char * lpszAddr);
	
	//This function converts an integer IP address to a dotted-decimal string.
	//This function is NOT THREAD SAFE!!!
	static void   ConvertAddrToString(const struct in_addr& theAddr, StrPtrLen * outAddr);
	static string ConvertAddrToString(DWORD dwHostAddr);
	
	static int  FormatAddress(SOCKADDR *sa, int salen, char * addrbuf,int addrbuflen, int * iport );

	// This function converts a dotted-decimal string IP address to a UInt32
	static UINT ConvertStringToAddr(char* inAddr);
	
	//You can get at all the IP addrs and DNS names on this machine this way
	static UINT	GetNumIPAddrs() { return sNumIPAddrs; }
	static inline UINT			GetIPAddr(UINT inAddrIndex);
	static inline StrPtrLen *	GetIPAddrStr(UINT inAddrIndex);
	static inline StrPtrLen *	GetDNSNameStr(UINT inDNSIndex);
private:
	static void GetIPV4List(Bool16 lookupDNSName = false);
	static void ReleaseIPV4Ary();
	//
	// For storing relevent information about each IP interface
	//
	struct IPAddrInfo
	{
		UINT 		fIPAddr;
		StrPtrLen	fIPAddrStr;
		StrPtrLen	fDNSNameStr;
	};
	
	static IPAddrInfo *		sIPAddrInfoArray;
	static UINT				sNumIPAddrs;
};

inline UINT	SocketUtils::GetIPAddr(UINT inAddrIndex)
{
	ASSERT(sIPAddrInfoArray != NULL);
	ASSERT(inAddrIndex < sNumIPAddrs);
	return sIPAddrInfoArray[inAddrIndex].fIPAddr;
}

inline StrPtrLen *	SocketUtils::GetIPAddrStr(UINT inAddrIndex)
{
	ASSERT(sIPAddrInfoArray != NULL);
	ASSERT(inAddrIndex < sNumIPAddrs);
	return &sIPAddrInfoArray[inAddrIndex].fIPAddrStr;
}

inline StrPtrLen *	SocketUtils::GetDNSNameStr(UINT inDNSIndex)
{
	ASSERT(sIPAddrInfoArray != NULL);
	ASSERT(inDNSIndex < sNumIPAddrs);
	return &sIPAddrInfoArray[inDNSIndex].fDNSNameStr;
}
