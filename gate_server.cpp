#include "gate_server.h"
#include "admin_commands.h"
#include "timefuncdef.h"
#include "errorcode.h"

static bool _sort_func(GateApi *p1, GateApi *p2)
{
	if(p1->getOpen() == 0) return false;
	if(p2->getOpen() == 0) return true;
	return p1->getLoad() < p2->getLoad();
}

GateServer::GateServer()
{
	m_pRollLog = NULL;
	m_pApiProcessor = NULL;
	m_heartbeat = 0;
	memset(m_pDayLog, '\0', sizeof(DayTrace *) * MAXLOGFILE);
}

GateServer::~GateServer()
{
	for(int i = 0; i < MAXLOGFILE; ++i)
	{
		if(m_pDayLog[i] != NULL)
		{
			delete m_pDayLog[i];
		}
	}

	if(m_pRollLog != NULL)
	{
		delete m_pRollLog;
	}

	if(m_pApiProcessor != NULL)
	{
		delete m_pApiProcessor;
	}

	for(std::map<int, std::vector<GateApi*> >::iterator i = m_svrs.begin(); i != m_svrs.end(); ++i)
	{
		for(std::vector<GateApi *>::iterator j = i->second.begin(); j != i->second.end(); ++j)
		{
			delete *j;
		}
	}
}

void GateServer::init(const char *conf_file) throw(runtime_error)
{
	try
	{
		Server::init(conf_file);
		m_confMgr.Init(conf_file);
		std::string logpath = m_confMgr["gateserver\\RollLog\\Name"];
		uint32_t logsize = s2u(m_confMgr["gateserver\\RollLog\\Size"]);
		uint32_t lognum = s2u(m_confMgr["gateserver\\RollLog\\Num"]);
		uint32_t loglevel = s2u(m_confMgr["gateserver\\RollLog\\Level"]);

		std::string pidpath = m_confMgr["gateserver\\pidLog\\Name"];

		m_pRollLog = new base::Logger(logpath.c_str(), logsize, lognum, loglevel, true, false);

		m_pApiProcessor = new ApiProcessor(m_pRollLog);

		init_daylog();

		init_server_list();

		redcordProcessPid(pidpath.c_str(), getpid());
		m_pApiProcessor->start();
		m_heartbeat = time(NULL);

		init_admin();
	}
	catch(conf_load_error &ex)
	{
		//cout<<"GateServer::Init failed:"<<ex.what()<<endl;
		throw ex;
	}
	catch(conf_not_find &ex)
	{
		//cout<<"GateServer::Init conf_not_find:"<<ex.what()<<endl;
		throw ex;
	}
        catch (std::string& e)
        {
              //cout<<"GateServer::Init:"<<e<<endl;
              throw e;
        }
	catch(...)
	{
		//cout<<"GateServer::Init Unkown error."<<endl;
               std::string str("unknowed error");
               throw str;
	}
}

void GateServer::dataReceived(Connection *pConn, const char *pData, unsigned int nLen)
{
	if(nLen < 10)
	{
		m_pRollLog->error("[%s:%d]GateServer::dataReceived:invalid packet, %d,%d", __FILE__, __LINE__, pConn->fd(), nLen);
		connectionLost(pConn);
		return;
	}
	ProtocolMgr header;
	try
	{
		header.decode_header(pData, nLen);
		if(header.m_header.stx != 0x09)
		{
			return;
		}
		//m_pRollLog->debug("recv proto(%d)(%d) from fd(%d)",header.m_header.cmd,header.m_header.seqno, pConn->fd());
		//m_pRollLog->debug("GateServer::dataReceived enter fd:%d, cmd[%d][%d]", pConn->fd(),header.m_header.cmd,header.m_header.seqno);
		Player *pPlayer = m_playerMgr.getPlayerByFd(pConn->fd());
        int uid = 0;
        if(pPlayer)
        {
            uid = pPlayer->getId();
        }
		//m_pRollLog->normal("GateServer::dataReceived fd:%d,cmd:%d,seq:%d", pConn->fd(),header.m_header.cmd,header.m_header.seqno);
		m_dayLogMgr.trace_reduce(GS_DATA_LOG , "%s|recv|%ld|%d|%d|%d|%s|%d", base::t2s(time(NULL)).c_str(), TimeFuncDef::getMilliSecondTimeStamp(), uid,header.m_header.cmd, header.m_header.seqno,pConn->getIPStr().c_str(), pConn->fd());
		//检测协议延迟
		//delayMsg2Log(header, pConn);
		
		if(header.m_header.cmd == ACCOUNT_LOGIN)
		{
			verifyUserLogin(pConn, pData, nLen);
		}
		else if(header.m_header.cmd == HEART_BEAT)
		{
			userHeartBeat(pConn, header, pData, nLen);
		}
		else if(header.m_header.cmd == WX_LOGIN)
		{
			verifyUserWxLogin(pConn, pData, nLen);
		}
		else
		{
			forwardClientMsg(pConn, header, pData, nLen);
		}
		//m_pRollLog->debug("GateServer::dataReceived exit fd:%d, cmd[%d][%d]", pConn->fd(),header.m_header.cmd,header.m_header.seqno);
	}
	catch(std::string &e)
	{
		m_pRollLog->error("[%s:%d] GateServer::init %s", __FILE__, __LINE__, e.c_str());
		return;
	}
	catch(exception &e)
	{
		m_pRollLog->error("[%s:%d] GateServer::init %s", __FILE__, __LINE__, e.what());
		return;
	}
	catch(...)
	{
		m_pRollLog->error("[%s:%d] GateServer::init unkown exception", __FILE__, __LINE__);
		return;
	}
}

