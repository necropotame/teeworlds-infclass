#ifndef ENGINE_SERVER_SQL_JOB_H
#define ENGINE_SERVER_SQL_JOB_H

#include "sql_string_helpers.h"
#include "sql_connector.h"

class CSqlJob
{
protected:
	bool m_ReadOnly;
	int m_Instance;
	
public:
	virtual ~CSqlJob();

	void StartReadOnly();
	void Start(bool ReadOnly=false);
	static void Exec(void* pData);
	
	virtual bool Job(CSqlServer* pSqlServer) = 0;
	virtual void CleanInstanceRef() = 0;
	
	int GetInstance() { return m_Instance; }
};

#endif

