#ifndef ENGINE_SERVER_SQL_JOB_H
#define ENGINE_SERVER_SQL_JOB_H

#include "sql_connector.h"

class CSqlJob
{
public:
	bool m_ReadOnly;
	
public:
	virtual ~CSqlJob();

	void StartReadOnly();
	void Start(bool ReadOnly=false);
	static void Exec(void* pData);
	virtual bool Job(CSqlServer* pSqlServer) = 0;
};

#endif

