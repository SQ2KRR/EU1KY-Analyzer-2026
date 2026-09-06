#include "lc_metrologia.h"

#define LCM_MIN_AUTOMAT_HZ 500000U
#include <math.h>
#include <stddef.h>

#define LCM_DWA_PI 6.28318530717958647692f
#define LCM_X_MIN_OHM 1.0f
#define LCM_X_MAX_OHM 5000.0f
#define LCM_MIN_PUNKTOW 3U
#define LCM_EPS_OCENY 0.001f

static int LCM_ObliczWartosc(LCM_TRYB_t tryb, float x_ohm, uint32_t f_hz, float *wartosc)
{
    if (tryb == LCM_TRYB_INDUKCYJNOSC)
        return LCM_ObliczIndukcyjnosc_uH(x_ohm, f_hz, wartosc);
    return LCM_ObliczPojemnosc_pF(x_ohm, f_hz, wartosc);
}

int LCM_ObliczIndukcyjnosc_uH(float reaktancja_ohm, uint32_t czestotliwosc_hz, float *wynik_uH)
{
    float wynik;

    if (wynik_uH == NULL || czestotliwosc_hz == 0U || !isfinite(reaktancja_ohm))
        return 0;
    if (reaktancja_ohm < LCM_X_MIN_OHM || reaktancja_ohm > LCM_X_MAX_OHM)
        return 0;

    wynik = 1.0e6f * reaktancja_ohm / (LCM_DWA_PI * (float)czestotliwosc_hz);
    if (!isfinite(wynik) || wynik <= 0.0f)
        return 0;

    *wynik_uH = wynik;
    return 1;
}

int LCM_ObliczPojemnosc_pF(float reaktancja_ohm, uint32_t czestotliwosc_hz, float *wynik_pF)
{
    float wynik;
    float modul_x;

    if (wynik_pF == NULL || czestotliwosc_hz == 0U || !isfinite(reaktancja_ohm))
        return 0;
    if (reaktancja_ohm >= -LCM_X_MIN_OHM || reaktancja_ohm < -LCM_X_MAX_OHM)
        return 0;

    modul_x = -reaktancja_ohm;
    wynik = 1.0e12f / (LCM_DWA_PI * (float)czestotliwosc_hz * modul_x);
    if (!isfinite(wynik) || wynik <= 0.0f)
        return 0;

    *wynik_pF = wynik;
    return 1;
}

static int LCM_PunktPoprawny(const float *x, const uint32_t *f, const uint8_t *maska,
                             uint32_t indeks, LCM_TRYB_t tryb, float *wartosc)
{
    if (maska != NULL && maska[indeks] == 0U)
        return 0;
    return LCM_ObliczWartosc(tryb, x[indeks], f[indeks], wartosc);
}

static float LCM_OcenaStabilnosci(float zmiennosc_proc, uint32_t liczba_punktow)
{
    if (!isfinite(zmiennosc_proc) || zmiennosc_proc < 0.0f ||
        liczba_punktow < LCM_MIN_PUNKTOW)
        return INFINITY;

    /*
     * Sama liczba punktow nie moze wygrywac z wyraznie stabilniejszym
     * odcinkiem. Dzielimy wspolczynnik zmiennosci przez sqrt(N), co nagradza
     * dluzszy zakres, ale nie pozwala jednemu dodatkowemu punktowi przykryc
     * duzego rozrzutu modelu L/C. Mniejszy wynik jest lepszy.
     */
    return zmiennosc_proc / sqrtf((float)liczba_punktow);
}

