#include "osl70cm.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "ff.h"
#include "gen.h"
#include "kalibracja_meta.h"
#include "oslfile.h"
#include "rf_akwizycja.h"

#define OSL70_PLIK_FORMAT 1UL
#define OSL70_PLIK_MAGIC  0x304D4337UL /* ASCII: "7CM0" w little-endian. */
#define OSL70_SCIEZKA_NAZWA "70CM.OSL"
#define OSL70_TMP_NAZWA     "70CM.TMP"
#define OSL70_BAK_NAZWA     "70CM.BAK"

#define OSL70_STATUS_SHORT  (1U << 0)
#define OSL70_STATUS_LOAD   (1U << 1)
#define OSL70_STATUS_OPEN   (1U << 2)
#define OSL70_STATUS_WSZYSTKO (OSL70_STATUS_SHORT | OSL70_STATUS_LOAD | OSL70_STATUS_OPEN)

/*
 * Naglowek ma wylacznie pola 32-bitowe, zeby jego format byl jednoznaczny
 * miedzy kompilacjami ARM. Wersja formatu pozwala pozniej rozszerzyc plik bez
 * zgadywania, co zawieraja stare dane.
 */
typedef struct
{
    uint32_t magic;
    uint32_t format;
    uint32_t fmin_hz;
    uint32_t fmax_hz;
    uint32_t krok_hz;
    uint32_t liczba_punktow;
    uint32_t metoda_2026;
    uint32_t rshort_miliohm;
    uint32_t rload_miliohm;
    uint32_t ropen_miliohm;
    uint32_t crc_hw_dostepny;
    uint32_t crc_hw;
} OSL70_NAGLOWEK_t;

/*
 * 601 punktow x 24 B = ok. 14,1 KiB. Bufor lezy w SDRAM, wiec nie zabiera
 * cennego SRAM mikrokontrolera. W trakcie skanu te same rekordy przechowuja
 * surowe Gamma, a po obliczeniu - wspolczynniki e00/e11/de.
 */
static S_OSLDATA osl70_dane[OSL70_LICZBA_PUNKTOW]
    __attribute__((section(".user_sdram"), aligned(32)));

static uint8_t osl70_status_skanu = 0U;
static uint8_t osl70_wazna = 0U;
static uint8_t osl70_metoda_skanu = 0U;
static uint8_t osl70_metoda_pliku = 0U;
static uint32_t osl70_generacja_hw = 0U;
static uint32_t osl70_rshort_miliohm = 0U;
static uint32_t osl70_rload_miliohm = 0U;
static uint32_t osl70_ropen_miliohm = 0U;

static float complex OSL70_BezpiecznyMianownik(float complex wartosc)
{
    if (cabsf(wartosc) < 1.0e-30f)
        return 1.0e-30f + 0.0f * I;
    return wartosc;
}

static float complex OSL70_Wyznacznik3(float complex a, float complex b, float complex c,
                                       float complex m, float complex n, float complex k,
                                       float complex u, float complex v, float complex w)
{
    return a * n * w + b * k * u + m * v * c - c * n * u - b * m * w - a * k * v;
}

static void OSL70_Cramer(float complex a11, float complex a12, float complex a13, float complex b1,
                         float complex a21, float complex a22, float complex a23, float complex b2,
                         float complex a31, float complex a32, float complex a33, float complex b3,
                         float complex wynik[3])
{
    float complex dzielnik = OSL70_Wyznacznik3(a11, a12, a13,
                                               a21, a22, a23,
                                               a31, a32, a33);
    dzielnik = OSL70_BezpiecznyMianownik(dzielnik);

    wynik[0] = OSL70_Wyznacznik3(b1, a12, a13,
                                 b2, a22, a23,
                                 b3, a32, a33) / dzielnik;
    wynik[1] = OSL70_Wyznacznik3(a11, b1, a13,
                                 a21, b2, a23,
                                 a31, b3, a33) / dzielnik;
    wynik[2] = OSL70_Wyznacznik3(a11, a12, b1,
                                 a21, a22, b2,
                                 a31, a32, b3) / dzielnik;
}

static uint32_t OSL70_CzestotliwoscPunktu(uint32_t indeks)
{
    if (indeks >= OSL70_LICZBA_PUNKTOW)
        return 0U;
    return OSL70_FMIN_HZ + indeks * OSL70_KROK_HZ;
}

