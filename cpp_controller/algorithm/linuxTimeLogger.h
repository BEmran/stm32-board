/* Copyright 2020-2022 The MathWorks, Inc. */

#ifndef _LINUXTIMELOGGER_H_
#define _LINUXTIMELOGGER_H_


#ifdef __cplusplus
extern "C"
{
#endif

 #if (defined(MATLAB_MEX_FILE) || defined(RSIM_PARAMETER_LOADING) ||  defined(RSIM_WITH_SL_SOLVER))
/* This will be used in Rapid Accelerator Mode */
#define MW_GetTimeInMillis()  (0)
#define MW_GetTimeInMicros()  (0)
#else

#include <stdio.h>
#include <stdint.h>
#include <time.h>
uint64_t MW_GetTimeInMillis();
uint64_t MW_GetTimeInMicros();
#endif

#ifdef __cplusplus
}
#endif

#endif