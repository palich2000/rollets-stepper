#ifndef _SUPPORT_H_
#define _SUPPORT_H_

#include <Syslog.h>
extern Syslog * syslog;

#define SYSLOG(ident, format, args...) \
{ \
    if (syslog) { \
    syslog->logf(ident, format, ##args); \
    } \
}

#define LOGSERIAL(enable)  \
{ \
if (syslog) { \
syslog->logSerial(enable); \
} \
}
#endif
