#ifndef __PLAYER_H__
#define __PLAYER_H__

#include <map>
#include "gate_api.h"
#include "base/lock.h"
#include "proto_base.h"
#include "nskernel/connection.h"

//玩家上次登录信息
struct PlayerLastLogInfo
{
	int m_lastLogOutTime; //上次下线时间
	int m_lastGameSvrId; //上次游戏服id
	int m_lastPlatFormSvrId; //上次大厅id

	PlayerLastLogInfo()
	{
		m_lastLogOutTime = 0;
		m_lastGameSvrId = 0;
		m_lastPlatFormSvrId = 0;
	}

	PlayerLastLogInfo(const PlayerLastLogInfo& copy)
	{
		m_lastLogOutTime = copy.m_lastLogOutTime;
		m_lastGameSvrId = copy.m_lastGameSvrId;
		m_lastPlatFormSvrId = copy.m_lastPlatFormSvrId;
	}
};
	

class Player
{
public:
	Player(int uid, Connection *pConn);
	~Player();

public:
	void setApi(GateApi *pApi);

	void unSetApi(GateApi *pApi);

	GateApi *getApi(int type);

	GateApi * getApi(int type, set<int> & setSvrId);

	int getId();

	int getFd();

	void online(int seqno, int nSvrType = -1);

	void offline();

	void sendMsg(const char *str, int len);

	const std::map<int, GateApi *> &getApiList();

	void sendMsg(CProtoBase *pMsg);

	Connection *getConnection();

	void setLastLogOutInfo(PlayerLastLogInfo & plInfo);

private:
	int m_uid;
	Connection *m_pConn;
	std::map<int, GateApi *> m_apis;

public:
	PlayerLastLogInfo m_playerLastLogInfo;
};

class PlayerMgr
{
public:
	PlayerMgr();
	~PlayerMgr();

public:
	Player *getPlayerById(int uid);
	Player *getPlayerByFd(int fd);
	void addPlayer(Player *pPlayer);
	void delPlayer(int uid);
	void syncPlayer2Server(GateApi *pApi);
	void broadcastMsg(int type, const char *str, int len);
	int getPlayerNum();

	void setPlayerLastLogInfo(int nUid, int nGameId, int nPlatFormId, int nLastLogOutTime);
	PlayerLastLogInfo getPlayerLastLogInfo(int nUid);
	void checkTimeOut();

	bool getFirstPlayer(Player **pPlayer); //获取玩家管理类的第一个玩家，存在则返回true否则false，传出玩家指针

private:
	std::map<int, Player *> m_players; //uid --> connection
	std::map<int, int> m_fd2id;		// fd --> uid
	std::map<int, PlayerLastLogInfo> m_mapUid2LastLogInfo; //uid -> 此玩家上次登录信息
	std::map<int, Player *> m_playersConnLost; //uid --> connectionLost //掉线下线的玩家信息，避免多线程操作时delete，定时删除
	base::Lock m_lock;
	base::Lock m_lockLastLog;
	base::Lock m_lockConnLost;
};


#endif
