/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <complex.h>
#include "arm_math.h"
#include "ff.h"

#include "LCD.h"
#include "touch.h"
#include "font.h"
#include "config.h"
#include "jezyk.h"
#include "crash.h"
#include "dsp.h"
#include "gen.h"
#include "oslfile.h"
#include "stm32746g_discovery_lcd.h"
#include "screenshot.h"
#include "tdr.h"
#include "tdr_metrologia.h"
#include "tdr_interpretacja.h"
#include "num_keypad.h"
#include "wejscia_uzytkownika.h"
#include "hit.h"
#include "sdram_heap.h"
#include "ui_wspolny.h"
#include "kalibracja_meta.h"
#include "komunikaty.h"
#include "metody_eksperymentalne.h"
#include "pomiar_s11.h"
#include "rejestr_metod.h"
#include "tryb_interfejsu.h"

extern void Sleep(uint32_t);

#define NUMTDRSAMPLES 512
//#define NUMTDRSAMPLES 256
#define X0 40
#define Y0 127
#define WWIDTH 400

//Modified by KD8CEC for reduce memory use rate
/*
static float time_domain[NUMTDRSAMPLES*2];
static float step_response[NUMTDRSAMPLES*2];
static float Ztv[NUMTDRSAMPLES*2];
static float complex freq_domain[NUMTDRSAMPLES];
*/
static float *time_domain;
static float *time_domain_raw;
static float *step_response;
static float *Ztv;
static float complex *freq_domain;


static uint32_t rqExit = 0;
static float normFactor = 1.f; //Factor for calculating graph magnitude scale. Calculated from maximum value in time domain.
static int TDR_cursorChangeCount;
static uint32_t TDR_isScanned = 0;
static uint32_t TDR_cursorPos = WWIDTH / 2;
static int TDR_Length = 150;    // length of measure WK
static int MaxTDR_Length = 150; // length of measure WK
static uint32_t Vf_x10000;      // 6600 oznacza Vf = 0,6600
static int variant;             // 0: skala liniowa odbicia, 1: pierwiastek szescienny
static uint8_t tdr_okno = METODA_TDR_KAISER; // Wynik kampanii LAB: Kaiser beta=6.
static float tdr_kaiser_beta = 6.0f;
static float tdr_indeks_markera_auto = NAN;
static uint32_t tdr_pozycja_markera_auto = 0U;

typedef struct
{
    uint32_t krok_hz;
    uint16_t liczba_binow;
    uint16_t liczba_probek_czasu;
    uint16_t punkty_poprawne;
    uint16_t punkty_pominiete;
    uint32_t najwyzsza_czestotliwosc_hz;
    uint8_t poprawny;
} TDR_STAN_SKANU_t;

typedef struct
{
    float czas_ns;
    float odleglosc_m;
    float vf;
    float amplituda;
    float z_kabla;
    uint8_t marker_automatyczny;
} TDR_WYNIK_KURSORA_t;

static TDR_STAN_SKANU_t stan_skanu;
static TDRI_WYNIK_t interpretacja_tdr;
static char komunikat_tdr[96];
static float vf_sugerowane = 0.0f;
static float weryfikacja_dlugosc_m = 0.0f;
static float weryfikacja_pomiar_m = 0.0f;
static float weryfikacja_czas_ns = 0.0f;

#define TDR_INDEKS_IMPEDANCJI_KABLA 2U

static void TDR_RysujPrzyciski(void);
static void TDR_Weryfikuj(void);
static float TDR_AktualnyVf(void);

//Half of Kaiser Bessel Derived (beta=3.5) window
static const float halfKBDwnd[] =
    {
        0.99999436304, 0.999949268201, 0.999859083001, 0.999723816398, 0.999543481825, 0.999318097191, 0.999047684879, 0.998732271741,
        0.998371889095, 0.997966572724, 0.997516362868, 0.997021304218, 0.996481445916, 0.995896841543, 0.995267549113, 0.994593631069,
        0.993875154272, 0.993112189992, 0.992304813902, 0.991453106066, 0.990557150929, 0.989617037306, 0.988632858372, 0.987604711648,
        0.986532698992, 0.985416926582, 0.984257504904, 0.98305454874, 0.981808177151, 0.980518513461, 0.979185685243, 0.977809824304,
        0.976391066665, 0.974929552546, 0.973425426348, 0.971878836631, 0.970289936102, 0.96865888159, 0.966985834026, 0.965270958426,
        0.963514423869, 0.961716403473, 0.959877074374, 0.957996617707, 0.956075218577, 0.954113066041, 0.952110353079, 0.950067276575,
        0.947984037286, 0.945860839822, 0.943697892616, 0.9414954079, 0.939253601676, 0.936972693691, 0.934652907407, 0.932294469974,
        0.9298976122, 0.927462568525, 0.924989576986, 0.922478879191, 0.919930720286, 0.917345348927, 0.914723017246, 0.912063980819,
        0.909368498635, 0.906636833063, 0.903869249818, 0.901066017929, 0.898227409703, 0.895353700693, 0.892445169662, 0.889502098546,
        0.886524772423, 0.883513479473, 0.880468510944, 0.877390161113, 0.874278727253, 0.871134509591, 0.867957811274, 0.864748938329,
        0.861508199624, 0.858235906832, 0.854932374389, 0.851597919456, 0.848232861879, 0.844837524149, 0.841412231361, 0.837957311176,
        0.834473093776, 0.830959911826, 0.827418100429, 0.82384799709, 0.820249941666, 0.816624276333, 0.812971345533, 0.809291495941,
        0.805585076414, 0.801852437954, 0.798093933658, 0.794309918679, 0.790500750182, 0.786666787295, 0.78280839107, 0.778925924436,
        0.775019752152, 0.771090240765, 0.767137758564, 0.763162675534, 0.759165363309, 0.75514619513, 0.751105545796, 0.747043791617,
        0.74296131037, 0.738858481252, 0.734735684835, 0.730593303013, 0.726431718965, 0.722251317099, 0.718052483011, 0.713835603434,
        0.709601066192, 0.705349260155, 0.701080575189, 0.696795402108, 0.692494132627, 0.688177159317, 0.683844875555, 0.679497675473,
        0.675135953919, 0.670760106399, 0.666370529037, 0.661967618523, 0.657551772066, 0.653123387346, 0.648682862468, 0.644230595912,
        0.639766986483, 0.63529243327, 0.630807335591, 0.626312092948, 0.62180710498, 0.617292771415, 0.612769492021, 0.60823766656,
        0.603697694737, 0.599149976159, 0.59459491028, 0.59003289636, 0.585464333414, 0.580889620164, 0.576309154997, 0.571723335912,
        0.567132560478, 0.562537225783, 0.557937728393, 0.553334464298, 0.548727828872, 0.544118216825, 0.539506022157, 0.534891638108,
        0.530275457121, 0.525657870789, 0.521039269811, 0.51642004395, 0.511800581985, 0.507181271668, 0.502562499676, 0.497944651573,
        0.493328111759, 0.488713263431, 0.484100488535, 0.479490167727, 0.474882680326, 0.470278404275, 0.465677716093, 0.461080990836,
        0.456488602056, 0.451900921754, 0.447318320343, 0.442741166606, 0.43816982765, 0.433604668874, 0.429046053918, 0.424494344632,
        0.419949901029, 0.415413081252, 0.410884241527, 0.40636373613, 0.401851917346, 0.39734913543, 0.392855738572, 0.388372072855,
        0.38389848222, 0.379435308429, 0.374982891028, 0.370541567311, 0.366111672282, 0.361693538624, 0.357287496659, 0.352893874316,
        0.348512997094, 0.344145188033, 0.339790767672, 0.335450054026, 0.331123362543, 0.32681100608, 0.322513294865, 0.318230536467,
        0.313963035767, 0.309711094925, 0.305475013349, 0.301255087667, 0.297051611696, 0.292864876411, 0.288695169924, 0.284542777444,
        0.28040798126, 0.276291060707, 0.272192292142, 0.268111948915, 0.264050301347, 0.260007616701, 0.255984159157, 0.25198018979,
        0.247995966544, 0.244031744206, 0.240087774389, 0.236164305502, 0.232261582733, 0.228379848025, 0.224519340055, 0.220680294213,
        0.216862942583, 0.21306751392, 0.209294233634, 0.20554332377, 0.201815002987, 0.198109486544, 0.194426986283, 0.190767710607,
        0.187131864468, 0.18351964935, 0.179931263253, 0.176366900678, 0.172826752616, 0.169311006528, 0.165819846336, 0.16235345241,
        0.158912001554, 0.155495666994, 0.152104618371, 0.148739021723, 0.14539903948, 0.142084830453, 0.138796549824, 0.135534349139};

static const float KBD_td_factor = 1.571921; //1.43502708f; //Factor for the above window to normalize time-domain cumulative power to 1.0

static float max1, Zmax;
int max_idx;

static uint32_t TDR_MaksPozycjaKursora(void)
{
    return (TDR_Length == 300) ? (WWIDTH * 2U - 1U) : (WWIDTH - 1U);
}

static void TDR_ParametrySkanuDlaZakresu(int zakres, uint32_t *krok_hz,
                                         uint16_t *liczba_binow,
                                         uint16_t *liczba_probek_czasu)
{
    if (zakres == 300)
    {
        *krok_hz = 250000U;
        *liczba_binow = NUMTDRSAMPLES;
        *liczba_probek_czasu = NUMTDRSAMPLES * 2U;
    }
    else if (zakres == 150)
    {
        *krok_hz = 500000U;
        *liczba_binow = NUMTDRSAMPLES / 2U;
        *liczba_probek_czasu = NUMTDRSAMPLES;
    }
    else
    {
        *krok_hz = 1000000U;
        *liczba_binow = NUMTDRSAMPLES / 2U;
        *liczba_probek_czasu = NUMTDRSAMPLES;
    }
}

static float TDR_IndeksProbkiDlaKursora(uint32_t pozycja)
{
    float indeks = (float)pozycja * ((float)TDR_Length / (float)MaxTDR_Length);
    const float maksimum = stan_skanu.liczba_probek_czasu > 0U
                               ? (float)(stan_skanu.liczba_probek_czasu - 1U)
                               : 0.0f;

    if (indeks < 0.0f)
        indeks = 0.0f;
    if (indeks > maksimum)
        indeks = maksimum;
    return indeks;
}

static uint16_t TDR_IndeksProbkiZaokraglony(uint32_t pozycja)
{
    float indeks = TDR_IndeksProbkiDlaKursora(pozycja);
    uint32_t wynik = (uint32_t)(indeks + 0.5f);

    if (stan_skanu.liczba_probek_czasu == 0U)
        return 0U;
    if (wynik >= stan_skanu.liczba_probek_czasu)
        wynik = stan_skanu.liczba_probek_czasu - 1U;
    return (uint16_t)wynik;
}

static bool TDR_CzasKursoraNs(uint32_t pozycja, float *czas_ns)
{
    float indeks;

    if (!stan_skanu.poprawny)
        return false;

    /*
     * Automatyczny marker zachowuje podpróbkowe położenie maksimum. Linia
     * kursora nadal trafia w piksel/próbkę, lecz czas i odległość korzystają
     * z dokładniejszej interpolacji. Ręczne przesunięcie kursora ją kasuje.
     */
    if (isfinite(tdr_indeks_markera_auto) && pozycja == tdr_pozycja_markera_auto)
        indeks = tdr_indeks_markera_auto;
    else
        indeks = TDR_IndeksProbkiDlaKursora(pozycja);

    return TDRM_ObliczCzasNs(indeks, stan_skanu.krok_hz,
                             stan_skanu.liczba_probek_czasu, czas_ns);
}

