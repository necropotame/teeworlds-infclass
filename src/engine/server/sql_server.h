#ifdef CONF_SQL
#ifndef ENGINE_SERVER_SQL_SERVER_H
#define ENGINE_SERVER_SQL_SERVER_H

#include <base/system.h>
#include <mysql_connection.h>

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/statement.h>

enum
{
	//Never, never, never, ..., NEVER change these values
	//otherwise, the statistics in the database will be corrupted
	SQL_SCORETYPE_ROUND_SCORE=0,
	
	SQL_SCORETYPE_ENGINEER_SCORE=100,
	SQL_SCORETYPE_SOLDIER_SCORE=101,
	SQL_SCORETYPE_SCIENTIST_SCORE=102,
	SQL_SCORETYPE_MEDIC_SCORE=103,
	SQL_SCORETYPE_NINJA_SCORE=104,
	SQL_SCORETYPE_MERCENARY_SCORE=105,
	SQL_SCORETYPE_SNIPER_SCORE=106,
	SQL_SCORETYPE_HERO_SCORE=107,
	SQL_SCORETYPE_BIOLOGIST_SCORE=108,
	SQL_SCORETYPE_LOOPER_SCORE=109,
	
	SQL_SCORETYPE_SMOKER_SCORE=200,
	SQL_SCORETYPE_HUNTER_SCORE=201,
	SQL_SCORETYPE_BOOMER_SCORE=202,
	SQL_SCORETYPE_GHOST_SCORE=203,
	SQL_SCORETYPE_SPIDER_SCORE=204,
	SQL_SCORETYPE_UNDEAD_SCORE=205,
	SQL_SCORETYPE_WITCH_SCORE=206,
	SQL_SCORETYPE_GHOUL_SCORE=207,
	SQL_SCORETYPE_SLUG_SCORE=208,
	
	SQL_SCORE_NUMROUND=32,
};

enum
{
	SQL_USERLEVEL_NORMAL = 0,
	SQL_USERLEVEL_MOD = 1,
	SQL_USERLEVEL_ADMIN = 2,
};

class CSqlServer
{
public:
	CSqlServer(const char* pDatabase, const char* pPrefix, const char* pUser, const char* pPass, const char* pIp, int Port, bool ReadOnly = true, bool SetUpDb = false);
	~CSqlServer();

	bool Connect();
	void Disconnect();
	void CreateTables();

	void executeSql(const char* pCommand);
	void executeSqlQuery(const char* pQuery);

	sql::ResultSet* GetResults() { return m_pResults; }

	const char* GetDatabase() { return m_aDatabase; }
	const char* GetPrefix() { return m_aPrefix; }
	const char* GetUser() { return m_aUser; }
	const char* GetPass() { return m_aPass; }
	const char* GetIP() { return m_aIp; }
	int GetPort() { return m_Port; }

	void Lock() { lock_wait(m_SqlLock); }
	void UnLock() { lock_unlock(m_SqlLock); }

	static int ms_NumReadServer;
	static int ms_NumWriteServer;

private:
	sql::Driver *m_pDriver;
	sql::Connection *m_pConnection;
	sql::Statement *m_pStatement;
	sql::ResultSet *m_pResults;

	// copy of config vars
	char m_aDatabase[64];
	char m_aPrefix[64];
	char m_aUser[64];
	char m_aPass[64];
	char m_aIp[64];
	int m_Port;

	bool m_SetUpDB;

	LOCK m_SqlLock;
};

#endif
#endif