void GateServer::connectionMade(Connection *pConn)
{
}

void GateServer::connectionLost(Connection *pConn)
{
	if(NULL == pConn)
	{
		return;
	}
	m_pRollLog->normal("GateServer::connectionLost fd:%d", pConn->fd());
	Player *pPlayer = m_playerMgr.getPlayerByFd(pConn->fd());
	if(NULL == pPlayer)
	{
		return;
	}
	int nLastGsId = 0;
	int nLastPfId = 0;
	GateApi *pGsApi = pPlayer->getApi(FISH_SERVER);
	GateApi *pPfApi = pPlayer->getApi(PLATFORM_SERVER);
	if(pGsApi != NULL)
	{
		nLastGsId = pGsApi->getId();
	}
	if(pPfApi != NULL)
	{
		nLastPfId = pPfApi->getId();
	}
	
	m_playerMgr.setPlayerLastLogInfo(pPlayer->getId(), nLastGsId, nLastPfId, time(NULL));
	pPlayer->offline();
	
	protosvr::SvrReportUserStateReq req;
	req.SEQNO = 0;
	req.UID = pPlayer->getId();
	req.STATE = 2;
	for(std::vector<GateApi *>::iterator i = m_svrs[LOGIN_SERVER].begin(); i != m_svrs[LOGIN_SERVER].end(); ++i)
	{
		(*i)->sendSvrMsg(protosvr::SVR_USERSTATE, req);
	}
	
	m_playerMgr.delPlayer(pPlayer->getId());
	pConn->close();

	//m_pRollLog->debug("GateServer::connectionLost exit fd()[%d]", pConn->fd());
}

void GateServer::timeoutHandller(time_t now)
{
	//m_pRollLog->debug("GateServer::timeoutHandller enter now[%d] m_heartbeat[%d]", now,m_heartbeat);
	if(m_heartbeat == 0 || now - m_heartbeat < 30)
	{
		//m_pRollLog->debug("GateServer::timeoutHandller exit1 now[%d] m_heartbeat[%d]", now,m_heartbeat);
		return;
	}
	
	std::map<int, std::vector<GateApi *> >::iterator i = m_svrs.find(LOGIN_SERVER);
	if(i == m_svrs.end())
	{
		//m_pRollLog->debug("GateServer::timeoutHandller exit2 now[%d] m_heartbeat[%d]", now,m_heartbeat);
		return ;
	}
	int n = m_playerMgr.getPlayerNum() + m_sess.size();
	//m_pRollLog->debug("GateServer::online %d", n);
	for(std::vector<GateApi *>::iterator j = i->second.begin(); j != i->second.end(); ++j)
	{
		(*j)->reportLoad(n);
	}
	init_server_list();

	if(base::s2i(m_confMgr["gateserver\\svrInfo\\AllocType"]) == 1)
	{
		for(std::map<int, std::vector<GateApi *> >::iterator i = m_svrs.begin(); i != m_svrs.end(); ++i)
		{
			std::sort(i->second.begin(), i->second.end(), _sort_func);
		}
	}
	
	m_heartbeat = now;

	if(now - m_lastChkPlayerLogInf > PLAYER_SVRINFO_OUTDATETIME)
	{
		m_playerMgr.checkTimeOut();
		m_lastChkPlayerLogInf = now;
	}
	//m_pRollLog->debug("GateServer::timeoutHandller exit now[%d] m_heartbeat[%d]", now,m_heartbeat);
}