static void OSL70_BudujSciezke(char *bufor, size_t rozmiar, const char *nazwa)
{
    if (bufor == NULL || rozmiar == 0U)
        return;
    snprintf(bufor, rozmiar, "%s/%s", g_cfg_osldir, nazwa);
}

static int OSL70_CzyNaglowekPoprawny(const OSL70_NAGLOWEK_t *naglowek)
{
    uint32_t crc_hw = 0U;
    const uint8_t metoda_2026 = 0U;

    if (naglowek == NULL)
        return 0;
    if (naglowek->magic != OSL70_PLIK_MAGIC ||
        naglowek->format != OSL70_PLIK_FORMAT ||
        naglowek->fmin_hz != OSL70_FMIN_HZ ||
        naglowek->fmax_hz != OSL70_FMAX_HZ ||
        naglowek->krok_hz != OSL70_KROK_HZ ||
        naglowek->liczba_punktow != OSL70_LICZBA_PUNKTOW)
        return 0;

    /*
     * OSL opisuje konkretny lancuch akwizycji. Profil zebrany metoda 2026
     * nie moze byc po cichu uzyty w metodzie klasycznej i odwrotnie.
     */
    if ((uint8_t)naglowek->metoda_2026 != metoda_2026)
        return 0;

    /*
     * Profil OSL opisuje konkretne wzorce. Zmiana ktorejkolwiek wartosci
     * rezystancji powoduje uniewaznienie starej kalibracji. Bez tego program
     * moglby po edycji wzorcow nadal uzywac wspolczynnikow policzonych dla
     * poprzedniego zestawu.
     */
    if (naglowek->rshort_miliohm != CFG_GetOslRshortMilliOhm() ||
        naglowek->rload_miliohm != CFG_GetOslRloadMilliOhm() ||
        naglowek->ropen_miliohm != CFG_GetOslRopenMilliOhm())
        return 0;

    /*
     * Po ponownej kalibracji HW lokalna OSL musi zostac wykonana ponownie.
     * CRC jest sprawdzane przy ladowaniu, a podczas normalnego pomiaru
     * wystarcza lekki licznik generacji HW.
     */
    if (naglowek->crc_hw_dostepny)
    {
        if (!KAL_META_ObliczCRCPliku(KAL_META_HW, -1, &crc_hw))
            return 0;
        if (crc_hw != naglowek->crc_hw)
            return 0;
    }

    return 1;
}

static int32_t OSL70_ZapiszPlik(void)
{
    FIL plik = {0};
    FRESULT fr;
    UINT zapisano = 0U;
    char sciezka[64];
    char sciezka_tmp[64];
    char sciezka_bak[64];
    OSL70_NAGLOWEK_t naglowek;
    uint32_t crc_hw = 0U;

    if (!CFG_CzyKartaSDDostepna())
        return -2;

    memset(&naglowek, 0, sizeof(naglowek));
    naglowek.magic = OSL70_PLIK_MAGIC;
    naglowek.format = OSL70_PLIK_FORMAT;
    naglowek.fmin_hz = OSL70_FMIN_HZ;
    naglowek.fmax_hz = OSL70_FMAX_HZ;
    naglowek.krok_hz = OSL70_KROK_HZ;
    naglowek.liczba_punktow = OSL70_LICZBA_PUNKTOW;
    naglowek.metoda_2026 = osl70_metoda_skanu;
    naglowek.rshort_miliohm = CFG_GetOslRshortMilliOhm();
    naglowek.rload_miliohm = CFG_GetOslRloadMilliOhm();
    naglowek.ropen_miliohm = CFG_GetOslRopenMilliOhm();
    if (KAL_META_ObliczCRCPliku(KAL_META_HW, -1, &crc_hw))
    {
        naglowek.crc_hw_dostepny = 1U;
        naglowek.crc_hw = crc_hw;
    }

    OSL70_BudujSciezke(sciezka, sizeof(sciezka), OSL70_SCIEZKA_NAZWA);
    OSL70_BudujSciezke(sciezka_tmp, sizeof(sciezka_tmp), OSL70_TMP_NAZWA);
    OSL70_BudujSciezke(sciezka_bak, sizeof(sciezka_bak), OSL70_BAK_NAZWA);

    (void)f_mkdir(g_cfg_osldir);
    (void)f_unlink(sciezka_tmp);

    fr = f_open(&plik, sciezka_tmp, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK)
        return -3;

    fr = f_write(&plik, &naglowek, (UINT)sizeof(naglowek), &zapisano);
    if (fr != FR_OK || zapisano != (UINT)sizeof(naglowek))
    {
        (void)f_close(&plik);
        (void)f_unlink(sciezka_tmp);
        return -4;
    }

    zapisano = 0U;
    fr = f_write(&plik, osl70_dane, (UINT)sizeof(osl70_dane), &zapisano);
    if (fr != FR_OK || zapisano != (UINT)sizeof(osl70_dane))
    {
        (void)f_close(&plik);
        (void)f_unlink(sciezka_tmp);
        return -4;
    }

    /* f_close() samo wykonuje f_sync(); nie synchronizujemy pliku dwa razy. */
    if (f_close(&plik) != FR_OK)
    {
        (void)f_unlink(sciezka_tmp);
        return -4;
    }

    /* Atomowa podmiana: stary profil zostaje do chwili pelnego zapisu nowego. */
    (void)f_unlink(sciezka_bak);
    if (f_rename(sciezka, sciezka_bak) != FR_OK)
        (void)f_unlink(sciezka_bak); /* Brak starego pliku jest poprawny. */

    if (f_rename(sciezka_tmp, sciezka) != FR_OK)
    {
        (void)f_unlink(sciezka_tmp);
        (void)f_rename(sciezka_bak, sciezka);
        return -4;
    }
    (void)f_unlink(sciezka_bak);
    return 0;
}