int LCM_WybierzPunkt(const float *reaktancja_ohm,
                     const uint32_t *czestotliwosc_hz,
                     const uint8_t *poprawny,
                     uint32_t liczba,
                     LCM_TRYB_t tryb,
                     LCM_WYNIK_WYBORU_t *wynik)
{
    uint32_t i = 0U;
    uint32_t najlepszy_start = 0U;
    uint32_t najlepszy_koniec = 0U;
    uint32_t najlepsza_liczba = 0U;
    float najlepsza_zmiennosc = INFINITY;
    float najlepsza_ocena = INFINITY;
    float najlepsza_srednia = 0.0f;

    if (reaktancja_ohm == NULL || czestotliwosc_hz == NULL || wynik == NULL || liczba < LCM_MIN_PUNKTOW)
        return 0;

    while (i < liczba)
    {
        uint32_t start;
        uint32_t koniec;
        uint32_t n;
        uint32_t j;
        float suma = 0.0f;
        float suma_kw = 0.0f;
        float srednia;
        float odchylenie;
        float zmiennosc;
        float ocena;
        float wartosc;

        if (!LCM_PunktPoprawny(reaktancja_ohm, czestotliwosc_hz, poprawny, i, tryb, &wartosc))
        {
            i++;
            continue;
        }

        start = i;
        while (i < liczba && LCM_PunktPoprawny(reaktancja_ohm, czestotliwosc_hz, poprawny, i, tryb, &wartosc))
        {
            suma += wartosc;
            i++;
        }
        koniec = i - 1U;
        n = koniec - start + 1U;
        if (n < LCM_MIN_PUNKTOW)
            continue;

        srednia = suma / (float)n;
        if (!isfinite(srednia) || srednia <= 0.0f)
            continue;

        for (j = start; j <= koniec; j++)
        {
            if (!LCM_ObliczWartosc(tryb, reaktancja_ohm[j], czestotliwosc_hz[j], &wartosc))
                return 0;
            {
                const float d = wartosc - srednia;
                suma_kw += d * d;
            }
        }

        odchylenie = (n > 1U) ? sqrtf(suma_kw / (float)(n - 1U)) : 0.0f;
        zmiennosc = 100.0f * odchylenie / srednia;

        ocena = LCM_OcenaStabilnosci(zmiennosc, n);

        /*
         * Wybieramy kompromis stabilnosci i liczby probek. Przy praktycznym
         * remisie oceny preferujemy dluzszy odcinek, a potem mniejszy surowy
         * rozrzut. Dzieki temu wynik nie przeskakuje do krotkiego fragmentu
         * tylko dlatego, ze przypadkiem ma minimalnie mniejsze odchylenie.
         */
        if (ocena + LCM_EPS_OCENY < najlepsza_ocena ||
            (fabsf(ocena - najlepsza_ocena) <= LCM_EPS_OCENY &&
             (n > najlepsza_liczba ||
              (n == najlepsza_liczba && zmiennosc < najlepsza_zmiennosc))))
        {
            najlepszy_start = start;
            najlepszy_koniec = koniec;
            najlepsza_liczba = n;
            najlepsza_zmiennosc = zmiennosc;
            najlepsza_ocena = ocena;
            najlepsza_srednia = srednia;
        }
    }

    if (najlepsza_liczba < LCM_MIN_PUNKTOW)
        return 0;

    {
        uint32_t start = najlepszy_start;
        uint32_t koniec = najlepszy_koniec;
        uint32_t najlepszy_indeks = start;
        float najlepsza_roznica = INFINITY;
        float najlepsza_wartosc = 0.0f;
        uint32_t j;

        /* Dla dłuższego odcinka pomijamy skrajne próbki jako bufor. */
        if (najlepsza_liczba >= 5U)
        {
            start++;
            koniec--;
        }

        for (j = start; j <= koniec; j++)
        {
            float wartosc;
            float roznica;
            if (!LCM_ObliczWartosc(tryb, reaktancja_ohm[j], czestotliwosc_hz[j], &wartosc))
                continue;
            roznica = fabsf(wartosc - najlepsza_srednia);
            if (roznica < najlepsza_roznica)
            {
                najlepsza_roznica = roznica;
                najlepszy_indeks = j;
                najlepsza_wartosc = wartosc;
            }
        }

        if (!isfinite(najlepsza_wartosc) || najlepsza_wartosc <= 0.0f)
            return 0;

        wynik->indeks = najlepszy_indeks;
        wynik->czestotliwosc_hz = czestotliwosc_hz[najlepszy_indeks];
        wynik->liczba_punktow = najlepsza_liczba;
        wynik->wartosc = najlepsza_wartosc;
        wynik->srednia = najlepsza_srednia;
        wynik->zmiennosc_proc = najlepsza_zmiennosc;
        wynik->reaktancja_ohm = reaktancja_ohm[najlepszy_indeks];
        wynik->wspolczynnik_czulosc = NAN;
        wynik->ocena = najlepsza_ocena;
    }

    return 1;
}

