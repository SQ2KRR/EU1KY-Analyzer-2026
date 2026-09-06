#include "kalibracja_meta.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "DS3231.h"
#include "ff.h"
#include "gen.h"
#include "mainwnd.h"
#include "wersja_projektu.h"

#define KAL_META_KATALOG "/aa/cal"
#define KAL_META_LOG "/aa/cal/log.csv"
#define KAL_META_FORMAT 4U
#define KAL_META_KOREKCJA_HW_WERSJA 3U
#define KAL_META_BUFOR_PLIKU 896U
#define KAL_META_BUFOR_CRC 256U

static uint32_t kal_meta_generacja_hw = 0U;

static void KAL_META_Sciezka(KAL_META_TYP_t typ, int32_t profil, char *bufor, size_t rozmiar)
{
    char litera = (profil >= 0 && profil < 26) ? (char)('A' + profil) : '-';

    switch (typ)
    {
    case KAL_META_HW:
        snprintf(bufor, rozmiar, KAL_META_KATALOG "/hw.txt");
        break;
    case KAL_META_OSL:
        snprintf(bufor, rozmiar, KAL_META_KATALOG "/osl_%c.txt", litera);
        break;
    case KAL_META_S21:
        snprintf(bufor, rozmiar, KAL_META_KATALOG "/s21.txt");
        break;
    case KAL_META_TDR:
        snprintf(bufor, rozmiar, KAL_META_KATALOG "/tdr.txt");
        break;
    case KAL_META_BATERIA:
        snprintf(bufor, rozmiar, KAL_META_KATALOG "/bateria.txt");
        break;
    case KAL_META_KWARC:
        snprintf(bufor, rozmiar, KAL_META_KATALOG "/kwarc.txt");
        break;
    case KAL_META_IF:
        snprintf(bufor, rozmiar, KAL_META_KATALOG "/if.txt");
        break;
    default:
        bufor[0] = '\0';
        break;
    }
}

static int KAL_META_SciezkaDanych(KAL_META_TYP_t typ, int32_t profil,
                                  char *bufor, size_t rozmiar)
{
    char litera = (profil >= 0 && profil < 26) ? (char)('A' + profil) : '-';

    if (bufor == NULL || rozmiar == 0U)
        return 0;

    switch (typ)
    {
    case KAL_META_HW:
        snprintf(bufor, rozmiar, "%s/errcorr.osl", g_cfg_osldir);
        return 1;
    case KAL_META_OSL:
        if (profil < 0 || profil >= 26)
            return 0;
        snprintf(bufor, rozmiar, "%s/%c.osl", g_cfg_osldir, litera);
        return 1;
    case KAL_META_S21:
        snprintf(bufor, rozmiar, "%s/txcorr.osl", g_cfg_osldir);
        return 1;
    default:
        bufor[0] = '\0';
        return 0;
    }
}

const char *KAL_META_Nazwa(KAL_META_TYP_t typ)
{
    switch (typ)
    {
    case KAL_META_HW: return "HW";
    case KAL_META_OSL: return "OSL";
    case KAL_META_S21: return "S21";
    case KAL_META_LC: return "LC-REZERWA";
    case KAL_META_TDR: return "TDR";
    case KAL_META_BATERIA: return "BATERIA";
    case KAL_META_KWARC: return "KWARC";
    case KAL_META_IF: return "IF";
    default: return "NIEZNANA";
    }
}

static int KAL_META_CzyTypRF(KAL_META_TYP_t typ)
{
    return typ == KAL_META_HW || typ == KAL_META_OSL ||
           typ == KAL_META_S21 || typ == KAL_META_IF;
}

static int KAL_META_CzyZalezyOdHW(KAL_META_TYP_t typ)
{
    return typ == KAL_META_OSL;
}

