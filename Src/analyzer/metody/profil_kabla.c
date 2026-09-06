#include "profil_kabla.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ff.h"

#define PROFIL_KABLA_MAGIC 0x4B424C31UL /* KBL1 */
#define PROFIL_KABLA_WERSJA 1U
#define PROFIL_KABLA_MIN_PROC_POPRAWNYCH 80U
#define PROFIL_KABLA_KATALOG "/aa/cable"

typedef struct
{
    uint32_t magic;
    uint16_t wersja;
    uint16_t liczba_punktow;
    uint16_t liczba_poprawnych;
    uint16_t rezerwowe;
    uint32_t f_min_hz;
    uint32_t f_max_hz;
    float z0_ohm;
    float load_rms_ohm;
    float load_max_proc;
    uint8_t load_sprawdzony;
    uint8_t rezerwa[3];
} PROFIL_KABLA_NAGLOWEK_t;

typedef struct
{
    uint32_t czestotliwosc_hz;
    float zc_re;
    float zc_im;
    float t_re;
    float t_im;
    uint8_t poprawny;
    uint8_t rezerwa[3];
} PROFIL_KABLA_PUNKT_PLIK_t;

static PROFIL_KABLA_t profil_aktywny;
static bool profil_aktywny_wczytany = false;

static bool PROFIL_KABLA_CzySkonczone(float complex z)
{
    return isfinite(crealf(z)) && isfinite(cimagf(z));
}

static float complex PROFIL_KABLA_InterpolujZespolone(float complex a, float complex b, float t)
{
    return a + (b - a) * t;
}

static bool PROFIL_KABLA_ZnajdzSasiednie(const PROFIL_KABLA_t *profil, uint32_t f,
                                         uint16_t *lewy, uint16_t *prawy)
{
    int32_t i;
    int32_t l = -1;
    int32_t p = -1;

    if (profil == NULL || lewy == NULL || prawy == NULL || profil->liczba_punktow == 0U)
        return false;
    if (f < profil->f_min_hz || f > profil->f_max_hz)
        return false;

    for (i = (int32_t)profil->liczba_punktow - 1; i >= 0; --i)
    {
        if (profil->punkty[i].poprawny && profil->punkty[i].czestotliwosc_hz <= f)
        {
            l = i;
            break;
        }
    }
    for (i = 0; i < (int32_t)profil->liczba_punktow; ++i)
    {
        if (profil->punkty[i].poprawny && profil->punkty[i].czestotliwosc_hz >= f)
        {
            p = i;
            break;
        }
    }
    if (l < 0 || p < 0)
        return false;
    *lewy = (uint16_t)l;
    *prawy = (uint16_t)p;
    return true;
}

bool PROFIL_KABLA_Utworz(const uint32_t *czestotliwosci_hz,
                         const float complex *z_open_ohm,
                         const float complex *z_short_ohm,
                         uint16_t liczba,
                         float z0_ohm,
                         PROFIL_KABLA_t *profil)
{
    uint16_t i;

    if (profil == NULL || czestotliwosci_hz == NULL || z_open_ohm == NULL || z_short_ohm == NULL ||
        liczba < 3U || liczba > PROFIL_KABLA_MAX_PUNKTOW || !isfinite(z0_ohm) || z0_ohm <= 0.0f)
        return false;

    memset(profil, 0, sizeof(*profil));
    profil->liczba_punktow = liczba;
    profil->f_min_hz = czestotliwosci_hz[0];
    profil->f_max_hz = czestotliwosci_hz[liczba - 1U];
    profil->z0_ohm = z0_ohm;
    profil->load_rms_ohm = NAN;
    profil->load_max_proc = NAN;

    if (profil->f_min_hz == 0U || profil->f_max_hz <= profil->f_min_hz)
        return false;

    for (i = 0U; i < liczba; ++i)
    {
        PROFIL_KABLA_PUNKT_t *p = &profil->punkty[i];
        p->czestotliwosc_hz = czestotliwosci_hz[i];
        if ((i > 0U && czestotliwosci_hz[i] <= czestotliwosci_hz[i - 1U]) ||
            !PROFIL_KABLA_CzySkonczone(z_open_ohm[i]) || !PROFIL_KABLA_CzySkonczone(z_short_ohm[i]))
            continue;
        if (KABEL_LINIA_ZOpenShort(z_open_ohm[i], z_short_ohm[i], &p->linia))
        {
            p->poprawny = 1U;
            ++profil->liczba_poprawnych;
        }
    }

    if ((uint32_t)profil->liczba_poprawnych * 100U <
        (uint32_t)profil->liczba_punktow * PROFIL_KABLA_MIN_PROC_POPRAWNYCH)
        return false;
    for (i = 0U; i < liczba; ++i)
        if (profil->punkty[i].poprawny) { profil->f_min_hz = profil->punkty[i].czestotliwosc_hz; break; }
    for (i = liczba; i > 0U; --i)
        if (profil->punkty[i - 1U].poprawny) { profil->f_max_hz = profil->punkty[i - 1U].czestotliwosc_hz; break; }
    return profil->f_max_hz > profil->f_min_hz;
}

