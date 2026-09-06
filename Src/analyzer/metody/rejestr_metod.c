#include "rejestr_metod.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "jezyk.h"
#include "element_rf.h"
#include "q_okrag.h"

#define METODA_RLC_MAKS_PUNKTOW 320U

static float met_f[METODA_RLC_MAKS_PUNKTOW];
static float met_r[METODA_RLC_MAKS_PUNKTOW];
static float met_x[METODA_RLC_MAKS_PUNKTOW];
static uint8_t met_ok[METODA_RLC_MAKS_PUNKTOW];

static const char *NazwaElementIdealny(void)
{
    return JEZYK_Wybierz("Idealny L/C", "Ideal L/C", "Ideales L/C", "Идеальный L/C");
}
static const char *OpisElementIdealny(void)
{
    return JEZYK_Wybierz("bez modelu pasożytniczego", "without parasitic model", "ohne parasitäres Modell", "без паразитной модели");
}
static const char *NazwaElementRLC(void)
{
    return JEZYK_Wybierz("Model RLC", "RLC model", "RLC-Modell", "Модель RLC");
}
static const char *OpisElementRLC(void)
{
    return JEZYK_Wybierz("dopasowanie wielu punktów", "multi-point fit", "Mehrpunkt-Fit", "аппроксимация по точкам");
}

static const char *NazwaElementRF(void)
{
    return JEZYK_Wybierz("Model pasożytniczy RF", "RF parasitic model", "HF-Parasitärmodell", "ВЧ-модель паразитов");
}
static const char *OpisElementRF(void)
{
    return JEZYK_Wybierz("Rs/ESR, Q, SRF i pasożyty", "Rs/ESR, Q, SRF and parasitics",
                         "Rs/ESR, Q, SRF und Parasiten",
                         "Rs/ESR, Q, SRF и паразиты");
}

static const char *NazwaQ3dB(void) { return "Q -3 dB"; }
static const char *OpisQ3dB(void)
{
    return JEZYK_Wybierz("metoda pasmowa", "bandwidth method", "Bandbreitenmethode", "метод полосы");
}
static const char *NazwaQLorentz(void) { return "Q Lorentz"; }
static const char *OpisQLorentz(void)
{
    return JEZYK_Wybierz("dopasowanie krzywej", "curve fit", "Kurvenanpassung", "аппроксимация кривой");
}
static const char *NazwaQRLC(void) { return "Q RLC"; }
static const char *OpisQRLC(void)
{
    return JEZYK_Wybierz("pełny model impedancji", "full impedance model", "vollständiges Impedanzmodell", "полная модель импеданса");
}
static const char *NazwaQOkrag(void) { return "Q-circle QL"; }
static const char *OpisQOkrag(void)
{
    return JEZYK_Wybierz("zespolony okrąg S11, dobroć obciążona",
                         "complex S11 circle, loaded Q",
                         "komplexer S11-Kreis, belastetes Q",
                         "комплексная окружность S11, нагруженная Q");
}

#define TDR_NAZWA_FUNKCJA(nazwa_funkcji, pl, en, de, ru) \
    static const char *nazwa_funkcji(void) { return JEZYK_Wybierz(pl, en, de, ru); }

TDR_NAZWA_FUNKCJA(NazwaTdrKbd, "KBD historyczne", "Historical KBD", "Historisches KBD", "Историческое KBD")
TDR_NAZWA_FUNKCJA(NazwaTdrProstokat, "Prostokątne", "Rectangular", "Rechteck", "Прямоугольное")
TDR_NAZWA_FUNKCJA(NazwaTdrHann, "Hann", "Hann", "Hann", "Hann")
TDR_NAZWA_FUNKCJA(NazwaTdrHamming, "Hamming", "Hamming", "Hamming", "Hamming")
TDR_NAZWA_FUNKCJA(NazwaTdrBlackman, "Blackman-Harris", "Blackman-Harris", "Blackman-Harris", "Blackman-Harris")
TDR_NAZWA_FUNKCJA(NazwaTdrKaiser, "Kaiser", "Kaiser", "Kaiser", "Kaiser")

