#include "q_porownanie.h"

#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "jezyk.h"
#include "metody_eksperymentalne.h"

#define Q_POROWNANIE_MAKS_PUNKTOW 320U
#define Q_POROWNANIE_EPS 1.0e-12f
#define METROLOGIA_MIN_AUTOMAT_HZ 500000U

static float q_f[Q_POROWNANIE_MAKS_PUNKTOW];
/* Dwa bufory robocze są celowo współdzielone: podczas rozpoznawania
 * topologii przechowują R/X, a po tej fazie amplitudę mocy i poziom dB.
 * Dzięki temu porównanie Q nie rezerwuje pięciu osobnych tablic float. */
static float q_bufor1[Q_POROWNANIE_MAKS_PUNKTOW];
static float q_bufor2[Q_POROWNANIE_MAKS_PUNKTOW];
static uint8_t q_ok[Q_POROWNANIE_MAKS_PUNKTOW];

static bool Q_POROWNANIE_PunktPoprawny(const POMIAR_S11_t *p)
{
    if (p == NULL || (p->flagi & POMIAR_S11_FLAGA_POPRAWNY) == 0U || p->czestotliwosc_hz == 0U)
        return false;
    if (p->czestotliwosc_hz < METROLOGIA_MIN_AUTOMAT_HZ)
        return false;
    return isfinite(crealf(p->impedancja_ohm)) && isfinite(cimagf(p->impedancja_ohm));
}

static Q_POROWNANIE_TOPOLOGIA_t Q_POROWNANIE_RozpoznajTopologie(const SERIA_S11_t *seria)
{
    MET_RLC_WYNIK_t szeregowy;
    MET_RLC_WYNIK_t rownolegly;
    bool ma_szeregowy;
    bool ma_rownolegly;
    uint16_t i;
    uint16_t n;

    if (seria == NULL || seria->punkty == NULL || seria->liczba < 3U)
        return Q_POROWNANIE_TOPOLOGIA_SZEREGOWA;

    n = seria->liczba > Q_POROWNANIE_MAKS_PUNKTOW ? Q_POROWNANIE_MAKS_PUNKTOW : seria->liczba;
    memset(q_ok, 0, sizeof(q_ok));
    for (i = 0U; i < n; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        q_f[i] = (float)p->czestotliwosc_hz;
        q_bufor1[i] = crealf(p->impedancja_ohm);
        q_bufor2[i] = cimagf(p->impedancja_ohm);
        q_ok[i] = Q_POROWNANIE_PunktPoprawny(p) ? 1U : 0U;
    }

    ma_szeregowy = MET_DopasujRLC(q_f, q_bufor1, q_bufor2, q_ok, n,
                                   MET_MODEL_RLC_SZEREGOWY, &szeregowy);
    ma_rownolegly = MET_DopasujRLC(q_f, q_bufor1, q_bufor2, q_ok, n,
                                    MET_MODEL_RLC_ROWNOLEGLY, &rownolegly);

    if (ma_rownolegly && (!ma_szeregowy || rownolegly.blad_rms_ohm < szeregowy.blad_rms_ohm))
        return Q_POROWNANIE_TOPOLOGIA_ROWNOLEGLA;
    return Q_POROWNANIE_TOPOLOGIA_SZEREGOWA;
}

