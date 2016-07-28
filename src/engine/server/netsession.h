#ifndef ENGINE_SHARED_NETSESSION_H
#define ENGINE_SHARED_NETSESSION_H

#include <base/system.h>
#include <engine/shared/netdatabase.h>

template<typename SESSIONDATA>
class CNetSession : public CNetDatabase
{
protected:
	struct CSessionInfo
	{
		enum
		{
			EXPIRES_NEVER=-1,
		};
		int m_Expires;
		SESSIONDATA m_Data;
	};

	typedef CPool<NETADDR, CSessionInfo, 1> CSessionPool;
	
	CSessionPool m_Pool;

public:
	virtual ~CNetSession() {}
	void Init()
	{
		m_Pool.Reset();
	}
	
	void Update()
	{
		int Now = time_timestamp();

		// remove expired nodes
		while(m_Pool.First() && m_Pool.First()->m_Info.m_Expires != CSessionInfo::EXPIRES_NEVER && m_Pool.First()->m_Info.m_Expires < Now)
		{
			m_Pool.Remove(m_Pool.First());
		}
	}
	
	int AddSession(const NETADDR *pAddr, int Seconds, SESSIONDATA* pData)
	{
		int Stamp = Seconds > 0 ? time_timestamp()+Seconds : CSessionPool::CInfoType::EXPIRES_NEVER;

		// set up info
		typename CSessionPool::CInfoType Info = {0};
		Info.m_Expires = Stamp;
		Info.m_Data = *pData;

		// check if it already exists and update info
		CNetHash NetHash(pAddr);
		CNode<typename CSessionPool::CDataType, typename CSessionPool::CInfoType> *pNode = m_Pool.Find(pAddr, &NetHash);
		if(pNode)
		{
			m_Pool.Update(pNode, &Info);
			return 1;
		}

		// add node
		pNode = m_Pool.Add(pAddr, &Info, &NetHash);
		if(!pNode)
			return -1;
		
		return 0;
	}
	
	int RemoveSession(const NETADDR *pAddr)
	{
		CNetHash NetHash(pAddr);
		CNode<typename CSessionPool::CDataType, typename CSessionPool::CInfoType> *pNode = m_Pool.Find(pAddr, &NetHash);
		if(!pNode)
			return -1;
		
		m_Pool.Remove(pNode);
		return 0;
	}
	
	SESSIONDATA* GetData(const NETADDR *pAddr) const
	{
		CNetHash aHash[17];
		int Length = CNetHash::MakeHashArray(pAddr, aHash);

		// check ban adresses
		CNode<typename CSessionPool::CDataType, typename CSessionPool::CInfoType> *pNode = m_Pool.Find(pAddr, &aHash[Length]);
		if(!pNode)
			return 0;
		
		return &pNode->m_Info.m_Data;
	}
	
	bool HasSession(const NETADDR *pAddr) const
	{
		CNetHash aHash[17];
		int Length = CNetHash::MakeHashArray(pAddr, aHash);

		// check ban adresses
		CNode<typename CSessionPool::CDataType, typename CSessionPool::CInfoType> *pNode = m_Pool.Find(pAddr, &aHash[Length]);
		if(!pNode)
			return false;
		
		return true;
	}
};

#endif