static float TDR_WagaOknaKBD(uint16_t indeks, int zakres)
{
    if (zakres != 300)
    {
        if (indeks >= (sizeof(halfKBDwnd) / sizeof(halfKBDwnd[0])))
            return 0.0f;
        return halfKBDwnd[indeks];
    }

    /*
     * Dla kroku 250 kHz tabela 500 kHz jest interpolowana pomiedzy
     * sasiednimi wartosciami. Historyczny kod dla pierwszego nieparzystego
     * binu odczytywal halfKBDwnd[-1], czyli pamiec przed tablica.
     */
    if ((indeks & 1U) == 0U)
    {
        const uint16_t k = indeks / 2U;
        return (k < (sizeof(halfKBDwnd) / sizeof(halfKBDwnd[0])))
                   ? halfKBDwnd[k]
                   : 0.0f;
    }
    else
    {
        const uint16_t k = indeks / 2U;
        const uint16_t n = (uint16_t)(sizeof(halfKBDwnd) / sizeof(halfKBDwnd[0]));
        if (k + 1U < n)
            return 0.5f * (halfKBDwnd[k] + halfKBDwnd[k + 1U]);
        if (k < n)
            return halfKBDwnd[k];
        return 0.0f;
    }
}

/*
 * Nazwy okien (w tym Blackman-Harris) pochodza z centralnego rejestru metod.
 * TDR nie utrzymuje drugiej tabeli nazw, aby UI i algorytm nie mogly sie rozjechac.
 */
static const METODA_TDR_REJESTR_t *TDR_PobierzOpisOkna(uint8_t wybor)
{
    uint8_t liczba = 0U;
    const METODA_TDR_REJESTR_t *rejestr = METODA_RejestrTDR(&liczba);
    if (rejestr == NULL || liczba == 0U)
        return NULL;
    if (wybor >= liczba)
        wybor = 0U;
    return &rejestr[wybor];
}

static float TDR_WagaOkna(uint16_t indeks, int zakres)
{
    uint32_t pelna_liczba;
    uint32_t przesuniety_indeks;

    const METODA_TDR_REJESTR_t *metoda = TDR_PobierzOpisOkna(tdr_okno);

    if (metoda == NULL || metoda->uzywa_historycznego_kbd)
        return TDR_WagaOknaKBD(indeks, zakres);

    if (stan_skanu.liczba_binow < 2U || indeks >= stan_skanu.liczba_binow)
        return 0.0f;

    /*
     * Dla TDR używamy prawej połowy symetrycznej funkcji okna: przy DC
     * współczynnik jest bliski 1, a przy najwyższej częstotliwości opada.
     * Rejestr wybiera matematykę; ekran nie zna już mapowania numer -> okno.
     */
    pelna_liczba = 2U * (uint32_t)stan_skanu.liczba_binow - 1U;
    przesuniety_indeks = (uint32_t)stan_skanu.liczba_binow - 1U - indeks;
    return MET_WspolczynnikOkna(metoda->okno, przesuniety_indeks, pelna_liczba,
                                tdr_kaiser_beta);
}

static float TDR_NormalizacjaOkna(void)
{
    double suma_kbd = 0.0;
    double suma_biezaca = 0.0;
    uint16_t i;

    if (tdr_okno == 0U || stan_skanu.liczba_binow < 2U)
        return KBD_td_factor;

    for (i = 1U; i < stan_skanu.liczba_binow; ++i)
    {
        suma_kbd += (double)TDR_WagaOknaKBD(i, TDR_Length);
        suma_biezaca += (double)TDR_WagaOkna(i, TDR_Length);
    }

    if (!(suma_biezaca > 1.0e-9) || !isfinite(suma_biezaca))
        return KBD_td_factor;

    /*
     * Zachowujemy w przybliżeniu tę samą skalę amplitudy co historyczne KBD.
     * To nie jest kalibracja metrologiczna nowych okien, tylko bezpieczne
     * porównanie metod na wspólnej skali do porównań eksperymentalnych.
     */
    return (float)((double)KBD_td_factor * suma_kbd / suma_biezaca);
}

static float TDR_ZakresWidocznyM(int zakres, int maks_zakres, float vf)
{
    uint32_t krok_hz;
    uint16_t liczba_binow;
    uint16_t liczba_probek_czasu;
    uint32_t maks_kursor = (zakres == 300) ? (WWIDTH * 2U - 1U) : (WWIDTH - 1U);
    float indeks;
    float czas_ns;
    float odleglosc_m;

    TDR_ParametrySkanuDlaZakresu(zakres, &krok_hz, &liczba_binow, &liczba_probek_czasu);
    (void)liczba_binow;
    indeks = (float)maks_kursor * ((float)zakres / (float)maks_zakres);

    if (!TDRM_ObliczCzasNs(indeks, krok_hz, liczba_probek_czasu, &czas_ns) ||
        !TDRM_ObliczOdlegloscM(czas_ns, vf, &odleglosc_m))
        return 0.0f;
    return odleglosc_m;
}


static const char *TDR_NazwaTrybuZasiegu(int zakres)
{
    switch (zakres)
    {
    case 10:
        return JEZYK_Wybierz("Bliski", "Near", "Nah", "Ближний");
    case 50:
        return JEZYK_Wybierz("Średni", "Medium", "Mittel", "Средний");
    case 150:
        return JEZYK_Wybierz("Długi", "Long", "Lang", "Дальний");
    case 300:
    default:
        return JEZYK_Wybierz("Bardzo długi", "Very long", "Sehr lang", "Очень дальний");
    }
}

static int TDR_IndeksTrybuZasiegu(void)
{
    if (TDR_Length == 10) return 0;
    if (TDR_Length == 50) return 1;
    if (TDR_Length == 150) return 2;
    return 3;
}

static void TDR_UstawTrybZasiegu(int indeks)
{
    static const int tryby[4] = {10, 50, 150, 300};

    if (indeks < 0)
        indeks = 3;
    if (indeks > 3)
        indeks = 0;

    TDR_Length = tryby[indeks];
    MaxTDR_Length = (TDR_Length == 10 || TDR_Length == 50) ? 75 : TDR_Length;
    TDR_cursorPos = 0U;
    TDR_isScanned = 0U;
    tdr_indeks_markera_auto = NAN;
    tdr_pozycja_markera_auto = 0U;
    memset(&stan_skanu, 0, sizeof(stan_skanu));
    memset(&interpretacja_tdr, 0, sizeof(interpretacja_tdr));
}

static const char *TDR_OpisTrybuZasiegu(int zakres)
{
    switch (zakres)
    {
    case 10:
        return JEZYK_Wybierz(
            "Bliski: powiększenie początku odpowiedzi. Używa tego samego skanu 1 MHz co tryb Średni, ale pokazuje krótszy fragment osi, dzięki czemu łatwiej ustawić kursor na bliskich odbiciach.",
            "Near: zooms the beginning of the response. It uses the same 1 MHz scan as Medium, but shows a shorter part of the axis.",
            "Nah: vergrößert den Anfang der Antwort. Gleicher 1-MHz-Scan wie Mittel, aber kürzerer sichtbarer Achsenabschnitt.",
            "Ближний: увеличивает начало отклика. Использует тот же шаг 1 МГц, что и Средний, но показывает меньшую часть оси.");
    case 50:
        return JEZYK_Wybierz(
            "Średni: pełniejszy widok tego samego szerokopasmowego skanu 1 MHz. Dobry wybór dla typowych krótkich i średnich przewodów.",
            "Medium: wider view of the same 1 MHz broadband scan. A good choice for typical short and medium cables.",
            "Mittel: breitere Ansicht desselben 1-MHz-Breitbandscans. Gut für typische kurze und mittlere Kabel.",
            "Средний: более полный вид того же широкополосного скана 1 МГц. Для коротких и средних кабелей.");
    case 150:
        return JEZYK_Wybierz(
            "Długi: zwiększa jednoznaczny zasięg przez zmniejszenie kroku RF do 500 kHz. Zasięg rośnie kosztem rozdzielczości przestrzennej.",
            "Long: increases unambiguous range by reducing the RF step to 500 kHz. Range increases at the cost of spatial resolution.",
            "Lang: größerer eindeutiger Bereich durch 500-kHz-Schritt. Mehr Reichweite auf Kosten der Ortsauflösung.",
            "Дальний: увеличивает диапазон шагом 500 кГц, ценой пространственного разрешения.");
    case 300:
    default:
        return JEZYK_Wybierz(
            "Bardzo długi: największy zasięg, krok RF 250 kHz i dłuższa odpowiedź czasowa. Wybieraj go dopiero wtedy, gdy krótsze tryby nie obejmują interesującego miejsca.",
            "Very long: maximum range, 250 kHz RF step and a longer time response. Use it when shorter modes do not cover the point of interest.",
            "Sehr lang: maximale Reichweite, 250-kHz-Schritt und längere Zeitantwort. Nur verwenden, wenn kürzere Modi nicht ausreichen.",
            "Очень дальний: максимальный диапазон, шаг 250 кГц и более длинный временной отклик. Используйте, если коротких режимов недостаточно.");
    }
}

static void TDR_PomocZasieg(void)
{
    float vf = TDR_AktualnyVf();
    float zasieg_m;
    char tresc[420];

    if (vf < 0.01f || vf > 1.0f)
        vf = 0.66f;
    zasieg_m = TDR_ZakresWidocznyM(TDR_Length, MaxTDR_Length, vf);

    snprintf(tresc, sizeof(tresc),
             JEZYK_Wybierz(
                 "%s\n\nPrzybliżony koniec widocznej osi dla bieżącego Vf = %.4f wynosi około %.1f m. Ta liczba nie jest gwarantowanym zasięgiem pomiarowym. Rzeczywista użyteczność zależy m.in. od strat kabla, jakości OSL i siły odbicia. Zmiana trybu nie zmienia Vf.",
                 "%s\n\nThe approximate end of the visible axis for Vf = %.4f is about %.1f m. This is not a guaranteed measurement range. Usable range depends on cable loss, OSL quality and reflection strength. Changing mode does not change Vf.",
                 "%s\n\nDas sichtbare Achsenende bei Vf = %.4f liegt ungefähr bei %.1f m. Dies ist keine garantierte Messreichweite. Nutzbarkeit hängt von Kabelverlust, OSL und Reflexion ab.",
                 "%s\n\nПриблизительный конец оси при Vf = %.4f: %.1f м. Это не гарантированная дальность; она зависит от потерь, OSL и уровня отражения."),
             TDR_OpisTrybuZasiegu(TDR_Length), vf, zasieg_m);

    KOMUNIKAT_PokazTekst(JEZYK_Wybierz("Tryb zasięgu TDR", "TDR range mode", "TDR-Bereich", "Режим дальности TDR"), tresc);
}

static void TDR_UstawKomunikat(const char *tekst)
{
    if (tekst == NULL)
        tekst = "";
    snprintf(komunikat_tdr, sizeof(komunikat_tdr), "%s", tekst);
}

static uint32_t TDR_PobierzVfX10000(void)
{
    uint32_t vf = CFG_GetParam(CFG_PARAM_TDR_VF_X10000);

    if (vf < 100U || vf > 10000U)
    {
        uint32_t stary = CFG_GetParam(CFG_PARAM_TDR_VF);
        if (stary < 1U || stary > 100U)
            stary = 66U;
        vf = stary * 100U;
    }
    return vf;
}