static const char *OpisTdrKbd(void)
{
    return JEZYK_Wybierz("zgodność ze starszym TDR", "legacy TDR compatibility", "Kompatibilität mit altem TDR", "совместимость со старым TDR");
}
static const char *OpisTdrProstokat(void)
{
    return JEZYK_Wybierz("najwyższa rozdzielczość", "highest resolution", "höchste Auflösung", "максимальное разрешение");
}
static const char *OpisTdrHann(void)
{
    return JEZYK_Wybierz("dobry kompromis", "balanced", "guter Kompromiss", "хороший компромисс");
}
static const char *OpisTdrHamming(void) { return OpisTdrHann(); }
static const char *OpisTdrBlackman(void)
{
    return JEZYK_Wybierz("bardzo małe listki boczne", "very low sidelobes", "sehr kleine Nebenkeulen", "очень низкие боковые лепестки");
}
static const char *OpisTdrKaiser(void)
{
    return JEZYK_Wybierz("domyślnie beta=6; niski poziom tła", "default beta=6; low background", "Standard beta=6; niedriger Hintergrund", "по умолчанию beta=6; низкий фон");
}

static bool ObliczElementIdealny(const METODA_ELEMENT_DANE_t *dane, METODA_ELEMENT_WYNIK_t *wynik)
{
    const POMIAR_S11_t *p;
    float x;
    float w;
    float r_abs;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    wynik->wartosc_h_lub_f = NAN;
    wynik->q = NAN;
    wynik->f0_hz = NAN;
    wynik->blad_rms_ohm = NAN;

    if (dane == NULL || dane->seria == NULL || dane->seria->punkty == NULL ||
        dane->indeks_reprezentatywny >= dane->seria->liczba)
    {
        wynik->jakosc = METODA_JAKOSC_BRAK_DANYCH;
        return false;
    }

    p = &dane->seria->punkty[dane->indeks_reprezentatywny];
    if ((p->flagi & POMIAR_S11_FLAGA_POPRAWNY) == 0U || p->czestotliwosc_hz == 0U)
    {
        wynik->jakosc = METODA_JAKOSC_BRAK_DANYCH;
        return false;
    }

    x = cimagf(p->impedancja_ohm);
    w = 2.0f * 3.14159265358979323846f * (float)p->czestotliwosc_hz;
    r_abs = fabsf(crealf(p->impedancja_ohm));

    if (dane->typ == METODA_ELEMENT_CEWKA)
    {
        if (!(x > 0.0f))
        {
            wynik->jakosc = METODA_JAKOSC_POZA_ZAKRESEM;
            return false;
        }
        wynik->wartosc_h_lub_f = x / w;
        wynik->l_h = wynik->wartosc_h_lub_f;
        wynik->c_f = NAN;
    }
    else
    {
        if (!(x < 0.0f))
        {
            wynik->jakosc = METODA_JAKOSC_POZA_ZAKRESEM;
            return false;
        }
        wynik->wartosc_h_lub_f = -1.0f / (w * x);
        wynik->c_f = wynik->wartosc_h_lub_f;
        wynik->l_h = NAN;
    }

    wynik->r_ohm = crealf(p->impedancja_ohm);
    wynik->q = r_abs > 1.0e-6f ? fabsf(x) / r_abs : NAN;
    wynik->poprawny = isfinite(wynik->wartosc_h_lub_f) && wynik->wartosc_h_lub_f > 0.0f;
    return wynik->poprawny;
}

static bool PrzygotujRLC(const SERIA_S11_t *seria, uint16_t *liczba)
{
    uint16_t i;
    uint16_t n;

    if (liczba == NULL || seria == NULL || seria->punkty == NULL || seria->liczba < 5U)
        return false;
    n = seria->liczba > METODA_RLC_MAKS_PUNKTOW ? METODA_RLC_MAKS_PUNKTOW : seria->liczba;
    for (i = 0U; i < n; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        met_f[i] = (float)p->czestotliwosc_hz;
        met_r[i] = crealf(p->impedancja_ohm);
        met_x[i] = cimagf(p->impedancja_ohm);
        met_ok[i] = ((p->flagi & POMIAR_S11_FLAGA_POPRAWNY) != 0U &&
                     isfinite(met_r[i]) && isfinite(met_x[i])) ? 1U : 0U;
    }
    *liczba = n;
    return true;
}

