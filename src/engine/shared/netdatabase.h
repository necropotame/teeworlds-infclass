#ifndef ENGINE_SHARED_NETDATABASE_H
#define ENGINE_SHARED_NETDATABASE_H

#include <base/system.h>

//~ inline int NetComp(const NETADDR *pAddr1, const NETADDR *pAddr2)
//~ {
	//~ return mem_comp(pAddr1, pAddr2, pAddr1->type==NETTYPE_IPV4 ? 8 : 20);
//~ }

class CNetDatabase
{
public:
	class CNetRange
	{
	public:
		NETADDR m_LB;
		NETADDR m_UB;

		bool IsValid() const { return m_LB.type == m_UB.type && NetComp(&m_LB, &m_UB) < 0; }
	};

	class CNetHash
	{
	public:
		int m_Hash;
		int m_HashIndex;	// matching parts for ranges, 0 for addr

		CNetHash() {}	
		CNetHash(const NETADDR *pAddr);
		CNetHash(const CNetRange *pRange);

		static int MakeHashArray(const NETADDR *pAddr, CNetHash aHash[17]);
	};

	template<typename DATATYPE, typename INFOTYPE>
	struct CNode
	{
		DATATYPE m_Data;
		INFOTYPE m_Info;
		CNetHash m_NetHash;

		// hash list
		CNode *m_pHashNext;
		CNode *m_pHashPrev;

		// used or free list
		CNode *m_pNext;
		CNode *m_pPrev;
	};

	template<typename DATATYPE, typename INFOTYPE, int HashCount>
	class CPool
	{
	public:
		typedef DATATYPE CDataType;
		typedef INFOTYPE CInfoType;
		
	public:
		CNode<CDataType, CInfoType> *Add(const CDataType *pData, const CInfoType *pInfo, const CNetHash *pNetHash)
		{
			if(!m_pFirstFree)
				return 0;

			// create new ban
			CNode<DATATYPE, INFOTYPE> *pNode = m_pFirstFree;
			pNode->m_Data = *pData;
			pNode->m_Info = *pInfo;
			pNode->m_NetHash = *pNetHash;
			if(pNode->m_pNext)
				pNode->m_pNext->m_pPrev = pNode->m_pPrev;
			if(pNode->m_pPrev)
				pNode->m_pPrev->m_pNext = pNode->m_pNext;
			else
				m_pFirstFree = pNode->m_pNext;

			// add it to the hash list
			if(m_paaHashList[pNetHash->m_HashIndex][pNetHash->m_Hash])
				m_paaHashList[pNetHash->m_HashIndex][pNetHash->m_Hash]->m_pHashPrev = pNode;
			pNode->m_pHashPrev = 0;
			pNode->m_pHashNext = m_paaHashList[pNetHash->m_HashIndex][pNetHash->m_Hash];
			m_paaHashList[pNetHash->m_HashIndex][pNetHash->m_Hash] = pNode;

			// insert it into the used list
			if(m_pFirstUsed)
			{
				for(CNode<DATATYPE, INFOTYPE> *p = m_pFirstUsed; ; p = p->m_pNext)
				{
					if(p->m_Info.m_Expires == INFOTYPE::EXPIRES_NEVER || (pInfo->m_Expires != INFOTYPE::EXPIRES_NEVER && pInfo->m_Expires <= p->m_Info.m_Expires))
					{
						// insert before
						pNode->m_pNext = p;
						pNode->m_pPrev = p->m_pPrev;
						if(p->m_pPrev)
							p->m_pPrev->m_pNext = pNode;
						else
							m_pFirstUsed = pNode;
						p->m_pPrev = pNode;
						break;
					}

					if(!p->m_pNext)
					{
						// last entry
						p->m_pNext = pNode;
						pNode->m_pPrev = p;
						pNode->m_pNext = 0;
						break;
					}
				}
			}
			else
			{
				m_pFirstUsed = pNode;
				pNode->m_pNext = pNode->m_pPrev = 0;
			}

			// update ban count
			++m_CountUsed;

			return pNode;
		}
		