static void KAL_META_PobierzCzas(KAL_META_DANE_t *dane)
{
    uint32_t data = 0U;
    uint32_t godzina = 0U;
    unsigned char sekunda = 0U;
    short ampm = 0;

    dane->czas_z_rtc = 0U;
    dane->data_yyyymmdd = 0U;
    dane->czas_hhmmss = 0U;

    if (!RTCpresent)
        return;

    getDate(&data);
    getTime(&godzina, &sekunda, &ampm, 0);

    /*
     * Odrzucamy wartości ewidentnie nieustawionego lub uszkodzonego zegara.
     * Metadane mają pomagać diagnostycznie, więc lepiej zapisać „brak czasu”
     * niż wiarygodnie wyglądającą, lecz fałszywą datę.
     */
    if (data < 20200101U || data > 20991231U || godzina > 2359U || sekunda > 59U)
        return;

    dane->czas_z_rtc = 1U;
    dane->data_yyyymmdd = data;
    dane->czas_hhmmss = (godzina / 100U) * 10000U + (godzina % 100U) * 100U + (uint32_t)sekunda;
}

static void KAL_META_PobierzTemperature(KAL_META_DANE_t *dane)
{
    float temperatura;

    dane->temperatura_rtc_dostepna = 0U;
    dane->temperatura_rtc_c_x100 = 0;

    if (!RTCpresent)
        return;

    temperatura = getTemperature();
    if (!isfinite(temperatura) || temperatura < -40.0f || temperatura > 85.0f)
        return;

    dane->temperatura_rtc_dostepna = 1U;
    dane->temperatura_rtc_c_x100 = (int32_t)(temperatura * 100.0f);
}

static uint32_t KAL_META_CRC32Aktualizuj(uint32_t crc, const uint8_t *dane, uint32_t rozmiar)
{
    uint32_t i;
    uint32_t bit;

    for (i = 0U; i < rozmiar; ++i)
    {
        crc ^= dane[i];
        for (bit = 0U; bit < 8U; ++bit)
        {
            const uint32_t maska = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320UL & maska);
        }
    }
    return crc;
}