static bool ObliczElementRLC(const METODA_ELEMENT_DANE_t *dane, METODA_ELEMENT_WYNIK_t *wynik)
{
    MET_RLC_WYNIK_t szeregowy;
    MET_RLC_WYNIK_t rownolegly;
    const MET_RLC_WYNIK_t *najlepszy = NULL;
    uint16_t liczba;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    wynik->wartosc_h_lub_f = NAN;
    wynik->q = NAN;
    wynik->f0_hz = NAN;
    wynik->blad_rms_ohm = NAN;

    if (dane == NULL || !PrzygotujRLC(dane->seria, &liczba))
    {
        wynik->jakosc = METODA_JAKOSC_MALO_PUNKTOW;
        return false;
    }

    if (MET_DopasujRLC(met_f, met_r, met_x, met_ok, liczba, MET_MODEL_RLC_SZEREGOWY, &szeregowy))
        najlepszy = &szeregowy;
    if (MET_DopasujRLC(met_f, met_r, met_x, met_ok, liczba, MET_MODEL_RLC_ROWNOLEGLY, &rownolegly) &&
        (najlepszy == NULL || rownolegly.blad_rms_ohm < najlepszy->blad_rms_ohm))
        najlepszy = &rownolegly;

    if (najlepszy == NULL)
    {
        wynik->jakosc = METODA_JAKOSC_SLABE_DOPASOWANIE;
        return false;
    }

    wynik->l_h = najlepszy->l_h;
    wynik->c_f = najlepszy->c_f;
    wynik->wartosc_h_lub_f = dane->typ == METODA_ELEMENT_CEWKA ? wynik->l_h : wynik->c_f;
    wynik->r_ohm = najlepszy->r_ohm;
    wynik->q = najlepszy->q;
    wynik->f0_hz = najlepszy->f0_hz;
    wynik->blad_rms_ohm = najlepszy->blad_rms_ohm;
    wynik->model_rlc = najlepszy->model;
    wynik->poprawny = isfinite(wynik->wartosc_h_lub_f) && wynik->wartosc_h_lub_f > 0.0f;
    if (wynik->poprawny && wynik->blad_rms_ohm > fmaxf(5.0f, fabsf(wynik->r_ohm) * 0.25f))
        wynik->jakosc |= METODA_JAKOSC_SLABE_DOPASOWANIE;
    return wynik->poprawny;
}

static bool ObliczElementRF(const METODA_ELEMENT_DANE_t *dane, METODA_ELEMENT_WYNIK_t *wynik)
{
    ELEMENT_RF_WYNIK_t rf;
    float fref = 0.0f;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    wynik->wartosc_h_lub_f = NAN;
    wynik->l_h = NAN;
    wynik->c_f = NAN;
    wynik->r_ohm = NAN;
    wynik->q = NAN;
    wynik->f0_hz = NAN;
    wynik->blad_rms_ohm = NAN;
    wynik->pasozyt_f_lub_h = NAN;
    wynik->srf_obserwowane_hz = NAN;

    if (dane == NULL || dane->seria == NULL || dane->seria->punkty == NULL ||
        dane->indeks_reprezentatywny >= dane->seria->liczba)
    {
        wynik->jakosc = METODA_JAKOSC_BRAK_DANYCH;
        return false;
    }

    fref = (float)dane->seria->punkty[dane->indeks_reprezentatywny].czestotliwosc_hz;
    if (!ELEMENT_RF_Analizuj(dane->seria,
                             dane->typ == METODA_ELEMENT_CEWKA ? ELEMENT_RF_CEWKA : ELEMENT_RF_KONDENSATOR,
                             fref, &rf))
    {
        wynik->jakosc = METODA_JAKOSC_SLABE_DOPASOWANIE;
        if ((rf.uwagi & ELEMENT_RF_UWAGA_MALO_PUNKTOW) != 0U)
            wynik->jakosc |= METODA_JAKOSC_MALO_PUNKTOW;
        return false;
    }

    wynik->l_h = rf.l_h;
    wynik->c_f = rf.c_f;
    wynik->wartosc_h_lub_f = dane->typ == METODA_ELEMENT_CEWKA ? rf.l_h : rf.c_f;
    wynik->r_ohm = rf.r_strat_ohm;
    wynik->q = rf.q_przy_fref;
    wynik->f0_hz = rf.srf_hz;
    wynik->blad_rms_ohm = rf.blad_rms_ohm;
    wynik->pasozyt_f_lub_h = rf.pasozyt_f_lub_h;
    wynik->srf_obserwowane_hz = rf.srf_obserwowane_hz;
    wynik->poprawny = rf.poprawny;

    if ((rf.uwagi & ELEMENT_RF_UWAGA_MALO_PUNKTOW) != 0U)
        wynik->jakosc |= METODA_JAKOSC_MALO_PUNKTOW;
    if ((rf.uwagi & ELEMENT_RF_UWAGA_SLABE_DOPASOWANIE) != 0U)
        wynik->jakosc |= METODA_JAKOSC_SLABE_DOPASOWANIE;
    if ((rf.uwagi & ELEMENT_RF_UWAGA_SRF_NA_BRZEGU) != 0U)
        wynik->jakosc |= METODA_JAKOSC_REZONANS_NA_BRZEGU;
    if ((rf.uwagi & ELEMENT_RF_UWAGA_SRF_POZA_SKANEM) != 0U)
        wynik->jakosc |= METODA_JAKOSC_WYNIK_NIEZALECANY;
    return wynik->poprawny;
}

