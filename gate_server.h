#ifndef __GATE_SERVER_H__
#define __GATE_SERVER_H__

#include "nskernel/server.h"
#include "base/logger.h"
#include "base/utils.h"
#include "base/configer.h"
#include "base/lock.h"
#include "nskernel/connection.h"
#include "protocolmgr.h"
#include "daytrace.h"
#include "common_struct.h"
#include <bson/bson.h>
//#include "idlproxyapi.h"
#include "gate_api.h"
#include "apiprocessor.h"
#include <map>
#include <vector>
#include "proto_base.h"
#include "common_func.h"
#include "config_mgr.h"
#include "forward.h"
#include "player.h"
#include "proto/proto_10001_account_login.h"
#include "proto/proto_10100_wx_login.h"
#include "async_log.h"

class GateServer : public Server
{
public:
	GateServer();
	virtual ~GateServer();

public:
	virtual void init(const char *conf_file) throw(runtime_error);

	virtual void dataReceived(Connection *pConn, const char *pData, unsigned int nLen);

	virtual void connectionMade(Connection *pConn);

	virtual void connectionLost(Connection *pConn);

	virtual void timeoutHandller(time_t now);

public:
	void init_daylog();

	void userVerifyResult(int sessId, int uid, int res, int platformId, int gameId, int seqno);

	void sendMsg2Client(int fd, const char *str, int len);

	void broadcastMsg2Client(int type, const char *str, int len);

	void broadcast2Server(GateApi *pApi, int uid, int type, int cmd, const char *str, int len);

	void onServerOnline(GateApi *pApi);

	void kickoutUser(int uid, int nCode = 10007001); //踢玩家下线，uid <= 0，踢全部玩家

	//global等服务指令给用户分配或注销游戏服id
	void regUid2GameSvrId(int uid, int gamesvrid, int state);

	void reload();

	void verifyUserWxLogin(Connection *pConn, const char *data, int len);

	void userVerifyWxLoginResult(int sessId, int code, string strErrmsg, string strOpenid, int nTimes, string strToken, int seqno);
	
private:
	void init_server_list();

	void init_admin();

	GateApi *createApi(const CsvConfig::Server &cfg);

	void clearApi();

	bool apiExist(int id);

	void userHeartBeat(Connection *pConn, const ProtocolMgr &header, const char *pData, int len);

	void verifyUserLogin(Connection *pConn, const char *pData, int len);

	GateApi* allocApi(int type, int uid, set<int> & setSvrId);

	GateApi* allocApi(int type, Player *pPlayer, set<int> & setSvrId);

	GateApi* getApi(int id);

	int allocSessId(Connection *pConn);

	void forwardClientMsg(GateApi *pApi, int uid, const char *pData, int len, const std::string &ip);

	void forwardClientMsg(Connection *pConn, const ProtocolMgr &header, const char *pData, int len);

	void sendClientMsg(Connection *pConn, CProtoBase *pReq);

	void reportServerInfo(int uid, int platformId, int gameId);

	GateApi *getLoginApi();

	void setSvrApiOpen(int nSvrId, int nOpen);

	void delayMsg2Log(ProtocolMgr &header, Connection *pConn);


private:
	base::Lock		m_lock;
	PlayerMgr		m_playerMgr;
	DayTrace		*m_pDayLog[MAXLOGFILE];
	base::Logger	*m_pRollLog;
	ApiProcessor	*m_pApiProcessor;
	base::FileConfig m_confMgr;
	time_t m_heartbeat;
	int m_lastChkPlayerLogInf;
	std::map<int, Connection *> m_sess;
	std::map<int, std::vector<GateApi *> > m_svrs;
public:
	AsyncDayLogger m_dayLogMgr;
	//std::map<int, GateApi *> m_mapSvrId2SvrApi;
};

#endif