int KAL_META_ObliczCRCPliku(KAL_META_TYP_t typ, int32_t profil, uint32_t *crc32)
{
    FIL plik = {0};
    char sciezka[64];
    uint8_t bufor[KAL_META_BUFOR_CRC];
    UINT odczytano = 0U;
    uint32_t crc = 0xFFFFFFFFUL;

    if (crc32 == NULL || !CFG_CzyKartaSDDostepna())
        return 0;
    if (!KAL_META_SciezkaDanych(typ, profil, sciezka, sizeof(sciezka)))
        return 0;
    if (f_open(&plik, sciezka, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return 0;

    do
    {
        if (f_read(&plik, bufor, sizeof(bufor), &odczytano) != FR_OK)
        {
            f_close(&plik);
            return 0;
        }
        crc = KAL_META_CRC32Aktualizuj(crc, bufor, (uint32_t)odczytano);
    } while (odczytano != 0U);

    f_close(&plik);
    *crc32 = ~crc;
    return 1;
}

static int KAL_META_ZapiszPlik(const char *sciezka, KAL_META_TYP_t typ, const KAL_META_DANE_t *dane)
{
    FIL plik = {0};
    FRESULT wynik;
    UINT zapisano = 0U;
    char tmp[72];
    char tresc[KAL_META_BUFOR_PLIKU];
    int dlugosc;

    {
        const char *ukosnik = strrchr(sciezka, '/');
        const char *kropka = strrchr(sciezka, '.');
        size_t dlugosc_bazy;

        /*
         * Przy wyłączonym LFN nazwa typu "hw.txt.tmp" jest niepoprawna.
         * Plik tymczasowy dostaje to samo jądro i rozszerzenie .tmp,
         * np. hw.txt -> hw.tmp, bateria.txt -> bateria.tmp.
         */
        if (kropka != NULL && (ukosnik == NULL || kropka > ukosnik))
            dlugosc_bazy = (size_t)(kropka - sciezka);
        else
            dlugosc_bazy = strlen(sciezka);

        if (dlugosc_bazy + 5U > sizeof(tmp))
            return 0;
        memcpy(tmp, sciezka, dlugosc_bazy);
        memcpy(tmp + dlugosc_bazy, ".tmp", 5U);
    }
    f_unlink(tmp);

    dlugosc = snprintf(tresc, sizeof(tresc),
                       "format=%u\r\n"
                       "typ=%s\r\n"
                       "profil=%ld\r\n"
                       "rtc=%u\r\n"
                       "data=%08lu\r\n"
                       "czas=%06lu\r\n"
                       "temperatura_rtc_dostepna=%u\r\n"
                       "temperatura_rtc_c_x100=%ld\r\n"
                       "firmware=%s\r\n"
                       "fmin_hz=%lu\r\n"
                       "fmax_hz=%lu\r\n"
                       "typ_syntezy=%lu\r\n"
                       "plan_harmoniczny=%lu\r\n"
                       "harmoniczna_max=%lu\r\n"
                       "si5351_max_hz=%lu\r\n"
                       "si5351_xtal_hz=%lu\r\n"
                       "si5351_korekcja_hz=%ld\r\n"
                       "korekcja_hw_wersja=%lu\r\n"
                       "crc_pliku_dostepny=%u\r\n"
                       "crc_pliku=%08lX\r\n"
                       "crc_hw_zaleznosc_dostepna=%u\r\n"
                       "crc_hw_zaleznosc=%08lX\r\n",
                       (unsigned)KAL_META_FORMAT,
                       KAL_META_Nazwa(typ),
                       (long)dane->profil,
                       (unsigned)dane->czas_z_rtc,
                       (unsigned long)dane->data_yyyymmdd,
                       (unsigned long)dane->czas_hhmmss,
                       (unsigned)dane->temperatura_rtc_dostepna,
                       (long)dane->temperatura_rtc_c_x100,
                       PROJEKT_WERSJA,
                       (unsigned long)dane->fmin_hz,
                       (unsigned long)dane->fmax_hz,
                       (unsigned long)dane->typ_syntezy,
                       (unsigned long)dane->plan_harmoniczny,
                       (unsigned long)dane->harmoniczna_max,
                       (unsigned long)dane->si5351_max_hz,
                       (unsigned long)dane->si5351_xtal_hz,
                       (long)dane->si5351_korekcja_hz,
                       (unsigned long)dane->korekcja_hw_wersja,
                       (unsigned)dane->crc_pliku_dostepny,
                       (unsigned long)dane->crc_pliku,
                       (unsigned)dane->crc_hw_zaleznosc_dostepna,
                       (unsigned long)dane->crc_hw_zaleznosc);
    if (dlugosc <= 0 || (size_t)dlugosc >= sizeof(tresc))
        return 0;

    wynik = f_open(&plik, tmp, FA_WRITE | FA_CREATE_ALWAYS);
    if (wynik != FR_OK)
        return 0;

    wynik = f_write(&plik, tresc, (UINT)dlugosc, &zapisano);
    f_close(&plik);
    if (wynik != FR_OK || zapisano != (UINT)dlugosc)
    {
        f_unlink(tmp);
        return 0;
    }

    /* Metadane są diagnostyczne. Wystarczy atomowa podmiana pliku bieżącego. */
    f_unlink(sciezka);
    if (f_rename(tmp, sciezka) != FR_OK)
    {
        f_unlink(tmp);
        return 0;
    }
    return 1;
}

int KAL_META_Usun(KAL_META_TYP_t typ, int32_t profil)
{
    char sciezka[64];
    char tmp[72];
    const char *ukosnik;
    const char *kropka;
    size_t dlugosc_bazy;
    FRESULT wynik;

    KAL_META_Sciezka(typ, profil, sciezka, sizeof(sciezka));
    if (sciezka[0] == '\0')
        return 0;

    ukosnik = strrchr(sciezka, '/');
    kropka = strrchr(sciezka, '.');
    if (kropka != NULL && (ukosnik == NULL || kropka > ukosnik))
        dlugosc_bazy = (size_t)(kropka - sciezka);
    else
        dlugosc_bazy = strlen(sciezka);

    if (dlugosc_bazy + 5U <= sizeof(tmp))
    {
        memcpy(tmp, sciezka, dlugosc_bazy);
        memcpy(tmp + dlugosc_bazy, ".tmp", 5U);
        (void)f_unlink(tmp);
    }

    wynik = f_unlink(sciezka);
    return wynik == FR_OK || wynik == FR_NO_FILE;
}

static void KAL_META_DopiszLog(KAL_META_TYP_t typ, const KAL_META_DANE_t *dane)
{
    FIL plik = {0};
    FRESULT wynik;
    UINT zapisano = 0U;
    char wiersz[384];
    const char *naglowek =
        "data;czas;rtc;typ;profil;firmware;fmin_hz;fmax_hz;typ_syntezy;"
        "plan_harmoniczny;harmoniczna_max;si5351_max_hz;temperatura_rtc_c_x100;"
        "crc_pliku;crc_hw_zaleznosc\r\n";
    int dlugosc;

    wynik = f_open(&plik, KAL_META_LOG, FA_WRITE | FA_OPEN_ALWAYS);
    if (wynik != FR_OK)
        return;

    if (f_size(&plik) == 0U)
    {
        if (f_write(&plik, naglowek, (UINT)strlen(naglowek), &zapisano) != FR_OK ||
            zapisano != (UINT)strlen(naglowek))
        {
            f_close(&plik);
            return;
        }
    }

    if (f_lseek(&plik, f_size(&plik)) != FR_OK)
    {
        f_close(&plik);
        return;
    }

    dlugosc = snprintf(wiersz, sizeof(wiersz),
                       "%08lu;%06lu;%u;%s;%ld;%s;%lu;%lu;%lu;%lu;%lu;%lu;%ld;%08lX;%08lX\r\n",
                       (unsigned long)dane->data_yyyymmdd,
                       (unsigned long)dane->czas_hhmmss,
                       (unsigned)dane->czas_z_rtc,
                       KAL_META_Nazwa(typ),
                       (long)dane->profil,
                       PROJEKT_WERSJA,
                       (unsigned long)dane->fmin_hz,
                       (unsigned long)dane->fmax_hz,
                       (unsigned long)dane->typ_syntezy,
                       (unsigned long)dane->plan_harmoniczny,
                       (unsigned long)dane->harmoniczna_max,
                       (unsigned long)dane->si5351_max_hz,
                       dane->temperatura_rtc_dostepna ? (long)dane->temperatura_rtc_c_x100 : 99999L,
                       (unsigned long)(dane->crc_pliku_dostepny ? dane->crc_pliku : 0U),
                       (unsigned long)(dane->crc_hw_zaleznosc_dostepna ? dane->crc_hw_zaleznosc : 0U));
    if (dlugosc > 0 && (size_t)dlugosc < sizeof(wiersz))
        (void)f_write(&plik, wiersz, (UINT)dlugosc, &zapisano);
    f_close(&plik);
}

int KAL_META_Zapisz(KAL_META_TYP_t typ, int32_t profil)
{
    KAL_META_DANE_t dane;
    char sciezka[64];
    uint32_t crc;

    if (!CFG_CzyKartaSDDostepna())
        return 0;

    memset(&dane, 0, sizeof(dane));
    dane.format = KAL_META_FORMAT;
    dane.profil = profil;
    dane.fmin_hz = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    dane.fmax_hz = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    dane.typ_syntezy = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);
    dane.plan_harmoniczny = GEN_PLAN_HARMONICZNY_WERSJA;
    dane.harmoniczna_max = GEN_MaksHarmoniczna();
    dane.si5351_max_hz = CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);
    dane.si5351_xtal_hz = CFG_GetParam(CFG_PARAM_SI5351_XTAL_FREQ);
    dane.si5351_korekcja_hz = (int32_t)CFG_GetParam(CFG_PARAM_SI5351_CORR);
    dane.korekcja_hw_wersja = KAL_META_KOREKCJA_HW_WERSJA;
    KAL_META_PobierzCzas(&dane);
    KAL_META_PobierzTemperature(&dane);

    if (KAL_META_ObliczCRCPliku(typ, profil, &crc))
    {
        dane.crc_pliku_dostepny = 1U;
        dane.crc_pliku = crc;
    }

    if (KAL_META_CzyZalezyOdHW(typ) && KAL_META_ObliczCRCPliku(KAL_META_HW, -1, &crc))
    {
        dane.crc_hw_zaleznosc_dostepna = 1U;
        dane.crc_hw_zaleznosc = crc;
    }

    (void)f_mkdir("/aa");
    (void)f_mkdir(KAL_META_KATALOG);
    KAL_META_Sciezka(typ, profil, sciezka, sizeof(sciezka));
    if (sciezka[0] == '\0')
        return 0;

    if (!KAL_META_ZapiszPlik(sciezka, typ, &dane))
        return 0;

    KAL_META_DopiszLog(typ, &dane);
    if (typ == KAL_META_HW)
        kal_meta_generacja_hw++;
    return 1;
}