void GateServer::init_daylog()
{
    m_pDayLog[NEW_PLAYER_RECORD] = new DayTrace();
	m_pDayLog[NEW_PLAYER_RECORD]->setLogDir(m_confMgr["gateserver\\dayLog\\LogDir"].c_str());
	m_pDayLog[NEW_PLAYER_RECORD]->setLogName(m_confMgr["gateserver\\dayLog\\newplayerrecord\\LogName"].c_str());
	m_pDayLog[NEW_PLAYER_RECORD]->setMaxSize(s2l(m_confMgr["gateserver\\dayLog\\MaxSize"]));
	m_pDayLog[NEW_PLAYER_RECORD]->setLevel(s2u(m_confMgr["gateserver\\dayLog\\Level"]));
	m_pDayLog[NEW_PLAYER_RECORD]->setHourName();

	m_pDayLog[REG_CONN_RECORD] = new DayTrace();
	m_pDayLog[REG_CONN_RECORD]->setLogDir(m_confMgr["gateserver\\dayLog\\LogDir"].c_str());
	m_pDayLog[REG_CONN_RECORD]->setLogName(m_confMgr["gateserver\\dayLog\\pipregconnrecord\\LogName"].c_str());
	m_pDayLog[REG_CONN_RECORD]->setMaxSize(s2l(m_confMgr["gateserver\\dayLog\\MaxSize"]));
	m_pDayLog[REG_CONN_RECORD]->setLevel(s2u(m_confMgr["gateserver\\dayLog\\Level"]));
	m_pDayLog[REG_CONN_RECORD]->setHourName();

	m_pDayLog[CONNECT_TIME_RECORD] = new DayTrace();
	m_pDayLog[CONNECT_TIME_RECORD]->setLogDir(m_confMgr["gateserver\\dayLog\\LogDir"].c_str());
	m_pDayLog[CONNECT_TIME_RECORD]->setLogName(m_confMgr["gateserver\\dayLog\\connecttimerecord\\LogName"].c_str());
	m_pDayLog[CONNECT_TIME_RECORD]->setMaxSize(s2l(m_confMgr["gateserver\\dayLog\\MaxSize"]));
	m_pDayLog[CONNECT_TIME_RECORD]->setLevel(s2u(m_confMgr["gateserver\\dayLog\\Level"]));
	m_pDayLog[CONNECT_TIME_RECORD]->setHourName();

	m_dayLogMgr.addHourDayLogger(GS_DATA_LOG, m_confMgr["gateserver\\dayLog\\LogDir"].c_str(), m_confMgr["gateserver\\dayLog\\datarecord\\LogName"].c_str(), s2l(m_confMgr["gateserver\\dayLog\\MaxSize"]), s2u(m_confMgr["gateserver\\dayLog\\Level"]));
}

void GateServer::init_server_list()
{
	const std::map<int, CsvConfig::Server> svrList = CsvConfigMgr::getInstance().getServerMap();
	//先把m_svrs里有，配置表无，即被删除的服务，从m_svrs和m_pApiProcessor 删掉
	for(std::map<int, std::vector<GateApi *> >::iterator it = m_svrs.begin(); it != m_svrs.end(); )
	{
		//for(int i = 0; i < it->second.size(); )
		for(std::vector<GateApi *>::iterator itVec = it->second.begin(); itVec != it->second.end();)
		{
			if(svrList.find((*itVec)->getId()) == svrList.end())
			{
				GateApi *pApi = *itVec;
				itVec = it->second.erase(itVec);
				m_pApiProcessor->delApi(pApi);
				delete pApi;
			}
			else
			{
				itVec++;
			}
		}

		if(it->second.size() == 0)
		{
			m_svrs.erase(it++);
		}
		else
		{
			it++;
		}
	}

	for(std::map<int, CsvConfig::Server>::const_iterator i = svrList.begin(); i != svrList.end(); ++i)
	{
		GateApi *pApi = createApi(i->second);
		//setSvrApiOpen(i->second.get_server_id(), i->second.get_open());
		if(NULL == pApi)
		{
			continue;
		}
		if(!pApi->init(&m_pApiProcessor->m_epoller, i->first, i->second.get_addr(), i->second.get_port()))
		{
			m_pRollLog->error("[%s:%d] connect to %s:%d failed", __FILE__, __LINE__, i->second.get_addr().c_str(), i->second.get_port());
			delete pApi;
			m_svrs[i->second.get_server_type()].pop_back();
			continue;
		}
		m_pApiProcessor->addApi(pApi);
	}
}

GateApi *GateServer::createApi(const CsvConfig::Server &cfg)
{
	if(apiExist(cfg.get_server_id()))
	{
		return NULL;
	}
	GateApi *pApi = new GateApi(cfg.get_server_type(), cfg.get_server_id(), cfg.get_global(), m_pRollLog, &m_confMgr);
	pApi->setServer(this);
	m_svrs[pApi->getType()].push_back(pApi);
	//m_mapSvrId2SvrApi[cfg.get_server_id()] = pApi;
	return pApi;
}

bool GateServer::apiExist(int id)
{
	if(getApi(id) != NULL)
	{
		return true;
	}
	return false;
}

GateApi *GateServer::getLoginApi()
{
	std::map<int, std::vector<GateApi *> >::iterator i = m_svrs.find(LOGIN_SERVER);
	if(i == m_svrs.end() || i->second.empty())
	{
		return NULL;
	}
	return i->second.front();
}

GateApi *GateServer::getApi(int id)
{
	for(std::map<int, std::vector<GateApi *> >::iterator i = m_svrs.begin(); i != m_svrs.end(); ++i)
	{
		for(std::vector<GateApi *>::iterator j = i->second.begin(); j != i->second.end(); ++j)
		{
			if((*j)->getId() == id)
			{
				return *j;
			}
		}
	}
	return NULL;
}

