#ifndef ENGINE_SHARED_NETBAN_H
#define ENGINE_SHARED_NETBAN_H

#include <base/system.h>
#include "netdatabase.h"

class CNetBan : public CNetDatabase
{
protected:
	struct CBanInfo
	{
		enum
		{
			EXPIRES_NEVER=-1,
			REASON_LENGTH=64,
		};
		int m_Expires;
		char m_aReason[REASON_LENGTH];
	};

	typedef CPool<NETADDR, CBanInfo, 1> CBanAddrPool;
	typedef CPool<CNetRange, CBanInfo, 16> CBanRangePool;
	typedef CNode<NETADDR, CBanInfo> CBanAddr;
	typedef CNode<CNetRange, CBanInfo> CBanRange;
	
	template<class DATATYPE> void MakeBanInfo(const CNode<DATATYPE, CBanInfo> *pBan, char *pBuf, unsigned BuffSize, int Type) const;
	template<class POOL> int Ban(POOL *pBanPool, const typename POOL::CDataType *pData, int Seconds, const char *pReason);
	template<class POOL> int Unban(POOL *pBanPool, const typename POOL::CDataType *pData);

	class IConsole *m_pConsole;
	class IStorage *m_pStorage;
	CBanAddrPool m_BanAddrPool;
	CBanRangePool m_BanRangePool;
	NETADDR m_LocalhostIPV4, m_LocalhostIPV6;

public:
	enum
	{
		MSGTYPE_PLAYER=0,
		MSGTYPE_LIST,
		MSGTYPE_BANADD,
		MSGTYPE_BANREM,
	};

	class IConsole *Console() const { return m_pConsole; }
	class IStorage *Storage() const { return m_pStorage; }

	virtual ~CNetBan() {}
	void Init(class IConsole *pConsole, class IStorage *pStorage);
	void Update();

	virtual int BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason);
	virtual int BanRange(const CNetRange *pRange, int Seconds, const char *pReason);
	int UnbanByAddr(const NETADDR *pAddr);
	int UnbanByRange(const CNetRange *pRange);
	int UnbanByIndex(int Index);
	void UnbanAll() { m_BanAddrPool.Reset(); m_BanRangePool.Reset(); }
	bool IsBanned(const NETADDR *pAddr, char *pBuf, unsigned BufferSize) const;

	static bool ConBan(class IConsole::IResult *pResult, void *pUser);
	static bool ConBanRange(class IConsole::IResult *pResult, void *pUser);
	static bool ConUnban(class IConsole::IResult *pResult, void *pUser);
	static bool ConUnbanRange(class IConsole::IResult *pResult, void *pUser);
	static bool ConUnbanAll(class IConsole::IResult *pResult, void *pUser);
	static bool ConBans(class IConsole::IResult *pResult, void *pUser);
	static bool ConBansSave(class IConsole::IResult *pResult, void *pUser);
};

#endif