static bool ObliczQ3dB(const METODA_Q_DANE_t *dane, METODA_Q_WYNIK_t *wynik)
{
    MET_Q_3DB_WYNIK_t q;
    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    if (dane == NULL || dane->f_hz == NULL || dane->poziom_db == NULL || dane->liczba < 5U ||
        !MET_QMetoda3dB(dane->f_hz, dane->poziom_db, dane->liczba,
                        dane->rezonans_jako_maksimum, &q))
    {
        wynik->jakosc = METODA_JAKOSC_BRAK_DANYCH;
        return false;
    }
    wynik->f0_hz = q.f0_hz;
    wynik->q = q.q;
    wynik->szerokosc_hz = q.f2_hz - q.f1_hz;
    wynik->blad = NAN;
    wynik->poprawny = true;
    return true;
}

static bool ObliczQLorentz(const METODA_Q_DANE_t *dane, METODA_Q_WYNIK_t *wynik)
{
    MET_Q_LORENTZ_WYNIK_t q;
    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    if (dane == NULL || dane->f_hz == NULL || dane->amplituda == NULL || dane->liczba < 7U ||
        !MET_QDopasujLorentza(dane->f_hz, dane->amplituda, dane->liczba, &q))
    {
        wynik->jakosc = METODA_JAKOSC_BRAK_DANYCH;
        return false;
    }
    wynik->f0_hz = q.f0_hz;
    wynik->q = q.q;
    wynik->szerokosc_hz = q.szerokosc_3db_hz;
    wynik->blad = q.blad_rms;
    wynik->poprawny = true;
    return true;
}

static bool ObliczQRLC(const METODA_Q_DANE_t *dane, METODA_Q_WYNIK_t *wynik)
{
    METODA_ELEMENT_DANE_t wejscie;
    METODA_ELEMENT_WYNIK_t rlc;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    if (dane == NULL || dane->seria_s11 == NULL)
    {
        wynik->jakosc = METODA_JAKOSC_BRAK_DANYCH;
        return false;
    }

    wejscie.seria = dane->seria_s11;
    wejscie.indeks_reprezentatywny = 0U;
    wejscie.typ = METODA_ELEMENT_CEWKA;
    if (!ObliczElementRLC(&wejscie, &rlc))
    {
        wynik->jakosc = rlc.jakosc;
        return false;
    }
    wynik->f0_hz = rlc.f0_hz;
    wynik->q = rlc.q;
    wynik->szerokosc_hz = (isfinite(rlc.q) && rlc.q > 0.0f) ? rlc.f0_hz / rlc.q : NAN;
    wynik->blad = rlc.blad_rms_ohm;
    wynik->jakosc = rlc.jakosc;
    wynik->poprawny = true;
    return true;
}