static uint16_t Q_POROWNANIE_PrzygotujPrzebieg(const SERIA_S11_t *seria,
                                                Q_POROWNANIE_TOPOLOGIA_t topologia)
{
    uint16_t i;
    uint16_t n;
    uint16_t indeks_max = 0U;
    uint16_t start;
    uint16_t koniec;
    uint16_t wyj = 0U;
    float maksimum = 0.0f;

    if (seria == NULL || seria->punkty == NULL)
        return 0U;
    n = seria->liczba > Q_POROWNANIE_MAKS_PUNKTOW ? Q_POROWNANIE_MAKS_PUNKTOW : seria->liczba;

    memset(q_ok, 0, sizeof(q_ok));
    for (i = 0U; i < n; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        const float complex z = p->impedancja_ohm;
        float a = NAN;

        q_f[i] = (float)p->czestotliwosc_hz;
        if (Q_POROWNANIE_PunktPoprawny(p))
        {
            const float modul_z = cabsf(z);
            if (topologia == Q_POROWNANIE_TOPOLOGIA_SZEREGOWA)
            {
                if (modul_z > Q_POROWNANIE_EPS)
                    a = 1.0f / modul_z;
            }
            else
            {
                a = modul_z;
            }
        }
        q_bufor1[i] = (isfinite(a) && a > 0.0f) ? a * a : NAN;
        if (isfinite(q_bufor1[i]) && q_bufor1[i] > 0.0f)
        {
            q_ok[i] = 1U;
            if (q_bufor1[i] > maksimum)
            {
                maksimum = q_bufor1[i];
                indeks_max = i;
            }
        }
    }

    if (!(maksimum > Q_POROWNANIE_EPS) || !isfinite(maksimum) || !q_ok[indeks_max])
        return 0U;

    /* Metody -3 dB i Lorentza nie mają maski poprawności. Nie wolno więc
     * pozwolić, aby interpolowały przez dziurę w pomiarze. Bierzemy tylko
     * ciągły fragment poprawnych punktów zawierający maksimum rezonansu. */
    start = indeks_max;
    while (start > 0U && q_ok[start - 1U])
        --start;
    koniec = indeks_max;
    while (koniec + 1U < n && q_ok[koniec + 1U])
        ++koniec;

    if (koniec < start || (uint16_t)(koniec - start + 1U) < 7U)
        return 0U;

    for (i = start; i <= koniec; ++i)
    {
        q_f[wyj] = (float)seria->punkty[i].czestotliwosc_hz;
        q_bufor1[wyj] = q_bufor1[i] / maksimum;
        q_bufor2[wyj] = 10.0f * log10f(fmaxf(q_bufor1[wyj], Q_POROWNANIE_EPS));
        ++wyj;
    }
    return wyj;
}

static uint8_t Q_POROWNANIE_WybierzReferencyjna(const Q_POROWNANIE_WYNIK_t *wynik)
{
    static const uint8_t kolejnosc[] = { METODA_Q_3DB, METODA_Q_OKRAG, METODA_Q_RLC, METODA_Q_LORENTZ };
    uint8_t i;
    if (wynik == NULL)
        return METODA_Q_3DB;

    for (i = 0U; i < sizeof(kolejnosc); ++i)
    {
        const uint8_t m = kolejnosc[i];
        if (wynik->dostepne[m] && wynik->metody[m].jakosc == METODA_JAKOSC_OK)
            return m;
    }
    for (i = 0U; i < sizeof(kolejnosc); ++i)
    {
        const uint8_t m = kolejnosc[i];
        if (wynik->dostepne[m])
            return m;
    }
    return METODA_Q_3DB;
}

static void Q_POROWNANIE_OznaczOdstajaca(Q_POROWNANIE_WYNIK_t *wynik)
{
    float qv[METODA_Q_LICZBA];
    uint8_t mv[METODA_Q_LICZBA];
    uint8_t n = 0U, i, j;
    float mediana;

    if (wynik == NULL || wynik->liczba_poprawnych < 3U)
        return;
    for (i = 0U; i < METODA_Q_LICZBA; ++i)
        if (wynik->dostepne[i]) { qv[n] = wynik->metody[i].q; mv[n] = i; ++n; }
    if (n < 3U)
        return;
    for (i = 1U; i < n; ++i)
        for (j = i; j > 0U && qv[j - 1U] > qv[j]; --j)
        {
            float tq = qv[j - 1U]; qv[j - 1U] = qv[j]; qv[j] = tq;
            uint8_t tm = mv[j - 1U]; mv[j - 1U] = mv[j]; mv[j] = tm;
        }
    mediana = (n & 1U) ? qv[n / 2U] : 0.5f * (qv[n / 2U - 1U] + qv[n / 2U]);
    if (!(mediana > Q_POROWNANIE_EPS))
        return;
    for (i = 0U; i < n; ++i)
        if (100.0f * fabsf(qv[i] - mediana) / mediana > 15.0f)
            wynik->metody[mv[i]].jakosc |= METODA_JAKOSC_ROZBIEZNOSC_METOD;
}

