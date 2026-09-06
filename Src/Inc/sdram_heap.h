/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#ifndef _SDRAM_HEAP_H_
#define _SDRAM_HEAP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void *SDRH_malloc(size_t nbytes);
    void *SDRH_try_malloc(size_t nbytes);
    void SDRH_free(void *ptr);
    void *SDRH_realloc(void *ptr, size_t nbytes);
    void *SDRH_try_realloc(void *ptr, size_t nbytes);
    void *SDRH_calloc(size_t nbytes);

    typedef struct
    {
        uint32_t pojemnosc_b;
        uint32_t uzyte_b;
        uint32_t wolne_b;
        uint32_t najwiekszy_wolny_blok_b;
    } SDRH_STATYSTYKA_t;

    void SDRH_PobierzStatystyke(SDRH_STATYSTYKA_t *statystyka);

#ifdef __cplusplus
}
#endif

#endif //_SDRAM_HEAP_H_
