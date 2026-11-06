#ifndef PRISM_SYSTEMINFO_H
#define PRISM_SYSTEMINFO_H

#include "core/types.h"

void system_read(SystemInfo *out);
void users_read(Snapshot *snap);
void startup_read(Snapshot *snap);
void startup_set_enabled(const StartupInfo *item, int enabled);
void services_read(Snapshot *snap);
void service_action(const char *unit, const char *verb);
void system_run_command(const char *command);
void tcp_read(TcpInfo *out);
void connections_read(Snapshot *snap);
char *service_journal(const char *unit, int lines);
char *system_journal(int lines);
int connection_kill(const ConnectionInfo *conn);

#endif
