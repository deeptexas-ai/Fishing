#include "gate_api.h"
#include "common_def.h"
#include "gate_server.h"
#include "timefuncdef.h"

GateApi::GateApi(int type, int id, int global, base::Logger *pLogger, base::FileConfig *pConf) : SvrApi(type, id, pLogger, pConf)
{
	m_global = global;
	m_pServer = NULL;
	m_load = 0;
}

GateApi::~GateApi()
{}

void GateApi::onPassClientMsg(const ProtocolMgr &header, const char *str, int len)
{
	//m_pLog->normal("GateApi::onPassClientMsg uid:%d,cmd:%d,seq:%d,len:%d", header.m_header.ext,header.m_header.cmd,header.m_header.seqno,len);
	m_pServer->m_dayLogMgr.trace_reduce(GS_DATA_LOG, "%s|send|%ld|%d|%d|%d", base::t2s(time(NULL)).c_str(), TimeFuncDef::getMilliSecondTimeStamp(), header.m_header.ext, header.m_header.cmd, header.m_header.seqno);
	//m_pLog->debug("GateApi::onPassClientMsg enter uid:%d, cmd[%d][%d]", header.m_header.ext,header.m_header.cmd,header.m_header.seqno);
	if(NULL == m_pServer)
	{
		return ;
	}
	SvrApi::onPassClientMsg(header, str, len);
	// ext1 == -1 && ext2 == -1 向所有的用户广播
	// ext1 == -1 && ext2 != -1 向所有服务器类型为ext2的用户广播
	// ext1 != -1 向ext1发送消息
	if(header.m_header.ext == -1) // 广播
	{
		m_pServer->broadcastMsg2Client(header.m_header.ext2, str, len);
	}
	else
	{
		m_pServer->sendMsg2Client(header.m_header.ext, str, len);
	}
	//m_pLog->debug("GateApi::onPassClientMsg exit uid:%d, cmd[%d][%d]", header.m_header.ext,header.m_header.cmd,header.m_header.seqno);
}

void GateApi::onPassServerMsg(const ProtocolMgr &header, const char *str, int len)
{
	//m_pLog->debug("GateApi::onPassServerMsg enter uid:%d, cmd[%d][%d]", header.m_header.ext,header.m_header.cmd,header.m_header.seqno);
	if(m_pServer == NULL)
	{
		return;
	}
	//m_pLog->debug("GateApi::onPassServerMsg cmd:%d",header.m_header.cmd);
	SvrApi::onPassServerMsg(header, str, len);
	if(header.m_header.cmd == protosvr::SVR_USERVERIFYRESULT)
	{
		msgpack::object obj = header.m_pUnpackBody->get();
		protosvr::SvrUserVerifyResultRsp rsp;
		obj.convert(&rsp);
		m_pServer->userVerifyResult(rsp.SESSION_ID, rsp.UID, rsp.RES, rsp.PLATFORM_ID, rsp.GAME_ID, rsp.SEQNO);
	}
	else if(header.m_header.cmd == protosvr::SVR_LOAD)
	{
		msgpack::object obj = header.m_pUnpackBody->get();
		protosvr::SvrUpdateLoadReq req;
		obj.convert(&req);
		m_load = req.LOAD;
	}
	else if(header.m_header.cmd == protosvr::SVR_KICKOUTUSER)
	{
		msgpack::object obj = header.m_pUnpackBody->get();
		protosvr::SvrKickoutUserReq req;
		obj.convert(&req);
		m_pServer->kickoutUser(req.UID, req.CODE);
	}
	else if(header.m_header.cmd == protosvr::SVR_REGUID2GAMESVRID)
	{
		msgpack::object obj = header.m_pUnpackBody->get();
		protosvr::SvrRegUid2GameSvrId req;
		obj.convert(&req);
		m_pServer->regUid2GameSvrId(req.UID, req.GAMESVRID, req.STATE);
	}
	if(header.m_header.cmd == protosvr::SVR_WXUSERVERIFYRESULT)
	{
		msgpack::object obj = header.m_pUnpackBody->get();
		protosvr::SvrUserWxVerifyResultRsp rsp;
		obj.convert(&rsp);
		m_pServer->userVerifyWxLoginResult(rsp.SESSION_ID, rsp.CODE, rsp.ERRMSG, rsp.OPENID, rsp.TIMES, rsp.TOKEN, rsp.SEQNO);
	}
	else
	{
		// ext == -1 向所有类型为ext2的服务器广播
		// ext != -1 向ext所在的ext2服务器发消息
		m_pServer->broadcast2Server(this, header.m_header.ext, header.m_header.ext2, header.m_header.cmd, str, len);
	}
	//m_pLog->debug("GateApi::onPassServerMsg exit uid:%d, cmd[%d][%d]", header.m_header.ext,header.m_header.cmd,header.m_header.seqno);
}

void GateApi::setServer(GateServer *pServer)
{
	m_pServer = pServer;
}

void GateApi::onOK()
{
	m_pServer->onServerOnline(this);
}

void GateApi::reportLoad(int n)
{
	protosvr::SvrUpdateLoadReq req;
	req.SVRID = getId();
	req.LOAD = n;
	sendSvrMsg(protosvr::SVR_LOAD, req);
}

int GateApi::getLoad()
{
	return m_load;
}

bool GateApi::isGlobal()
{
	return m_global;
}
