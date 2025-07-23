
#ifndef __SVC_H__
#define __SVC_H__


#ifdef __cplusplus
	#define SVC_EXPORT extern "C"
#else
	#define SVC_EXPORT
#endif

typedef void GHANDLE;

SVC_EXPORT GHANDLE * svc_create(const char *config_file,char * errormsg,unsigned msgsize);

SVC_EXPORT void svc_run(GHANDLE * ghandle);

SVC_EXPORT void svc_run_thread(GHANDLE * ghandle);

SVC_EXPORT void svc_destory(GHANDLE * ghandle);

SVC_EXPORT void svc_version(char * version,unsigned versionsize);

SVC_EXPORT void svc_info(GHANDLE * ghandle,char * msg,unsigned msgsize);

SVC_EXPORT void svc_reload(GHANDLE * ghandle);

SVC_EXPORT void svc_quit(GHANDLE * ghandle);


#endif

