/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

/*
    This code implements a simple memory heap utilizing a pool of equal size memory blocks.
*/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "sdram_heap.h"
#include "crash.h"

#define SDRH_BLKSIZE 128
#define SDRH_HEAPSIZE 0x200000 // Must be agreed with linker scrpt's _SDRAM_HEAP_SIZE, and must be multiple of SDRH_BLKSIZE !!!
#define SDRH_NBLOCKS (SDRH_HEAPSIZE / SDRH_BLKSIZE)
#define SDRH_ADDR(block) ((void *)(SDRH_START + (block) * SDRH_BLKSIZE))

#if ((SDRH_BLKSIZE == 0) || (SDRH_HEAPSIZE % SDRH_BLKSIZE) != 0)
#error SDRAM heap size is incorrect
#endif

#if (SDRH_NBLOCKS >= 0x10000) // Must be less than 65536 to fit into uint16_t
#error SDRAM heap: too many blocks
#endif

extern uint8_t __sdram_heap_start__;
extern uint8_t __sdram_heap_end__;

static uint8_t *const SDRH_START = &__sdram_heap_start__;
static uint8_t *const SDRH_END = &__sdram_heap_end__;

static uint16_t __attribute__((section(".user_sdram"))) SDRH_descr[SDRH_NBLOCKS]; //Heap descriptor

static void SDRH_Init(void)
{
    static bool isSDRHInitialized = false;
    if (!isSDRHInitialized)
    {
        memset(SDRH_descr, 0, sizeof(SDRH_descr));
        isSDRHInitialized = true;
    }
}
//It is expected that ptr belongs to SDRAM HEAP designated area, and is aligned at the block start
static bool _isValidPtr(void *ptr)
{
    const uintptr_t adres = (uintptr_t)ptr;
    const uintptr_t poczatek = (uintptr_t)SDRH_START;
    const uintptr_t koniec = (uintptr_t)SDRH_END;

    if (adres < poczatek || adres >= koniec)
        return false;
    if (((adres - poczatek) % SDRH_BLKSIZE) != 0U)
        return false;
    return true;
}

static uint32_t _find_area(uint32_t nblocks)
{
    uint32_t i = 0;
    while (i < SDRH_NBLOCKS)
    {
        if (0 == SDRH_descr[i]) //found free
        {
            if (1 == nblocks)
                return i;
            //count how many free blocks exist ahead
            uint32_t j = i + 1;
            uint32_t nfound = 1;
            while (j < SDRH_NBLOCKS)
            {
                if (0 == SDRH_descr[j])
                {
                    if (++nfound == nblocks)
                    {
                        return i; //End of search: enough blocks found
                    }
                    j++;
                }
                else
                    break;
            }
            //not found
            if (j >= SDRH_NBLOCKS)
                break;             //Return not found
            i = j + SDRH_descr[j]; //Skip over too small area and next occupied area
        }
        else
        {
            i += SDRH_descr[i]; //Skip over occupied area
        }
    }
    return 0xFFFFFFFFul; //Not found
}

static void *SDRH_Alokuj(size_t nbytes, bool krytyczny)
{
    if (0 == nbytes)
        return 0;

    SDRH_Init();

    uint32_t nblocks;
    nblocks = (nbytes / SDRH_BLKSIZE) + (0 != (nbytes % SDRH_BLKSIZE));

    uint32_t start_block = _find_area(nblocks);

    if (start_block >= SDRH_NBLOCKS)
    {
        /*
         * Starszy kod oczekuje zatrzymania programu przy braku SDRAM i nadal
         * dostaje takie zachowanie przez SDRH_malloc(). Nowe ekrany, które
         * potrafią bezpiecznie zrezygnować z funkcji, używają wariantu try.
         */
        if (krytyczny)
            CRASHF("SDRH_malloc: cannot allocate %u bytes", nbytes);
        return 0;
    }

    void *addr = SDRH_ADDR(start_block);
    while (nblocks)
    {
        SDRH_descr[start_block++] = nblocks--;
    }
    return addr;
}