		int Remove(CNode<CDataType, CInfoType> *pNode)
		{
			if(pNode == 0)
				return -1;

			// remove from hash list
			if(pNode->m_pHashNext)
				pNode->m_pHashNext->m_pHashPrev = pNode->m_pHashPrev;
			if(pNode->m_pHashPrev)
				pNode->m_pHashPrev->m_pHashNext = pNode->m_pHashNext;
			else
				m_paaHashList[pNode->m_NetHash.m_HashIndex][pNode->m_NetHash.m_Hash] = pNode->m_pHashNext;
			pNode->m_pHashNext = pNode->m_pHashPrev = 0;

			// remove from used list
			if(pNode->m_pNext)
				pNode->m_pNext->m_pPrev = pNode->m_pPrev;
			if(pNode->m_pPrev)
				pNode->m_pPrev->m_pNext = pNode->m_pNext;
			else
				m_pFirstUsed = pNode->m_pNext;

			// add to recycle list
			if(m_pFirstFree)
				m_pFirstFree->m_pPrev = pNode;
			pNode->m_pPrev = 0;
			pNode->m_pNext = m_pFirstFree;
			m_pFirstFree = pNode;

			// update ban count
			--m_CountUsed;

			return 0;
		}
		
		void Update(CNode<CDataType, CInfoType> *pNode, const CInfoType *pInfo)
		{
			pNode->m_Info = *pInfo;

			// remove from used list
			if(pNode->m_pNext)
				pNode->m_pNext->m_pPrev = pNode->m_pPrev;
			if(pNode->m_pPrev)
				pNode->m_pPrev->m_pNext = pNode->m_pNext;
			else
				m_pFirstUsed = pNode->m_pNext;

			// insert it into the used list
			if(m_pFirstUsed)
			{
				for(CNode<DATATYPE, INFOTYPE> *p = m_pFirstUsed; ; p = p->m_pNext)
				{
					if(p->m_Info.m_Expires == INFOTYPE::EXPIRES_NEVER || (pInfo->m_Expires != INFOTYPE::EXPIRES_NEVER && pInfo->m_Expires <= p->m_Info.m_Expires))
					{
						// insert before
						pNode->m_pNext = p;
						pNode->m_pPrev = p->m_pPrev;
						if(p->m_pPrev)
							p->m_pPrev->m_pNext = pNode;
						else
							m_pFirstUsed = pNode;
						p->m_pPrev = pNode;
						break;
					}

					if(!p->m_pNext)
					{
						// last entry
						p->m_pNext = pNode;
						pNode->m_pPrev = p;
						pNode->m_pNext = 0;
						break;
					}
				}
			}
			else
			{
				m_pFirstUsed = pNode;
				pNode->m_pNext = pNode->m_pPrev = 0;
			}
		}
		
		void Reset()
		{
			mem_zero(m_paaHashList, sizeof(m_paaHashList));
			mem_zero(m_aBans, sizeof(m_aBans));
			m_pFirstUsed = 0;
			m_CountUsed = 0;

			for(int i = 1; i < MAX_ENTRIES-1; ++i)
			{
				m_aBans[i].m_pNext = &m_aBans[i+1];
				m_aBans[i].m_pPrev = &m_aBans[i-1];
			}

			m_aBans[0].m_pNext = &m_aBans[1];
			m_aBans[MAX_ENTRIES-1].m_pPrev = &m_aBans[MAX_ENTRIES-2];
			m_pFirstFree = &m_aBans[0];
		}
	
		int Num() const { return m_CountUsed; }
		bool IsFull() const { return m_CountUsed == MAX_ENTRIES; }