uint32_t KAL_META_PobierzGeneracjeHW(void)
{
    return kal_meta_generacja_hw;
}

static void KAL_META_ParsujLinie(char *linia, KAL_META_DANE_t *dane)
{
    char *rownosc = strchr(linia, '=');
    char *klucz;
    char *wartosc;

    if (rownosc == NULL)
        return;
    *rownosc = '\0';
    klucz = linia;
    wartosc = rownosc + 1;
    wartosc[strcspn(wartosc, "\r\n")] = '\0';

    if (strcmp(klucz, "format") == 0)
        dane->format = (uint8_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "profil") == 0)
        dane->profil = (int32_t)strtol(wartosc, NULL, 10);
    else if (strcmp(klucz, "rtc") == 0)
        dane->czas_z_rtc = (uint8_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "data") == 0)
        dane->data_yyyymmdd = (uint32_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "czas") == 0)
        dane->czas_hhmmss = (uint32_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "temperatura_rtc_dostepna") == 0)
        dane->temperatura_rtc_dostepna = (uint8_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "temperatura_rtc_c_x100") == 0)
        dane->temperatura_rtc_c_x100 = (int32_t)strtol(wartosc, NULL, 10);
    else if (strcmp(klucz, "firmware") == 0)
        snprintf(dane->firmware, sizeof(dane->firmware), "%s", wartosc);
    else if (strcmp(klucz, "fmin_hz") == 0)
        dane->fmin_hz = (uint32_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "fmax_hz") == 0)
        dane->fmax_hz = (uint32_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "typ_syntezy") == 0)
        dane->typ_syntezy = (uint32_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "plan_harmoniczny") == 0)
        dane->plan_harmoniczny = (uint32_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "harmoniczna_max") == 0)
        dane->harmoniczna_max = (uint32_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "si5351_max_hz") == 0)
        dane->si5351_max_hz = (uint32_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "si5351_xtal_hz") == 0)
        dane->si5351_xtal_hz = (uint32_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "si5351_korekcja_hz") == 0)
        dane->si5351_korekcja_hz = (int32_t)strtol(wartosc, NULL, 10);
    else if (strcmp(klucz, "korekcja_hw_wersja") == 0)
        dane->korekcja_hw_wersja = (uint32_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "crc_pliku_dostepny") == 0)
        dane->crc_pliku_dostepny = (uint8_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "crc_pliku") == 0)
        dane->crc_pliku = (uint32_t)strtoul(wartosc, NULL, 16);
    else if (strcmp(klucz, "crc_hw_zaleznosc_dostepna") == 0)
        dane->crc_hw_zaleznosc_dostepna = (uint8_t)strtoul(wartosc, NULL, 10);
    else if (strcmp(klucz, "crc_hw_zaleznosc") == 0)
        dane->crc_hw_zaleznosc = (uint32_t)strtoul(wartosc, NULL, 16);
}

