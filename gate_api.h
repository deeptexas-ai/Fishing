#ifndef __GATE_API_H__
#define __GATE_API_H__

#include "svrapi.h"

class GateServer;

class GateApi : public SvrApi
{
public:
	GateApi(int type, int id, int global, base::Logger *pLogger, base::FileConfig *pConf);
	~GateApi();

public:
	virtual void onOK();
	virtual void onPassClientMsg(const ProtocolMgr &header, const char *str, int len);
	virtual void onPassServerMsg(const ProtocolMgr &header, const char *str, int len);

public:
	void setServer(GateServer *pServer);
	void reportLoad(int n);
	int	getLoad();
	bool isGlobal();

private:
	int m_load;
	int m_global;
	base::Lock m_lock;
	GateServer *m_pServer;
};
#endif