bool Q_POROWNANIE_AnalizujMaska(const SERIA_S11_t *seria, uint32_t maska_metod,
                                 Q_POROWNANIE_WYNIK_t *wynik)
{
    METODA_Q_DANE_t dane;
    uint16_t n;
    uint8_t m;
    float suma = 0.0f;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    wynik->q_min = NAN;
    wynik->q_max = NAN;
    wynik->q_srednie = NAN;
    wynik->rozrzut_proc = NAN;
    wynik->zgodnosc = Q_POROWNANIE_ZGODNOSC_BRAK;
    maska_metod &= Q_POROWNANIE_MASKA_WSZYSTKIE;

    if (maska_metod == 0U || seria == NULL || seria->punkty == NULL || seria->liczba < 7U)
        return false;

    wynik->topologia = Q_POROWNANIE_RozpoznajTopologie(seria);
    n = Q_POROWNANIE_PrzygotujPrzebieg(seria, wynik->topologia);
    if (n < 7U)
        return false;

    memset(&dane, 0, sizeof(dane));
    dane.f_hz = q_f;
    dane.poziom_db = q_bufor2;
    dane.amplituda = q_bufor1;
    dane.liczba = n;
    dane.rezonans_jako_maksimum = true;
    dane.seria_s11 = seria;

    for (m = 0U; m < METODA_Q_LICZBA; ++m)
    {
        if ((maska_metod & Q_POROWNANIE_MASKA(m)) == 0U)
            continue;
        if (METODA_ObliczQ(m, &dane, &wynik->metody[m]) &&
            isfinite(wynik->metody[m].q) && wynik->metody[m].q > 0.0f)
        {
            const float q = wynik->metody[m].q;
            wynik->dostepne[m] = 1U;
            if (wynik->liczba_poprawnych == 0U)
            {
                wynik->q_min = q;
                wynik->q_max = q;
            }
            else
            {
                if (q < wynik->q_min) wynik->q_min = q;
                if (q > wynik->q_max) wynik->q_max = q;
            }
            suma += q;
            ++wynik->liczba_poprawnych;
        }
    }

    if (wynik->liczba_poprawnych == 0U)
        return false;

    wynik->q_srednie = suma / (float)wynik->liczba_poprawnych;
    if (wynik->liczba_poprawnych >= 2U && wynik->q_srednie > Q_POROWNANIE_EPS)
    {
        wynik->rozrzut_proc = 100.0f * (wynik->q_max - wynik->q_min) / wynik->q_srednie;
        if (wynik->rozrzut_proc <= 5.0f)
            wynik->zgodnosc = Q_POROWNANIE_ZGODNOSC_DOBRA;
        else if (wynik->rozrzut_proc <= 15.0f)
            wynik->zgodnosc = Q_POROWNANIE_ZGODNOSC_UMIARKOWANA;
        else
            wynik->zgodnosc = Q_POROWNANIE_ZGODNOSC_SLABA;
    }
    Q_POROWNANIE_OznaczOdstajaca(wynik);
    /* Przy słabej zgodności żadna metoda nie dostaje pozornego statusu
     * referencyjnej. Liczby pozostają widoczne diagnostycznie, ale program
     * nie sugeruje użytkownikowi, że jedna z nich jest wynikiem końcowym. */
    wynik->metoda_referencyjna =
        wynik->zgodnosc == Q_POROWNANIE_ZGODNOSC_SLABA
            ? UINT8_MAX
            : Q_POROWNANIE_WybierzReferencyjna(wynik);
    return true;
}

bool Q_POROWNANIE_Analizuj(const SERIA_S11_t *seria, Q_POROWNANIE_WYNIK_t *wynik)
{
    return Q_POROWNANIE_AnalizujMaska(seria, Q_POROWNANIE_MASKA_WSZYSTKIE, wynik);
}

const char *Q_POROWNANIE_TekstZgodnosci(Q_POROWNANIE_ZGODNOSC_t zgodnosc)
{
    switch (zgodnosc)
    {
    case Q_POROWNANIE_ZGODNOSC_DOBRA:
        return JEZYK_Wybierz("dobra zgodność", "good agreement", "gute Übereinstimmung", "хорошее совпадение");
    case Q_POROWNANIE_ZGODNOSC_UMIARKOWANA:
        return JEZYK_Wybierz("umiarkowana rozbieżność", "moderate disagreement", "mäßige Abweichung", "умеренное расхождение");
    case Q_POROWNANIE_ZGODNOSC_SLABA:
        return JEZYK_Wybierz("duża rozbieżność", "large disagreement", "große Abweichung", "большое расхождение");
    case Q_POROWNANIE_ZGODNOSC_BRAK:
    default:
        return JEZYK_Wybierz("za mało metod do porównania", "too few methods to compare",
                             "zu wenige Methoden", "недостаточно методов");
    }
}

const char *Q_POROWNANIE_TekstTopologii(Q_POROWNANIE_TOPOLOGIA_t topologia)
{
    if (topologia == Q_POROWNANIE_TOPOLOGIA_ROWNOLEGLA)
        return JEZYK_Wybierz("równoległa", "parallel", "parallel", "параллельная");
    return JEZYK_Wybierz("szeregowa", "series", "seriell", "последовательная");
}