		CNode<CDataType, CInfoType> *First() const { return m_pFirstUsed; }
		CNode<CDataType, CInfoType> *First(const CNetHash *pNetHash) const { return m_paaHashList[pNetHash->m_HashIndex][pNetHash->m_Hash]; }
		CNode<CDataType, CInfoType> *Find(const CDataType *pData, const CNetHash *pNetHash) const
		{
			for(CNode<CDataType, CInfoType> *pNode = m_paaHashList[pNetHash->m_HashIndex][pNetHash->m_Hash]; pNode; pNode = pNode->m_pHashNext)
			{
				if(NetComp(&pNode->m_Data, pData) == 0)
					return pNode;
			}

			return 0;
		}
		
		CNode<CDataType, CInfoType> *Get(int Index) const
		{
			if(Index < 0 || Index >= Num())
				return 0;

			for(CNetDatabase::CNode<DATATYPE, INFOTYPE> *pBan = m_pFirstUsed; pBan; pBan = pBan->m_pNext, --Index)
			{
				if(Index == 0)
					return pBan;
			}

			return 0;
		}

	private:
		enum
		{
			MAX_ENTRIES=1024,
		};

		CNode<CDataType, CInfoType> *m_paaHashList[HashCount][256];
		CNode<CDataType, CInfoType> m_aBans[MAX_ENTRIES];
		CNode<CDataType, CInfoType> *m_pFirstFree;
		CNode<CDataType, CInfoType> *m_pFirstUsed;
		int m_CountUsed;
	};
	
public:
	static inline int NetComp(const NETADDR *pAddr1, const NETADDR *pAddr2)
	{
		return mem_comp(pAddr1, pAddr2, pAddr1->type==NETTYPE_IPV4 ? 8 : 20);
	}
	
	static inline int NetComp(const CNetRange *pRange1, const CNetRange *pRange2)
	{
		return NetComp(&pRange1->m_LB, &pRange2->m_LB) || NetComp(&pRange1->m_UB, &pRange2->m_UB);
	}
	
protected:
	bool NetMatch(const NETADDR *pAddr1, const NETADDR *pAddr2) const
	{
		return NetComp(pAddr1, pAddr2) == 0;
	}
	
	bool NetMatch(const CNetRange *pRange, const NETADDR *pAddr, int Start, int Length) const
	{
		return pRange->m_LB.type == pAddr->type && (Start == 0 || mem_comp(&pRange->m_LB.ip[0], &pAddr->ip[0], Start) == 0) &&
			mem_comp(&pRange->m_LB.ip[Start], &pAddr->ip[Start], Length-Start) <= 0 && mem_comp(&pRange->m_UB.ip[Start], &pAddr->ip[Start], Length-Start) >= 0;
	}
	
	bool NetMatch(const CNetRange *pRange, const NETADDR *pAddr) const
	{
		return NetMatch(pRange, pAddr, 0, pRange->m_LB.type==NETTYPE_IPV4 ? 4 : 16);
	}
	
	const char *NetToString(const NETADDR *pData, char *pBuffer, unsigned BufferSize) const
	{
		char aAddrStr[NETADDR_MAXSTRSIZE];
		net_addr_str(pData, aAddrStr, sizeof(aAddrStr), false);
		str_format(pBuffer, BufferSize, "'%s'", aAddrStr);
		return pBuffer;
	}

	const char *NetToString(const CNetRange *pData, char *pBuffer, unsigned BufferSize) const
	{
		char aAddrStr1[NETADDR_MAXSTRSIZE], aAddrStr2[NETADDR_MAXSTRSIZE];
		net_addr_str(&pData->m_LB, aAddrStr1, sizeof(aAddrStr1), false);
		net_addr_str(&pData->m_UB, aAddrStr2, sizeof(aAddrStr2), false);
		str_format(pBuffer, BufferSize, "'%s' - '%s'", aAddrStr1, aAddrStr2);
		return pBuffer;
	}

public:
	static bool StrAllnum(const char *pStr);
};

#endif