void GateServer::userHeartBeat(Connection *pConn, const ProtocolMgr &header, const char *pData, int len)
{
	try
	{
		char *ptr = (char *)pData;
		ptr += 24;
        len -= 24;

		msgpack::unpacker unpack_;
		unpack_.reserve_buffer(len);

		memcpy(unpack_.buffer(), ptr, len);
		unpack_.buffer_consumed(len);

		msgpack::unpacked result_;
		unpack_.next(&result_);

		long syc = 0;
		result_.get().convert(&syc);


        long now = TimeFuncDef::getMilliSecondTimeStamp();

        /*if(now - syc >= 1000)
        {
            Player *pPlayer = m_playerMgr.getPlayerByFd(pConn->fd());
            int uid = 0;
            if(NULL != pPlayer)
            {
                uid = pPlayer->getId();
            }
            m_pRollLog->error("GateServer::userHeartBeat fd(%d) uid(%d) heart beat timeout. %ld %ld", pConn->fd(), uid, now, syc);
        }*/
        
		ProtocolMgr mgr;
		mgr.m_header.stx = 0x09;
		mgr.m_header.cmd = HEART_BEAT;
		mgr.m_header.seqno = header.m_header.seqno;

		mgr.m_pEncoder->pack((int)time(NULL));
		mgr.m_pEncoder->pack(syc);

		char buf[64] = {'\0'};
		int nLen = sizeof(buf);

		mgr.setWebSocketSupport(pConn->isWebSocket());
		mgr.encode(buf, nLen);

		pConn->sendMessage(buf, nLen);

		//20180802打印日志，查找超时协议的问题，查完后可把上段error日志打开并删掉下面代码
		/*Player *pPlayer = m_playerMgr.getPlayerByFd(pConn->fd());
        int uid = 0;
        if(NULL != pPlayer)
        {
            uid = pPlayer->getId();
        }
        if(now - syc >= 1000)
        {
            m_pRollLog->error("GateServer::userHeartBeat fd(%d) uid(%d) heart beat timeout. %ld %ld", pConn->fd(), uid, now, syc);
        }*/
		//m_pRollLog->debug("HeartBeat [%d] [%d][%d][%d]", pConn->fd(), uid, mgr.m_header.cmd, mgr.m_header.seqno);
	}
	catch(...)
	{
		m_pRollLog->error("GateServer::userHeartBeat [%s:%d] unkown exception", __FILE__, __LINE__);
	}
}

void GateServer::verifyUserLogin(Connection *pConn, const char *data, int len)
{
	try
	{
		set<int> setSvrId = ForwardMgr::getInstance().getForwardSvrIdSet(ACCOUNT_LOGIN);
		Player *pPlayer = NULL;
		GateApi *pApi = allocApi(LOGIN_SERVER, pPlayer, setSvrId);
		if(NULL == pApi)
		{
			m_pRollLog->error("GateServer::verifyUserLogin [%s:%d] failed", __FILE__, __LINE__);
			connectionLost(pConn);
			return;
		}
		int sessId = allocSessId(pConn);
		forwardClientMsg(pApi, sessId, data, len, pConn->getIPStr());
	}
	catch(...)
	{
		m_pRollLog->error("GateServer::verifyUserLogin [%s:%d] unkown exception", __FILE__, __LINE__);
	}
}

GateApi *GateServer::allocApi(int type, int uid, set<int> & setSvrId)
{
	std::vector<GateApi *> &apiList = m_svrs[type];
	if(apiList.empty())
	{
		return NULL;
	}
	if(type == PLATFORM_SERVER)
	{
		int idx = uid % apiList.size();
		return apiList[idx];
	}
	else
	{
		if(base::s2i(m_confMgr["gateserver\\svrInfo\\AllocType"]) == 1)
		{
			for(int i = 0; i < apiList.size(); i++)
			{
				if(setSvrId.find(apiList[i]->getId()) != setSvrId.end() && apiList[i]->getOpen() == 1)
				{
					return apiList[i];
				}
			}
			return apiList.front();
		}
		else
		{
			GateApi *pApi = NULL;
			for (std::vector<GateApi *>::iterator i = apiList.begin(); i != apiList.end(); ++i)
			{
				if (setSvrId.find((*i)->getId()) == setSvrId.end())
				{
					continue;
				}
				if ((*i)->getOpen() == 0 || ((*i)->getLoad() >= (*i)->getMaxLoad()))
				{
					continue;
				}
				if (pApi == NULL || pApi->getLoad() < (*i)->getLoad())
				{
					pApi = *i;
				}
			}
			return pApi;
		}
	}
	return NULL;
}

