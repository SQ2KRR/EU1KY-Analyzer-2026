#ifndef _ADF4351_H_
#define _ADF4351_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
    void ADF4351_Init(void);
    void ADF4351_Off(void);
    void ADF4351_SetF0(uint32_t hz);
    void ADF4351_SetLO(uint32_t hz);
    uint32_t ADF4351_MinFreq(void);
    uint32_t ADF4351_MaxFreq(void);
#ifdef __cplusplus
}
#endif

#endif
