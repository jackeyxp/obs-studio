
#pragma once

#include "HYDefine.h"

#define STRPTRLENTESTING 0

class StrPtrLen
{
	public:
		StrPtrLen();
		StrPtrLen(char * sp);
		StrPtrLen(char * sp, UINT len);
		~StrPtrLen();
		//
		//OPERATORS:
		//
		BOOL Equal(const StrPtrLen &compare) const;
		BOOL Equal(const char* compare, const UINT len) const;
		BOOL EqualIgnoreCase(const char* compare, const UINT len) const;
		BOOL EqualIgnoreCase(const StrPtrLen &compare) const { return EqualIgnoreCase(compare.Ptr, compare.Len); }
		BOOL NumEqualIgnoreCase(const char* compare, const UINT len) const;
		
		//void Delete();
		char *ToUpper();
		char *ToLower();
		
		char *FindStringCase(char *queryCharStr, StrPtrLen *resultStr, BOOL caseSensitive) const;

		char *FindString(StrPtrLen *queryStr, StrPtrLen *outResultStr);
		
		char *FindStringIgnoreCase(StrPtrLen *queryStr, StrPtrLen *outResultStr);

		char *FindString(StrPtrLen *queryStr);
		
		char *FindStringIgnoreCase(StrPtrLen *queryStr);
																					
		char *FindString(char *queryCharStr);
		char *FindStringIgnoreCase(char *queryCharStr);
		char *FindString(char *queryCharStr, StrPtrLen *outResultStr);
		char *FindStringIgnoreCase(char *queryCharStr, StrPtrLen *outResultStr);

		char *FindString(StrPtrLen &query, StrPtrLen *outResultStr);
		char *FindStringIgnoreCase(StrPtrLen &query, StrPtrLen *outResultStr);
		char *FindString(StrPtrLen &query);
		char *FindStringIgnoreCase(StrPtrLen &query);
		
		StrPtrLen& operator=(const StrPtrLen& newStr);
        char operator[](int i);
		void Set(char* inPtr, UINT inLen);
		void Set(char* inPtr);
		char* GetAsCString() const;					// convert to a "NEW'd" zero terminated char array
		//
		//This is a non-encapsulating interface. The class allows you to access its data.
		//
		char* 		Ptr;
		UINT		Len;
#if STRPTRLENTESTING
		static BOOL	Test();
#endif
	private:
		static BYTE 	sCaseInsensitiveMask[];
};
