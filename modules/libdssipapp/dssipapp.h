#ifndef DSSIPAPP_H__
#define DSSIPAPP_H__

#ifdef __cplusplus
extern "C" {
#endif

int simple_call(const char *uri);
int simple_hangup(void);
int simple_quit(void);
int start_sip(const char *path);

char config_path[250];

#ifdef __cplusplus
}
#endif // end __cplusplus
#endif // end DSSIPAPP_H__