bool PROFIL_KABLA_Interpoluj(const PROFIL_KABLA_t *profil, uint32_t czestotliwosc_hz,
                             KABEL_LINIA_PUNKT_t *linia)
{
    uint16_t il, ip;
    const PROFIL_KABLA_PUNKT_t *l;
    const PROFIL_KABLA_PUNKT_t *p;
    float t;

    if (linia == NULL || !PROFIL_KABLA_ZnajdzSasiednie(profil, czestotliwosc_hz, &il, &ip))
        return false;
    l = &profil->punkty[il];
    p = &profil->punkty[ip];
    if (il == ip || p->czestotliwosc_hz == l->czestotliwosc_hz)
    {
        *linia = l->linia;
        return linia->status == KABEL_LINIA_OK;
    }

    t = (float)(czestotliwosc_hz - l->czestotliwosc_hz) /
        (float)(p->czestotliwosc_hz - l->czestotliwosc_hz);
    linia->zc_ohm = PROFIL_KABLA_InterpolujZespolone(l->linia.zc_ohm, p->linia.zc_ohm, t);
    linia->tanh_gamma_l = PROFIL_KABLA_InterpolujZespolone(l->linia.tanh_gamma_l, p->linia.tanh_gamma_l, t);
    linia->status = KABEL_LINIA_OK;
    return PROFIL_KABLA_CzySkonczone(linia->zc_ohm) &&
           PROFIL_KABLA_CzySkonczone(linia->tanh_gamma_l) && crealf(linia->zc_ohm) > 0.0f;
}

bool PROFIL_KABLA_Deembeduj(const PROFIL_KABLA_t *profil, uint32_t czestotliwosc_hz,
                            float complex z_wejscia_ohm, float complex *z_obciazenia_ohm)
{
    KABEL_LINIA_PUNKT_t linia;
    if (!PROFIL_KABLA_Interpoluj(profil, czestotliwosc_hz, &linia))
        return false;
    return KABEL_LINIA_Deembeduj(&linia, z_wejscia_ohm, z_obciazenia_ohm);
}

bool PROFIL_KABLA_CzyZweryfikowany(const PROFIL_KABLA_t *profil)
{
    if (profil == NULL || !profil->load_sprawdzony)
        return false;
    if (!isfinite(profil->load_rms_ohm) || !isfinite(profil->load_max_proc))
        return false;
    return profil->load_rms_ohm >= 0.0f && profil->load_max_proc >= 0.0f &&
           profil->load_max_proc <= PROFIL_KABLA_MAX_BLAD_LOAD_PROC;
}

