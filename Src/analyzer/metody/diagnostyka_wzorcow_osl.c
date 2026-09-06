#include "diagnostyka_wzorcow_osl.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ff.h"

#define OSL_DIAG_MAGIC 0x4F444731UL /* ODG1 */
#define OSL_DIAG_WERSJA 1U
#define OSL_DIAG_KATALOG "/aa/osl_diag"
#define OSL_DIAG_MIN_PROC_POPRAWNYCH 80U

typedef struct
{
    uint32_t magic;
    uint16_t wersja;
    uint16_t liczba_punktow;
    uint16_t liczba_poprawnych;
    uint8_t typ;
    uint8_t rezerwa;
    uint32_t f_min_hz;
    uint32_t f_max_hz;
} OSL_DIAG_NAGLOWEK_PLIKU_t;

typedef struct
{
    uint32_t czestotliwosc_hz;
    float gamma_re;
    float gamma_im;
    uint8_t poprawny;
    uint8_t rezerwa[3];
} OSL_DIAG_PUNKT_PLIKU_t;

static bool OSL_DIAG_CzyGammaPoprawna(float complex gamma)
{
    return isfinite(crealf(gamma)) && isfinite(cimagf(gamma));
}

static const char *OSL_DIAG_Sciezka(OSL_DIAG_TYP_t typ)
{
    switch (typ)
    {
    case OSL_DIAG_SHORT: return OSL_DIAG_KATALOG "/short.bin";
    case OSL_DIAG_LOAD:  return OSL_DIAG_KATALOG "/load.bin";
    case OSL_DIAG_OPEN:  return OSL_DIAG_KATALOG "/open.bin";
    default: return NULL;
    }
}

const char *OSL_DIAG_NazwaTypu(OSL_DIAG_TYP_t typ)
{
    switch (typ)
    {
    case OSL_DIAG_SHORT: return "SHORT";
    case OSL_DIAG_LOAD:  return "LOAD";
    case OSL_DIAG_OPEN:  return "OPEN";
    default: return "?";
    }
}

bool OSL_DIAG_Utworz(OSL_DIAG_TYP_t typ,
                     const uint32_t *czestotliwosci_hz,
                     const float complex *gamma_przed_osl,
                     const uint8_t *poprawny,
                     uint16_t liczba,
                     OSL_DIAG_PROFIL_t *profil)
{
    uint16_t i;

    if (profil == NULL || czestotliwosci_hz == NULL || gamma_przed_osl == NULL ||
        liczba < 3U || liczba > OSL_DIAG_MAX_PUNKTOW || typ > OSL_DIAG_OPEN)
        return false;

    memset(profil, 0, sizeof(*profil));
    profil->typ = typ;
    profil->liczba_punktow = liczba;
    profil->f_min_hz = czestotliwosci_hz[0];
    profil->f_max_hz = czestotliwosci_hz[liczba - 1U];
    if (profil->f_min_hz == 0U || profil->f_max_hz <= profil->f_min_hz)
        return false;

    for (i = 0U; i < liczba; ++i)
    {
        OSL_DIAG_PUNKT_t *p = &profil->punkty[i];
        p->czestotliwosc_hz = czestotliwosci_hz[i];
        if ((i > 0U && czestotliwosci_hz[i] <= czestotliwosci_hz[i - 1U]) ||
            !OSL_DIAG_CzyGammaPoprawna(gamma_przed_osl[i]) ||
            (poprawny != NULL && poprawny[i] == 0U))
            continue;
        p->gamma_przed_osl = gamma_przed_osl[i];
        p->poprawny = 1U;
        ++profil->liczba_poprawnych;
    }

    return (uint32_t)profil->liczba_poprawnych * 100U >=
           (uint32_t)profil->liczba_punktow * OSL_DIAG_MIN_PROC_POPRAWNYCH;
}