static void TDR_UstawVfX10000(uint32_t vf_x10000, bool zapisz_na_karte)
{
    if (vf_x10000 < 100U)
        vf_x10000 = 100U;
    if (vf_x10000 > 10000U)
        vf_x10000 = 10000U;

    Vf_x10000 = vf_x10000;

    /*
     * Tryb tymczasowy zmienia tylko stan bieżącej sesji. Nie dotykamy nawet
     * kopii konfiguracji w RAM, bo późniejszy niezwiązany CFG_Flush() mógłby
     * niechcący utrwalić wartość, którą użytkownik chciał tylko sprawdzić.
     */
    if (zapisz_na_karte)
    {
        CFG_SetParam(CFG_PARAM_TDR_VF_X10000, vf_x10000);
        /* Stare pole procentowe pozostaje zsynchronizowane dla zgodnosci
           z poprzednimi wersjami firmware i narzedziami czytajacymi config. */
        CFG_SetParam(CFG_PARAM_TDR_VF, (vf_x10000 + 50U) / 100U);
        CFG_Flush();
    }
}

static float TDR_AktualnyVf(void)
{
    float vf = (float)Vf_x10000 / 10000.0f;
    if (vf < 0.01f || vf > 1.0f)
        vf = 0.66f;
    return vf;
}

// Pomiar widma odbicia i transformacja do dziedziny czasu.
static void TDR_ZastosujFiltrCzasu(uint32_t liczba)
{
    const uint32_t tryb = CFG_GetParam(CFG_PARAM_TDR_FILTR_CZASU);
    uint32_t i;
    MET_KALMAN_1D_t kalman;

    if (time_domain == NULL || time_domain_raw == NULL || liczba == 0U)
        return;

    memcpy(time_domain, time_domain_raw, sizeof(float) * liczba);
    if (tryb == 0U)
        return;

    if (tryb == 1U || tryb == 3U)
        MET_SavitzkyGolay5(time_domain_raw, time_domain, liczba);

    if (tryb == 2U)
        memcpy(time_domain, time_domain_raw, sizeof(float) * liczba);

    if (tryb == 2U || tryb == 3U)
    {
        /*
         * Parametry są celowo łagodne. To filtr wizualno-analityczny po IFFT,
         * a nie próba zwiększania fizycznej rozdzielczości TDR. Surowy przebieg
         * pozostaje w time_domain_raw i może być rysowany równolegle.
         */
        MET_Kalman1DInit(&kalman, 5.0e-4f, 2.0e-3f);
        for (i = 0U; i < liczba; ++i)
            time_domain[i] = MET_Kalman1DAktualizuj(&kalman, time_domain[i]);
    }
}

static bool TDR_Scan(void)
{
    float z0;
    uint32_t fmin;
    uint32_t fmax;
    uint16_t i;
    arm_rfft_fast_instance_f32 fft;
    arm_status status_fft;

    memset(&stan_skanu, 0, sizeof(stan_skanu));
    memset(&interpretacja_tdr, 0, sizeof(interpretacja_tdr));
    tdr_indeks_markera_auto = NAN;
    tdr_pozycja_markera_auto = 0U;
    TDR_ParametrySkanuDlaZakresu(TDR_Length, &stan_skanu.krok_hz,
                                 &stan_skanu.liczba_binow,
                                 &stan_skanu.liczba_probek_czasu);

    if (time_domain == NULL || time_domain_raw == NULL || step_response == NULL || Ztv == NULL || freq_domain == NULL)
    {
        TDR_UstawKomunikat(JEZYK_Wybierz("Brak pamięci dla TDR.", "Not enough memory for TDR.", "Nicht genug Speicher für TDR.", "Недостаточно памяти для TDR."));
        GEN_SetMeasurementFreq(0);
        return false;
    }

    memset(freq_domain, 0, sizeof(float complex) * NUMTDRSAMPLES);
    memset(time_domain, 0, sizeof(float) * NUMTDRSAMPLES * 2U);
    memset(step_response, 0, sizeof(float) * NUMTDRSAMPLES * 2U);
    memset(Ztv, 0, sizeof(float) * NUMTDRSAMPLES * 2U);

    z0 = (float)CFG_GetParam(CFG_PARAM_R0);
    fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    {
        POMIAR_S11_t rozgrzewka;
        const POMIAR_S11_USTAWIENIA_t ustawienia = {
            .tor = POMIAR_S11_TOR_STANDARD, .liczba_usrednien = 1U,
            .korekcja_hw = true, .korekcja_osl = true, .kompensacja_portu = false};
        /* Pierwszy pomiar stabilizuje tor po zmianie trybu generatora. */
        (void)POMIAR_S11_PobierzPunkt(BAND_FMIN, &ustawienia, &rozgrzewka);
    }

    /*
     * Format wejscia CMSIS RFFT wykorzystuje pierwszy zespolony element
     * jako DC i Nyquist. Oba skladniki zerujemy jawnie; TDR korzysta z
     * kolejnych dodatnich harmonicznych o stalym kroku czestotliwosci.
     */
    freq_domain[0] = 0.0f + 0.0f * I;

    for (i = 1U; i < stan_skanu.liczba_binow; ++i)
    {
        const uint32_t freq_hz = (uint32_t)i * stan_skanu.krok_hz;
        POMIAR_S11_t pomiar;
        POMIAR_S11_USTAWIENIA_t ustawienia = {
            .tor = POMIAR_S11_TOR_STANDARD,
            .liczba_usrednien = (uint8_t)CFG_GetParam(CFG_PARAM_PAN_NSCANS),
            .korekcja_hw = true,
            .korekcja_osl = true,
            .kompensacja_portu = false};
        float complex gamma;
        float waga;

        if (freq_hz < fmin || freq_hz > fmax)
        {
            freq_domain[i] = 0.0f + 0.0f * I;
            ++stan_skanu.punkty_pominiete;
            continue;
        }

        if (!POMIAR_S11_PobierzPunkt(freq_hz, &ustawienia, &pomiar))
        {
            freq_domain[i] = 0.0f + 0.0f * I;
            ++stan_skanu.punkty_pominiete;
            continue;
        }

        gamma = pomiar.gamma;
        if (!isfinite(crealf(gamma)) || !isfinite(cimagf(gamma)))
        {
            freq_domain[i] = 0.0f + 0.0f * I;
            ++stan_skanu.punkty_pominiete;
            continue;
        }


        waga = TDR_WagaOkna(i, TDR_Length);
        if (!isfinite(waga) || waga < 0.0f)
        {
            freq_domain[i] = 0.0f + 0.0f * I;
            ++stan_skanu.punkty_pominiete;
            continue;
        }

        freq_domain[i] = gamma * waga;
        ++stan_skanu.punkty_poprawne;
        stan_skanu.najwyzsza_czestotliwosc_hz = freq_hz;

        /* Pasek postepu jest jedynie informacja dla uzytkownika. */
        LCD_SetPixel(LCD_MakePoint(X0 + i / 2U + 72U, 145), UI_KolorRamki(UI_STYL_AKCENT));
        LCD_SetPixel(LCD_MakePoint(X0 + i / 2U + 72U, 146), UI_KolorRamki(UI_STYL_AKCENT));
        LCD_SetPixel(LCD_MakePoint(X0 + i / 2U + 72U, 147), UI_KolorRamki(UI_STYL_AKCENT));
        LCD_SetPixel(LCD_MakePoint(X0 + i / 2U + 72U, 148), UI_KolorRamki(UI_STYL_AKCENT));
    }
    GEN_SetMeasurementFreq(0);

    if (stan_skanu.punkty_poprawne < 16U)
    {
        TDR_UstawKomunikat(JEZYK_Wybierz("Za mało poprawnych punktów RF dla TDR.", "Too few valid RF points for TDR.", "Zu wenige gültige HF-Punkte für TDR.", "Слишком мало корректных ВЧ-точек для TDR."));
        return false;
    }

    status_fft = arm_rfft_fast_init_f32(&fft, stan_skanu.liczba_probek_czasu);
    if (status_fft != ARM_MATH_SUCCESS)
    {
        TDR_UstawKomunikat(JEZYK_Wybierz("Błąd inicjalizacji RFFT.", "RFFT initialization error.", "Fehler bei RFFT-Initialisierung.", "Ошибка инициализации RFFT."));
        return false;
    }

    arm_rfft_fast_f32(&fft, (float32_t *)freq_domain, time_domain, 1);

    const float normalizacja_okna = TDR_NormalizacjaOkna();
    max1 = 0.0f;
    max_idx = 0;

    for (i = 0U; i < stan_skanu.liczba_probek_czasu; ++i)
        time_domain_raw[i] = time_domain[i] * normalizacja_okna;

    TDR_ZastosujFiltrCzasu(stan_skanu.liczba_probek_czasu);

    for (i = 0U; i < stan_skanu.liczba_probek_czasu; ++i)
    {
        float d;
        float gamma_skoku;
        float z_kabla;

        d = fabsf(time_domain[i]);
        if (isfinite(d) && d > max1)
        {
            max1 = d;
            max_idx = (int)i;
        }

        /*
         * Historyczna galaz DH1AKF rekonstruuje przebieg skokowy z obwiedni
         * zmian odpowiedzi impulsowej. Zachowujemy ten algorytm jako historyczny,
         * poniewaz jego skala impedancji wymaga jeszcze walidacji wzorcowym
         * kablem. Test30 porzadkuje os czasu i jawnie oznacza Z jako wartosc
         * orientacyjna, ale nie podmienia tego modelu bez danych pomiarowych.
         */
        if (i == 0U)
        {
            step_response[i] = time_domain[i] * 0.5f;
        }
        else
        {
            const float przyrost = fabsf((time_domain[i] - time_domain[i - 1U]) * 0.5f);
            if ((time_domain[i - 1U] + time_domain[i]) >= 0.0f)
                step_response[i] = step_response[i - 1U] + przyrost;
            else
                step_response[i] = step_response[i - 1U] - przyrost;
        }

        if (!isfinite(step_response[i]))
            step_response[i] = 0.0f;
        if (step_response[i] >= 1.0f)
            step_response[i] = 0.99999f;
        if (step_response[i] <= -1.0f)
            step_response[i] = -0.99999f;

        gamma_skoku = step_response[i];

        if (TDRM_ObliczImpedancjeZGamma(z0, gamma_skoku, &z_kabla))
        {
            if (z_kabla > 2999.0f)
                z_kabla = 2999.0f;
            Ztv[i] = z_kabla;
        }
        else
        {
            Ztv[i] = NAN;
        }
    }

    /*
     * Interpretacja nie uczestniczy w IFFT ani w rekonstrukcji przebiegu.
     * Dostaje gotową odpowiedź czasową i może jedynie zaproponować marker.
     * Gdy wynik jest słaby, zachowujemy historyczne maksimum amplitudy.
     */
    (void)TDRI_Analizuj(time_domain, stan_skanu.liczba_probek_czasu,
                        &interpretacja_tdr);

    normFactor = 1.0f;

    {
        float indeks_markera = (float)max_idx;
        uint32_t pozycja;
        const uint32_t maksimum = TDR_MaksPozycjaKursora();

        if (interpretacja_tdr.poprawny)
        {
            if ((interpretacja_tdr.flagi & TDRI_FLAGA_SLABY_SYGNAL) == 0U)
                indeks_markera = interpretacja_tdr.indeks_pierwszego_dokladny;
            else
                indeks_markera = interpretacja_tdr.indeks_najsilniejszego_dokladny;
        }
        if (!isfinite(indeks_markera))
            indeks_markera = (float)max_idx;

        pozycja = (uint32_t)(indeks_markera *
                             ((float)MaxTDR_Length / (float)TDR_Length) + 0.5f);
        if (pozycja > maksimum)
            pozycja = maksimum;
        TDR_cursorPos = pozycja;
        tdr_pozycja_markera_auto = pozycja;
        tdr_indeks_markera_auto = indeks_markera;
    }

    stan_skanu.poprawny = 1U;
    if (stan_skanu.punkty_pominiete > 0U)
    {
        snprintf(komunikat_tdr, sizeof(komunikat_tdr),
                 JEZYK_Wybierz("Poprawne punkty RF: %u, pominięte: %u", "Valid RF points: %u, skipped: %u", "Gültige HF-Punkte: %u, übersprungen: %u", "Корректных ВЧ-точек: %u, пропущено: %u"),
                 (unsigned)stan_skanu.punkty_poprawne,
                 (unsigned)stan_skanu.punkty_pominiete);
    }
    else
    {
        snprintf(komunikat_tdr, sizeof(komunikat_tdr),
                 JEZYK_Wybierz("Pomiar poprawny: %u punktów RF", "Scan OK: %u RF points", "Scan OK: %u HF-Punkte", "Скан OK: %u ВЧ-точек"),
                 (unsigned)stan_skanu.punkty_poprawne);
    }
    return true;
}