static bool ObliczQOkrag(const METODA_Q_DANE_t *dane, METODA_Q_WYNIK_t *wynik)
{
    Q_OKRAG_WYNIK_t q;
    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    if (dane == NULL || dane->seria_s11 == NULL ||
        !Q_OKRAG_Dopasuj(dane->seria_s11, &q) || !q.obliczony)
    {
        wynik->jakosc = METODA_JAKOSC_BRAK_DANYCH;
        return false;
    }
    wynik->f0_hz = q.f0_hz;
    wynik->q = q.q_obciazone;
    wynik->szerokosc_hz = (q.q_obciazone > 0.0f) ? q.f0_hz / q.q_obciazone : NAN;
    wynik->blad = fmaxf(q.blad_okregu_wzgledny, q.blad_fazy_rms_rad);
    if ((q.jakosc & Q_OKRAG_JAKOSC_MALO_PUNKTOW) != 0U) wynik->jakosc |= METODA_JAKOSC_MALO_PUNKTOW;
    if ((q.jakosc & (Q_OKRAG_JAKOSC_ZDEGENEROWANY_OKRAG | Q_OKRAG_JAKOSC_MALY_LUK |
                     Q_OKRAG_JAKOSC_BLAD_OKREGU | Q_OKRAG_JAKOSC_BLAD_FAZY)) != 0U)
        wynik->jakosc |= METODA_JAKOSC_SLABE_DOPASOWANIE;
    if ((q.jakosc & Q_OKRAG_JAKOSC_REZONANS_PRZY_BRZEGU) != 0U)
        wynik->jakosc |= METODA_JAKOSC_REZONANS_NA_BRZEGU;
    wynik->poprawny = isfinite(wynik->q) && wynik->q > 0.0f;
    return wynik->poprawny;
}

static const METODA_ELEMENT_REJESTR_t rejestr_elementow[METODA_ELEMENT_LICZBA] =
{
    { { "lc_ideal", NazwaElementIdealny, OpisElementIdealny, METODA_FLAGA_ZALECANA }, ObliczElementIdealny },
    { { "lc_rlc", NazwaElementRLC, OpisElementRLC, METODA_FLAGA_EKSPERYMENTALNA | METODA_FLAGA_TYLKO_ZAAWANSOWANY }, ObliczElementRLC },
    { { "lc_rf_parasitic", NazwaElementRF, OpisElementRF, METODA_FLAGA_EKSPERYMENTALNA | METODA_FLAGA_TYLKO_ZAAWANSOWANY }, ObliczElementRF },
};

static const METODA_Q_REJESTR_t rejestr_q[METODA_Q_LICZBA] =
{
    { { "q_3db", NazwaQ3dB, OpisQ3dB, METODA_FLAGA_ZALECANA }, ObliczQ3dB, 3.0f, 500.0f },
    { { "q_lorentz", NazwaQLorentz, OpisQLorentz, METODA_FLAGA_EKSPERYMENTALNA | METODA_FLAGA_TYLKO_ZAAWANSOWANY }, ObliczQLorentz, 2.0f, 2000.0f },
    { { "q_rlc", NazwaQRLC, OpisQRLC, METODA_FLAGA_EKSPERYMENTALNA | METODA_FLAGA_TYLKO_ZAAWANSOWANY }, ObliczQRLC, 1.0f, 5000.0f },
    { { "q_circle_ql", NazwaQOkrag, OpisQOkrag, METODA_FLAGA_EKSPERYMENTALNA | METODA_FLAGA_TYLKO_ZAAWANSOWANY }, ObliczQOkrag, 10.0f, 10000.0f },
};