GateApi *GateServer::allocApi(int type, Player *pPlayer, set<int> & setSvrId)
{
	
	std::vector<GateApi *> &apiList = m_svrs[type];
	//m_pRollLog->normal("GateServer::allocApi type:%d, apiList.size:%d", type, apiList.size());
	if(apiList.empty())
	{
		m_pRollLog->error("GateServer::allocApi [%s:%d] failed, apiList.empty", __FILE__, __LINE__);
		return NULL;
	}
	if(type == PLATFORM_SERVER)
	{
		//上次登录的服务器还有效，就分配到上次的服务器
		if(pPlayer != NULL && pPlayer->m_playerLastLogInfo.m_lastPlatFormSvrId > 0 
			&& (time(NULL) - pPlayer->m_playerLastLogInfo.m_lastLogOutTime) < PLAYER_SVRINFO_OUTDATETIME)
		{
			GateApi *pLastPfApi = getApi(pPlayer->m_playerLastLogInfo.m_lastPlatFormSvrId);
			if(pLastPfApi != NULL)
			{
				return pLastPfApi;
			}
		}

		std::vector<GateApi *> openApiList;
		for(int i = 0; i < apiList.size(); i++)
		{
			if(apiList[i]->getOpen() == 1)
			{
				openApiList.push_back(apiList[i]);
			}
		}
		if(openApiList.size() > 0)
		{
			int idx = pPlayer->getId() % openApiList.size();
			return openApiList[idx];
		}
		else
		{
			int idx = pPlayer->getId() % apiList.size();
			return apiList[idx];
		}
		
	}
	else if(type == FISH_SERVER)
	{
		if(pPlayer != NULL && pPlayer->m_playerLastLogInfo.m_lastGameSvrId > 0 
			&& (time(NULL) - pPlayer->m_playerLastLogInfo.m_lastLogOutTime) < PLAYER_SVRINFO_OUTDATETIME
			&& setSvrId.find(pPlayer->m_playerLastLogInfo.m_lastGameSvrId) != setSvrId.end())
		{
			GateApi *pLastGsApi = getApi(pPlayer->m_playerLastLogInfo.m_lastGameSvrId);
			if(pLastGsApi != NULL)
			{
				return pLastGsApi;
			}
		}
	}
	
	if(base::s2i(m_confMgr["gateserver\\svrInfo\\AllocType"]) == 0) //服务器分配方式(大厅和游戏服) 1:轮询分配  0:分满一个后再分另外一个
	{
		GateApi *pApi = NULL;
		for (std::vector<GateApi *>::iterator i = apiList.begin(); i != apiList.end(); ++i)
		{
			if (setSvrId.find((*i)->getId()) == setSvrId.end())
			{
				continue;
			}
			if ((*i)->getOpen() == 0 || ((*i)->getLoad() >= (*i)->getMaxLoad()))
			{
				continue;
			}
			if (pApi == NULL || pApi->getLoad() < (*i)->getLoad())
			{
				pApi = *i;
			}
		}
		//避免分配失败
		if(pApi == NULL)
		{
			int idx = pPlayer->getId() % apiList.size();
			pApi = apiList[idx];
		}
		return pApi;
		
	}
	else //20190618 默认轮询分配
	{
		for(int i = 0; i < apiList.size(); i++)
		{
			if(setSvrId.find(apiList[i]->getId()) != setSvrId.end() && apiList[i]->getOpen() == 1)
			{
				return apiList[i];
			}
		}
		return apiList.front();
	}
}

int GateServer::allocSessId(Connection *pConn)
{
	static int sessId = 0;
	if(sessId == 0x7FFFFFFF)
	{
		sessId = 0;
	}
	++sessId;
	base::Guard g(m_lock);
	m_sess[sessId] = pConn;
	return sessId;
}

void GateServer::forwardClientMsg(GateApi *pApi, int uid, const char *pData, int len, const std::string &ip)
{
	if(NULL == pApi)
	{
		return;
	}
	char *pTemp = (char *)pData;
	pTemp += 20;
	base::h2n_32(pTemp, inet_addr((char *)(ip.c_str())));
	pTemp = (char *)pData;
	pTemp += 16;
	base::h2n_32(pTemp, uid);
	pApi->sendMsg(pData, len);
}

void GateServer::forwardClientMsg(Connection *pConn, const ProtocolMgr &header, const char *pData, int len)
{
	//m_pRollLog->debug("GateServer::forwardClientMsg enter fd[%d] cmd[%d] seq[%d]", pConn->fd(),header.m_header.cmd,header.m_header.seqno);
	int svrType = ForwardMgr::getInstance().getForwardSvrType(header.m_header.cmd);
	if(-1 == svrType)
	{
		m_pRollLog->error("GateServer::forwardClientMsg [%s:%d] cmd[%d] not found svrType", __FILE__, __LINE__, header.m_header.cmd);
		return;
	}
	set<int> setSvrId = ForwardMgr::getInstance().getForwardSvrIdSet(header.m_header.cmd);
	if(setSvrId.size() < 1)
	{
		m_pRollLog->error("GateServer::forwardClientMsg [%s:%d] cmd[%d] not found setSvrId", __FILE__, __LINE__, header.m_header.cmd);
		return;
	}

	Player *pPlayer = m_playerMgr.getPlayerByFd(pConn->fd());
	if(NULL == pPlayer)
	{
		m_pRollLog->error("GateServer::forwardClientMsg [%s:%d] fd[%d] not found player", __FILE__, __LINE__, pConn->fd());
		return;
	}
	GateApi *pApi = pPlayer->getApi(svrType, setSvrId);
	if(NULL == pApi)
	{
		GateApi *pOldApi = pPlayer->getApi(svrType); //一个玩家一次只能在一个类型的服，如之前已有相同类型的服，则对那个服下线
		if(pOldApi != NULL)
		{
			pPlayer->unSetApi(pOldApi);
		}
		pApi = allocApi(svrType, pPlayer, setSvrId);
        if(NULL == pApi)
        {
            m_pRollLog->error("GateServer::forwardClientMsg alloc server failed cmd:%d seq:%d uid:%d", header.m_header.cmd, header.m_header.seqno, pPlayer->getId());
            return;
        }
        if(!pApi->isGlobal())
        {
            pPlayer->setApi(pApi);
            int nPlatSvrId = -1;
            int nGameSvrId = -1;
            if(svrType == PLATFORM_SERVER)
            {
                nPlatSvrId = pApi->getId();
            }
            else if(svrType == FISH_SERVER)
            {
                nGameSvrId = pApi->getId();
                pPlayer->online(0, svrType);
            }
            if(nPlatSvrId > 0 || nGameSvrId > 0)
            {
                reportServerInfo(pPlayer->getId(), nPlatSvrId, nGameSvrId);
            }
        }
	}
	
	m_pRollLog->normal("GateServer::forwardClientMsg  uid:%d,cmd:%d,seq:%d,fd:%d,svr:%d", pPlayer->getId(), header.m_header.cmd, header.m_header.seqno, pConn->fd(),  pApi->getId());
	forwardClientMsg(pApi, pPlayer->getId(), pData, len, pConn->getIPStr());
	//m_pRollLog->debug("GateServer::forwardClientMsg exit fd[%d] cmd[%d] seq[%d]", pConn->fd(),header.m_header.cmd,header.m_header.seqno);
}

