#ifndef HMI_H
#define HMI_H

#include "mod_types.h"

void HMI_SetText(const char *obj, const char *txt);
void HMI_ShowStatus(const char *s);
void HMI_ShowResult(const EstResult_t *r);
void dbg_printf(const char *fmt, ...);

#endif