static void TDR_Exit(void)
{
    rqExit = 1;
}

static void TDR_Screenshot(void)
{
    char *fname = 0;
    fname = SCREENSHOT_SelectFileName();

    if (strlen(fname) == 0)
        return;

    SCREENSHOT_DeleteOldest();

    LCD_FillRect((LCDPoint){70, 241}, (LCDPoint){479, 271}, BackGrColor);
    Date_Time_Stamp();
    if (CFG_GetParam(CFG_PARAM_SCREENSHOT_FORMAT))
        SCREENSHOT_SavePNG(fname);
    else
        SCREENSHOT_Save(fname);
}

static UI_STYL_t TDR_StylMeta(KAL_META_TYP_t typ, int32_t profil, int zaladowana, char *opis, size_t rozmiar)
{
    KAL_META_DANE_t meta;
    KAL_META_OCENA_t ocena;

    if (!zaladowana)
    {
        snprintf(opis, rozmiar, "%s", JEZYK_Tekst(TEKST_STATUS_BRAK));
        return UI_STYL_OSTRZEZENIE;
    }

    memset(&meta, 0, sizeof(meta));
    memset(&ocena, 0, sizeof(ocena));
    if (!KAL_META_Pobierz(typ, profil, &meta))
    {
        snprintf(opis, rozmiar, "%s", JEZYK_Wybierz("OK (bez historii)", "OK (no history)",
                                                      "OK (ohne Verlauf)",
                                                      "OK (без истории)"));
        return UI_STYL_NORMALNY;
    }

    KAL_META_Ocen(typ, profil, &meta, &ocena);
    if (ocena.uwagi & KAL_META_UWAGA_HW_ZMIENIONE)
    {
        snprintf(opis, rozmiar, "!HW");
        return UI_STYL_OSTRZEZENIE;
    }
    if (ocena.uwagi & (KAL_META_UWAGA_KONFIGURACJA |
                       KAL_META_UWAGA_BRAK_PLIKU |
                       KAL_META_UWAGA_PLIK_ZMIENIONY))
    {
        snprintf(opis, rozmiar, "%s", JEZYK_Wybierz("sprawdź", "check", "prüfen", "проверить"));
        return UI_STYL_OSTRZEZENIE;
    }

    snprintf(opis, rozmiar, "%s", JEZYK_Tekst(TEKST_STATUS_OK));
    return UI_STYL_AKTYWNY;
}

static void TDR_RysujStanPrzedSkanem(void)
{
    char hw[32];
    char osl[32];
    char linia[96];
    const int32_t profil = OSL_GetSelected();
    UI_STYL_t styl_hw;
    UI_STYL_t styl_osl;

    if (TDR_isScanned)
        return;

    styl_hw = TDR_StylMeta(KAL_META_HW, -1, OSL_IsErrCorrLoaded(), hw, sizeof(hw));
    styl_osl = TDR_StylMeta(KAL_META_OSL, profil, OSL_IsSelectedValid(), osl, sizeof(osl));

    snprintf(linia, sizeof(linia), "HW: %s    OSL %s: %s", hw,
             profil >= 0 ? OSL_GetSelectedName() : "-", osl);
    FONT_Write(FONT_FRAN,
               (styl_hw == UI_STYL_OSTRZEZENIE || styl_osl == UI_STYL_OSTRZEZENIE)
                   ? UI_KolorTekstu(UI_STYL_OSTRZEZENIE)
                   : UI_KolorTekstu(UI_STYL_AKTYWNY),
               BackGrColor, 42, 33, linia);

    {
        char opis_zasiegu[128];
        uint32_t krok_hz;
        uint16_t liczba_binow;
        uint16_t liczba_probek_czasu;
        TDR_ParametrySkanuDlaZakresu(TDR_Length, &krok_hz, &liczba_binow, &liczba_probek_czasu);
        (void)liczba_binow;
        (void)liczba_probek_czasu;
        snprintf(opis_zasiegu, sizeof(opis_zasiegu),
                 TDR_Length == 10
                     ? JEZYK_Wybierz("Bliski: powiększenie, krok RF %lu kHz", "Near: zoom, RF step %lu kHz", "Nah: Zoom, HF-Schritt %lu kHz", "Ближний: увеличение, шаг %lu кГц")
                     : TDR_Length == 50
                           ? JEZYK_Wybierz("Średni: pełny widok, krok RF %lu kHz", "Medium: full view, RF step %lu kHz", "Mittel: Vollansicht, HF-Schritt %lu kHz", "Средний: полный вид, шаг %lu кГц")
                           : TDR_Length == 150
                                 ? JEZYK_Wybierz("Długi: większy zasięg, krok RF %lu kHz", "Long: more range, RF step %lu kHz", "Lang: mehr Reichweite, HF-Schritt %lu kHz", "Дальний: больше диапазон, шаг %lu кГц")
                                 : JEZYK_Wybierz("Bardzo długi: maks. zasięg, krok RF %lu kHz", "Very long: max range, RF step %lu kHz", "Sehr lang: max. Reichweite, HF-Schritt %lu kHz", "Очень дальний: макс. диапазон, шаг %lu кГц"),
                 (unsigned long)(krok_hz / 1000U));
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BackGrColor, 42, 45,
                   opis_zasiegu);
    }
}

static const char *TDR_OpisCharakteruOdbicia(TDRI_CHARAKTER_t charakter)
{
    switch (charakter)
    {
    case TDRI_CHARAKTER_WZROST_IMPEDANCJI:
        return JEZYK_Wybierz("wzrost Z", "Z increase", "Z steigt", "рост Z");
    case TDRI_CHARAKTER_SPADEK_IMPEDANCJI:
        return JEZYK_Wybierz("spadek Z", "Z decrease", "Z fällt", "падение Z");
    case TDRI_CHARAKTER_NIEJEDNOZNACZNY:
        return JEZYK_Wybierz("niejednoznaczne", "ambiguous", "uneindeutig", "неоднозначно");
    case TDRI_CHARAKTER_BRAK:
    default:
        return JEZYK_Wybierz("brak", "none", "keine", "нет");
    }
}

static bool TDR_OdlegloscDlaIndeksu(float indeks, float vf, float *odleglosc_m)
{
    float czas_ns;

    if (odleglosc_m == NULL)
        return false;
    if (!TDRM_ObliczCzasNs(indeks, stan_skanu.krok_hz,
                           stan_skanu.liczba_probek_czasu, &czas_ns))
        return false;
    return TDRM_ObliczOdlegloscM(czas_ns, vf, odleglosc_m);
}

static bool TDR_PobierzWynikKursora(TDR_WYNIK_KURSORA_t *wynik)
{
    uint16_t indeks;

    if (wynik == NULL || !TDR_isScanned || !stan_skanu.poprawny)
        return false;

    memset(wynik, 0, sizeof(*wynik));
    wynik->z_kabla = NAN;
    wynik->vf = TDR_AktualnyVf();
    if (wynik->vf < 0.01f || wynik->vf > 1.0f)
        wynik->vf = 0.66f;

    indeks = TDR_IndeksProbkiZaokraglony(TDR_cursorPos);
    if (indeks < stan_skanu.liczba_probek_czasu)
        wynik->amplituda = time_domain[indeks];

    if (!TDR_CzasKursoraNs(TDR_cursorPos, &wynik->czas_ns))
        return false;
    if (!TDRM_ObliczOdlegloscM(wynik->czas_ns, wynik->vf, &wynik->odleglosc_m))
        return false;

    if (TDR_INDEKS_IMPEDANCJI_KABLA < stan_skanu.liczba_probek_czasu)
        wynik->z_kabla = Ztv[TDR_INDEKS_IMPEDANCJI_KABLA];

    wynik->marker_automatyczny =
        (isfinite(tdr_indeks_markera_auto) && TDR_cursorPos == tdr_pozycja_markera_auto) ? 1U : 0U;
    return true;
}

static void TDR_RysujSterowanieZasiegiem(void)
{
    char opis_zasiegu[40];
    float vf = TDR_AktualnyVf();
    float zasieg_m;

    if (vf < 0.01f || vf > 1.0f)
        vf = 0.66f;
    zasieg_m = TDR_ZakresWidocznyM(TDR_Length, MaxTDR_Length, vf);
    snprintf(opis_zasiegu, sizeof(opis_zasiegu),
             JEZYK_Wybierz("%s | do ~%.0f m", "%s | to ~%.0f m",
                           "%s | bis ~%.0f m", "%s | до ~%.0f м"),
             TDR_NazwaTrybuZasiegu(TDR_Length), zasieg_m);

    UI_RysujPrzycisk(300, 1, 30, 30, "<", UI_STYL_NORMALNY, FONT_FRANBIG);
    UI_RysujPrzyciskZaznaczony(330, 1, 120, 30, opis_zasiegu, UI_STYL_NORMALNY,
                               FONT_FRAN, 1);
    UI_RysujPrzycisk(450, 1, 30, 30, ">", UI_STYL_NORMALNY, FONT_FRANBIG);
}

static void TDR_DrawCursorText(void)
{
    char tytul[48];
    TDR_WYNIK_KURSORA_t wynik;
    LCDColor kolor_akcentu = UI_KolorRamki(UI_STYL_AKCENT);

    if (!TDR_PobierzWynikKursora(&wynik))
        return;

    /*
     * Wynik kursora nie może już zasłaniać pierwszych kilkudziesięciu pikseli
     * wykresu. W poprzednim układzie czyszczenie obszaru y=32..56 usuwało
     * również fragment narysowanej krzywej, ponieważ wykres zaczyna się przy
     * y=37. Najważniejsza wartość trafia więc do wolnej lewej części nagłówka,
     * a szczegóły są dostępne na osobnym ekranie „Wynik”.
     */
    snprintf(tytul, sizeof(tytul), "TDR   %.2f m", wynik.odleglosc_m);
    UI_RysujNaglowek(tytul);
    TDR_RysujSterowanieZasiegiem();

    FONT_Write(FONT_FRANBIG, kolor_akcentu, BackGrColor, 4, Y0 + 20, "<");
    FONT_Write(FONT_FRANBIG, kolor_akcentu, BackGrColor, 462, Y0 + 20, ">");

    LCD_Rectangle(LCD_MakePoint(X0 - 4, Y0 - 90),
                  LCD_MakePoint(X0 + WWIDTH - 1, Y0 + 90),
                  UI_KolorRamki(UI_STYL_NORMALNY));
}