static int LCM_OcenOknoLokalne(const float *reaktancja_ohm,
                               const uint32_t *czestotliwosc_hz,
                               const uint8_t *poprawny,
                               uint32_t liczba,
                               uint32_t indeks,
                               LCM_TRYB_t tryb,
                               float *srednia,
                               float *zmiennosc_proc,
                               uint32_t *liczba_punktow)
{
    uint32_t start;
    uint32_t koniec;
    uint32_t i;
    uint32_t n = 0U;
    float suma = 0.0f;
    float suma_kw = 0.0f;

    if (reaktancja_ohm == NULL || czestotliwosc_hz == NULL ||
        srednia == NULL || zmiennosc_proc == NULL || liczba_punktow == NULL ||
        indeks >= liczba)
        return 0;

    start = indeks > 2U ? indeks - 2U : 0U;
    koniec = indeks + 2U < liczba ? indeks + 2U : liczba - 1U;

    /*
     * Automat powinien opierac sie na sasiednich, ciaglych punktach. Nie
     * przeskakujemy przez dziure w danych, bo moglaby ona oznaczac rezonans,
     * nasycenie toru albo chwilowo niewiarygodny pomiar.
     */
    for (i = indeks; i > start; --i)
    {
        float wartosc;
        if (!LCM_PunktPoprawny(reaktancja_ohm, czestotliwosc_hz, poprawny,
                               i - 1U, tryb, &wartosc))
        {
            start = i;
            break;
        }
    }
    for (i = indeks; i < koniec; ++i)
    {
        float wartosc;
        if (!LCM_PunktPoprawny(reaktancja_ohm, czestotliwosc_hz, poprawny,
                               i + 1U, tryb, &wartosc))
        {
            koniec = i;
            break;
        }
    }

    for (i = start; i <= koniec; ++i)
    {
        float wartosc;
        if (!LCM_PunktPoprawny(reaktancja_ohm, czestotliwosc_hz, poprawny,
                               i, tryb, &wartosc))
            return 0;
        suma += wartosc;
        ++n;
    }

    if (n < LCM_MIN_PUNKTOW)
        return 0;

    *srednia = suma / (float)n;
    if (!isfinite(*srednia) || *srednia <= 0.0f)
        return 0;

    for (i = start; i <= koniec; ++i)
    {
        float wartosc;
        float d;
        if (!LCM_ObliczWartosc(tryb, reaktancja_ohm[i],
                               czestotliwosc_hz[i], &wartosc))
            return 0;
        d = wartosc - *srednia;
        suma_kw += d * d;
    }

    *zmiennosc_proc = n > 1U
        ? 100.0f * sqrtf(suma_kw / (float)(n - 1U)) / *srednia
        : 0.0f;
    *liczba_punktow = n;
    return isfinite(*zmiennosc_proc);
}

int LCM_WybierzPunktAutomatyczny(const float *reaktancja_ohm,
                                 const uint32_t *czestotliwosc_hz,
                                 const uint8_t *poprawny,
                                 uint32_t liczba,
                                 LCM_TRYB_t tryb,
                                 float z0_ohm,
                                 LCM_WYNIK_WYBORU_t *wynik)
{
    uint32_t i;
    int znaleziono = 0;
    float najlepsza_ocena = INFINITY;
    float najlepsza_zmiennosc = INFINITY;
    float najlepszy_wspolczynnik = INFINITY;

    if (reaktancja_ohm == NULL || czestotliwosc_hz == NULL || wynik == NULL ||
        liczba < LCM_MIN_PUNKTOW || !isfinite(z0_ohm) || z0_ohm <= 0.0f)
        return 0;

    for (i = 0U; i < liczba; ++i)
    {
        float wartosc;
        float srednia;
        float zmiennosc;
        float x_abs;
        float wspolczynnik_czulosc;
        float ocena;
        uint32_t n_lokalne;

        if (!LCM_PunktPoprawny(reaktancja_ohm, czestotliwosc_hz, poprawny,
                               i, tryb, &wartosc))
            continue;
        /* Dół pasma pozostaje dostępny ręcznie, lecz testy wzorców wykazały,
         * że nie powinien sterować automatycznym doborem punktu/modelu. */
        if (czestotliwosc_hz[i] < LCM_MIN_AUTOMAT_HZ)
            continue;

        if (!LCM_OcenOknoLokalne(reaktancja_ohm, czestotliwosc_hz, poprawny,
                                 liczba, i, tryb, &srednia, &zmiennosc,
                                 &n_lokalne))
            continue;

        x_abs = fabsf(reaktancja_ohm[i]);
        if (!isfinite(x_abs) || x_abs < LCM_X_MIN_OHM || x_abs > LCM_X_MAX_OHM)
            continue;

        /*
         * Dla Z=jX wzgledna czulosc wyznaczenia X z fazy odbicia ma minimum
         * przy |X|=Z0. Symetryczny wspolczynnik 0,5*(X/Z0 + Z0/X) ma wtedy
         * wartosc 1 i rosnie dla obu skrajow zakresu.
         */
        wspolczynnik_czulosc = 0.5f * (x_abs / z0_ohm + z0_ohm / x_abs);

        /*
         * Zmiennosc jest w procentach. Dzielnik 2 oznacza, ze rozrzut 1%
         * zwieksza kare o 50%. Automat nadal preferuje stabilny model, ale
         * nie wybiera przypadkowo punktu z X=1 lub 5000 omow tylko dlatego,
         * ze trzy probki wygladaly tam podobnie.
         */
        ocena = wspolczynnik_czulosc * (1.0f + zmiennosc / 2.0f);

        if (!isfinite(ocena))
            continue;

        if (!znaleziono || ocena + LCM_EPS_OCENY < najlepsza_ocena ||
            (fabsf(ocena - najlepsza_ocena) <= LCM_EPS_OCENY &&
             (zmiennosc < najlepsza_zmiennosc ||
              (fabsf(zmiennosc - najlepsza_zmiennosc) <= LCM_EPS_OCENY &&
               wspolczynnik_czulosc < najlepszy_wspolczynnik))))
        {
            wynik->indeks = i;
            wynik->czestotliwosc_hz = czestotliwosc_hz[i];
            wynik->liczba_punktow = n_lokalne;
            wynik->wartosc = wartosc;
            wynik->srednia = srednia;
            wynik->zmiennosc_proc = zmiennosc;
            wynik->reaktancja_ohm = reaktancja_ohm[i];
            wynik->wspolczynnik_czulosc = wspolczynnik_czulosc;
            wynik->ocena = ocena;

            najlepsza_ocena = ocena;
            najlepsza_zmiennosc = zmiennosc;
            najlepszy_wspolczynnik = wspolczynnik_czulosc;
            znaleziono = 1;
        }
    }

    return znaleziono;
}


