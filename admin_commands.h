#include <stdio.h>
#include "gate_server.h"
#include "base/utils.h"
#include "encoder.h"


#ifndef ADMIN_COMMANDS_H
#define	ADMIN_COMMANDS_H

using namespace std;

namespace admin {
  void admin_test(void* para,int argc, char **argv, std::ostream& os); 
  void reload_conf(void* para,int argc, char **argv, std::ostream& os);
};

#endif	