int32_t OSL70_Reload(void)
{
    FIL plik = {0};
    FRESULT fr;
    UINT odczytano = 0U;
    char sciezka[64];
    OSL70_NAGLOWEK_t naglowek;

    osl70_wazna = 0U;
    OSL70_BudujSciezke(sciezka, sizeof(sciezka), OSL70_SCIEZKA_NAZWA);

    fr = f_open(&plik, sciezka, FA_READ | FA_OPEN_EXISTING);
    if (fr != FR_OK)
        return 1;

    memset(&naglowek, 0, sizeof(naglowek));
    fr = f_read(&plik, &naglowek, (UINT)sizeof(naglowek), &odczytano);
    if (fr != FR_OK || odczytano != (UINT)sizeof(naglowek))
    {
        (void)f_close(&plik);
        return 2;
    }

    if (!OSL70_CzyNaglowekPoprawny(&naglowek))
    {
        (void)f_close(&plik);
        return 3;
    }

    odczytano = 0U;
    fr = f_read(&plik, osl70_dane, (UINT)sizeof(osl70_dane), &odczytano);
    if (fr != FR_OK || odczytano != (UINT)sizeof(osl70_dane))
    {
        (void)f_close(&plik);
        return 4;
    }

    if ((uint32_t)f_size(&plik) != (uint32_t)(sizeof(naglowek) + sizeof(osl70_dane)))
    {
        (void)f_close(&plik);
        return 5;
    }
    (void)f_close(&plik);

    for (uint32_t i = 0U; i < OSL70_LICZBA_PUNKTOW; ++i)
    {
        if (!isfinite(crealf(osl70_dane[i].e00)) || !isfinite(cimagf(osl70_dane[i].e00)) ||
            !isfinite(crealf(osl70_dane[i].e11)) || !isfinite(cimagf(osl70_dane[i].e11)) ||
            !isfinite(crealf(osl70_dane[i].de)) || !isfinite(cimagf(osl70_dane[i].de)))
            return 6;
    }

    osl70_metoda_pliku = (uint8_t)naglowek.metoda_2026;
    osl70_rshort_miliohm = naglowek.rshort_miliohm;
    osl70_rload_miliohm = naglowek.rload_miliohm;
    osl70_ropen_miliohm = naglowek.ropen_miliohm;
    osl70_generacja_hw = KAL_META_PobierzGeneracjeHW();
    osl70_wazna = 1U;
    osl70_status_skanu = 0U;
    return 0;
}

bool OSL70_IsValid(void)
{
    if (!osl70_wazna)
        return false;

    if (osl70_generacja_hw != KAL_META_PobierzGeneracjeHW() ||
        osl70_metoda_pliku != 0U ||
        osl70_rshort_miliohm != CFG_GetOslRshortMilliOhm() ||
        osl70_rload_miliohm != CFG_GetOslRloadMilliOhm() ||
        osl70_ropen_miliohm != CFG_GetOslRopenMilliOhm())
    {
        osl70_wazna = 0U;
    }

    return osl70_wazna != 0U;
}

const char *OSL70_Nazwa(void)
{
    return "70 cm";
}