static void TDR_DrawCursor(void)
{
    LCDPoint p;
    if (!TDR_isScanned)
        return;

    //Draw cursor line as inverted image
    if (TDR_Length == 300)
        p = LCD_MakePoint(X0 + TDR_cursorPos / 2, Y0 - 90);
    else
        p = LCD_MakePoint(X0 + TDR_cursorPos, Y0 - 90);
    while (p.y < Y0 + 90)
    {
        LCD_InvertPixel(p);
        p.y++;
    }
}
static const int yi[] = {89, 74, 59, 44, 35, 20, 0};
static const char texti[7][3] = {"600", "300", "150", " 75", " 50", " 25", "Ohm"};

static void TDR_DrawGrid(void)
{
    int i;
    char text0[4];

    text0[3] = 0;
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_TDR_TYTUL));
    LCD_FillRect(LCD_MakePoint(0, Y0 - 90 - 2),
                 LCD_MakePoint(LCD_GetWidth() - 1, Y0 + 90 + 2), BackGrColor);
    LCD_Rectangle(LCD_MakePoint(X0 - 4, Y0 - 90),
                  LCD_MakePoint(X0 + WWIDTH - 1, Y0 + 90),
                  UI_KolorRamki(UI_STYL_NORMALNY));
    LCD_Line(LCD_MakePoint(X0, Y0), LCD_MakePoint(X0 + WWIDTH - 1, Y0),
             UI_KolorRamki(UI_STYL_NIEAKTYWNY));

    /*
     * Historyczne 10/50/150/300 były wewnętrznymi trybami i wyglądały jak
     * deklarowane metry. Użytkownik wybiera teraz nazwany tryb zasięgu,
     * a obok widzi rzeczywisty przybliżony koniec osi dla aktualnego Vf.
     */
    TDR_RysujSterowanieZasiegiem();

    for (i = 0; i < 7; i++)
    {
        LCD_Line(LCD_MakePoint(X0, Y0 - yi[i]),
                 LCD_MakePoint(X0 + WWIDTH - 1, Y0 - yi[i]),
                 UI_KolorRamki(UI_STYL_NIEAKTYWNY));
        strncpy(text0, texti[i], 3);
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), BackGrColor,
                   X0 + WWIDTH + 2, Y0 - yi[i] - 10, text0);
    }
}

static void TDR_DecrCursor(void)
{
    if (!TDR_isScanned || !stan_skanu.poprawny)
        return;
    if (TDR_cursorPos == 0)
        return;
    TDR_DrawCursor();
    tdr_indeks_markera_auto = NAN;
    TDR_cursorPos--;
    TDR_DrawCursor();
    TDR_DrawCursorText();
    if (TDR_cursorChangeCount++ < 10)
        Sleep(100); //Slow down at first steps
}

static void TDR_AdvCursor(void)
{
    const uint32_t wd = TDR_MaksPozycjaKursora();
    if (!TDR_isScanned || !stan_skanu.poprawny)
        return;
    if (TDR_cursorPos >= wd)
        return;
    TDR_DrawCursor();
    tdr_indeks_markera_auto = NAN;
    TDR_cursorPos++;
    TDR_DrawCursor();
    TDR_DrawCursorText();
    if (TDR_cursorChangeCount++ < 10)
        Sleep(100); //Slow down at first steps
}

static void TDR_DrawGraph(void)
{
    uint32_t i;
    uint32_t factor = (TDR_Length == 300) ? 2U : 1U;
    int32_t lasty = Y0;
    int32_t lastz = Y0;
    uint8_t poprzednie_z_poprawne = 0U;

    if (!TDR_isScanned || !stan_skanu.poprawny || stan_skanu.liczba_probek_czasu == 0U)
        return;

    normFactor = 1.0f;
    Zmax = 600.0f;

    /*
     * W trybie porównawczym rysujemy najpierw surową odpowiedź IFFT cienką,
     * nieaktywną linią. Główny przebieg pozostaje wynikiem wybranej metody.
     * Pozwala to zobaczyć rzeczywisty efekt filtracji bez ukrywania danych.
     */
    if (time_domain_raw != NULL && CFG_GetParam(CFG_PARAM_TDR_FILTR_CZASU) != 0U &&
        CFG_GetParam(CFG_PARAM_TDR_POROWNAJ_FILTR) != 0U)
    {
        int32_t poprzedni_y_raw = Y0;
        bool ma_poprzedni_raw = false;
        for (i = 0U;
             i < stan_skanu.liczba_probek_czasu &&
             i * (uint32_t)MaxTDR_Length / ((uint32_t)TDR_Length * factor) <= WWIDTH + 1U;
             ++i)
        {
            const int32_t x_raw = X0 + (int32_t)(i * (uint32_t)MaxTDR_Length /
                                                  ((uint32_t)TDR_Length * factor));
            float w_raw = time_domain_raw[i];
            int32_t y_raw = Y0;
            if (!isfinite(w_raw))
                w_raw = 0.0f;
            if (variant == 0)
                y_raw = Y0 - (int32_t)(w_raw * 90.0f);
            else
            {
                const float modul_raw = powf(fabsf(w_raw), 1.0f / 3.0f) * 90.0f;
                y_raw = (w_raw >= 0.0f) ? (Y0 - (int32_t)modul_raw) : (Y0 + (int32_t)modul_raw);
            }
            if (y_raw < Y0 - 89) y_raw = Y0 - 89;
            if (y_raw > Y0 + 89) y_raw = Y0 + 89;
            if (ma_poprzedni_raw)
            {
                const int32_t x_poprzedni_raw = X0 + (int32_t)((i - 1U) * (uint32_t)MaxTDR_Length /
                                                               ((uint32_t)TDR_Length * factor));
                LCD_Line(LCD_MakePoint((uint16_t)x_poprzedni_raw, (uint16_t)poprzedni_y_raw),
                         LCD_MakePoint((uint16_t)x_raw, (uint16_t)y_raw),
                         UI_KolorTekstu(UI_STYL_NIEAKTYWNY));
            }
            poprzedni_y_raw = y_raw;
            ma_poprzedni_raw = true;
        }
    }

    for (i = 0U;
         i < stan_skanu.liczba_probek_czasu &&
         i * (uint32_t)MaxTDR_Length / ((uint32_t)TDR_Length * factor) <= WWIDTH + 1U;
         ++i)
    {
        const int32_t x = X0 + (int32_t)(i * (uint32_t)MaxTDR_Length /
                                         ((uint32_t)TDR_Length * factor));
        int32_t y = Y0;
        int32_t z = Y0;
        uint8_t z_poprawne = 0U;
        float w = time_domain[i] * normFactor;

        if (!isfinite(w))
            w = 0.0f;

        if (variant == 0)
            y = Y0 - (int32_t)(w * 90.0f);
        else
        {
            const float modul = powf(fabsf(w), 1.0f / 3.0f) * 90.0f;
            y = (w >= 0.0f) ? (Y0 - (int32_t)modul) : (Y0 + (int32_t)modul);
        }

        if (y < Y0 - 89) y = Y0 - 89;
        if (y > Y0 + 89) y = Y0 + 89;

        if (isfinite(Ztv[i]) && Ztv[i] > 0.0f)
        {
            if (Ztv[i] <= Zmax)
                z = Y0 - (int32_t)(log10f(Ztv[i]) * 50.0f - 49.5f);
            else
                z = Y0 - 87;
            if (z < Y0 - 87) z = Y0 - 87;
            if (z > Y0 + 87) z = Y0 + 87;
            z_poprawne = 1U;
        }

        if (i != 0U)
        {
            const int32_t x_poprzedni = X0 + (int32_t)((i - 1U) * (uint32_t)MaxTDR_Length /
                                                       ((uint32_t)TDR_Length * factor));
            LCD_Line(LCD_MakePoint((uint16_t)x_poprzedni, (uint16_t)lasty),
                     LCD_MakePoint((uint16_t)x, (uint16_t)y), Color1);
            if (FatLines)
            {
                LCD_Line(LCD_MakePoint((uint16_t)x_poprzedni, (uint16_t)(lasty + 1)),
                         LCD_MakePoint((uint16_t)x, (uint16_t)(y + 1)), Color1);
            }

            if (poprzednie_z_poprawne && z_poprawne)
            {
                LCD_Line(LCD_MakePoint((uint16_t)x_poprzedni, (uint16_t)lastz),
                         LCD_MakePoint((uint16_t)x, (uint16_t)z),
                         UI_KolorRamki(UI_STYL_AKCENT));
                if (FatLines)
                {
                    LCD_Line(LCD_MakePoint((uint16_t)x_poprzedni, (uint16_t)(lastz + 1)),
                             LCD_MakePoint((uint16_t)x, (uint16_t)(z + 1)),
                             UI_KolorRamki(UI_STYL_AKCENT));
                }
            }
        }

        lasty = y;
        if (z_poprawne)
            lastz = z;
        poprzednie_z_poprawne = z_poprawne;
    }

    FONT_Print(FONT_FRAN, TextColor, BackGrColor, 0, Y0 - 90 + 1, "%.3f", 1.0f / normFactor);
    FONT_Print(FONT_FRAN, TextColor, BackGrColor, 0, Y0 + 90 - 15, "%.3f", -1.0f / normFactor);
}

static void TDR_DoScan(void)
{
    TDR_DrawGrid();
    UI_RysujPanel(135, 91, 210, 48, JEZYK_Tekst(TEKST_TDR_SKANOWANIE), UI_STYL_AKCENT);

    TDR_isScanned = 0U;
    if (!TDR_Scan())
    {
        GEN_SetMeasurementFreq(0);
        TDR_DrawGrid();
        TDR_RysujPrzyciski();
        UI_RysujPanel(80, 91, 320, 60,
                      komunikat_tdr[0] != '\0' ? komunikat_tdr :
                      (JEZYK_Wybierz("Pomiar TDR nieudany.", "TDR scan failed.", "TDR-Messung fehlgeschlagen.", "Измерение TDR не удалось.")),
                      UI_STYL_OSTRZEZENIE);
        Sleep(1600);
        TDR_DrawGrid();
        TDR_RysujPrzyciski();
        return;
    }

    TDR_isScanned = 1U;
    TDR_DrawGrid();
    TDR_RysujPrzyciski();
    TDR_RysujStanPrzedSkanem();
    TDR_DrawGraph();
    TDR_DrawCursor();
    TDR_DrawCursorText();
}

static void TDR_PoprzedniZasieg(void)
{
    while (TOUCH_IsPressed())
        ;
    Sleep(80);
    TDR_UstawTrybZasiegu(TDR_IndeksTrybuZasiegu() - 1);
    TDR_DrawGrid();
    TDR_RysujPrzyciski();
    TDR_RysujStanPrzedSkanem();
}

