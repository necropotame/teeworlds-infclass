#include <base/math.h>

#include <engine/console.h>
#include <engine/storage.h>
#include <engine/shared/config.h>

#include "netdatabase.h"

bool CNetDatabase::StrAllnum(const char *pStr)
{
	while(*pStr)
	{
		if(!(*pStr >= '0' && *pStr <= '9'))
			return false;
		pStr++;
	}
	return true;
}


CNetDatabase::CNetHash::CNetHash(const NETADDR *pAddr)
{
	if(pAddr->type==NETTYPE_IPV4)
		m_Hash = (pAddr->ip[0]+pAddr->ip[1]+pAddr->ip[2]+pAddr->ip[3])&0xFF;
	else
		m_Hash = (pAddr->ip[0]+pAddr->ip[1]+pAddr->ip[2]+pAddr->ip[3]+pAddr->ip[4]+pAddr->ip[5]+pAddr->ip[6]+pAddr->ip[7]+
			pAddr->ip[8]+pAddr->ip[9]+pAddr->ip[10]+pAddr->ip[11]+pAddr->ip[12]+pAddr->ip[13]+pAddr->ip[14]+pAddr->ip[15])&0xFF;
	m_HashIndex = 0;
}

CNetDatabase::CNetHash::CNetHash(const CNetRange *pRange)
{
	m_Hash = 0;
	m_HashIndex = 0;
	for(int i = 0; pRange->m_LB.ip[i] == pRange->m_UB.ip[i]; ++i)
	{
		m_Hash += pRange->m_LB.ip[i];
		++m_HashIndex;
	}
	m_Hash &= 0xFF;
}

int CNetDatabase::CNetHash::MakeHashArray(const NETADDR *pAddr, CNetHash aHash[17])
{
	int Length = pAddr->type==NETTYPE_IPV4 ? 4 : 16;
	aHash[0].m_Hash = 0;
	aHash[0].m_HashIndex = 0;
	for(int i = 1, Sum = 0; i <= Length; ++i)
	{
		Sum += pAddr->ip[i-1];
		aHash[i].m_Hash = Sum&0xFF;
		aHash[i].m_HashIndex = i%Length;
	}
	return Length;
}