uint32_t OSL70_PobierzFminHz(void) { return OSL70_FMIN_HZ; }
uint32_t OSL70_PobierzFmaxHz(void) { return OSL70_FMAX_HZ; }
uint32_t OSL70_PobierzKrokHz(void) { return OSL70_KROK_HZ; }
uint8_t OSL70_PobierzMetodeAkwizycji(void) { return osl70_metoda_pliku; }

bool OSL70_CzyUzycDlaZakresu(uint32_t od_hz, uint32_t do_hz)
{
    uint32_t pierwszy = od_hz;
    uint32_t ostatni = do_hz;
    const uint8_t metoda = 0U;

    if (pierwszy > ostatni)
    {
        const uint32_t tmp = pierwszy;
        pierwszy = ostatni;
        ostatni = tmp;
    }

    if (pierwszy < OSL70_FMIN_HZ || ostatni > OSL70_FMAX_HZ)
        return false;

    if (!OSL70_IsValid())
    {
        if (OSL70_Reload() != 0)
            return false;
    }

    return OSL70_IsValid() && osl70_metoda_pliku == metoda;
}

static int OSL70_ZmierzGamma(uint32_t czestotliwosc_hz, float complex *gamma)
{
    RF_AKW_PUNKT_t punkt;
    const uint8_t liczba = (uint8_t)OSL_PobierzLiczbeUsrednienKalibracji();

    if (gamma == NULL)
        return 0;

    for (int proba = 0; proba < 3; ++proba)
    {
        if (RF_AKW_PobierzPunkt(czestotliwosc_hz,
                                RF_AKW_TOR_STANDARD,
                                true,
                                liczba,
                                &punkt) &&
            isfinite(crealf(punkt.gamma_przed_osl)) &&
            isfinite(cimagf(punkt.gamma_przed_osl)))
        {
            *gamma = punkt.gamma_przed_osl;
            return 1;
        }
    }
    return 0;
}

static int32_t OSL70_Skanuj(uint8_t flaga, int pole, void (*progresscb)(uint32_t))
{
    const uint8_t metoda_biezaca = 0U;

    if (osl70_status_skanu == 0U)
        osl70_metoda_skanu = metoda_biezaca;
    else if (osl70_metoda_skanu != metoda_biezaca)
        return -6;

    for (uint32_t i = 0U; i < OSL70_LICZBA_PUNKTOW; ++i)
    {
        float complex gamma;
        const uint32_t f = OSL70_CzestotliwoscPunktu(i);

        if (!OSL70_ZmierzGamma(f, &gamma))
        {
            GEN_SetMeasurementFreq(0U);
            return -4;
        }

        if (pole == 0)
            osl70_dane[i].gshort = gamma;
        else if (pole == 1)
            osl70_dane[i].gload = gamma;
        else
            osl70_dane[i].gopen = gamma;

        if (progresscb != NULL)
            progresscb((i * 100U) / OSL70_LICZBA_PUNKTOW);
    }

    GEN_SetMeasurementFreq(0U);
    osl70_status_skanu |= flaga;
    osl70_wazna = 0U; /* Stara kalibracja nadal jest na SD, ale RAM zawiera skan. */
    if (progresscb != NULL)
        progresscb(100U);
    return 0;
}

int32_t OSL70_ScanShort(void (*progresscb)(uint32_t))
{
    return OSL70_Skanuj(OSL70_STATUS_SHORT, 0, progresscb);
}

int32_t OSL70_ScanLoad(void (*progresscb)(uint32_t))
{
    return OSL70_Skanuj(OSL70_STATUS_LOAD, 1, progresscb);
}

int32_t OSL70_ScanOpen(void (*progresscb)(uint32_t))
{
    return OSL70_Skanuj(OSL70_STATUS_OPEN, 2, progresscb);
}