static void TDR_NastepnyZasieg(void)
{
    while (TOUCH_IsPressed())
        ;
    Sleep(80);
    TDR_UstawTrybZasiegu(TDR_IndeksTrybuZasiegu() + 1);
    TDR_DrawGrid();
    TDR_RysujPrzyciski();
    TDR_RysujStanPrzedSkanem();
}

void ShowVf(void)
{
    char wartosc[16];
    float vf = TDR_AktualnyVf();

    snprintf(wartosc, sizeof(wartosc), "%.4f", vf);
    UI_RysujPoleWartosci(140, 82, 200, 72,
                            JEZYK_Wybierz("Vf kabla", "Cable Vf", "Kabel-Vf", "Vf кабеля"),
                            wartosc);
}

static void Vf_plus(void)
{
    while (TOUCH_IsPressed())
        ;
    Sleep(100);
    if (Vf_x10000 <= 9900U)
        Vf_x10000 += 100U;
    ShowVf();
}
static void Vf_minus(void)
{
    while (TOUCH_IsPressed())
        ;
    Sleep(100);
    if (Vf_x10000 >= 200U)
        Vf_x10000 -= 100U;
    ShowVf();
}

static void Vf_storePerm(void)
{
    TDR_UstawVfX10000(Vf_x10000, true);
    rqExit = 1;
}
static bool Vf_wywolajKalibracje = false;

static void Vf_Kalibruj(void)
{
    Vf_wywolajKalibracje = true;
    rqExit = 1;
}

static const struct HitRect hitVfArr[] =
    {
        HITRECT(40, 100, 80, 54, Vf_minus),
        HITRECT(360, 100, 80, 54, Vf_plus),
        /* Pozycje 1 i 2 wspólnego dolnego rastra 70 x 45 px. */
        HITRECT(82, 220, 70, 45, Vf_Kalibruj),
        HITRECT(164, 220, 70, 45, Vf_storePerm),
        HITEND};

static void TDR_Vf(void)
{
    while (TOUCH_IsPressed())
        ;
    Sleep(100);

    rqExit = 0;
    Vf_wywolajKalibracje = false;
    LCD_Push();

    UI_WyczyscEkran();
    BackGrColor = UI_KolorTlaEkranu();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_TDR_ZMIEN_VF_TYTUL));

    UI_RysujPanel(20, 52, 440, 120,
                  JEZYK_Wybierz("Współczynnik skrócenia kabla", "Cable propagation factor", "Verkürzungsfaktor des Kabels", "Коэффициент укорочения кабеля"),
                  UI_STYL_NORMALNY);
    UI_RysujPrzycisk(40, 100, 80, 54, "-", UI_STYL_NORMALNY, FONT_FRANBIG);
    UI_RysujPrzycisk(360, 100, 80, 54, "+", UI_STYL_NORMALNY, FONT_FRANBIG);
    ShowVf();

    FONT_Write(FONT_FRAN, LCD_GRAY, BackGrColor, 40, 177,
               JEZYK_Wybierz("Typowe kable koncentryczne: około 0,66...0,85; krok ręczny 0,01", "Typical coaxial cables: about 0.66...0.85", "Typische Koaxkabel: etwa 0,66...0,85", "Типичные коаксиальные кабели: около 0,66...0,85"));

    UI_RysujWsteczDolny(false);
    {
        /*
         * POPRAWKA: "Stałe" i "Na teraz" było dwoma przyciskami zapisu, z
         * których "Na teraz" tylko wychodził bez zmian - dokładnie to samo,
         * co już robi widoczny na ekranie przycisk Wstecz. Zamiast
         * duplikować Wstecz, drugie miejsce dostaje właściwą kalibrację:
         * pomiar znanej długości kabla, który już istnieje w aplikacji
         * (TDR_Weryfikuj, dotąd dostępny tylko z ekranu Wynik) i sam
         * wylicza Vf zamiast każenia zgadywać ręcznie przyciskami +/-.
         */
        const UI_PROSTOKAT_t kalibruj = UI_ObszarPrzyciskuDolnego(1U);
        const UI_PROSTOKAT_t zapisz = UI_ObszarPrzyciskuDolnego(2U);
        UI_RysujPrzycisk(kalibruj.x, kalibruj.y, kalibruj.szerokosc, kalibruj.wysokosc,
                         JEZYK_Wybierz("Kalibruj wg kabla", "Calibrate by cable", "Kalibr. n. Kabel", "Калибр. по кабелю"),
                         UI_STYL_AKCENT, FONT_FRAN);
        UI_RysujPrzycisk(zapisz.x, zapisz.y, zapisz.szerokosc, zapisz.wysokosc,
                         JEZYK_Tekst(TEKST_TDR_ZAPISZ),
                         UI_STYL_NORMALNY, FONT_FRAN);
    }

    WEJSCIA_WyczyscZdarzenia();
    for (;;)
    {
        LCDPoint pt;
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            rqExit = 1;
        if (TOUCH_Poll(&pt))
        {
            if (UI_CzyDotknietoWstecz(pt))
                rqExit = 1;
            else
                HitTest(hitVfArr, pt.x, pt.y);
        }

        if (rqExit)
        {
            rqExit = 0;
            LCD_Pop();

            while (TOUCH_IsPressed())
                ;
            TDR_DrawCursorText();
            Sleep(200);
            if (Vf_wywolajKalibracje)
                TDR_Weryfikuj();
            return;
        }
        Sleep(20);
    }
}

static void TDR_WeryfikacjaZastosuj(void)
{
    uint32_t vf_x10000;

    if (!isfinite(vf_sugerowane) || vf_sugerowane < 0.01f || vf_sugerowane > 1.0f)
        return;
    vf_x10000 = (uint32_t)(vf_sugerowane * 10000.0f + 0.5f);
    TDR_UstawVfX10000(vf_x10000, false);
    rqExit = 1U;
}

static void TDR_WeryfikacjaZapisz(void)
{
    uint32_t vf_x10000;

    if (!isfinite(vf_sugerowane) || vf_sugerowane < 0.01f || vf_sugerowane > 1.0f)
        return;
    vf_x10000 = (uint32_t)(vf_sugerowane * 10000.0f + 0.5f);
    TDR_UstawVfX10000(vf_x10000, true);
    (void)KAL_META_Zapisz(KAL_META_TDR, -1);
    rqExit = 1U;
}

static void TDR_WeryfikacjaWstecz(void)
{
    rqExit = 1U;
}

static const struct HitRect hitWeryfikacjaArr[] =
    {
        HITRECT(0, 220, 70, 45, TDR_WeryfikacjaWstecz),
        HITRECT(82, 220, 70, 45, TDR_WeryfikacjaZastosuj),
        HITRECT(164, 220, 70, 45, TDR_WeryfikacjaZapisz),
        HITEND};

static void TDR_RysujWeryfikacje(void)
{
    char tekst[128];
    const float blad_m = weryfikacja_pomiar_m - weryfikacja_dlugosc_m;
    const float blad_proc = (weryfikacja_dlugosc_m > 0.0f)
                                ? 100.0f * blad_m / weryfikacja_dlugosc_m
                                : 0.0f;

    UI_WyczyscEkran();
    BackGrColor = UI_KolorTlaEkranu();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_TDR_WERYFIKACJA_TYTUL));
    UI_RysujPanel(18, 42, 444, 165,
                  JEZYK_Wybierz("Znany koniec kabla", "Known cable marker", "Marker des bekannten Kabels", "Маркер известного кабеля"),
                  UI_STYL_NORMALNY);

    snprintf(tekst, sizeof(tekst),
             JEZYK_Wybierz("Czas kursora: %.2f ns", "Cursor time: %.2f ns", "Cursorzeit: %.2f ns", "Время курсора: %.2f нс"),
             weryfikacja_czas_ns);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(), 36, 70, tekst);

    snprintf(tekst, sizeof(tekst),
             JEZYK_Wybierz("Długość rzeczywista: %.3f m", "Known length: %.3f m", "Bekannte Länge: %.3f m", "Известная длина: %.3f м"),
             weryfikacja_dlugosc_m);
    FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(), 36, 91, tekst);

    snprintf(tekst, sizeof(tekst), "%s: %.3f m   (%+.3f m, %+.2f%%)",
             JEZYK_Tekst(TEKST_TDR_DLUGOSC_ZMIERZONA),
             weryfikacja_pomiar_m, blad_m, blad_proc);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(), 36, 124, tekst);

    snprintf(tekst, sizeof(tekst),
             JEZYK_Wybierz("Aktualne Vf: %.4f   sugerowane: %.4f", "Current Vf: %.4f   suggested: %.4f", "Aktuelles Vf: %.4f   Vorschlag: %.4f", "Текущий Vf: %.4f   предложен: %.4f"),
             TDR_AktualnyVf(), vf_sugerowane);
    FONT_Write(FONT_FRANBIG, UI_KolorRamki(UI_STYL_AKCENT), UI_KolorTlaPola(), 36, 147, tekst);

    FONT_Write(FONT_FRAN, LCD_GRAY, UI_KolorTlaPola(), 36, 178,
               JEZYK_Tekst(TEKST_TDR_WERYFIKACJA_INFO));

    UI_RysujWsteczDolny(false);
    {
        const UI_PROSTOKAT_t zastosuj = UI_ObszarPrzyciskuDolnego(1U);
        const UI_PROSTOKAT_t zapisz = UI_ObszarPrzyciskuDolnego(2U);
        UI_RysujPrzycisk(zastosuj.x, zastosuj.y, zastosuj.szerokosc, zastosuj.wysokosc,
                         JEZYK_Wybierz("Użyj Vf", "Use Vf", "Vf nutzen", "Применить Vf"),
                         UI_STYL_NORMALNY, FONT_FRAN);
        UI_RysujPrzycisk(zapisz.x, zapisz.y, zapisz.szerokosc, zapisz.wysokosc,
                         JEZYK_Wybierz("Zapisz", "Save", "Speichern", "Сохранить"),
                         UI_STYL_AKCENT, FONT_FRAN);
    }
}

