#ifndef __LOG_H__
#define __LOG_H__

void WriteLog(char *format, ...);
#ifdef DEBUG
#define DebugLog(a) WriteLog a
#else
#define DebugLog(a)
#endif /* DEBUG */

#endif /* __LOG_H__ */