void GateServer::sendClientMsg(Connection *pConn, CProtoBase *pReq)
{
	char buf[10240] = {0};
	int len = sizeof(buf);
	pReq->encode_s2c(buf, len);
	if (pConn->isWebSocket())
   {
	   int nLenTmp = len+32;	
	   char*bufTmp  = new char [nLenTmp] ;//{'\0'};
	   memset(bufTmp,0,nLenTmp);
	   WSHandller::packData(buf,len,bufTmp, nLenTmp);
	   pConn->sendMessage(bufTmp,nLenTmp);
	   delete []bufTmp;
	   return;
   }
	pConn->sendMessage(buf, len);
}

void GateServer::reportServerInfo(int uid, int platformId, int gameId)
{
	protosvr::SvrReportServerInfoReq req;
	req.UID = uid;
	req.PLATFORM_ID = platformId;
	req.GAME_ID = gameId;

	GateApi *pApi = getLoginApi();
	if(NULL == pApi)
	{
		m_pRollLog->error("GateServer::reportServerInfo [%s:%d] Player[%d] get login api NULL", __FILE__, __LINE__, uid);
		return;
	}
	pApi->sendSvrMsg(protosvr::SVR_REPORTSVR, req);
}

void GateServer::userVerifyResult(int sessId, int uid, int res, int platformId, int gameId, int seqno)
{
	//m_pRollLog->debug("GateServer::userVerifyResult Player[%d] sessId[%d] res:%d, platformId:%d, gameId:%d, seqno:%d", 
	//	uid, sessId,res, platformId, gameId, seqno);
	base::Guard g(m_lock);
	try
	{
		Connection *pConn = NULL;
		std::map<int, Connection *>::iterator i = m_sess.find(sessId);
		if(i == m_sess.end())
		{
			m_pRollLog->error("GateServer::userVerifyResult [%s:%d] Player[%d] sessId[%d] not found", __FILE__, __LINE__, uid, sessId);
			return;
		}
		pConn = i->second;
		m_sess.erase(i);
		if(NULL == pConn)
		{
			return;
		}
		if(res != 0)
		{
			proto10login::CProto10001AccountLogin ack;
			ack.seqno = seqno;
			ack.m_s2c.reason = res;
			sendClientMsg(pConn, &ack);
			connectionLost(pConn);
			return;
		}

		//要支持多网关，在登录服踢人
		/*Player *pOldPlayer = m_playerMgr.getPlayerById(uid);
		if(pOldPlayer != NULL)
		{
			m_pRollLog->debug("GateServer::userVerifyResult [%s:%d] Player[%d] other oline, kickoff", __FILE__, __LINE__, uid);
			kickoutUser(uid, ERROR_CODE::EC_ACC_LOGIN_OTHENWHERE);
		}*/

		Player *pPlayer = new Player(uid, pConn);

		GateApi *pPlatform = getApi(platformId);
	
		if(pPlatform == NULL)
		{
			//所有用户大厅的cmd都一样，可不跟据cmd进行分配
			set<int> setSvrId;
			pPlatform = allocApi(PLATFORM_SERVER, pPlayer, setSvrId);
		}
		if(NULL == pPlatform)
		{
			proto10login::CProto10001AccountLogin ack;
			ack.seqno = seqno;
			ack.m_s2c.reason = ERROR_CODE::EC_SVR_MAINTAINING;
			sendClientMsg(pConn, &ack);
			connectionLost(pConn);
			//kickoutUser(uid, 10007003);
			m_pRollLog->error("GateServer::userVerifyResult [%s:%d] Player[%d] alloc platform failed", __FILE__, __LINE__, uid);
			delete pPlayer;
			return;
		}
		pPlayer->setApi(pPlatform);

		GateApi *pGame = getApi(gameId);
		if(pGame != NULL)
		{
			pPlayer->setApi(pGame);
		}

		pPlayer->online(seqno);

		reportServerInfo(uid, pPlatform->getId(), gameId);

		m_playerMgr.addPlayer(pPlayer);

		PlayerLastLogInfo plInfo = m_playerMgr.getPlayerLastLogInfo(uid);
		pPlayer->setLastLogOutInfo(plInfo);
	}
	catch(msgpack::type_error &e)
	{
		m_pRollLog->error("GateServer::userVerifyResult [%s:%d] %s", __FILE__, __LINE__, e.what());
	}
	catch(...)
	{
		m_pRollLog->error("GateServer::userVerifyResult [%s:%d]unkown", __FILE__, __LINE__);
	}
}

