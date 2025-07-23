#include "player.h"

Player::Player(int uid, Connection *pConn)
{
	m_uid = uid;
	m_pConn = pConn;
}

Player::~Player()
{
	m_pConn = NULL;
}

void Player::setApi(GateApi *pApi)
{
	std::map<int, GateApi *>::iterator it = m_apis.find(pApi->getType());
	if(it != m_apis.end())
	{
		m_apis.erase(it);
	}
	m_apis.insert(std::make_pair(pApi->getType(), pApi));
}

void Player::unSetApi(GateApi *pApi)
{
	std::map<int, GateApi *>::iterator it = m_apis.find(pApi->getType());
	if(it != m_apis.end())
	{
		protosvr::SvrReportUserStateReq req;
		req.UID = m_uid;
		req.STATE = USER_STAT_OFFLINE;
		it->second->sendSvrMsg(protosvr::SVR_USERSTATE, req, m_uid);
		
		m_apis.erase(it);
	}
}


Connection *Player::getConnection()
{
	return m_pConn;
}

GateApi *Player::getApi(int type)
{
	std::map<int, GateApi *>::iterator i = m_apis.find(type);
	if(i == m_apis.end())
	{
		return NULL;
	}
	return i->second;
}

GateApi *Player::getApi(int type, set<int> & setSvrId)
{
	std::map<int, GateApi *>::iterator i = m_apis.find(type);
	if(i == m_apis.end())
	{
		return NULL;
	}
	if(setSvrId.find(i->second->getId()) == setSvrId.end())
	{
		return NULL;
	}
	return i->second;
}


int Player::getId()
{
	return m_uid;
}

void Player::online(int seqno, int nSvrType)
{
	try
	{
		protosvr::SvrReportUserStateReq req;
		req.SEQNO = seqno;
		req.UID = m_uid;
		req.STATE = 1;
		for(std::map<int, GateApi *>::iterator i = m_apis.begin(); i != m_apis.end(); ++i)
		{
			if(nSvrType > 0 && nSvrType != i->second->getType())
			{
				continue;
			}
			i->second->sendSvrMsg(protosvr::SVR_USERSTATE, req, m_uid);
		}
	}
	catch(...)
	{
		//printf("[%s:%d](%s) unkown exception\n", __FILE__, __LINE__, __func__);
	}
}

void Player::offline()
{
	try
	{
		protosvr::SvrReportUserStateReq req;
		req.UID = m_uid;
		req.STATE = 2;
		for(std::map<int, GateApi *>::iterator i = m_apis.begin(); i != m_apis.end(); ++i)
		{
			i->second->sendSvrMsg(protosvr::SVR_USERSTATE, req, m_uid);
		}
	}
	catch(...)
	{
		//printf("[%s:%d](%s) unkown exception\n", __FILE__, __LINE__, __func__);
	}
}

int Player::getFd()
{
	if(NULL == m_pConn)
	{
		return -1;
	}
	return m_pConn->fd();
}

void Player::sendMsg(const char *str, int len)
{
	if(m_pConn)
	{
		if (m_pConn->isWebSocket())
	   {
		   int nLenTmp = len+32;	
		   char*bufTmp  = new char [nLenTmp] ;//{'\0'};
		   memset(bufTmp,0,nLenTmp);
		   WSHandller::packData(str,len,bufTmp, nLenTmp);
		   m_pConn->sendMessage(bufTmp,nLenTmp);
		   delete []bufTmp;
		   return;
	   }
		m_pConn->sendMessage(str, len);
	}
}

const std::map<int, GateApi *> &Player::getApiList()
{
	return m_apis;
}

void Player::sendMsg(CProtoBase *pMsg)
{
	char buf[10240] = {0};
	int len = sizeof(buf);

	pMsg->encode_s2c(buf, len);
	if(m_pConn)
	{
		if (m_pConn->isWebSocket())
	   {
		   int nLenTmp = len+32;	
		   char*bufTmp  = new char [nLenTmp] ;//{'\0'};
		   memset(bufTmp,0,nLenTmp);
		   WSHandller::packData(buf,len,bufTmp, nLenTmp);
		   m_pConn->sendMessage(bufTmp,nLenTmp);
		   delete []bufTmp;
		   return;
	   }
		m_pConn->sendMessage(buf, len);
	}
}

void Player::setLastLogOutInfo(PlayerLastLogInfo & plInfo)
{
	m_playerLastLogInfo = plInfo;
}



PlayerMgr::PlayerMgr()
{}

PlayerMgr::~PlayerMgr()
{
	for(std::map<int, Player *>::iterator i = m_players.begin(); i != m_players.end(); ++i)
	{
		delete i->second;
	}
	m_players.clear();
}

void PlayerMgr::addPlayer(Player *pPlayer)
{
	base::Guard g(m_lock);
	m_players[pPlayer->getId()] = pPlayer;
	int fd = pPlayer->getFd();
	m_fd2id[fd] = pPlayer->getId();
}