int LCM_WybierzZakresAutomatyczny(const float *reaktancja_ohm,
                                   const uint32_t *czestotliwosc_hz,
                                   const uint8_t *poprawny,
                                   uint32_t liczba,
                                   const uint32_t *granice_hz,
                                   uint32_t liczba_zakresow,
                                   LCM_TRYB_t tryb,
                                   LCM_WYNIK_DOBORU_ZAKRESU_t *wynik)
{
    uint32_t zakres;
    uint32_t najlepsza_liczba = 0U;
    float najlepsza_zmiennosc = INFINITY;
    float najlepsza_ocena = INFINITY;
    int znaleziono = 0;

    if (reaktancja_ohm == NULL || czestotliwosc_hz == NULL ||
        granice_hz == NULL || wynik == NULL || liczba_zakresow == 0U || liczba < LCM_MIN_PUNKTOW)
        return 0;

    for (zakres = 0U; zakres < liczba_zakresow; ++zakres)
    {
        uint32_t start = 0U;
        uint32_t koniec = 0U;
        uint32_t i;
        LCM_WYNIK_WYBORU_t kandydat;
        const uint32_t fmin = granice_hz[zakres];
        const uint32_t fmax = granice_hz[zakres + 1U];

        while (start < liczba && czestotliwosc_hz[start] < fmin)
            start++;
        koniec = start;
        while (koniec < liczba && czestotliwosc_hz[koniec] < fmax)
            koniec++;

        if (koniec <= start || koniec - start < LCM_MIN_PUNKTOW)
            continue;

        if (!LCM_WybierzPunkt(reaktancja_ohm + start,
                              czestotliwosc_hz + start,
                              poprawny != NULL ? poprawny + start : NULL,
                              koniec - start, tryb, &kandydat))
            continue;

        kandydat.indeks += start;

        {
            const float ocena = LCM_OcenaStabilnosci(kandydat.zmiennosc_proc,
                                                      kandydat.liczba_punktow);

            /*
             * Zakres automatyczny stosuje to samo kryterium co wybor punktu:
             * stabilnosc modelu z umiarkowana premia za wieksza liczbe probek.
             */
            if (!znaleziono || ocena + LCM_EPS_OCENY < najlepsza_ocena ||
                (fabsf(ocena - najlepsza_ocena) <= LCM_EPS_OCENY &&
                 (kandydat.liczba_punktow > najlepsza_liczba ||
                  (kandydat.liczba_punktow == najlepsza_liczba &&
                   kandydat.zmiennosc_proc < najlepsza_zmiennosc))))
            {
                wynik->indeks_zakresu = zakres;
                wynik->wybor = kandydat;
                najlepsza_liczba = kandydat.liczba_punktow;
                najlepsza_zmiennosc = kandydat.zmiennosc_proc;
                najlepsza_ocena = ocena;
                znaleziono = 1;
            }
        }

        /* Unikamy ostrzeżenia o nieużywanym i przy okazji jawnie sprawdzamy
         * monotoniczność częstotliwości w wybranym fragmencie. */
        for (i = start + 1U; i < koniec; ++i)
        {
            if (czestotliwosc_hz[i] < czestotliwosc_hz[i - 1U])
                return 0;
        }
    }

    return znaleziono;
}