void *SDRH_malloc(size_t nbytes)
{
    return SDRH_Alokuj(nbytes, true);
}

void *SDRH_try_malloc(size_t nbytes)
{
    return SDRH_Alokuj(nbytes, false);
}

void SDRH_free(void *ptr)
{
    if (!_isValidPtr(ptr))
    {
        if (0 != ptr)
        {
            CRASHF("SDRH_free invalid pointer 0x%08lX", (unsigned long)(uintptr_t)ptr);
        }
        return;
    }
    SDRH_Init();
    uint32_t block = (uint32_t)(((uintptr_t)ptr - (uintptr_t)SDRH_START) / SDRH_BLKSIZE);
    uint32_t nblocks = SDRH_descr[block];
    if (block + nblocks > SDRH_NBLOCKS) //Should not happen, but let's take care of this
        nblocks = SDRH_NBLOCKS - block;
    while (nblocks--)
    {
        SDRH_descr[block++] = 0;
    }
}

static void *SDRH_ZmienRozmiar(void *ptr, size_t nbytes, bool krytyczny)
{
    if (0 == nbytes)
    {
        SDRH_free(ptr);
        return 0;
    }
    if (0 == ptr)
        return SDRH_Alokuj(nbytes, krytyczny);
    if (!_isValidPtr(ptr))
    {
        if (krytyczny)
            CRASHF("SDRH_realloc invalid pointer 0x%08lX", (unsigned long)(uintptr_t)ptr);
        return 0;
    }

    SDRH_Init();

    uint32_t nblocks;
    nblocks = (nbytes / SDRH_BLKSIZE) + (0 != (nbytes % SDRH_BLKSIZE));

    uint32_t block = (uint32_t)(((uintptr_t)ptr - (uintptr_t)SDRH_START) / SDRH_BLKSIZE);
    if (SDRH_descr[block] >= nblocks)
        return ptr;

    void *pnew = SDRH_Alokuj(nbytes, krytyczny);
    if (0 == pnew)
        return 0;

    memcpy(pnew, ptr, SDRH_descr[block] * SDRH_BLKSIZE);
    SDRH_free(ptr);
    return pnew;
}

void *SDRH_realloc(void *ptr, size_t nbytes)
{
    return SDRH_ZmienRozmiar(ptr, nbytes, true);
}

void *SDRH_try_realloc(void *ptr, size_t nbytes)
{
    return SDRH_ZmienRozmiar(ptr, nbytes, false);
}

void *SDRH_calloc(size_t nbytes)
{
    void *ptr = SDRH_malloc(nbytes);
    if (0 != ptr)
    {
        memset(ptr, 0, nbytes);
    }
    return ptr;
}

void SDRH_PobierzStatystyke(SDRH_STATYSTYKA_t *statystyka)
{
    uint32_t i;
    uint32_t uzyte_bloki = 0U;
    uint32_t biezacy_wolny = 0U;
    uint32_t najwiekszy_wolny = 0U;

    if (statystyka == 0)
        return;

    SDRH_Init();

    for (i = 0U; i < SDRH_NBLOCKS; i++)
    {
        if (SDRH_descr[i] == 0U)
        {
            biezacy_wolny++;
            if (biezacy_wolny > najwiekszy_wolny)
                najwiekszy_wolny = biezacy_wolny;
        }
        else
        {
            uzyte_bloki++;
            biezacy_wolny = 0U;
        }
    }

    statystyka->pojemnosc_b = SDRH_HEAPSIZE;
    statystyka->uzyte_b = uzyte_bloki * SDRH_BLKSIZE;
    statystyka->wolne_b = SDRH_HEAPSIZE - statystyka->uzyte_b;
    statystyka->najwiekszy_wolny_blok_b = najwiekszy_wolny * SDRH_BLKSIZE;
}