static float OSL_DIAG_RoznicaFazyStopnie(float a, float b)
{
    float d = a - b;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

bool OSL_DIAG_Porownaj(const OSL_DIAG_PROFIL_t *baza,
                       const OSL_DIAG_PROFIL_t *aktualny,
                       OSL_DIAG_POROWNANIE_t *wynik)
{
    uint16_t i;
    uint16_t n = 0U;
    double suma_gamma2 = 0.0;
    double suma_db2 = 0.0;
    double suma_faza2 = 0.0;
    float max_gamma = 0.0f;

    if (baza == NULL || aktualny == NULL || wynik == NULL ||
        baza->typ != aktualny->typ || baza->liczba_punktow != aktualny->liczba_punktow ||
        baza->liczba_punktow < 3U)
        return false;

    memset(wynik, 0, sizeof(*wynik));

    for (i = 0U; i < baza->liczba_punktow; ++i)
    {
        float complex gb;
        float complex ga;
        float dg;
        float mb;
        float ma;
        float db = 0.0f;
        float faza;

        if (!baza->punkty[i].poprawny || !aktualny->punkty[i].poprawny ||
            baza->punkty[i].czestotliwosc_hz != aktualny->punkty[i].czestotliwosc_hz)
            continue;

        gb = baza->punkty[i].gamma_przed_osl;
        ga = aktualny->punkty[i].gamma_przed_osl;
        if (!OSL_DIAG_CzyGammaPoprawna(gb) || !OSL_DIAG_CzyGammaPoprawna(ga))
            continue;

        dg = cabsf(ga - gb);
        mb = cabsf(gb);
        ma = cabsf(ga);
        if (mb > 1.0e-9f && ma > 1.0e-9f)
            db = 20.0f * log10f(ma / mb);
        faza = OSL_DIAG_RoznicaFazyStopnie(cargf(ga) * 57.2957795131f,
                                           cargf(gb) * 57.2957795131f);

        suma_gamma2 += (double)dg * (double)dg;
        suma_db2 += (double)db * (double)db;
        suma_faza2 += (double)faza * (double)faza;
        if (dg > max_gamma) max_gamma = dg;
        ++n;
    }

    if ((uint32_t)n * 100U < (uint32_t)baza->liczba_punktow * 60U)
        return false;

    wynik->liczba_porownanych = n;
    wynik->rms_delta_gamma = (float)sqrt(suma_gamma2 / (double)n);
    wynik->max_delta_gamma = max_gamma;
    wynik->rms_delta_amplitudy_db = (float)sqrt(suma_db2 / (double)n);
    wynik->rms_delta_fazy_stopnie = (float)sqrt(suma_faza2 / (double)n);
    return true;
}

bool OSL_DIAG_Zapisz(const OSL_DIAG_PROFIL_t *profil)
{
    FIL plik;
    FRESULT fr;
    UINT zapisano;
    OSL_DIAG_NAGLOWEK_PLIKU_t h;
    const char *sciezka;
    uint16_t i;

    if (profil == NULL || profil->typ > OSL_DIAG_OPEN ||
        profil->liczba_punktow < 3U || profil->liczba_punktow > OSL_DIAG_MAX_PUNKTOW)
        return false;
    sciezka = OSL_DIAG_Sciezka(profil->typ);
    if (sciezka == NULL)
        return false;
    fr = f_mkdir(OSL_DIAG_KATALOG);
    if (fr != FR_OK && fr != FR_EXIST)
        return false;
    if (f_open(&plik, sciezka, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return false;

    memset(&h, 0, sizeof(h));
    h.magic = OSL_DIAG_MAGIC;
    h.wersja = OSL_DIAG_WERSJA;
    h.liczba_punktow = profil->liczba_punktow;
    h.liczba_poprawnych = profil->liczba_poprawnych;
    h.typ = (uint8_t)profil->typ;
    h.f_min_hz = profil->f_min_hz;
    h.f_max_hz = profil->f_max_hz;
    if (f_write(&plik, &h, sizeof(h), &zapisano) != FR_OK || zapisano != sizeof(h))
    { f_close(&plik); return false; }

    for (i = 0U; i < profil->liczba_punktow; ++i)
    {
        OSL_DIAG_PUNKT_PLIKU_t p;
        memset(&p, 0, sizeof(p));
        p.czestotliwosc_hz = profil->punkty[i].czestotliwosc_hz;
        p.gamma_re = crealf(profil->punkty[i].gamma_przed_osl);
        p.gamma_im = cimagf(profil->punkty[i].gamma_przed_osl);
        p.poprawny = profil->punkty[i].poprawny;
        if (f_write(&plik, &p, sizeof(p), &zapisano) != FR_OK || zapisano != sizeof(p))
        { f_close(&plik); return false; }
    }
    (void)f_sync(&plik);
    f_close(&plik);
    return true;
}

bool OSL_DIAG_Wczytaj(OSL_DIAG_TYP_t typ, OSL_DIAG_PROFIL_t *profil)
{
    FIL plik;
    UINT odczytano;
    OSL_DIAG_NAGLOWEK_PLIKU_t h;
    const char *sciezka = OSL_DIAG_Sciezka(typ);
    uint16_t i;

    if (profil == NULL || sciezka == NULL ||
        f_open(&plik, sciezka, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return false;
    if (f_read(&plik, &h, sizeof(h), &odczytano) != FR_OK || odczytano != sizeof(h) ||
        h.magic != OSL_DIAG_MAGIC || h.wersja != OSL_DIAG_WERSJA || h.typ != (uint8_t)typ ||
        h.liczba_punktow < 3U || h.liczba_punktow > OSL_DIAG_MAX_PUNKTOW)
    { f_close(&plik); return false; }

    memset(profil, 0, sizeof(*profil));
    profil->typ = typ;
    profil->liczba_punktow = h.liczba_punktow;
    profil->f_min_hz = h.f_min_hz;
    profil->f_max_hz = h.f_max_hz;
    for (i = 0U; i < h.liczba_punktow; ++i)
    {
        OSL_DIAG_PUNKT_PLIKU_t p;
        if (f_read(&plik, &p, sizeof(p), &odczytano) != FR_OK || odczytano != sizeof(p))
        { f_close(&plik); return false; }
        if (i > 0U && p.czestotliwosc_hz <= profil->punkty[i - 1U].czestotliwosc_hz)
        { f_close(&plik); return false; }
        profil->punkty[i].czestotliwosc_hz = p.czestotliwosc_hz;
        profil->punkty[i].gamma_przed_osl = p.gamma_re + p.gamma_im * I;
        profil->punkty[i].poprawny = p.poprawny && OSL_DIAG_CzyGammaPoprawna(profil->punkty[i].gamma_przed_osl);
        if (profil->punkty[i].poprawny) ++profil->liczba_poprawnych;
    }
    f_close(&plik);
    return profil->f_max_hz > profil->f_min_hz &&
           (uint32_t)profil->liczba_poprawnych * 100U >=
           (uint32_t)profil->liczba_punktow * OSL_DIAG_MIN_PROC_POPRAWNYCH;
}

bool OSL_DIAG_CzyIstnieje(OSL_DIAG_TYP_t typ)
{
    FILINFO info;
    const char *sciezka = OSL_DIAG_Sciezka(typ);
    return sciezka != NULL && f_stat(sciezka, &info) == FR_OK;
}