int KAL_META_Pobierz(KAL_META_TYP_t typ, int32_t profil, KAL_META_DANE_t *dane)
{
    FIL plik = {0};
    char sciezka[64];
    char tresc[KAL_META_BUFOR_PLIKU];
    char *linia;
    UINT odczytano = 0U;

    if (dane == NULL)
        return 0;
    memset(dane, 0, sizeof(*dane));
    dane->profil = profil;

    KAL_META_Sciezka(typ, profil, sciezka, sizeof(sciezka));
    if (sciezka[0] == '\0' || f_open(&plik, sciezka, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return 0;

    /*
     * W tej konfiguracji FatFs funkcje f_gets/f_puts są wyłączone
     * (_USE_STRFUNC=0). Plik metadanych jest mały i ma stały, jawny format,
     * dlatego czytamy go jednorazowo i dzielimy na linie w RAM. Plik większy
     * od bufora traktujemy jako uszkodzony zamiast analizować jego fragment.
     */
    if (f_size(&plik) >= sizeof(tresc) ||
        f_read(&plik, tresc, sizeof(tresc) - 1U, &odczytano) != FR_OK)
    {
        f_close(&plik);
        return 0;
    }
    f_close(&plik);
    tresc[odczytano] = '\0';

    linia = strtok(tresc, "\n");
    while (linia != NULL)
    {
        KAL_META_ParsujLinie(linia, dane);
        linia = strtok(NULL, "\n");
    }

    dane->istnieje = 1U;
    return 1;
}

static uint64_t KAL_META_Znacznik(const KAL_META_DANE_t *dane)
{
    return (uint64_t)dane->data_yyyymmdd * 1000000ULL + (uint64_t)dane->czas_hhmmss;
}

int KAL_META_CzyStarsza(const KAL_META_DANE_t *pierwsza, const KAL_META_DANE_t *druga)
{
    if (pierwsza == NULL || druga == NULL || !pierwsza->istnieje || !druga->istnieje ||
        !pierwsza->czas_z_rtc || !druga->czas_z_rtc)
        return 0;
    return KAL_META_Znacznik(pierwsza) < KAL_META_Znacznik(druga);
}

int KAL_META_CzyZgodnaZKonfiguracja(const KAL_META_DANE_t *dane)
{
    if (dane == NULL || !dane->istnieje)
        return 0;

    return dane->fmin_hz == CFG_GetParam(CFG_PARAM_BAND_FMIN) &&
           dane->fmax_hz == CFG_GetParam(CFG_PARAM_BAND_FMAX) &&
           dane->typ_syntezy == CFG_GetParam(CFG_PARAM_SYNTH_TYPE) &&
           dane->plan_harmoniczny == GEN_PLAN_HARMONICZNY_WERSJA &&
           dane->harmoniczna_max == GEN_MaksHarmoniczna() &&
           dane->si5351_max_hz == CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ) &&
           dane->si5351_xtal_hz == CFG_GetParam(CFG_PARAM_SI5351_XTAL_FREQ) &&
           dane->si5351_korekcja_hz == (int32_t)CFG_GetParam(CFG_PARAM_SI5351_CORR) &&
           dane->korekcja_hw_wersja == KAL_META_KOREKCJA_HW_WERSJA;
}

static int64_t KAL_META_DniOdEpoki(uint32_t data_yyyymmdd)
{
    int64_t rok = (int64_t)(data_yyyymmdd / 10000U);
    unsigned miesiac = (unsigned)((data_yyyymmdd / 100U) % 100U);
    unsigned dzien = (unsigned)(data_yyyymmdd % 100U);
    int64_t era;
    unsigned rok_ery;
    unsigned dzien_roku;
    unsigned dzien_ery;
    int miesiac_przesuniety;

    rok -= miesiac <= 2U ? 1 : 0;
    era = (rok >= 0 ? rok : rok - 399) / 400;
    rok_ery = (unsigned)(rok - era * 400);
    miesiac_przesuniety = (int)miesiac + (miesiac > 2U ? -3 : 9);
    dzien_roku = (unsigned)((153 * miesiac_przesuniety + 2) / 5) + dzien - 1U;
    dzien_ery = rok_ery * 365U + rok_ery / 4U - rok_ery / 100U + dzien_roku;
    return era * 146097LL + (int64_t)dzien_ery - 719468LL;
}

static int KAL_META_PobierzAktualnyZnacznik(uint64_t *znacznik, uint32_t *data)
{
    KAL_META_DANE_t teraz;

    if (znacznik == NULL)
        return 0;
    memset(&teraz, 0, sizeof(teraz));
    KAL_META_PobierzCzas(&teraz);
    if (!teraz.czas_z_rtc)
        return 0;

    *znacznik = KAL_META_Znacznik(&teraz);
    if (data != NULL)
        *data = teraz.data_yyyymmdd;
    return 1;
}

void KAL_META_Ocen(KAL_META_TYP_t typ, int32_t profil,
                   const KAL_META_DANE_t *dane, KAL_META_OCENA_t *ocena)
{
    uint32_t crc;
    uint64_t teraz_znacznik;
    uint32_t teraz_data = 0U;
    char sciezka_danych[64];

    if (ocena == NULL)
        return;
    memset(ocena, 0, sizeof(*ocena));

    /* Ta funkcja jest wyłącznie diagnostyczna. Jej wynik nie blokuje pomiaru,
     * nie wyłącza aktywnej kalibracji i nie modyfikuje współczynników OSL/HW. */
    if (dane == NULL || !dane->istnieje)
    {
        ocena->uwagi |= KAL_META_UWAGA_BRAK_META;
        return;
    }

    if (!dane->czas_z_rtc)
        ocena->uwagi |= KAL_META_UWAGA_BRAK_RTC;
    if (dane->format < KAL_META_FORMAT)
        ocena->uwagi |= KAL_META_UWAGA_STARY_FORMAT;
    if (KAL_META_CzyTypRF(typ) && !KAL_META_CzyZgodnaZKonfiguracja(dane))
        ocena->uwagi |= KAL_META_UWAGA_KONFIGURACJA;

    if (KAL_META_SciezkaDanych(typ, profil, sciezka_danych, sizeof(sciezka_danych)))
    {
        if (KAL_META_ObliczCRCPliku(typ, profil, &crc))
        {
            ocena->crc_pliku_biezacy_dostepny = 1U;
            ocena->crc_pliku_biezacy = crc;
            if (dane->crc_pliku_dostepny && dane->crc_pliku != crc)
                ocena->uwagi |= KAL_META_UWAGA_PLIK_ZMIENIONY;
        }
        else
        {
            ocena->uwagi |= KAL_META_UWAGA_BRAK_PLIKU;
        }
    }

    if (KAL_META_CzyZalezyOdHW(typ) && dane->crc_hw_zaleznosc_dostepna)
    {
        if (KAL_META_ObliczCRCPliku(KAL_META_HW, -1, &crc))
        {
            ocena->crc_hw_biezacy_dostepny = 1U;
            ocena->crc_hw_biezacy = crc;
            if (dane->crc_hw_zaleznosc != crc)
                ocena->uwagi |= KAL_META_UWAGA_HW_ZMIENIONE;
        }
        else
        {
            ocena->uwagi |= KAL_META_UWAGA_HW_ZMIENIONE;
        }
    }

    /*
     * CRC jest podstawowym dowodem zależności OSL/L-C od konkretnej HW,
     * bo działa również po przestawieniu zegara. Data stanowi drugi,
     * czytelny dla użytkownika bezpiecznik: jeśli bieżąca HW ma późniejszy
     * znacznik RTC niż OSL/L-C, kalibracja zależna nie może być uznana za
     * aktualną nawet w mało prawdopodobnym przypadku identycznego CRC.
     * Brak prawidłowego RTC nie unieważnia niczego — wtedy pozostaje CRC.
     */
    if (KAL_META_CzyZalezyOdHW(typ))
    {
        KAL_META_DANE_t meta_hw;
        memset(&meta_hw, 0, sizeof(meta_hw));
        if (KAL_META_Pobierz(KAL_META_HW, -1, &meta_hw) &&
            KAL_META_CzyStarsza(dane, &meta_hw))
        {
            ocena->uwagi |= KAL_META_UWAGA_HW_ZMIENIONE;
        }
    }

    if (dane->czas_z_rtc && KAL_META_PobierzAktualnyZnacznik(&teraz_znacznik, &teraz_data))
    {
        const uint64_t zapis = KAL_META_Znacznik(dane);
        if (teraz_znacznik < zapis)
        {
            ocena->uwagi |= KAL_META_UWAGA_RTC_COFNIETY;
        }
        else
        {
            const int64_t dni_teraz = KAL_META_DniOdEpoki(teraz_data);
            const int64_t dni_zapisu = KAL_META_DniOdEpoki(dane->data_yyyymmdd);
            if (dni_teraz >= dni_zapisu)
            {
                ocena->wiek_dostepny = 1U;
                ocena->wiek_dni = (uint32_t)(dni_teraz - dni_zapisu);
            }
        }
    }
}

void KAL_META_FormatujCzas(const KAL_META_DANE_t *dane, char *bufor, size_t rozmiar)
{
    if (bufor == NULL || rozmiar == 0U)
        return;
    if (dane == NULL || !dane->istnieje)
    {
        snprintf(bufor, rozmiar, "--");
        return;
    }
    if (!dane->czas_z_rtc)
    {
        snprintf(bufor, rozmiar, "RTC --");
        return;
    }

    snprintf(bufor, rozmiar, "%02lu.%02lu.%04lu %02lu:%02lu:%02lu",
             (unsigned long)(dane->data_yyyymmdd % 100U),
             (unsigned long)((dane->data_yyyymmdd / 100U) % 100U),
             (unsigned long)(dane->data_yyyymmdd / 10000U),
             (unsigned long)(dane->czas_hhmmss / 10000U),
             (unsigned long)((dane->czas_hhmmss / 100U) % 100U),
             (unsigned long)(dane->czas_hhmmss % 100U));
}
