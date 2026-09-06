#include "zworka_cal.h"

#include "config.h"
#include "dsp.h"
#include "gen.h"
#include "oslfile.h"

#include <math.h>
#include <string.h>

#define ZWORKA_CAL_F_HZ                 440000000U
#define ZWORKA_CAL_ROZGRZEWKA           4
#define ZWORKA_CAL_USREDNIANIE_1        16
#define ZWORKA_CAL_USREDNIANIE_2        32

/*
 * Progi wynikają z CALWORK1/CALWORK2, ale nie są progami absolutnymi.
 * Porównujemy bieżący RAW z fingerprintem własnej, aktualnej kalibracji HW.
 *
 * CALWORK2: przy CAL i zmianie obciążenia 0/50/100/200/500 Ohm maksymalnie:
 *   |d(V/I)| = 0.366 dB, |d(fazy)| = 2.743 deg.
 * Dajemy ponad dwukrotny zapas dla V/I i ~1.6x dla fazy.
 *
 * WORK+50 z CALWORK1 było oddalone o ok. 2.28 dB i 6.75 deg, więc strefa
 * "CAL pewne" pozostaje wyraźnie wewnątrz obserwowanej separacji.
 */
#define ZWORKA_CAL_PEWNE_VI_DB          0.80f
#define ZWORKA_CAL_PEWNE_FAZA_DEG       4.50f
#define ZWORKA_CAL_LUZNE_VI_DB          1.25f
#define ZWORKA_CAL_LUZNE_FAZA_DEG       6.00f

static float ZWORKA_CAL_RoznicaFazyRad(float a, float b)
{
    return remainderf(a - b, 2.0f * (float)M_PI);
}

static uint8_t ZWORKA_CAL_PobierzWzorzec(float *diff_ref, float *faza_ref)
{
    float mag = 1.0f;
    float ph = 0.0f;

    if (diff_ref == 0 || faza_ref == 0 || !OSL_IsErrCorrLoaded())
        return 0U;

    /*
     * OSL_CorrectErr() dla mag=1 zwraca dokładnie interpolowany mag0,
     * a dla ph=0 zwraca -phase0. Dzięki temu nie otwieramy wnętrza oslfile.c
     * i nie modyfikujemy sprawdzonego toru korekcji HW.
     */
    OSL_CorrectErr(ZWORKA_CAL_F_HZ, &mag, &ph);
    if (!isfinite(mag) || !isfinite(ph) || fabsf(mag) < 1.0e-9f)
        return 0U;

    *diff_ref = 1.0f / mag;
    *faza_ref = remainderf(-ph, 2.0f * (float)M_PI);
    return isfinite(*diff_ref) && *diff_ref > 0.0f ? 1U : 0U;
}

static uint8_t ZWORKA_CAL_Zmierz(int liczba_usrednien,
                                 float diff_ref, float faza_ref,
                                 float *delta_vi_db, float *delta_faza_deg)
{
    float diff;
    float faza;

    if (delta_vi_db == 0 || delta_faza_deg == 0)
        return 0U;

    DSP_Measure(ZWORKA_CAL_F_HZ, 0, 0, ZWORKA_CAL_ROZGRZEWKA);
    DSP_Measure(ZWORKA_CAL_F_HZ, 0, 0, liczba_usrednien);

    diff = DSP_MeasuredDiff();
    faza = DSP_MeasuredPhase();
    if (!isfinite(diff) || diff <= 0.0f || !isfinite(faza))
        return 0U;

    *delta_vi_db = 20.0f * log10f(diff / diff_ref);
    *delta_faza_deg = ZWORKA_CAL_RoznicaFazyRad(faza, faza_ref) *
                      (180.0f / (float)M_PI);

    return isfinite(*delta_vi_db) && isfinite(*delta_faza_deg) ? 1U : 0U;
}