void GateServer::sendMsg2Client(int uid, const char *str, int len)
{
	Player *pPlayer = m_playerMgr.getPlayerById(uid);
	if(NULL == pPlayer)
	{
		m_pRollLog->error("GateServer::sendMsg2Client [%s:%d] Player[%d] not found", __FILE__, __LINE__, uid);
		return;
	}
	pPlayer->sendMsg(str, len);
	//m_pRollLog->normal("GateServer::sendMsg2Client end, uid:%d, len:%d", uid,len);
}

void GateServer::broadcastMsg2Client(int type, const char *str, int len)
{
	m_playerMgr.broadcastMsg(type, str, len);
}

void GateServer::broadcast2Server(GateApi *pApi, int uid, int type, int cmd, const char *str, int len)
{
	//m_pRollLog->debug("GateServer::broadcast2Server enter uid:%d, cmd[%d][%d]", uid,cmd,type);
	if(-1 == uid)
	{
		std::map<int, std::vector<GateApi *> >::iterator i = m_svrs.find(type);
		if(i == m_svrs.end())
		{
			m_pRollLog->error("GateServer::broadcast2Server [%s:%d] SvrType[%d] not found", __FILE__, __LINE__, type);
			return;
		}
		for(std::vector<GateApi *>::iterator j = i->second.begin(); j != i->second.end(); ++j)
		{
			//m_pRollLog->debug("GateServer::broadcast2Server[%s:%d] [%d] send %d to [%d]", __FILE__, __LINE__, pApi->getId(), cmd, (*j)->getId());
			(*j)->sendMsg(str, len);
		}
	}
	else
	{
		Player *pPlayer = m_playerMgr.getPlayerById(uid);
		if(NULL == pPlayer)
		{
			m_pRollLog->error("GateServer::broadcast2Server [%s:%d] Player[%d] not found", __FILE__, __LINE__, uid);
			return;
		}
		GateApi *p = pPlayer->getApi(type);
		if(NULL == p)
		{
			m_pRollLog->error("GateServer::broadcast2Server [%s:%d] SvrType[%d] not found", __FILE__, __LINE__, type);
			return;
		}
		//m_pRollLog->debug("GateServer::broadcast2Server[%s:%d] [%d] send %d to [%d]", __FILE__, __LINE__, pApi->getId(), cmd, p->getId());
		p->sendMsg(str, len);
	}
	//m_pRollLog->debug("GateServer::broadcast2Server exit uid:%d, cmd[%d][%d]", uid,cmd,type);
}

void GateServer::onServerOnline(GateApi *pApi)
{
	m_playerMgr.syncPlayer2Server(pApi);
}

void GateServer::kickoutUser(int uid, int nCode)
{
	//m_pRollLog->debug("GateServer::kickoutUser uid:%d, nCode:%d", uid, nCode);
	proto10login::CProto10007AccountLogout ack;
	ack.m_s2c.code = nCode;

	if(uid > 0)
	{
		Player *pPlayer = m_playerMgr.getPlayerById(uid);
		if(NULL == pPlayer)
		{
			return;
		}

		pPlayer->sendMsg(&ack);

		connectionLost(pPlayer->getConnection());
	}
	else if(uid == -99)
	{
		//踢所有玩家下线
		Player * pPlayer = NULL;
		bool bHasPlayer = m_playerMgr.getFirstPlayer(&pPlayer);
		while(bHasPlayer)
		{
			if(pPlayer != NULL && pPlayer->getId() > 0)
			{
				kickoutUser(pPlayer->getId(), nCode);
			}
		}
	}
	
}

//global等服务指令给用户分配或注销游戏服id
void GateServer::regUid2GameSvrId(int uid, int gamesvrid, int state)
{
	//m_pRollLog->debug("GateServer::regUid2GameSvrId [%s:%d] begin [%d][%d][%d]", __FILE__, __LINE__, uid,gamesvrid,state);
	Player *pPlayer = m_playerMgr.getPlayerById(uid);
	if(NULL == pPlayer)
	{
		return;
	}
	
	GateApi *pGame = getApi(gamesvrid);
	if(pGame != NULL)
	{
		if(state == USER_STAT_ONLINE)
		{
			GateApi *pOldGame = pPlayer->getApi(pGame->getType());
			if(pOldGame != NULL && pOldGame->getId() != gamesvrid)
			{
				pPlayer->unSetApi(pOldGame);
				pPlayer->setApi(pGame);
				pPlayer->online(0, FISH_SERVER);//给新游戏服通知玩家上线
				//m_pRollLog->debug("GateServer::regUid2GameSvrId 1 [%s:%d] send online![%d][%d][%d]", __FILE__, __LINE__, uid,gamesvrid,state);
			}
			else if(pOldGame == NULL)
			{
				pPlayer->setApi(pGame);
				pPlayer->online(0, FISH_SERVER);//给新游戏服通知玩家上线
				//m_pRollLog->debug("GateServer::regUid2GameSvrId 2 [%s:%d] send online![%d][%d][%d]", __FILE__, __LINE__, uid,gamesvrid,state);
			}
			reportServerInfo(uid, -1, gamesvrid);
		}
		else
		{
			pPlayer->unSetApi(pGame);
		}
	}
	else
	{
		m_pRollLog->error("GateServer::regUid2GameSvrId [%s:%d] getApi err![%d][%d][%d]", __FILE__, __LINE__, uid,gamesvrid,state);
	}
}


