#ifndef FORMAT_KONFIGURACJI_H
#define FORMAT_KONFIGURACJI_H

#include <stdbool.h>
#include <stdint.h>

#define FORMAT_KONFIG_MAGIC 0x31474643ul /* "CFG1" w little-endian. */
#define FORMAT_KONFIG_WERSJA 1ul

typedef struct
{
    uint32_t magic;
    uint32_t wersja_formatu;
    uint32_t rozmiar_naglowka;
    uint32_t liczba_parametrow;
    uint32_t rozmiar_danych;
    uint32_t crc32;
} FORMAT_KONFIG_NAGLOWEK_t;

uint32_t FORMAT_KONFIG_ObliczCRC32(const uint8_t *dane, uint32_t rozmiar);
void FORMAT_KONFIG_UtworzNaglowek(FORMAT_KONFIG_NAGLOWEK_t *naglowek,
                                  uint32_t liczba_parametrow,
                                  const uint8_t *dane,
                                  uint32_t rozmiar_danych);
bool FORMAT_KONFIG_CzyNaglowekPoprawny(const FORMAT_KONFIG_NAGLOWEK_t *naglowek,
                                       uint32_t maksymalny_rozmiar_danych);

#endif