static const METODA_TDR_REJESTR_t rejestr_tdr[METODA_TDR_LICZBA] =
{
    { { "tdr_kbd", NazwaTdrKbd, OpisTdrKbd, METODA_FLAGA_EKSPERYMENTALNA | METODA_FLAGA_TYLKO_ZAAWANSOWANY }, METODA_TDR_KBD, MET_OKNO_HANN, true, false },
    { { "tdr_rect", NazwaTdrProstokat, OpisTdrProstokat, METODA_FLAGA_EKSPERYMENTALNA | METODA_FLAGA_TYLKO_ZAAWANSOWANY }, METODA_TDR_PROSTOKATNE, MET_OKNO_PROSTOKATNE, false, false },
    { { "tdr_hann", NazwaTdrHann, OpisTdrHann, METODA_FLAGA_EKSPERYMENTALNA | METODA_FLAGA_TYLKO_ZAAWANSOWANY }, METODA_TDR_HANN, MET_OKNO_HANN, false, false },
    { { "tdr_hamming", NazwaTdrHamming, OpisTdrHamming, METODA_FLAGA_EKSPERYMENTALNA | METODA_FLAGA_TYLKO_ZAAWANSOWANY }, METODA_TDR_HAMMING, MET_OKNO_HAMMING, false, false },
    { { "tdr_blackman_harris", NazwaTdrBlackman, OpisTdrBlackman, METODA_FLAGA_EKSPERYMENTALNA | METODA_FLAGA_TYLKO_ZAAWANSOWANY }, METODA_TDR_BLACKMAN_HARRIS, MET_OKNO_BLACKMAN_HARRIS, false, false },
    { { "tdr_kaiser", NazwaTdrKaiser, OpisTdrKaiser, METODA_FLAGA_ZALECANA }, METODA_TDR_KAISER, MET_OKNO_KAISER, false, true },
};

const METODA_ELEMENT_REJESTR_t *METODA_RejestrElementow(uint8_t *liczba)
{
    if (liczba != NULL)
        *liczba = METODA_ELEMENT_LICZBA;
    return rejestr_elementow;
}

const METODA_Q_REJESTR_t *METODA_RejestrQ(uint8_t *liczba)
{
    if (liczba != NULL)
        *liczba = METODA_Q_LICZBA;
    return rejestr_q;
}

const METODA_TDR_REJESTR_t *METODA_RejestrTDR(uint8_t *liczba)
{
    if (liczba != NULL)
        *liczba = METODA_TDR_LICZBA;
    return rejestr_tdr;
}

bool METODA_CzyWidoczna(const METODA_OPIS_t *opis, bool tryb_zaawansowany)
{
    if (opis == NULL)
        return false;
    if ((opis->flagi & METODA_FLAGA_TYLKO_ZAAWANSOWANY) != 0U && !tryb_zaawansowany)
        return false;
    return true;
}

static uint8_t ZnajdzId(const char *identyfikator, const METODA_OPIS_t *opisy,
                        uint8_t liczba, size_t krok)
{
    uint8_t i;
    const uint8_t *p = (const uint8_t *)opisy;
    if (identyfikator == NULL || opisy == NULL)
        return 0U;
    for (i = 0U; i < liczba; ++i, p += krok)
    {
        const METODA_OPIS_t *opis = (const METODA_OPIS_t *)p;
        if (opis->identyfikator != NULL && strcmp(opis->identyfikator, identyfikator) == 0)
            return i;
    }
    return 0U;
}

uint8_t METODA_ZnajdzPoIdentyfikatorzeElementu(const char *identyfikator)
{
    return ZnajdzId(identyfikator, &rejestr_elementow[0].opis,
                    METODA_ELEMENT_LICZBA, sizeof(rejestr_elementow[0]));
}

uint8_t METODA_ZnajdzPoIdentyfikatorzeTDR(const char *identyfikator)
{
    return ZnajdzId(identyfikator, &rejestr_tdr[0].opis,
                    METODA_TDR_LICZBA, sizeof(rejestr_tdr[0]));
}


uint8_t METODA_ZnajdzPoIdentyfikatorzeQ(const char *identyfikator)
{
    return ZnajdzId(identyfikator, &rejestr_q[0].opis,
                    METODA_Q_LICZBA, sizeof(rejestr_q[0]));
}

bool METODA_ObliczElement(uint8_t metoda, const METODA_ELEMENT_DANE_t *dane,
                          METODA_ELEMENT_WYNIK_t *wynik)
{
    if (wynik == NULL)
        return false;
    if (metoda >= METODA_ELEMENT_LICZBA)
    {
        memset(wynik, 0, sizeof(*wynik));
        wynik->jakosc = METODA_JAKOSC_BRAK_DANYCH;
        return false;
    }
    return rejestr_elementow[metoda].oblicz(dane, wynik);
}