int32_t OSL70_Calculate(void)
{
    const float r_load = CFG_GetOslRloadOhm();
    const float r_short = CFG_GetOslRshortOhm();
    const float r_open = CFG_GetOslRopenOhm();
    const float complex gamma_load = (r_load - 50.0f) / (r_load + 50.0f) + 0.0f * I;
    const float complex gamma_short = (r_short - 50.0f) / (r_short + 50.0f) + 0.0f * I;
    const float complex gamma_open = (r_open - 50.0f) / (r_open + 50.0f) + 0.0f * I;
    const float complex plus1 = 1.0f + 0.0f * I;
    const float complex minus1 = -1.0f + 0.0f * I;

    if ((osl70_status_skanu & OSL70_STATUS_WSZYSTKO) != OSL70_STATUS_WSZYSTKO)
        return -2;

    for (uint32_t i = 0U; i < OSL70_LICZBA_PUNKTOW; ++i)
    {
        S_OSLDATA *pd = &osl70_dane[i];
        float complex wynik[3];
        const float complex a12 = gamma_short * pd->gshort;
        const float complex a22 = gamma_load * pd->gload;
        const float complex a32 = gamma_open * pd->gopen;
        const float complex a13 = minus1 * gamma_short;
        const float complex a23 = minus1 * gamma_load;
        const float complex a33 = minus1 * gamma_open;

        OSL70_Cramer(plus1, a12, a13, pd->gshort,
                     plus1, a22, a23, pd->gload,
                     plus1, a32, a33, pd->gopen,
                     wynik);

        if (!isfinite(crealf(wynik[0])) || !isfinite(cimagf(wynik[0])) ||
            !isfinite(crealf(wynik[1])) || !isfinite(cimagf(wynik[1])) ||
            !isfinite(crealf(wynik[2])) || !isfinite(cimagf(wynik[2])))
            return -5;

        pd->e00 = wynik[0];
        pd->e11 = wynik[1];
        pd->de = wynik[2];
    }

    {
        const int32_t wynik_zapisu = OSL70_ZapiszPlik();
        if (wynik_zapisu != 0)
            return wynik_zapisu;
    }

    /* Ponowny odczyt jest czescia transakcji: mierzymy dopiero tym, co jest na SD. */
    if (OSL70_Reload() != 0)
        return -7;
    return 0;
}

static float complex OSL70_CorrectG(uint32_t czestotliwosc_hz, float complex gamma_zmierzona)
{
    uint32_t indeks;
    uint32_t f_dol;
    float udzial;
    S_OSLDATA wsp;
    float complex mianownik;

    if (!OSL70_IsValid() ||
        czestotliwosc_hz < OSL70_FMIN_HZ ||
        czestotliwosc_hz > OSL70_FMAX_HZ)
        return gamma_zmierzona;

    if (czestotliwosc_hz == OSL70_FMAX_HZ)
    {
        wsp = osl70_dane[OSL70_LICZBA_PUNKTOW - 1U];
    }
    else
    {
        indeks = (czestotliwosc_hz - OSL70_FMIN_HZ) / OSL70_KROK_HZ;
        if (indeks >= OSL70_LICZBA_PUNKTOW - 1U)
            indeks = OSL70_LICZBA_PUNKTOW - 2U;

        f_dol = OSL70_FMIN_HZ + indeks * OSL70_KROK_HZ;
        if (czestotliwosc_hz == f_dol)
        {
            wsp = osl70_dane[indeks];
        }
        else
        {
            /*
             * Przy gestym kroku 50 kHz liniowa interpolacja jest spokojniejsza
             * od paraboli i nie tworzy nadstrzalow pomiedzy punktami.
             */
            udzial = (float)(czestotliwosc_hz - f_dol) / (float)OSL70_KROK_HZ;
            wsp.e00 = osl70_dane[indeks].e00 +
                      (osl70_dane[indeks + 1U].e00 - osl70_dane[indeks].e00) * udzial;
            wsp.e11 = osl70_dane[indeks].e11 +
                      (osl70_dane[indeks + 1U].e11 - osl70_dane[indeks].e11) * udzial;
            wsp.de = osl70_dane[indeks].de +
                     (osl70_dane[indeks + 1U].de - osl70_dane[indeks].de) * udzial;
        }
    }

    mianownik = gamma_zmierzona * wsp.e11 - wsp.de;
    return (gamma_zmierzona - wsp.e00) / OSL70_BezpiecznyMianownik(mianownik);
}

float complex OSL70_CorrectZ(uint32_t czestotliwosc_hz,
                             float complex impedancja_przed_osl)
{
    float complex gamma = OSL_GFromZ(impedancja_przed_osl, 50.0f);

    gamma = OSL70_CorrectG(czestotliwosc_hz, gamma);

    /* Takie samo ograniczenie numeryczne jak w szerokopasmowej OSL. */
    if (crealf(gamma) > 1.0f)
        gamma = 1.0f + cimagf(gamma) * I;
    else if (crealf(gamma) < -1.0f)
        gamma = -1.0f + cimagf(gamma) * I;
    if (cimagf(gamma) > 1.0f)
        gamma = crealf(gamma) + 1.0f * I;
    else if (cimagf(gamma) < -1.0f)
        gamma = crealf(gamma) - 1.0f * I;

    return OSL_ZFromG(gamma, 50.0f);
}
