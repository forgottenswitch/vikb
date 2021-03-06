#ifndef KA_H_INCLUDED
#define KA_H_INCLUDED

#include "stdafx.h"

/* ka.h -- key actions */

#define KA_dkn_count 32

#define KA_PARAMS bool down, SC sc
typedef void (*KA_FUNC)(KA_PARAMS);

void KA_init(void);
int KA_call(UINT id, KA_PARAMS);
int KA_name_to_id(char *name);

void KA_update_dk_names(void);

#endif // KA_H_INCLUDED