static void TDR_Weryfikuj(void)
{
    uint32_t znana_dlugosc_mm;
    uint32_t domyslna_dlugosc_mm;
    float czas_ns;
    float zmierzona_m;
    float vf;

    while (TOUCH_IsPressed())
        Sleep(5);

    /*
     * POPRAWKA: zamiast tylko ostrzegać "najpierw wykonaj pomiar" i kończyć,
     * wymuszamy skan od razu, tak jak przycisk Skanuj. TDR_DoScan() ustawia
     * też kursor automatycznie na wykrytym odbiciu, więc nie trzeba go
     * pozycjonować ręcznie przed podaniem znanej długości.
     */
    if (!TDR_isScanned || !stan_skanu.poprawny)
        TDR_DoScan();

    if (!TDR_isScanned || !stan_skanu.poprawny ||
        !TDR_CzasKursoraNs(TDR_cursorPos, &czas_ns))
    {
        UI_RysujPanel(90, 90, 300, 68, JEZYK_Tekst(TEKST_TDR_NAJPIERW_POMIAR),
                      UI_STYL_OSTRZEZENIE);
        Sleep(1400);
        TDR_DrawGrid();
        TDR_RysujPrzyciski();
        if (TDR_isScanned && stan_skanu.poprawny)
        {
            TDR_DrawGraph();
            TDR_DrawCursor();
            TDR_DrawCursorText();
        }
        return;
    }

    vf = TDR_AktualnyVf();
    if (!TDRM_ObliczOdlegloscM(czas_ns, vf, &zmierzona_m))
        return;

    domyslna_dlugosc_mm = (uint32_t)(zmierzona_m * 1000.0f + 0.5f);
    if (domyslna_dlugosc_mm < 10U)
        domyslna_dlugosc_mm = 10U;
    if (domyslna_dlugosc_mm > 999999U)
        domyslna_dlugosc_mm = 999999U;

    LCD_Push();
    znana_dlugosc_mm = NumKeypad(domyslna_dlugosc_mm, 10U, 999999U,
                                 JEZYK_Tekst(TEKST_TDR_DLUGOSC_ZNANA_MM));
    if (znana_dlugosc_mm == 0U)
    {
        LCD_Pop();
        return;
    }

    weryfikacja_czas_ns = czas_ns;
    weryfikacja_dlugosc_m = (float)znana_dlugosc_mm / 1000.0f;
    weryfikacja_pomiar_m = zmierzona_m;
    if (!TDRM_WyznaczVf(czas_ns, weryfikacja_dlugosc_m, &vf_sugerowane))
    {
        UI_WyczyscEkran();
        UI_RysujNaglowek(JEZYK_Tekst(TEKST_TDR_WERYFIKACJA_TYTUL));
        UI_RysujPanel(80, 95, 320, 70,
                      JEZYK_Wybierz("Wybrany kursor i długość dają nieprawidłowe Vf.", "The selected marker and length give an invalid Vf.", "Marker und Länge ergeben ein ungültiges Vf.", "Выбранные маркер и длина дают неверный Vf."),
                      UI_STYL_OSTRZEZENIE);
        Sleep(1800);
        LCD_Pop();
        return;
    }

    rqExit = 0U;
    TDR_RysujWeryfikacje();
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        LCDPoint pt;
        WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (TOUCH_Poll(&pt))
            HitTest(hitWeryfikacjaArr, pt.x, pt.y);
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            rqExit = 1U;

        if (rqExit)
        {
            rqExit = 0U;
            LCD_Pop();
            TDR_DrawGrid();
            TDR_RysujPrzyciski();
            TDR_DrawGraph();
            TDR_DrawCursor();
            TDR_DrawCursorText();
            return;
        }
        Sleep(20);
    }
}

static uint8_t tdr_wynik_akcja = 0U;

static void TDR_WynikWstecz(void)
{
    tdr_wynik_akcja = 1U;
}

static void TDR_WynikUstalVf(void)
{
    tdr_wynik_akcja = 2U;
}

static const struct HitRect hitWynikArr[] =
    {
        HITRECT(0, 220, 70, 45, TDR_WynikWstecz),
        HITRECT(328, 220, 152, 45, TDR_WynikUstalVf),
        HITEND};

static void TDR_RysujWynikPomiaru(void)
{
    TDR_WYNIK_KURSORA_t wynik;
    char odleglosc[20];
    char czas[20];
    char vf[20];
    char opis_odbicia[64];
    char diagnostyka[96];
    float odleglosc_pierwszego_m = 0.0f;
    bool ma_pierwsze_odbicie = false;
    UI_STYL_t styl_odbicia = UI_STYL_NORMALNY;

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Wybierz("TDR - wynik pomiaru", "TDR - measurement result",
                                    "TDR - Messergebnis", "TDR - результат измерения"));

    if (!TDR_PobierzWynikKursora(&wynik))
    {
        UI_RysujPanel(70, 92, 340, 70,
                      JEZYK_Tekst(TEKST_TDR_NAJPIERW_POMIAR),
                      UI_STYL_OSTRZEZENIE);
        UI_RysujWsteczDolny(false);
        return;
    }

    snprintf(odleglosc, sizeof(odleglosc), "%.2f", wynik.odleglosc_m);
    snprintf(czas, sizeof(czas), "%.1f", wynik.czas_ns);
    snprintf(vf, sizeof(vf), "%.4f", wynik.vf);

    UI_RysujPoleLiczboweGlowne(18, 42, 216, 88,
                                JEZYK_Wybierz("Odległość z bieżącym Vf", "Distance with current Vf",
                                              "Entfernung mit aktuellem Vf", "Расстояние с текущим Vf"),
                                odleglosc, "m");
    UI_RysujPoleLiczboweGlowne(246, 42, 216, 88,
                                JEZYK_Wybierz("Czas echa (tam + powrót)", "Echo time (round trip)",
                                              "Echozeit (Hin + Rückweg)", "Время эха (туда + обратно)"),
                                czas, "ns");

    UI_RysujPoleWartosci(18, 138, 138, 66,
                         JEZYK_Wybierz("Współczynnik Vf", "Velocity factor Vf",
                                       "Verkürzungsfaktor Vf", "Коэффициент Vf"),
                         vf);

    if (interpretacja_tdr.poprawny)
    {
        ma_pierwsze_odbicie = TDR_OdlegloscDlaIndeksu(
            interpretacja_tdr.indeks_pierwszego_dokladny, wynik.vf,
            &odleglosc_pierwszego_m);
        if ((interpretacja_tdr.flagi & (TDRI_FLAGA_WIELE_ODBIC | TDRI_FLAGA_SLABY_SYGNAL)) != 0U)
            styl_odbicia = UI_STYL_OSTRZEZENIE;
    }

    UI_RysujPanel(166, 138, 296, 66,
                  wynik.marker_automatyczny
                      ? JEZYK_Wybierz("Marker automatyczny", "Automatic marker",
                                      "Automatischer Marker", "Автоматический маркер")
                      : JEZYK_Wybierz("Marker ręczny", "Manual marker",
                                      "Manueller Marker", "Ручной маркер"),
                  styl_odbicia);

    if (interpretacja_tdr.poprawny)
    {
        if (ma_pierwsze_odbicie)
            snprintf(opis_odbicia, sizeof(opis_odbicia), "%s  %.2f m",
                     TDR_OpisCharakteruOdbicia(interpretacja_tdr.charakter),
                     odleglosc_pierwszego_m);
        else
            snprintf(opis_odbicia, sizeof(opis_odbicia), "%s",
                     TDR_OpisCharakteruOdbicia(interpretacja_tdr.charakter));
        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(styl_odbicia), UI_KolorTlaPola(),
                   178, 158, opis_odbicia);

        if (isfinite(wynik.z_kabla))
            snprintf(diagnostyka, sizeof(diagnostyka),
                     JEZYK_Wybierz("S/tło %.1fx   Z~%.0f Ohm   RF-:%u",
                                   "P/bg %.1fx   Z~%.0f Ohm   RF-:%u",
                                   "S/HG %.1fx   Z~%.0f Ohm   HF-:%u",
                                   "пик/фон %.1fx   Z~%.0f Ом   ВЧ-:%u"),
                     interpretacja_tdr.stosunek_szczyt_tlo, wynik.z_kabla,
                     (unsigned)stan_skanu.punkty_pominiete);
        else
            snprintf(diagnostyka, sizeof(diagnostyka),
                     JEZYK_Wybierz("S/tło %.1fx   RF-:%u", "P/bg %.1fx   RF-:%u",
                                   "S/HG %.1fx   HF-:%u", "пик/фон %.1fx   ВЧ-:%u"),
                     interpretacja_tdr.stosunek_szczyt_tlo,
                     (unsigned)stan_skanu.punkty_pominiete);
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
                   178, 184, diagnostyka);
    }
    else
    {
        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), UI_KolorTlaPola(),
                   178, 164,
                   JEZYK_Wybierz("Brak pewnego odbicia", "No reliable echo",
                                 "Kein sicheres Echo", "Нет надежного эха"));
    }

    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(),
               18, 204,
               JEZYK_Wybierz("Odległość zależy od Vf; czas echa jest wynikiem bezpośrednim.",
                             "Distance depends on Vf; echo time is measured directly.",
                             "Die Entfernung hängt von Vf ab; die Echozeit ist direkt gemessen.",
                             "Расстояние зависит от Vf; время эха измеряется напрямую."));

    UI_RysujWsteczDolny(false);
    UI_RysujPrzycisk(328, 220, 152, 45,
                     JEZYK_Wybierz("Ustal Vf", "Set Vf", "Vf bestimmen", "Определить Vf"),
                     UI_STYL_AKCENT, FONT_FRANBIG);
}

static void TDR_Wynik(void)
{
    if (!TDR_isScanned || !stan_skanu.poprawny)
    {
        UI_RysujPanel(90, 90, 300, 68, JEZYK_Tekst(TEKST_TDR_NAJPIERW_POMIAR),
                      UI_STYL_OSTRZEZENIE);
        Sleep(1200);
        TDR_DrawGrid();
        TDR_RysujPrzyciski();
        TDR_RysujStanPrzedSkanem();
        return;
    }

    while (TOUCH_IsPressed())
        Sleep(5);

    LCD_Push();
    tdr_wynik_akcja = 0U;
    TDR_RysujWynikPomiaru();
    WEJSCIA_WyczyscZdarzenia();

    while (tdr_wynik_akcja == 0U)
    {
        LCDPoint pt;
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            tdr_wynik_akcja = 1U;
        if (TOUCH_Poll(&pt))
            HitTest(hitWynikArr, pt.x, pt.y);
        Sleep(20);
    }

    while (TOUCH_IsPressed())
        Sleep(5);
    LCD_Pop();
    Sleep(80);

    if (tdr_wynik_akcja == 2U)
        TDR_Weryfikuj();
}