static uint8_t ZWORKA_CAL_WStrefie(float dvi, float dph, float vi_lim, float ph_lim)
{
    return fabsf(dvi) <= vi_lim && fabsf(dph) <= ph_lim ? 1U : 0U;
}

ZWORKA_CAL_WYNIK_t ZWORKA_CAL_Sprawdz(void)
{
    ZWORKA_CAL_WYNIK_t w;
    float diff_ref = 0.0f;
    float faza_ref = 0.0f;
    uint8_t pierwsze_pewne;
    uint8_t pierwsze_luzne;
    uint8_t drugie_pewne;
    uint8_t drugie_luzne;

    memset(&w, 0, sizeof(w));
    w.status = ZWORKA_CAL_STATUS_BRAK_WZORCA;
    w.f_hz = ZWORKA_CAL_F_HZ;

    if (ZWORKA_CAL_F_HZ < CFG_GetParam(CFG_PARAM_BAND_FMIN) ||
        ZWORKA_CAL_F_HZ > CFG_GetParam(CFG_PARAM_BAND_FMAX) ||
        !GEN_CzyCzestotliwoscObslugiwana(ZWORKA_CAL_F_HZ))
        return w;

    if (!ZWORKA_CAL_PobierzWzorzec(&diff_ref, &faza_ref))
        return w;

    if (!ZWORKA_CAL_Zmierz(ZWORKA_CAL_USREDNIANIE_1, diff_ref, faza_ref,
                           &w.delta_vi_db, &w.delta_faza_deg))
    {
        GEN_SetMeasurementFreq(0U);
        w.status = ZWORKA_CAL_STATUS_BLAD;
        return w;
    }

    pierwsze_pewne = ZWORKA_CAL_WStrefie(w.delta_vi_db, w.delta_faza_deg,
                                          ZWORKA_CAL_PEWNE_VI_DB,
                                          ZWORKA_CAL_PEWNE_FAZA_DEG);
    pierwsze_luzne = ZWORKA_CAL_WStrefie(w.delta_vi_db, w.delta_faza_deg,
                                         ZWORKA_CAL_LUZNE_VI_DB,
                                         ZWORKA_CAL_LUZNE_FAZA_DEG);

    /* Wynik daleko od fingerprintu CAL: nie tracimy czasu na drugą serię. */
    if (!pierwsze_luzne)
    {
        GEN_SetMeasurementFreq(0U);
        w.status = ZWORKA_CAL_STATUS_WORK;
        return w;
    }

    w.wykonano_powtorzenie = 1U;
    if (!ZWORKA_CAL_Zmierz(ZWORKA_CAL_USREDNIANIE_2, diff_ref, faza_ref,
                           &w.delta_vi_db_2, &w.delta_faza_deg_2))
    {
        GEN_SetMeasurementFreq(0U);
        w.status = ZWORKA_CAL_STATUS_BLAD;
        return w;
    }
    GEN_SetMeasurementFreq(0U);

    drugie_pewne = ZWORKA_CAL_WStrefie(w.delta_vi_db_2, w.delta_faza_deg_2,
                                        ZWORKA_CAL_PEWNE_VI_DB,
                                        ZWORKA_CAL_PEWNE_FAZA_DEG);
    drugie_luzne = ZWORKA_CAL_WStrefie(w.delta_vi_db_2, w.delta_faza_deg_2,
                                       ZWORKA_CAL_LUZNE_VI_DB,
                                       ZWORKA_CAL_LUZNE_FAZA_DEG);

    /* Twarda blokada wymaga DWÓCH niezależnych trafień w ciasne okno CAL. */
    if (pierwsze_pewne && drugie_pewne)
        w.status = ZWORKA_CAL_STATUS_CAL_PEWNE;
    else if (pierwsze_luzne && drugie_luzne)
        w.status = ZWORKA_CAL_STATUS_PODEJRZENIE;
    else
        w.status = ZWORKA_CAL_STATUS_WORK;

    return w;
}