bool PROFIL_KABLA_OcenLoad(PROFIL_KABLA_t *profil,
                           const uint32_t *czestotliwosci_hz,
                           const float complex *z_load_zmierzony_ohm,
                           uint16_t liczba, float complex z_load_wzorcowy_ohm)
{
    uint16_t i;
    uint16_t n = 0U;
    double suma2 = 0.0;
    float maks_proc = 0.0f;

    if (profil == NULL || czestotliwosci_hz == NULL || z_load_zmierzony_ohm == NULL ||
        liczba == 0U || !PROFIL_KABLA_CzySkonczone(z_load_wzorcowy_ohm))
        return false;

    for (i = 0U; i < liczba; ++i)
    {
        float complex z_po;
        float blad;
        float proc;
        if (!PROFIL_KABLA_Deembeduj(profil, czestotliwosci_hz[i], z_load_zmierzony_ohm[i], &z_po))
            continue;
        blad = cabsf(z_po - z_load_wzorcowy_ohm);
        proc = 100.0f * blad / fmaxf(cabsf(z_load_wzorcowy_ohm), 1.0e-6f);
        suma2 += (double)blad * (double)blad;
        if (proc > maks_proc) maks_proc = proc;
        ++n;
    }
    if (n < 3U)
        return false;
    profil->load_rms_ohm = (float)sqrt(suma2 / (double)n);
    profil->load_max_proc = maks_proc;
    profil->load_sprawdzony = 1U;
    return true;
}