static void METODA_OcenZakresQ(uint8_t metoda, const METODA_Q_DANE_t *dane,
                               METODA_Q_WYNIK_t *wynik)
{
    const METODA_Q_REJESTR_t *opis;
    float krok;
    float margines;

    if (wynik == NULL || metoda >= METODA_Q_LICZBA || !wynik->poprawny)
        return;

    opis = &rejestr_q[metoda];
    if (!isfinite(wynik->q) || wynik->q < opis->q_min_zalecane || wynik->q > opis->q_max_zalecane)
        wynik->jakosc |= METODA_JAKOSC_WYNIK_NIEZALECANY;

    /* Rezonans na samym brzegu skanu jest liczbowo podejrzany, nawet gdy
     * algorytm zwrocil wartosc. Uzywamy jednego kroku jako marginesu. */
    if (dane == NULL || dane->f_hz == NULL || dane->liczba < 2U || !isfinite(wynik->f0_hz))
        return;
    krok = fabsf(dane->f_hz[1] - dane->f_hz[0]);
    margines = fmaxf(krok, 1.0f);
    if (wynik->f0_hz <= dane->f_hz[0] + margines ||
        wynik->f0_hz >= dane->f_hz[dane->liczba - 1U] - margines)
        wynik->jakosc |= METODA_JAKOSC_REZONANS_NA_BRZEGU;
}

bool METODA_ObliczQ(uint8_t metoda, const METODA_Q_DANE_t *dane,
                    METODA_Q_WYNIK_t *wynik)
{
    bool ok;

    if (wynik == NULL)
        return false;
    if (metoda >= METODA_Q_LICZBA)
    {
        memset(wynik, 0, sizeof(*wynik));
        wynik->jakosc = METODA_JAKOSC_BRAK_DANYCH;
        return false;
    }

    ok = rejestr_q[metoda].oblicz(dane, wynik);
    if (ok)
        METODA_OcenZakresQ(metoda, dane, wynik);
    return ok;
}

const char *METODA_TekstJakosci(uint32_t jakosc)
{
    if ((jakosc & METODA_JAKOSC_BRAK_DANYCH) != 0U)
        return JEZYK_Wybierz("brak danych", "no data", "keine Daten", "нет данных");
    if ((jakosc & METODA_JAKOSC_MALO_PUNKTOW) != 0U)
        return JEZYK_Wybierz("za mało punktów", "too few points", "zu wenige Punkte", "слишком мало точек");
    if ((jakosc & METODA_JAKOSC_REZONANS_NA_BRZEGU) != 0U)
        return JEZYK_Wybierz("rezonans przy brzegu zakresu", "resonance near scan edge", "Resonanz am Bereichsrand", "резонанс у края диапазона");
    if ((jakosc & METODA_JAKOSC_SLABE_DOPASOWANIE) != 0U)
        return JEZYK_Wybierz("słabe dopasowanie modelu", "weak model fit", "schwache Modellanpassung", "слабое соответствие модели");
    if ((jakosc & METODA_JAKOSC_ROZBIEZNOSC_METOD) != 0U)
        return JEZYK_Wybierz("rozbieżność z innymi metodami", "disagrees with other methods",
                             "Abweichung von anderen Methoden",
                             "расходится с другими методами");
    if ((jakosc & METODA_JAKOSC_WYNIK_NIEZALECANY) != 0U)
        return JEZYK_Wybierz("poza zalecanym zakresem metody", "outside recommended method range", "außerhalb des empfohlenen Methodenbereichs", "вне рекомендуемого диапазона метода");
    if ((jakosc & METODA_JAKOSC_POZA_ZAKRESEM) != 0U)
        return JEZYK_Wybierz("poza zakresem modelu", "outside model range", "außerhalb des Modellbereichs", "вне диапазона модели");
    return JEZYK_Wybierz("wynik wiarygodny", "result credible", "Ergebnis plausibel", "результат достоверен");
}
