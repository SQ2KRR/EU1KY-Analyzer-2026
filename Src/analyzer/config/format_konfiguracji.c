#include "format_konfiguracji.h"

uint32_t FORMAT_KONFIG_ObliczCRC32(const uint8_t *dane, uint32_t rozmiar)
{
    uint32_t crc = 0xFFFFFFFFul;

    for (uint32_t i = 0; i < rozmiar; ++i)
    {
        crc ^= dane[i];
        for (uint32_t bit = 0; bit < 8; ++bit)
        {
            const uint32_t maska = (uint32_t)-(int32_t)(crc & 1ul);
            crc = (crc >> 1) ^ (0xEDB88320ul & maska);
        }
    }

    return ~crc;
}

void FORMAT_KONFIG_UtworzNaglowek(FORMAT_KONFIG_NAGLOWEK_t *naglowek,
                                  uint32_t liczba_parametrow,
                                  const uint8_t *dane,
                                  uint32_t rozmiar_danych)
{
    if (naglowek == 0)
        return;

    naglowek->magic = FORMAT_KONFIG_MAGIC;
    naglowek->wersja_formatu = FORMAT_KONFIG_WERSJA;
    naglowek->rozmiar_naglowka = sizeof(FORMAT_KONFIG_NAGLOWEK_t);
    naglowek->liczba_parametrow = liczba_parametrow;
    naglowek->rozmiar_danych = rozmiar_danych;
    naglowek->crc32 = FORMAT_KONFIG_ObliczCRC32(dane, rozmiar_danych);
}

bool FORMAT_KONFIG_CzyNaglowekPoprawny(const FORMAT_KONFIG_NAGLOWEK_t *naglowek,
                                       uint32_t maksymalny_rozmiar_danych)
{
    if (naglowek == 0)
        return false;

    if (naglowek->magic != FORMAT_KONFIG_MAGIC)
        return false;
    if (naglowek->wersja_formatu != FORMAT_KONFIG_WERSJA)
        return false;
    if (naglowek->rozmiar_naglowka != sizeof(FORMAT_KONFIG_NAGLOWEK_t))
        return false;
    if (naglowek->rozmiar_danych != naglowek->liczba_parametrow * sizeof(uint32_t))
        return false;
    if (naglowek->rozmiar_danych > maksymalny_rozmiar_danych)
        return false;

    return true;
}