void GateServer::init_admin()
{
	ServerAdmin& as = getServerAdmin();

	AdminCmdInfo info;
	info.func_para = this;
	info.desc = "usage:test_admin para1 para2";
	info.func = admin::admin_test;
	as.addCommand("test_admin", info);

	AdminCmdInfo infoReload;
	infoReload.func_para = this;
	infoReload.desc = "usage:reload";
	infoReload.func = admin::reload_conf;
	as.addCommand("reload", infoReload);

}

void GateServer::reload()
{
	CsvConfigMgr::getInstance().reload();
	m_confMgr.Load();
	m_pRollLog->normal("GateServer::reload end");
}

void GateServer::setSvrApiOpen(int nSvrId, int nOpen)
{
	GateApi *pApi = getApi(nSvrId);
	if(pApi != NULL)
	{
		pApi->setOpen(nOpen);
	}
}

void GateServer::verifyUserWxLogin(Connection *pConn, const char *data, int len)
{
	try
	{
		set<int> setSvrId = ForwardMgr::getInstance().getForwardSvrIdSet(WX_LOGIN);
		Player *pPlayer = NULL;
		GateApi *pApi = allocApi(LOGIN_SERVER, pPlayer, setSvrId);
		if(NULL == pApi)
		{
			m_pRollLog->error("GateServer::verifyUserWxLogin [%s:%d] failed", __FILE__, __LINE__);
			connectionLost(pConn);
			return;
		}
		int sessId = allocSessId(pConn);
		forwardClientMsg(pApi, sessId, data, len, pConn->getIPStr());
	}
	catch(...)
	{
		m_pRollLog->error("GateServer::verifyUserWxLogin [%s:%d] unkown exception", __FILE__, __LINE__);
	}
}

void GateServer::userVerifyWxLoginResult(int sessId, int code, string strErrmsg, string strOpenid, int nTimes, string strToken, int seqno)
{
	base::Guard g(m_lock);
	try
	{
		Connection *pConn = NULL;
		std::map<int, Connection *>::iterator i = m_sess.find(sessId);
		if(i == m_sess.end())
		{
			m_pRollLog->error("GateServer::userVerifyWxLoginResult [%s:%d] seqno[%d] sessId[%d] not found", __FILE__, __LINE__, seqno, sessId);
			return;
		}
		pConn = i->second;
		m_sess.erase(i);
		if(NULL == pConn)
		{
			return;
		}

		proto10login::CProto10100WxLogin ack;
		ack.seqno = seqno;
		ack.m_s2c.code = code;
		ack.m_s2c.errmsg = strErrmsg;
		ack.m_s2c.openid = strOpenid;
		ack.m_s2c.timestamp = nTimes;
		ack.m_s2c.token = strToken;
		sendClientMsg(pConn, &ack);
		//connectionLost(pConn);
		//m_pRollLog->debug("GateServer::userVerifyWxLoginResult [%s:%d] code:%d, openid:%s", __FILE__, __LINE__, code, strOpenid.c_str());
	}
	catch(msgpack::type_error &e)
	{
		m_pRollLog->error("GateServer::userVerifyWxLoginResult [%s:%d] %s", __FILE__, __LINE__, e.what());
	}
	catch(...)
	{
		m_pRollLog->error("GateServer::userVerifyWxLoginResult [%s:%d]unkown", __FILE__, __LINE__);
	}
}

void GateServer::delayMsg2Log(ProtocolMgr &header, Connection *pConn)
{
	//m_pRollLog->debug("recv proto(%d)(%d) from fd(%d)",header.m_header.cmd,header.m_header.seqno, pConn->fd());
	int nNow = time(NULL);
	if(header.m_header.ext != 0 && nNow - header.m_header.ext > 1) //1秒以上就是延迟，写日志，客户端ext传入发送时间戳
	{
		int nUid = 0;
		int fd = 0;
		if(pConn)
		{
			fd = pConn->fd();
			
			Player *pPlayer = m_playerMgr.getPlayerByFd(pConn->fd());
		
			if(pPlayer != NULL) nUid = pPlayer->getId();
		}
		
		m_pDayLog[CONNECT_TIME_RECORD]->trace_debug("uid:%d,cmd:%d,seq:%d,now:%d,sndtime:%d,fd:%d", 
		nUid,header.m_header.cmd, header.m_header.seqno, nNow, header.m_header.ext,fd);
	}
}

