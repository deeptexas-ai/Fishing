
#include "svc.h"
#include "base/utils.h"
#include <string.h>
#include <iostream>

using namespace std;
using namespace base;

GHANDLE * _p = NULL;

void sigusr1_handle( int iSigVal )
{
	svc_reload(_p);
	signal(SIGUSR1, sigusr1_handle);
}

void sigusr2_handle( int iSigVal )
{
	svc_quit(_p);
	signal(SIGUSR2, sigusr2_handle);
}

int main(int argc, char **argv)
{
  	char version[128];
	unsigned versionsize = sizeof(version);
	char errormsg[4096];
	unsigned msgsize = sizeof(errormsg);
	svc_version(version, versionsize);
	if(argc<2 || strcasecmp(argv[1],"HELP")==0)
	{
		cout << "Usage: " << argv[0] << " configfile" << endl;
		exit(1);
	} 
	if(argc<3 || strcasecmp(argv[2],"debug")!=0) 
	{
		Daemon();
	}
	cout << "piperserver "<< version << endl;

	signal(SIGUSR1, sigusr1_handle);
	signal(SIGUSR2, sigusr2_handle);

	_p = svc_create(argv[1], errormsg, msgsize);
	if(_p == NULL) 
	{
		cerr << errormsg << endl;
		exit(-1);
	}

	svc_run(_p);
	svc_destory(_p);
	_p = NULL;
	return 0;
}