void PlayerMgr::delPlayer(int uid)
{
	base::Guard g(m_lock);
	std::map<int, Player *>::iterator i = m_players.find(uid);
	if(i == m_players.end())
	{
		return;
	}
	int fd = i->second->getFd();
	m_fd2id.erase(fd);
	//20190513 两个线程同时用指针操作player对象避免加锁复杂化，玩家下线时丢到m_playersConnLost，定时检测删除
	//delete i->second;
	{
		base::Guard gConnLost(m_lockConnLost);
		if(m_playersConnLost.find(uid) != m_playersConnLost.end())
		{
			delete m_playersConnLost[uid];
		}
		m_playersConnLost[uid] = i->second;
	}
	m_players.erase(i);
}

Player *PlayerMgr::getPlayerById(int uid)
{
	base::Guard g(m_lock);
	std::map<int, Player *>::iterator i = m_players.find(uid);
	if(i == m_players.end())
	{
		return NULL;
	}
	return i->second;
}

Player *PlayerMgr::getPlayerByFd(int fd)
{
	int uid = -1;
	{
		base::Guard g(m_lock);
		std::map<int, int>::iterator i = m_fd2id.find(fd);
		if(i == m_fd2id.end())
		{
			return NULL;
		}
		uid = i->second;
	}
	if(uid == -1)
	{
		return NULL;
	}
	return getPlayerById(uid);
}

void PlayerMgr::broadcastMsg(int type, const char *str, int len)
{
	try
	{
		base::Guard g(m_lock);
		for(std::map<int, Player *>::iterator i = m_players.begin(); i != m_players.end(); ++i)
		{
			if(-1 == type)
			{
				i->second->sendMsg(str, len);
			}
			else
			{
				if(i->second->getApi(type) != NULL)
				{
					i->second->sendMsg(str, len);
				}
			}
		}
	}
	catch(...)
	{
		//printf("[%s:%d](%s) unkown exception\n", __FILE__, __LINE__, __func__);
	}
}

void PlayerMgr::syncPlayer2Server(GateApi *pApi)
{
	try
	{
		base::Guard g(m_lock);
		protosvr::SvrConnectionMake req;
		for(std::map<int, Player *>::iterator i = m_players.begin(); i != m_players.end(); ++i)
		{
			const std::map<int, GateApi *> &apiList = i->second->getApiList();
			for(std::map<int, GateApi *>::const_iterator j = apiList.begin(); j != apiList.end(); ++j)
			{
				if(j->second->getId() == pApi->getId())
				{
					req.USERLIST.push_back(i->first);
				}
			}
		}
		pApi->sendSvrMsg(protosvr::SVR_CONNECTIONMAKE, req);
	}
	catch(...)
	{
		//printf("[%s:%d](%s) unkown exception\n", __FILE__, __LINE__, __func__);
	}
}

int PlayerMgr::getPlayerNum()
{
	base::Guard g(m_lock);
	return m_players.size();
}

void PlayerMgr::setPlayerLastLogInfo(int nUid, int nGameId, int nPlatFormId, int nLastLogOutTime)
{
	base::Guard g(m_lockLastLog);
	if(m_mapUid2LastLogInfo.find(nUid) != m_mapUid2LastLogInfo.end())
	{
		if(nGameId > 0) m_mapUid2LastLogInfo[nUid].m_lastGameSvrId = nGameId;
		if(nPlatFormId > 0) m_mapUid2LastLogInfo[nUid].m_lastPlatFormSvrId = nPlatFormId;
		if(nLastLogOutTime > 0) m_mapUid2LastLogInfo[nUid].m_lastLogOutTime = nLastLogOutTime;
	}
	else
	{
		PlayerLastLogInfo plInfo;
		plInfo.m_lastGameSvrId = nGameId;
		plInfo.m_lastPlatFormSvrId = nPlatFormId;
		plInfo.m_lastLogOutTime = nLastLogOutTime;

		m_mapUid2LastLogInfo[nUid] = plInfo;
	}
}

PlayerLastLogInfo PlayerMgr::getPlayerLastLogInfo(int nUid)
{
	base::Guard g(m_lockLastLog);
	if(m_mapUid2LastLogInfo.find(nUid) != m_mapUid2LastLogInfo.end())
	{
		return m_mapUid2LastLogInfo[nUid];
	}
	else
	{
		PlayerLastLogInfo plInfo;
		return plInfo;
	}
}

void PlayerMgr::checkTimeOut()
{
	int nCurTime = time(NULL);
	base::Guard g(m_lockLastLog);
	for(std::map<int, PlayerLastLogInfo>::iterator it = m_mapUid2LastLogInfo.begin(); it != m_mapUid2LastLogInfo.end(); )
	{
		if(nCurTime - it->second.m_lastLogOutTime > PLAYER_SVRINFO_OUTDATETIME)
		{
			{
				base::Guard gConnLost(m_lockConnLost);
				//定时检测下线玩家的map是否过期，删除
				std::map<int, Player *>::iterator itLost = m_playersConnLost.find(it->first);
				if(itLost != m_playersConnLost.end())
				{
					delete itLost->second;
					m_playersConnLost.erase(itLost);
				}
			}
			m_mapUid2LastLogInfo.erase(it++);
		}
		else
		{
			it++;
		}
	}
	
}

bool PlayerMgr::getFirstPlayer(Player **pPlayer)
{
	base::Guard g(m_lock);
	std::map<int, Player *>::iterator i = m_players.begin();
	if(i != m_players.end())
	{
		*pPlayer = i->second;
		return true;
	}
	else
	{
		*pPlayer = NULL;
	}
	return false;
}