static void TDR_Variant(void)
{
    LCDPoint pt;
    uint8_t pole = 0U;
    uint8_t odswiez = 1U;
    const bool tryb_zaawansowany = TRYB_CzyZaawansowany();
    const uint8_t stare_okno = tdr_okno;
    const int stary_variant = variant;

    if (!tryb_zaawansowany)
        tdr_okno = METODA_TDR_KAISER;

    while (TOUCH_IsPressed())
        Sleep(5);
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        UI_METODA_OPCJA_t skala[2] = {
            { JEZYK_Wybierz("Liniowa", "Linear", "Linear", "Линейная"),
              JEZYK_Wybierz("wierna amplituda", "direct amplitude", "direkte Amplitude", "прямая амплитуда"), true },
            { JEZYK_Wybierz("Wzmocniony", "Enhanced", "Verstärkt", "Усиленная"),
              JEZYK_Wybierz("uwydatnia słabe odbicia", "emphasizes weak echoes", "betont schwache Reflexionen", "усиливает слабые отражения"), true },
        };
        UI_METODA_OPCJA_t okna[METODA_TDR_LICZBA];
        uint8_t liczba_okien = 0U;
        uint8_t oi;
        const METODA_TDR_REJESTR_t *rejestr_okien = METODA_RejestrTDR(&liczba_okien);
        const METODA_TDR_REJESTR_t *wybrane_okno = TDR_PobierzOpisOkna(tdr_okno);
        WEJSCIE_ZDARZENIE_t zdarzenie;

        if (liczba_okien > METODA_TDR_LICZBA)
            liczba_okien = METODA_TDR_LICZBA;
        for (oi = 0U; oi < liczba_okien; ++oi)
        {
            okna[oi].nazwa = rejestr_okien[oi].opis.nazwa();
            okna[oi].opis = rejestr_okien[oi].opis.opis();
            okna[oi].dostepna = METODA_CzyWidoczna(&rejestr_okien[oi].opis, tryb_zaawansowany);
        }

        if (odswiez)
        {
            char beta_txt[24];
            UI_WyczyscEkran();
            UI_RysujNaglowek(JEZYK_Wybierz("TDR - opcje analizy", "TDR - analysis options",
                                            "TDR - Analyseoptionen",
                                            "TDR - параметры анализа"));
            UI_RysujWyborMetody(18, 40, 444, 58,
                                JEZYK_Wybierz("Skala wykresu", "Graph scale", "Diagrammskala", "Масштаб графика"),
                                skala, 2U, (uint8_t)variant);
            if (tryb_zaawansowany)
            {
                UI_RysujWyborMetody(18, 106, 444, 68,
                                    JEZYK_Wybierz("Okno IFFT", "IFFT window", "IFFT-Fenster", "Окно IFFT"),
                                    okna, liczba_okien, tdr_okno);
            }
            else
            {
                UI_RysujPoleInformacyjne(18, 106, 444, 68,
                                          JEZYK_Wybierz("Okno IFFT", "IFFT window", "IFFT-Fenster", "Окно IFFT"),
                                          wybrane_okno != NULL ? wybrane_okno->opis.nazwa() : "KBD");
            }

            if (tryb_zaawansowany && wybrane_okno != NULL && wybrane_okno->ma_parametr_beta)
            {
                snprintf(beta_txt, sizeof(beta_txt), "beta = %.1f", tdr_kaiser_beta);
                UI_RysujPrzycisk(18, 182, 70, 38, "-", UI_STYL_NORMALNY, FONT_FRANBIG);
                UI_RysujPoleWartosci(98, 182, 284, 38,
                                     JEZYK_Wybierz("Kaiser", "Kaiser", "Kaiser", "Kaiser"), beta_txt);
                UI_RysujPrzycisk(392, 182, 70, 38, "+", UI_STYL_NORMALNY, FONT_FRANBIG);
            }
            else
            {
                UI_RysujPanel(18, 182, 444, 38, 0, UI_STYL_NORMALNY);
                FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(), 30, 194,
                           JEZYK_Wybierz("Zmiana okna wymaga ponownego skanu.",
                                         "Changing the window requires a new scan.",
                                         "Ein Fensterwechsel erfordert einen neuen Scan.",
                                         "Смена окна требует нового сканирования."));
            }
            UI_RysujWsteczDolny(false);
            odswiez = 0U;
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            pole = (uint8_t)((pole + 1U) % (!tryb_zaawansowany ? 1U : ((wybrane_okno != NULL && wybrane_okno->ma_parametr_beta) ? 3U : 2U)));
            odswiez = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO || zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            const int8_t kierunek = zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? 1 : -1;
            if (pole == 0U)
                variant = (int)UI_NastepnaDostepnaMetoda(skala, 2U, (uint8_t)variant, kierunek);
            else if (pole == 1U && tryb_zaawansowany)
                tdr_okno = UI_NastepnaDostepnaMetoda(okna, liczba_okien, tdr_okno, kierunek);
            else if (tryb_zaawansowany && wybrane_okno != NULL && wybrane_okno->ma_parametr_beta)
            {
                tdr_kaiser_beta += kierunek > 0 ? 0.5f : -0.5f;
                if (tdr_kaiser_beta < 0.0f) tdr_kaiser_beta = 0.0f;
                if (tdr_kaiser_beta > 14.0f) tdr_kaiser_beta = 14.0f;
            }
            odswiez = 1U;
        }

        if (TOUCH_Poll(&pt))
        {
            if (pt.y >= 40U && pt.y < 98U)
                variant = (int)UI_WyborMetodyPoDotyku(pt, 18, 40, 444, 58, skala, 2U, (uint8_t)variant);
            else if (tryb_zaawansowany && pt.y >= 106U && pt.y < 174U)
                tdr_okno = UI_WyborMetodyPoDotyku(pt, 18, 106, 444, 68, okna, liczba_okien, tdr_okno);
            else if (tryb_zaawansowany && wybrane_okno != NULL && wybrane_okno->ma_parametr_beta && pt.y >= 182U && pt.y < 220U)
            {
                if (pt.x < 98U) tdr_kaiser_beta -= 0.5f;
                else if (pt.x >= 382U) tdr_kaiser_beta += 0.5f;
                if (tdr_kaiser_beta < 0.0f) tdr_kaiser_beta = 0.0f;
                if (tdr_kaiser_beta > 14.0f) tdr_kaiser_beta = 14.0f;
            }
            else if (UI_CzyDotknietoWstecz(pt))
            {
                TOUCH_CzekajNaPuszczenie(20U);
                break;
            }
            TOUCH_CzekajNaPuszczenie(20U);
            odswiez = 1U;
        }
        Sleep(10);
    }

    if (tdr_okno != stare_okno)
    {
        TDR_isScanned = 0U;
        memset(&stan_skanu, 0, sizeof(stan_skanu));
        TDR_UstawKomunikat(JEZYK_Wybierz("Zmieniono okno IFFT - wykonaj nowy skan.",
                                         "IFFT window changed - run a new scan.",
                                         "IFFT-Fenster geändert - neuen Scan ausführen.",
                                         "Окно IFFT изменено - выполните новый скан."));
    }

    TDR_DrawGrid();
    TDR_RysujPrzyciski();
    if (TDR_isScanned && stan_skanu.poprawny)
    {
        TDR_DrawGraph();
        TDR_DrawCursor();
        TDR_DrawCursorText();
    }
    else
        TDR_RysujStanPrzedSkanem();

    (void)stary_variant;
}

static const struct HitRect hitArr[] =
    {
        HITRECT(0,   220, 70, 45, TDR_Exit),
        HITRECT(82,  220, 70, 45, TDR_Vf),
        HITRECT(164, 220, 70, 45, TDR_Screenshot),
        HITRECT(246, 220, 70, 45, TDR_DoScan),
        HITRECT(328, 220, 70, 45, TDR_Wynik),
        HITRECT(410, 220, 70, 45, TDR_Variant),
        HITRECT(300, 1, 30, 30, TDR_PoprzedniZasieg),
        HITRECT(330, 1, 120, 30, TDR_PomocZasieg),
        HITRECT(450, 1, 30, 30, TDR_NastepnyZasieg),
        HITRECT(430, Y0 + 20, 50, 72, TDR_AdvCursor),
        HITRECT(0, Y0 + 20, 50, 72, TDR_DecrCursor),
        HITEND};

static void TDR_RysujPrzyciski(void)
{
    UI_RysujWsteczDolny(false);
    {
        const char *etykiety[5] = {
            "Vf", JEZYK_Tekst(TEKST_TDR_ZAPISZ), JEZYK_Tekst(TEKST_TDR_SKANUJ),
            JEZYK_Wybierz("Wynik", "Result", "Ergebnis", "Результат"),
            JEZYK_Wybierz("Opcje", "Options", "Optionen", "Опции")};
        uint8_t i;
        for (i = 0U; i < 5U; ++i)
        {
            const UI_PROSTOKAT_t o = UI_ObszarPrzyciskuDolnego((uint8_t)(i + 1U));
            UI_STYL_t styl = UI_STYL_NORMALNY;
            if (i == 2U)
                styl = UI_STYL_AKCENT;
            else if (i == 4U &&
                     (variant != 0 || tdr_okno != METODA_TDR_KAISER || CFG_GetParam(CFG_PARAM_TDR_FILTR_CZASU) != 0U))
                styl = UI_STYL_AKCENT;
            UI_RysujPrzycisk(o.x, o.y, o.szerokosc, o.wysokosc, etykiety[i], styl, FONT_FRAN);
        }
    }
}






void TDR_Proc(void)
{
    rqExit = 0U;
    TDR_isScanned = 0U;
    memset(&stan_skanu, 0, sizeof(stan_skanu));
    TDR_UstawKomunikat("");
    Vf_x10000 = TDR_PobierzVfX10000();

    /* SetColours ustala paletę samego wykresu. Nie nadpisujemy jej
     * paletą menu, bo wtedy przełącznik jasne/ciemne nie miał żadnego efektu. */
    SetColours();

    time_domain = (float *)SDRH_malloc(sizeof(float) * NUMTDRSAMPLES * 2U);
    time_domain_raw = (float *)SDRH_malloc(sizeof(float) * NUMTDRSAMPLES * 2U);
    step_response = (float *)SDRH_malloc(sizeof(float) * NUMTDRSAMPLES * 2U);
    Ztv = (float *)SDRH_malloc(sizeof(float) * NUMTDRSAMPLES * 2U);
    freq_domain = (float complex *)SDRH_malloc(sizeof(float complex) * NUMTDRSAMPLES);

    if (time_domain == NULL || time_domain_raw == NULL || step_response == NULL || Ztv == NULL || freq_domain == NULL)
    {
        if (time_domain != NULL) SDRH_free(time_domain);
        if (time_domain_raw != NULL) SDRH_free(time_domain_raw);
        if (step_response != NULL) SDRH_free(step_response);
        if (Ztv != NULL) SDRH_free(Ztv);
        if (freq_domain != NULL) SDRH_free(freq_domain);
        time_domain = NULL;
        time_domain_raw = NULL;
        step_response = NULL;
        Ztv = NULL;
        freq_domain = NULL;

        UI_WyczyscEkran();
        UI_RysujNaglowek(JEZYK_Tekst(TEKST_TDR_TYTUL));
        UI_RysujPanel(70, 92, 340, 70,
                      JEZYK_Wybierz("Brak pamięci SDRAM na bufory TDR.", "Not enough SDRAM for TDR buffers.", "Nicht genug SDRAM für TDR-Puffer.", "Недостаточно SDRAM для буферов TDR."),
                      UI_STYL_OSTRZEZENIE);
        Sleep(1800);
        return;
    }

    TDR_cursorChangeCount = 0;
    BSP_LCD_SelectLayer(1);
    LCD_FillAll(BackGrColor);
    LCD_ShowActiveLayerOnly();

    while (TOUCH_IsPressed())
        Sleep(5);

    BSP_LCD_SelectLayer(0);
    LCD_FillAll(BackGrColor);
    BSP_LCD_SelectLayer(1);
    LCD_FillAll(BackGrColor);
    LCD_ShowActiveLayerOnly();

    UI_RysujNaglowek(JEZYK_Tekst(TEKST_TDR_TYTUL));
    TDR_DrawGrid();
    TDR_RysujPrzyciski();
    TDR_RysujStanPrzedSkanem();
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        LCDPoint pt;
        uint8_t aktywnosc = 0U;
        WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
        {
            rqExit = 1U;
            aktywnosc = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
        {
            TDR_DecrCursor();
            aktywnosc = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            TDR_AdvCursor();
            aktywnosc = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_START_STOP ||
                 (zdarzenie == WEJSCIE_ZDARZENIE_OK && !TDR_isScanned))
        {
            TDR_DoScan();
            aktywnosc = 1U;
        }

        if (TOUCH_Poll(&pt))
        {
            HitTest(hitArr, pt.x, pt.y);
            aktywnosc = 1U;
        }

        if (rqExit)
        {
            GEN_SetMeasurementFreq(0);
            Sleep(20);

            SDRH_free(time_domain);
            SDRH_free(time_domain_raw);
            SDRH_free(step_response);
            SDRH_free(Ztv);
            SDRH_free(freq_domain);
            time_domain = NULL;
            time_domain_raw = NULL;
            step_response = NULL;
            Ztv = NULL;
            freq_domain = NULL;
            TDR_isScanned = 0U;
            memset(&stan_skanu, 0, sizeof(stan_skanu));
            return;
        }

        if (!aktywnosc)
            TDR_cursorChangeCount = 0;
        Sleep(20);
    }
}
