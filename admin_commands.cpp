/*********************************************************************
 *  Copyright 2012, by feiyin.
 *  All right reserved.
 *
 *  功能：命令管理进程
 *
 *  Edit History:
 *
 *    2012/07/04 - Administrator 建立.
 *************************************************************************/
#include <bson/util/json.h>
#include <ostream>

#include "admin_commands.h"


using namespace base;
using namespace std;
using namespace bson;

namespace admin {

	void admin_test(void* para,int argc, char **argv, std::ostream& os)
	{
	   if (argc < 2)
	   {
	    	os<<"found invalid param"<<endl;
	     	return;
	   }

	   os<<"succefully:para count:"<<argc<<",para:";
	   for (int i = 0 ; i < argc;++i)
	   {
	     	os<<argv[i]<<",";
	   }
       os<<endl;
	}


	void reload_conf(void* para,int argc, char **argv, std::ostream& os)
	{
   	   GateServer* pSelf = (GateServer*)para;
	   pSelf->reload();
	   os<<"succefully:reload done"<<endl;
	}

};