bool PROFIL_KABLA_Zapisz(const PROFIL_KABLA_t *profil)
{
    FIL plik;
    UINT zapisano;
    FRESULT fr;
    PROFIL_KABLA_NAGLOWEK_t h;
    uint16_t i;

    if (profil == NULL || profil->liczba_punktow < 3U || profil->liczba_punktow > PROFIL_KABLA_MAX_PUNKTOW)
        return false;
    fr = f_mkdir(PROFIL_KABLA_KATALOG);
    if (fr != FR_OK && fr != FR_EXIST)
        return false;
    if (f_open(&plik, PROFIL_KABLA_SCIEZKA, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return false;

    memset(&h, 0, sizeof(h));
    h.magic = PROFIL_KABLA_MAGIC;
    h.wersja = PROFIL_KABLA_WERSJA;
    h.liczba_punktow = profil->liczba_punktow;
    h.liczba_poprawnych = profil->liczba_poprawnych;
    h.f_min_hz = profil->f_min_hz;
    h.f_max_hz = profil->f_max_hz;
    h.z0_ohm = profil->z0_ohm;
    h.load_rms_ohm = profil->load_rms_ohm;
    h.load_max_proc = profil->load_max_proc;
    h.load_sprawdzony = profil->load_sprawdzony;
    fr = f_write(&plik, &h, sizeof(h), &zapisano);
    if (fr != FR_OK || zapisano != sizeof(h)) { f_close(&plik); return false; }

    for (i = 0U; i < profil->liczba_punktow; ++i)
    {
        PROFIL_KABLA_PUNKT_PLIK_t d;
        memset(&d, 0, sizeof(d));
        d.czestotliwosc_hz = profil->punkty[i].czestotliwosc_hz;
        d.zc_re = crealf(profil->punkty[i].linia.zc_ohm);
        d.zc_im = cimagf(profil->punkty[i].linia.zc_ohm);
        d.t_re = crealf(profil->punkty[i].linia.tanh_gamma_l);
        d.t_im = cimagf(profil->punkty[i].linia.tanh_gamma_l);
        d.poprawny = profil->punkty[i].poprawny;
        fr = f_write(&plik, &d, sizeof(d), &zapisano);
        if (fr != FR_OK || zapisano != sizeof(d)) { f_close(&plik); return false; }
    }
    f_sync(&plik);
    f_close(&plik);
    profil_aktywny = *profil;
    profil_aktywny_wczytany = true;
    return true;
}

bool PROFIL_KABLA_Wczytaj(PROFIL_KABLA_t *profil)
{
    FIL plik;
    UINT odczytano;
    PROFIL_KABLA_NAGLOWEK_t h;
    uint16_t i;

    if (profil == NULL || f_open(&plik, PROFIL_KABLA_SCIEZKA, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return false;
    if (f_read(&plik, &h, sizeof(h), &odczytano) != FR_OK || odczytano != sizeof(h) ||
        h.magic != PROFIL_KABLA_MAGIC || h.wersja != PROFIL_KABLA_WERSJA ||
        h.liczba_punktow < 3U || h.liczba_punktow > PROFIL_KABLA_MAX_PUNKTOW)
    { f_close(&plik); return false; }

    memset(profil, 0, sizeof(*profil));
    profil->liczba_punktow = h.liczba_punktow;
    profil->liczba_poprawnych = h.liczba_poprawnych;
    profil->f_min_hz = h.f_min_hz;
    profil->f_max_hz = h.f_max_hz;
    profil->z0_ohm = h.z0_ohm;
    profil->load_rms_ohm = h.load_rms_ohm;
    profil->load_max_proc = h.load_max_proc;
    profil->load_sprawdzony = h.load_sprawdzony;

    profil->liczba_poprawnych = 0U;
    for (i = 0U; i < h.liczba_punktow; ++i)
    {
        PROFIL_KABLA_PUNKT_PLIK_t d;
        if (f_read(&plik, &d, sizeof(d), &odczytano) != FR_OK || odczytano != sizeof(d))
        { f_close(&plik); return false; }
        if (i > 0U && d.czestotliwosc_hz <= profil->punkty[i - 1U].czestotliwosc_hz)
        { f_close(&plik); return false; }
        profil->punkty[i].czestotliwosc_hz = d.czestotliwosc_hz;
        profil->punkty[i].linia.zc_ohm = d.zc_re + d.zc_im * I;
        profil->punkty[i].linia.tanh_gamma_l = d.t_re + d.t_im * I;
        profil->punkty[i].poprawny = d.poprawny &&
            PROFIL_KABLA_CzySkonczone(profil->punkty[i].linia.zc_ohm) &&
            PROFIL_KABLA_CzySkonczone(profil->punkty[i].linia.tanh_gamma_l) &&
            crealf(profil->punkty[i].linia.zc_ohm) > 0.0f;
        profil->punkty[i].linia.status = profil->punkty[i].poprawny ? KABEL_LINIA_OK : KABEL_LINIA_BRAK_DANYCH;
        if (profil->punkty[i].poprawny) ++profil->liczba_poprawnych;
    }
    f_close(&plik);
    if (profil->f_max_hz <= profil->f_min_hz || !isfinite(profil->z0_ohm) || profil->z0_ohm <= 0.0f)
        return false;
    return (uint32_t)profil->liczba_poprawnych * 100U >=
           (uint32_t)profil->liczba_punktow * PROFIL_KABLA_MIN_PROC_POPRAWNYCH;
}

bool PROFIL_KABLA_WczytajAktywny(void)
{
    if (profil_aktywny_wczytany)
        return true;
    if (!PROFIL_KABLA_Wczytaj(&profil_aktywny))
        return false;
    profil_aktywny_wczytany = true;
    return true;
}

bool PROFIL_KABLA_CzyAktywnyWczytany(void)
{
    return profil_aktywny_wczytany;
}

bool PROFIL_KABLA_PobierzAktywny(PROFIL_KABLA_t *profil)
{
    if (profil == NULL || !PROFIL_KABLA_WczytajAktywny())
        return false;
    *profil = profil_aktywny;
    return true;
}

bool PROFIL_KABLA_DeembedujAktywny(uint32_t czestotliwosc_hz,
                                   float complex z_wejscia_ohm,
                                   float complex *z_obciazenia_ohm)
{
    if (!PROFIL_KABLA_WczytajAktywny())
        return false;
    /*
     * OPEN/SHORT wyznacza model matematycznie, lecz przy dużym |Gamma| mały
     * błąd pomiaru może stworzyć pozornie poprawny, a fizycznie zły profil.
     * Niezależny LOAD jest więc bramką bezpieczeństwa dla de-embeddingu.
     */
    if (!PROFIL_KABLA_CzyZweryfikowany(&profil_aktywny))
        return false;
    return PROFIL_KABLA_Deembeduj(&profil_aktywny, czestotliwosc_hz,
                                  z_wejscia_ohm, z_obciazenia_ohm);
}

void PROFIL_KABLA_UniewaznijCache(void)
{
    memset(&profil_aktywny, 0, sizeof(profil_aktywny));
    profil_aktywny_wczytany = false;
}
