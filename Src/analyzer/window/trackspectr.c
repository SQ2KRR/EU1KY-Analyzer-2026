/*
  S21-Gain for EU1KY's AA

  by KD8CEC
  kd8cec@gmail.com
  -----------------------------------------------------------------------------
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include <string.h>

#include "LCD.h"
#include "touch.h"
#include "font.h"
#include "config.h"
#include "jezyk.h"
#include "ui_wspolny.h"
#include "ff.h"
#include "crash.h"
#include "dsp.h"
#include "gen.h"
#include "oslfile.h"
#include "spectr.h"
#include "stm32746g_discovery_lcd.h"
#include "screenshot.h"
#include "panvswr2.h"
#include "panfreq.h"
#include "smith.h"
#include "textbox.h"
#include "generator.h"
#include "FreqCounter.h"
#include "bitmaps/bitmaps.h"
#include "si5351.h"
#include "si5351_hs.h"
#include "sdram_heap.h"
#include "s21_analiza.h"
#include "kalibracja_meta.h"
#include "wejscia_uzytkownika.h"

#define X0 51
#define Y0 18
#define WWIDTH 400
#define WHEIGHT 190
#define WY(offset) ((WHEIGHT + Y0) - (offset))
#define RED1 LCD_RGB(245, 0, 0)
#define RED2 LCD_RGB(235, 0, 0)

/*
 * Paleta wykresu S21 zgodna z wykresem SWR w motywie Retro.
 *
 * Wcześniej S21 używał historycznej palety SetColours(): zielona krzywa,
 * szara siatka i niebieskie pasma amatorskie. Sam panel pomiarowy V2.1 jest
 * natomiast utrzymany w ciemnym bakelicie, mosiądzu i kremowym tekście.
 * Poniższe kolory są celowo takie same jak w panoramicznym wykresie SWR,
 * żeby przejście między ekranami nie wyglądało jak zmiana programu.
 */
static LCDColor S21_KolorTlaWykresu(void)
{
    return LCD_RGB(10, 8, 5);
}

static LCDColor S21_KolorSiatkiDrobnej(void)
{
    return LCD_RGB(45, 38, 28);
}

static LCDColor S21_KolorSiatkiGlownej(void)
{
    return LCD_RGB(90, 74, 50);
}

static LCDColor S21_KolorTekstuWykresu(void)
{
    return LCD_RGB(230, 210, 175);
}

static LCDColor S21_KolorKrzywej(void)
{
    /* Jedno ustawienie koloru obsługuje teraz główne krzywe SWR i S21. */
    switch (CFG_GetParam(CFG_PARAM_KOLOR_KRZYWEJ_SWR))
    {
    case 1U: return LCD_RGB(80, 235, 90);   /* Zielony */
    case 2U: return LCD_RGB(235, 175, 60);  /* Bursztynowy */
    case 3U: return LCD_RGB(235, 80, 70);   /* Czerwony */
    default: return LCD_RGB(240, 235, 220); /* Stalowy */
    }
}

static void S21_ZastosujPaleteWykresu(void)
{
    BackGrColor = S21_KolorTlaWykresu();
    TextColor = S21_KolorTekstuWykresu();
    CurvColor = S21_KolorKrzywej();
}

extern uint8_t AUDIO1;

static void save_snapshot(void);
static void Track_DrawCurve(void);
static void RedrawWindowS21(int justGraphDraw);
static void S21_AktualizujTloRX(uint8_t wymus);

static int redrawRequired, exitScan;

//static const char *modstr = "CEC v." AAVERSION_CECV " ";

//To have the same range to reduce memory usage
extern const char *BSSTR[];
extern const char *BSSTR_HALF[];
extern const uint32_t BSVALUES[];

#define BSSTR_TRACK BSSTR
#define BSSTR_TRACK_HALF BSSTR_HALF
#define BSVALUES_TRACK BSVALUES

static char autoScanFactor = 1;
static uint32_t f1 = 14000000; //Scan range start frequency, in Hz
static uint32_t fstart, freq1;
static BANDSPAN span21 = BS400;
//static char buf[64];
static LCDPoint pt0;
float *valuesmI;
static float *s21_napiecie_v_mv;
static float *s21_napiecie_i_mv;
static float *s21_rozrzut_i_proc;
static uint8_t *s21_punkt_poprawny;
static uint8_t *s21_punkt_zmierzony;
static float *s21_ref_wartosci;
static uint8_t *s21_ref_poprawny;
static S21_ANALIZA_t g_s21_analiza;
static S21_ANALIZA_t g_s21_ref_analiza;
static uint32_t s21_ref_fstart;
static BANDSPAN s21_ref_span;
static uint8_t s21_ref_aktywny;
static uint8_t s21_widok_info;
#define S21_LICZBA_STRON_WYNIKOW 8U

/*
 * S21 korzysta z obu buforow LTDC. Gotowa klatka jest rysowana na warstwie
 * niewidocznej i pokazywana dopiero przy odswiezaniu pionowym. To usuwa
 * miganie wykresu podczas Auto bez spowalniania samego pomiaru RF.
 */
static LCD_BUFOR_KLATKI_t s21_bufor_klatki;

/*
 * Tło odbiornika mierzone z wyłączonym wyjściem TX. To nie jest pełny pomiar
 * izolacji/crosstalku, ale daje praktyczny dolny próg amplitudy odbiornika dla
 * bieżącego zakresu. Wyniku nie odejmujemy od danych - służy wyłącznie do
 * oceny wiarygodności i ostrzegania, że ślad zbliża się do dna toru.
 */
static float s21_tlo_rx_db = NAN;
static float s21_tlo_rx_rozrzut_proc = NAN;
static uint8_t s21_tlo_rx_wazne;
static uint32_t s21_tlo_fstart;
static BANDSPAN s21_tlo_span;
static uint8_t s21_tlo_licznik_odswiezania;
static char s21_meta_kal_txt[20];

#define displayRate 2.9f //Display Zoom
#define displayOffset 190
//static char DisplayType = 0;      //values or valuesmI
static int isMeasured = 0;
static uint32_t cursorPos = WWIDTH / 2;

static uint32_t cursorChangeCount = 0;
static uint32_t autofast = 0;
static uint8_t s21_ostatni_skan_auto = 0U;
//static void Track_DrawRX();
static int trackbeep;

static uint32_t activeLayerS21;
static int cursorVisibleS21;
//static int firstRun;

//=====================================================================
//Menu
//---------------------------------------------------------------------
#define S21_BOTTOM_MENU_TOP 242U
#define S21_BOTTOM_MENU_W 66U
#define S21_BOTTOM_MENU_H 30U
#define S21_BOTTOM_MENU_GAP 3U
#define S21_BOTTOM_MENU_X(pozycja) ((uint16_t)((pozycja) * (S21_BOTTOM_MENU_W + S21_BOTTOM_MENU_GAP)))
#define trackMenu_Length 11

static void TRACK_WypelnijKontrolki(UI_KONTROLKA_t kontrolki[trackMenu_Length])
{
    const char *freq_txt = JEZYK_Wybierz("Częst.", "Freq.", "Freq.", "Част.");
    const char *zrzut_txt = JEZYK_Wybierz("Zrzut", "Snapshot", "Bild", "Снимок");

    /*
     * Siedem widocznych przycisków ma dokładnie tę samą szerokość i wysokość.
     * 7 * 66 px + 6 * 3 px = 480 px, więc pasek wypełnia ekran bez resztek,
     * wyjątków i ręcznie dobieranych szerokości.
     */
    kontrolki[0] = UI_UtworzKontrolke(0, S21_BOTTOM_MENU_X(0U), S21_BOTTOM_MENU_TOP,
        S21_BOTTOM_MENU_W, S21_BOTTOM_MENU_H, JEZYK_Tekst(TEKST_WSTECZ),
        UI_STYL_POWROT, UI_ROLA_TEKSTU_PRZYCISK, true, false);
    kontrolki[1] = UI_UtworzKontrolke(1, S21_BOTTOM_MENU_X(1U), S21_BOTTOM_MENU_TOP,
        S21_BOTTOM_MENU_W, S21_BOTTOM_MENU_H, JEZYK_Tekst(TEKST_SKANUJ),
        UI_STYL_AKCENT, UI_ROLA_TEKSTU_PRZYCISK, true, false);
    kontrolki[2] = UI_UtworzKontrolke(2, S21_BOTTOM_MENU_X(2U), S21_BOTTOM_MENU_TOP,
        S21_BOTTOM_MENU_W, S21_BOTTOM_MENU_H, "-",
        UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);
    kontrolki[3] = UI_UtworzKontrolke(3, S21_BOTTOM_MENU_X(3U), S21_BOTTOM_MENU_TOP,
        S21_BOTTOM_MENU_W, S21_BOTTOM_MENU_H, "+",
        UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);
    kontrolki[4] = UI_UtworzKontrolke(4, S21_BOTTOM_MENU_X(4U), S21_BOTTOM_MENU_TOP,
        S21_BOTTOM_MENU_W, S21_BOTTOM_MENU_H, freq_txt,
        UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);

    /*
     * Pasek 209..240 pozostaje krotkim podsumowaniem pomiaru. Jawny przycisk
     * Wyniki otwiera osiem pelnoekranowych stron; nie ma juz ukrytego
     * przelaczania stron przez dotykanie lewej/prawej polowy paska.
     */
    kontrolki[5] = UI_UtworzKontrolke(5, 394U, 209U, 86U, 31U,
        JEZYK_Wybierz("Wyniki", "Results", "Ergebnisse", "Результаты"),
        isMeasured ? UI_STYL_AKCENT : UI_STYL_NIEAKTYWNY,
        UI_ROLA_TEKSTU_PRZYCISK, isMeasured != 0, false);

    kontrolki[6] = UI_UtworzKontrolke(6, S21_BOTTOM_MENU_X(5U), S21_BOTTOM_MENU_TOP,
        S21_BOTTOM_MENU_W, S21_BOTTOM_MENU_H, zrzut_txt,
        UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);

    /* Obszar samego wykresu ustawia kursor. */
    kontrolki[7] = UI_UtworzKontrolke(7, X0, Y0, WWIDTH + 1, WHEIGHT + 1, "",
        UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);

    kontrolki[8] = UI_UtworzKontrolke(8, S21_BOTTOM_MENU_X(6U), S21_BOTTOM_MENU_TOP,
        S21_BOTTOM_MENU_W, S21_BOTTOM_MENU_H, "Auto",
        autofast ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY,
        UI_ROLA_TEKSTU_PRZYCISK, true, autofast != 0U);

    /* Boczne przyciski kursora są wyłączone; kursor ustawiamy dotykiem wykresu. */
    kontrolki[9] = UI_UtworzKontrolke(9, 0, 0, 0, 0, "", UI_STYL_NIEAKTYWNY,
        UI_ROLA_TEKSTU_PRZYCISK, false, false);
    kontrolki[10] = UI_UtworzKontrolke(10, 0, 0, 0, 0, "", UI_STYL_NIEAKTYWNY,
        UI_ROLA_TEKSTU_PRZYCISK, false, false);
}


void TRACK_Beep(int duration)
{
    if (BeepOn1 == 0)
        return;

    if (trackbeep == 0)
    {
        trackbeep = 1;
        AUDIO1 = 1;
        UB_TIMER2_Init_FRQ(880);
        UB_TIMER2_Start();
        Sleep(100);
        AUDIO1 = 0;
        // UB_TIMER2_Stop();
    }

    if (duration == 1)
        trackbeep = 0;
}

static void DrawAutoText(void)
{
    UI_KONTROLKA_t kontrolki[trackMenu_Length];
    TRACK_WypelnijKontrolki(kontrolki);
    UI_RysujKontrolke(&kontrolki[8]);
}

static void TRACK_DrawFootText(void)
{
    UI_KONTROLKA_t kontrolki[trackMenu_Length];
    static const uint8_t indeksy[] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 8U };
    uint8_t i;

    TRACK_WypelnijKontrolki(kontrolki);
    for (i = 0U; i < sizeof(indeksy) / sizeof(indeksy[0]); ++i)
        UI_RysujKontrolke(&kontrolki[indeksy[i]]);
}

float RawVoltage2;

static float S21_PobierzSkaleDb(void)
{
    const uint32_t skala = CFG_GetParam(CFG_PARAM_S21_SKALA_DB);

    if (skala == 20U || skala == 40U || skala == 60U || skala == 80U)
        return (float)skala;
    return 60.0f;
}

static float S21_StrataDoWyswietlenia(float strata_db)
{
    if (!isfinite(strata_db))
        return strata_db;

    if (CFG_GetParam(CFG_PARAM_S21_NORMALIZUJ) != 0U &&
        g_s21_analiza.poprawny && isfinite(g_s21_analiza.strata_min_db))
    {
        strata_db -= g_s21_analiza.strata_min_db;
        if (strata_db < 0.0f)
            strata_db = 0.0f;
    }
    return strata_db;
}

static float S21_PobierzOrientacyjnyLimitDb(void)
{
    OSL_S21_KOREKCJA_LINIOWOSCI_t kor;
    float limit_db = CFG_GetS21TlumikDb() + 10.0f;

    OSL_S21_PobierzKorekcjeLiniowosci(&kor);
    if (kor.aktywna && kor.dostepna && kor.liczba_wzorcow > 0U)
    {
        /*
         * Po świadomie włączonej weryfikacji 29/40/60 nie udajemy, że
         * ekstrapolacja ponad ostatni wzorzec ma tę samą wiarygodność.
         * Granicą użytkową staje się ostatni rzeczywiście sprawdzony poziom.
         */
        const float zweryfikowany_db = kor.wzorce_db[kor.liczba_wzorcow - 1U];
        if (isfinite(zweryfikowany_db) && zweryfikowany_db > 0.0f)
            limit_db = zweryfikowany_db;
    }

    /*
     * Bez korekcji wielopunktowej drugi punkt kalibracji wyznacza liniowość
     * do wartości wzorca i dopuszczamy jedynie niewielką ekstrapolację +10 dB.
     * Jeżeli znamy tło RX, dodatkowo zostawiamy 6 dB marginesu od podłogi.
     * To wskaźnik użytkowy, nie deklarowana niepewność metrologiczna.
     */
    if (s21_tlo_rx_wazne && isfinite(s21_tlo_rx_db) && s21_tlo_rx_db > 6.0f)
    {
        const float limit_rx = s21_tlo_rx_db - 6.0f;
        if (limit_rx < limit_db)
            limit_db = limit_rx;
    }
    if (limit_db < 0.0f)
        limit_db = 0.0f;
    return limit_db;
}

static void S21_AktualizujMetaKalibracji(void)
{
    KAL_META_DANE_t meta;
    KAL_META_OCENA_t ocena;
    uint8_t ostrzezenie = 0U;

    snprintf(s21_meta_kal_txt, sizeof(s21_meta_kal_txt), "meta--");
    if (!KAL_META_Pobierz(KAL_META_S21, -1, &meta))
        return;

    /*
     * KAL_META_Ocen() sprawdza także CRC pliku na SD. To jest celowo wywołane
     * tylko raz przy wejściu do ekranu S21, a nie przy każdym przesunięciu
     * kursora. UI nie może wykonywać operacji na karcie w ścieżce rysowania.
     */
    KAL_META_Ocen(KAL_META_S21, -1, &meta, &ocena);
    ostrzezenie = (ocena.uwagi & (KAL_META_UWAGA_KONFIGURACJA |
                                  KAL_META_UWAGA_BRAK_PLIKU |
                                  KAL_META_UWAGA_PLIK_ZMIENIONY)) != 0U;

    if (ocena.wiek_dostepny)
        snprintf(s21_meta_kal_txt, sizeof(s21_meta_kal_txt), "age%lud%s",
                 (unsigned long)ocena.wiek_dni, ostrzezenie ? "!" : "");
    else
        snprintf(s21_meta_kal_txt, sizeof(s21_meta_kal_txt),
                 "age--%s", ostrzezenie ? "!" : "");
}

static void S21_FormatujWiekKalibracji(char *bufor, size_t rozmiar)
{
    if (bufor == 0 || rozmiar == 0U)
        return;
    snprintf(bufor, rozmiar, "%s", s21_meta_kal_txt[0] ? s21_meta_kal_txt : "meta--");
}

typedef struct
{
    uint32_t id;
    const char *nazwa;
    uint32_t f_nom_hz;
    uint32_t dut_ohm;
    float bw3_min_hz;
    float bw3_ref_hz;
    float il_max_db;
    uint8_t test_zgodnosci;
} S21_PROFIL_SPEC_t;

typedef enum
{
    S21_SPEC_BRAK = 0,
    S21_SPEC_INFO,
    S21_SPEC_ADAPTER,
    S21_SPEC_ZA_MALO_DANYCH,
    S21_SPEC_OK,
    S21_SPEC_NIEZGODNY
} S21_SPEC_STATUS_t;

static uint8_t S21_PobierzProfilSpec(S21_PROFIL_SPEC_t *profil)
{
    const uint32_t id = CFG_GetParam(CFG_PARAM_S21_PROFIL_SPEC);

    if (profil == 0)
        return 0U;
    memset(profil, 0, sizeof(*profil));
    profil->id = id;

    if (id == 1U)
    {
        profil->nazwa = "SFE 5.5MB";
        profil->f_nom_hz = 5500000U;
        profil->dut_ohm = 600U;
        profil->bw3_min_hz = 150000.0f;
        profil->il_max_db = 6.0f;
        profil->test_zgodnosci = 1U;
        return 1U;
    }
    if (id == 2U)
    {
        /*
         * Wartość 15 kHz pochodzi z opublikowanego pomiaru dokładnie modelu
         * PP-10,7-B2/2. Traktujemy ją jako punkt odniesienia, a nie oficjalny
         * limit katalogowy - stąd brak automatycznego PASS/FAIL.
         */
        profil->nazwa = "OMIG PP-10,7-B2/2";
        profil->f_nom_hz = 10700000U;
        profil->bw3_ref_hz = 15000.0f;
        profil->test_zgodnosci = 0U;
        return 1U;
    }
    return 0U;
}

static S21_SPEC_STATUS_t S21_OcenProfilSpec(const S21_PROFIL_SPEC_t *profil)
{
    if (profil == 0 || profil->id == 0U || !isMeasured || !g_s21_analiza.poprawny)
        return S21_SPEC_BRAK;

    if (!profil->test_zgodnosci)
        return S21_SPEC_INFO;

    if (profil->dut_ohm != 0U && CFG_GetParam(CFG_PARAM_S21_DUT_OHM) != profil->dut_ohm)
        return S21_SPEC_ADAPTER;

    if (g_s21_analiza.typ != S21_TYP_BPF || !g_s21_analiza.ma_lewy_3db ||
        !g_s21_analiza.ma_prawy_3db || g_s21_analiza.punkty_zmierzone_bw3 < 5U)
        return S21_SPEC_ZA_MALO_DANYCH;

    if ((profil->bw3_min_hz > 0.0f && g_s21_analiza.pasmo_3db_hz < profil->bw3_min_hz) ||
        (profil->il_max_db > 0.0f && g_s21_analiza.strata_min_db > profil->il_max_db))
        return S21_SPEC_NIEZGODNY;

    return S21_SPEC_OK;
}

static const char *S21_NazwaStatusuSpec(S21_SPEC_STATUS_t status)
{
    switch (status)
    {
    case S21_SPEC_OK: return "OK";
    case S21_SPEC_NIEZGODNY: return "NIE";
    case S21_SPEC_ADAPTER: return "ADAPT";
    case S21_SPEC_ZA_MALO_DANYCH: return "DANE";
    case S21_SPEC_INFO: return "REF";
    default: return "--";
    }
}

typedef struct
{
    uint8_t ma_p1;
    uint8_t ma_p2;
    uint8_t ma_dolek;
    uint16_t indeks_p1;
    uint16_t indeks_p2;
    uint16_t indeks_dolka;
    float strata_p1_db;
    float strata_p2_db;
    float strata_dolka_db;
} S21_EKSTREMA_LOKALNE_t;

static float S21_WartoscLokalnieWygladzona(uint16_t indeks)
{
    static const uint8_t wagi[5] = { 1U, 2U, 3U, 2U, 1U };
    float suma = 0.0f;
    uint16_t suma_wag = 0U;
    int32_t przesuniecie;

    if (valuesmI == 0 || s21_punkt_poprawny == 0)
        return NAN;

    for (przesuniecie = -2; przesuniecie <= 2; ++przesuniecie)
    {
        const int32_t j = (int32_t)indeks + przesuniecie;
        const uint8_t waga = wagi[przesuniecie + 2];
        if (j < 0 || j > (int32_t)WWIDTH)
            continue;
        if (!s21_punkt_poprawny[j] || !isfinite(valuesmI[j]))
            continue;
        suma += valuesmI[j] * (float)waga;
        suma_wag = (uint16_t)(suma_wag + waga);
    }

    return suma_wag != 0U ? suma / (float)suma_wag : NAN;
}

static uint32_t S21_CzestotliwoscDlaIndeksu(uint16_t indeks)
{
    const uint64_t span_hz = (uint64_t)BSVALUES_TRACK[span21] * 1000ULL;
    return fstart + (uint32_t)((span_hz * (uint64_t)indeks) / (uint64_t)WWIDTH);
}

static uint8_t S21_ZnajdzEkstremaLokalne(S21_EKSTREMA_LOKALNE_t *wynik)
{
    typedef struct
    {
        uint16_t indeks;
        float strata_db;
    } KANDYDAT_t;

    KANDYDAT_t kandydaci[12];
    uint8_t liczba_kandydatow = 0U;
    uint16_t i;
    const uint16_t promien = 12U;
    const uint16_t min_odstep = (uint16_t)((WWIDTH / 20U) > 10U ? (WWIDTH / 20U) : 10U);
    const float min_prominencja_db = 0.15f;

    if (wynik == 0)
        return 0U;
    memset(wynik, 0, sizeof(*wynik));

    /*
     * Analiza P1/P2/dołka jest wyłącznie warstwą prezentacji.
     * W czasie ciągłego Auto nie wykonujemy jej, aby nie dokładać obliczeń
     * do krytycznej pętli pomiarowej ani nie wpływać na responsywność dotyku.
     */
    if (!isMeasured || valuesmI == 0 || s21_punkt_poprawny == 0)
        return 0U;

    for (i = 3U; i + 3U <= WWIDTH; ++i)
    {
        const float lewo = S21_WartoscLokalnieWygladzona((uint16_t)(i - 1U));
        const float srodek = S21_WartoscLokalnieWygladzona(i);
        const float prawo = S21_WartoscLokalnieWygladzona((uint16_t)(i + 1U));
        float maksimum_lewo = -INFINITY;
        float maksimum_prawo = -INFINITY;
        float prominencja;
        uint16_t j;

        if (!isfinite(lewo) || !isfinite(srodek) || !isfinite(prawo))
            continue;
        if (!((srodek <= lewo && srodek < prawo) ||
              (srodek < lewo && srodek <= prawo)))
            continue;

        /*
         * Maksimum transmisji jest minimum dodatniej straty. Żeby pojedynczy
         * pik szumu nie został nazwany maksimum lokalnym, wymagamy niewielkiej
         * prominencji po obu stronach. To kryterium służy wyłącznie opisowi
         * ekranu i nigdy nie steruje automatycznym zakresem pomiaru.
         */
        for (j = i > promien ? (uint16_t)(i - promien) : 0U; j + 1U < i; ++j)
        {
            const float v = S21_WartoscLokalnieWygladzona(j);
            if (isfinite(v) && v > maksimum_lewo)
                maksimum_lewo = v;
        }
        for (j = (uint16_t)(i + 2U); j <= WWIDTH && j <= (uint16_t)(i + promien); ++j)
        {
            const float v = S21_WartoscLokalnieWygladzona(j);
            if (isfinite(v) && v > maksimum_prawo)
                maksimum_prawo = v;
        }
        if (!isfinite(maksimum_lewo) || !isfinite(maksimum_prawo))
            continue;

        prominencja = fminf(maksimum_lewo - srodek, maksimum_prawo - srodek);
        if (prominencja < min_prominencja_db)
            continue;
        if (g_s21_analiza.poprawny && srodek > g_s21_analiza.strata_min_db + 8.0f)
            continue;

        if (liczba_kandydatow < (uint8_t)(sizeof(kandydaci) / sizeof(kandydaci[0])))
        {
            kandydaci[liczba_kandydatow].indeks = i;
            kandydaci[liczba_kandydatow].strata_db = srodek;
            ++liczba_kandydatow;
        }
    }

    if (liczba_kandydatow == 0U)
        return 0U;

    /* Najsilniejsze maksimum transmisji: najmniejsza strata. */
    {
        uint8_t najlepszy = 0U;
        uint8_t k;
        for (k = 1U; k < liczba_kandydatow; ++k)
        {
            if (kandydaci[k].strata_db < kandydaci[najlepszy].strata_db)
                najlepszy = k;
        }
        wynik->ma_p1 = 1U;
        wynik->indeks_p1 = kandydaci[najlepszy].indeks;
        wynik->strata_p1_db = kandydaci[najlepszy].strata_db;
    }

    /* Drugie maksimum musi być rozdzielone w osi częstotliwości. */
    {
        uint8_t znaleziono = 0U;
        uint8_t najlepszy = 0U;
        uint8_t k;
        for (k = 0U; k < liczba_kandydatow; ++k)
        {
            const uint16_t a = kandydaci[k].indeks;
            const uint16_t b = wynik->indeks_p1;
            const uint16_t odstep = a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
            if (odstep < min_odstep)
                continue;
            if (!znaleziono || kandydaci[k].strata_db < kandydaci[najlepszy].strata_db)
            {
                najlepszy = k;
                znaleziono = 1U;
            }
        }
        if (znaleziono)
        {
            wynik->ma_p2 = 1U;
            wynik->indeks_p2 = kandydaci[najlepszy].indeks;
            wynik->strata_p2_db = kandydaci[najlepszy].strata_db;
        }
    }

    if (!wynik->ma_p2)
        return 1U;

    /* Uporządkowanie P1/P2 według częstotliwości ułatwia czytanie ekranu. */
    if (wynik->indeks_p2 < wynik->indeks_p1)
    {
        const uint16_t indeks_tmp = wynik->indeks_p1;
        const float strata_tmp = wynik->strata_p1_db;
        wynik->indeks_p1 = wynik->indeks_p2;
        wynik->strata_p1_db = wynik->strata_p2_db;
        wynik->indeks_p2 = indeks_tmp;
        wynik->strata_p2_db = strata_tmp;
    }

    /* Dołek to największa strata pomiędzy dwoma wykrytymi maksimami. */
    {
        float strata_dolka = -INFINITY;
        uint16_t indeks_dolka = wynik->indeks_p1;
        for (i = (uint16_t)(wynik->indeks_p1 + 2U); i + 2U < wynik->indeks_p2; ++i)
        {
            const float v = S21_WartoscLokalnieWygladzona(i);
            if (isfinite(v) && v > strata_dolka)
            {
                strata_dolka = v;
                indeks_dolka = i;
            }
        }
        if (isfinite(strata_dolka) &&
            strata_dolka >= fmaxf(wynik->strata_p1_db, wynik->strata_p2_db) + 0.10f)
        {
            wynik->ma_dolek = 1U;
            wynik->indeks_dolka = indeks_dolka;
            wynik->strata_dolka_db = strata_dolka;
        }
    }

    return 1U;
}

static const char *S21_StatusJakosciSkrot(OSL_S21_STATUS_JAKOSCI_t status)
{
    switch (status)
    {
    case OSL_S21_JAKOSC_DOBRY: return "OK";
    case OSL_S21_JAKOSC_SLABY: return "SL";
    case OSL_S21_JAKOSC_INTERPOLOWANY: return "INT";
    case OSL_S21_JAKOSC_SZACOWANY: return "EST";
    default: return "--";
    }
}

static const char *S21_TekstJakosciProbkowania(uint16_t punkty_bw3)
{
    /*
     * Nie alarmujemy binarnym komunikatem „za mało punktów”. Liczba próbek
     * pozostaje widoczna w wierszu BW3, a użytkownik dostaje spokojną,
     * stopniowaną ocenę. Progi opisują przydatność odczytu, nie formalną
     * niepewność metrologiczną.
     */
    if (punkty_bw3 == 0U)
        return JEZYK_Wybierz("brak BW3", "no BW3", "kein BW3", "нет BW3");
    if (punkty_bw3 < 4U)
        return JEZYK_Wybierz("orientacyjny", "rough", "grob", "ориентир.");
    if (punkty_bw3 < 8U)
        return JEZYK_Wybierz("wstępny", "preliminary", "vorläufig", "предварит.");
    if (punkty_bw3 < 16U)
        return JEZYK_Wybierz("użyteczny", "usable", "brauchbar", "пригодно");
    if (punkty_bw3 < 32U)
        return JEZYK_Wybierz("dobry", "good", "gut", "хорошо");
    return JEZYK_Wybierz("bardzo dobry", "very good", "sehr gut", "очень хорошо");
}


static void DrawMeasuredValues(void)
{
    uint32_t f_cursor;
    char linia1[128];
    char linia2[128];
    UI_KONTROLKA_t kontrolki[trackMenu_Length];

    LCD_FillRect(LCD_MakePoint(0U, 209U), LCD_MakePoint(479U, 240U), BackGrColor);

    if (!isMeasured || !g_s21_analiza.poprawny)
    {
        snprintf(linia1, sizeof(linia1), "%s",
                 JEZYK_Wybierz("Pomiar: jeden skan. Auto: ciągły skan w stałym zakresie.",
                               "Measure: one scan. Auto: continuous scan at fixed span.",
                               "Messung: ein Scan. Auto: Dauerscan im festen Bereich.",
                               "Измерение: один скан. Auto: непрерывно, диапазон фиксирован."));
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 4U, 217U, linia1);
        return;
    }

    f_cursor = fstart + (uint32_t)((cursorPos * BSVALUES_TRACK[span21] * 1000.0f) / WWIDTH);
    if (f_cursor > CFG_GetParam(CFG_PARAM_BAND_FMAX))
        f_cursor = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    snprintf(linia1, sizeof(linia1),
             "MAX %.5fMHz %.2fdB  MIN %.5fMHz %.2fdB",
             (double)g_s21_analiza.f_min_hz / 1000000.0,
             (double)(-g_s21_analiza.strata_min_db),
             (double)g_s21_analiza.f_max_hz / 1000000.0,
             (double)(-g_s21_analiza.strata_max_db));

    if (s21_punkt_poprawny != 0 && s21_punkt_poprawny[cursorPos] &&
        isfinite(valuesmI[cursorPos]))
    {
        snprintf(linia2, sizeof(linia2), "M4 %.5fMHz %.2fdB",
                 (double)f_cursor / 1000000.0,
                 (double)(-valuesmI[cursorPos]));
    }
    else
    {
        snprintf(linia2, sizeof(linia2), "M4 %.5fMHz --",
                 (double)f_cursor / 1000000.0);
    }

    /* Rezerwujemy prawa czesc paska na jawny przycisk Wyniki. */
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_AKCENT), BackGrColor, 4U, 210U, linia1);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 4U, 224U, linia2);

    TRACK_WypelnijKontrolki(kontrolki);
    UI_RysujKontrolke(&kontrolki[5]);
}

#define S21_WYNIKI_BACK_X 0U
#define S21_WYNIKI_BACK_Y 209U
#define S21_WYNIKI_BACK_W 92U
#define S21_WYNIKI_BACK_H 63U
#define S21_WYNIKI_NAV_X 96U
#define S21_WYNIKI_NAV_Y 209U
#define S21_WYNIKI_NAV_W 384U
#define S21_WYNIKI_NAV_H 63U
#define S21_WYNIKI_WIERSZ_X 14U
#define S21_WYNIKI_WIERSZ_W 452U
#define S21_WYNIKI_WIERSZ_H 31U
#define S21_WYNIKI_WIERSZ_Y0 40U
#define S21_WYNIKI_WIERSZ_KROK 33U

static void S21_RysujWierszWynikow(uint8_t numer, const char *etykieta,
                                    const char *wartosc, uint8_t duza_wartosc)
{
    const uint16_t y = (uint16_t)(S21_WYNIKI_WIERSZ_Y0 + numer * S21_WYNIKI_WIERSZ_KROK);
    const uint16_t x = S21_WYNIKI_WIERSZ_X;
    const uint16_t w = S21_WYNIKI_WIERSZ_W;
    const uint16_t h = S21_WYNIKI_WIERSZ_H;
    const uint16_t x_wartosci = 170U;
    const LCDColor tlo = UI_KolorTlaPrzycisku(UI_STYL_NORMALNY);
    const LCDColor ramka = UI_KolorRamki(UI_STYL_NORMALNY);
    const LCDColor tekst = duza_wartosc ? UI_KolorTekstu(UI_STYL_AKCENT) : TextColor;
    const uint32_t font_wartosci = duza_wartosc ? FONT_FRANBIG : FONT_FRAN;
    int y_etykiety;
    int y_wartosci;

    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U)), ramka);

    y_etykiety = (int)y + ((int)h - (int)FONT_GetHeight(FONT_FRAN)) / 2;
    y_wartosci = (int)y + ((int)h - (int)FONT_GetHeight(font_wartosci)) / 2;
    if (y_etykiety < (int)y + 1) y_etykiety = (int)y + 1;
    if (y_wartosci < (int)y + 1) y_wartosci = (int)y + 1;

    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo,
               (uint16_t)(x + 14U), (uint16_t)y_etykiety, etykieta != 0 ? etykieta : "");
    FONT_Write(font_wartosci, tekst, tlo, x_wartosci, (uint16_t)y_wartosci,
               wartosc != 0 ? wartosc : "--");
}

static void S21_UtworzOpcjeStron(UI_METODA_OPCJA_t opcje[S21_LICZBA_STRON_WYNIKOW])
{
    opcje[0] = (UI_METODA_OPCJA_t){JEZYK_Wybierz("1/8 Podsumowanie", "1/8 Summary", "1/8 Übersicht", "1/8 Итоги"), 0, true};
    opcje[1] = (UI_METODA_OPCJA_t){JEZYK_Wybierz("2/8 Pasmo -3 dB", "2/8 -3 dB band", "2/8 -3-dB-Band", "2/8 Полоса -3 дБ"), 0, true};
    opcje[2] = (UI_METODA_OPCJA_t){JEZYK_Wybierz("3/8 Szerokości", "3/8 Bandwidths", "3/8 Bandbreiten", "3/8 Полосы"), 0, true};
    opcje[3] = (UI_METODA_OPCJA_t){JEZYK_Wybierz("4/8 Selektywność", "4/8 Selectivity", "4/8 Selektivität", "4/8 Селективность"), 0, true};
    opcje[4] = (UI_METODA_OPCJA_t){JEZYK_Wybierz("5/8 Maksima P1/P2", "5/8 Peaks P1/P2", "5/8 Maxima P1/P2", "5/8 Максимумы P1/P2"), 0, true};
    opcje[5] = (UI_METODA_OPCJA_t){JEZYK_Wybierz("6/8 Dołek D", "6/8 Dip D", "6/8 Senke D", "6/8 Провал D"), 0, true};
    opcje[6] = (UI_METODA_OPCJA_t){JEZYK_Wybierz("7/8 Jakość pomiaru", "7/8 Measurement quality", "7/8 Messqualität", "7/8 Качество измерения"), 0, true};
    opcje[7] = (UI_METODA_OPCJA_t){JEZYK_Wybierz("8/8 Kalibracja", "8/8 Calibration", "8/8 Kalibrierung", "8/8 Калибровка"), 0, true};
}

static void S21_FormatujPasmo(char *bufor, size_t rozmiar, uint8_t ma_pasmo,
                              float f1_hz, float f2_hz, float bw_hz)
{
    if (bufor == 0 || rozmiar == 0U)
        return;

    if (!ma_pasmo || !isfinite(f1_hz) || !isfinite(f2_hz) || !isfinite(bw_hz))
    {
        snprintf(bufor, rozmiar, "%s",
                 JEZYK_Wybierz("brak obu przecięć", "crossings unavailable",
                               "Schnittpunkte fehlen", "нет обеих точек"));
        return;
    }

    snprintf(bufor, rozmiar, "L %.5f  H %.5f  BW %.2f kHz",
             (double)f1_hz / 1000000.0,
             (double)f2_hz / 1000000.0,
             (double)bw_hz / 1000.0);
}

static void S21_RysujStroneWynikow(uint8_t strona)
{
    char w0[128];
    char w1[128];
    char w2[128];
    char w3[128];
    char w4[128];
    UI_METODA_OPCJA_t opcje[S21_LICZBA_STRON_WYNIKOW];
    const uint32_t impedancja_dut = CFG_GetParam(CFG_PARAM_S21_DUT_OHM);
    const float tlumik_kal_db = CFG_GetS21TlumikDb();
    uint32_t f_cursor = fstart + (uint32_t)((cursorPos * BSVALUES_TRACK[span21] * 1000.0f) / WWIDTH);
    uint8_t pewnosc = 0U;
    OSL_S21_STATUS_JAKOSCI_t status_jakosci = OSL_S21_JAKOSC_BRAK;

    if (strona >= S21_LICZBA_STRON_WYNIKOW)
        strona = 0U;
    if (f_cursor > CFG_GetParam(CFG_PARAM_BAND_FMAX))
        f_cursor = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    w0[0] = w1[0] = w2[0] = w3[0] = w4[0] = '\0';
    LCD_FillAll(BackGrColor);
    UI_RysujPasekGorny(JEZYK_Wybierz("S21 - podsumowanie", "S21 - results",
                                      "S21 - Ergebnisse", "S21 - результаты"),
                        false, false, 0);

    if (!isMeasured || !g_s21_analiza.poprawny)
    {
        S21_RysujWierszWynikow(0U, JEZYK_Wybierz("Stan", "Status", "Status", "Состояние"),
                               JEZYK_Wybierz("Brak poprawnego pomiaru", "No valid measurement",
                                             "Keine gültige Messung", "Нет корректного измерения"), 1U);
    }
    else if (strona == 0U)
    {
        const float dynamika = g_s21_analiza.strata_max_db - g_s21_analiza.strata_min_db;
        snprintf(w0, sizeof(w0), "%.6f MHz   %.3f dB",
                 (double)g_s21_analiza.f_min_hz / 1000000.0,
                 (double)(-g_s21_analiza.strata_min_db));
        snprintf(w1, sizeof(w1), "%.6f MHz   %.3f dB",
                 (double)g_s21_analiza.f_max_hz / 1000000.0,
                 (double)(-g_s21_analiza.strata_max_db));
        snprintf(w2, sizeof(w2), "%.2f dB", (double)dynamika);
        if (s21_punkt_poprawny != 0 && s21_punkt_poprawny[cursorPos] && isfinite(valuesmI[cursorPos]))
            snprintf(w3, sizeof(w3), "%.6f MHz   %.3f dB", (double)f_cursor / 1000000.0,
                     (double)(-valuesmI[cursorPos]));
        else
            snprintf(w3, sizeof(w3), "%.6f MHz   --", (double)f_cursor / 1000000.0);
        snprintf(w4, sizeof(w4), "%lu om   KAL %.0f dB",
                 (unsigned long)impedancja_dut, (double)tlumik_kal_db);

        S21_RysujWierszWynikow(0U, "MAX |S21|", w0, 0U);
        S21_RysujWierszWynikow(1U, "MIN |S21|", w1, 0U);
        S21_RysujWierszWynikow(2U, JEZYK_Wybierz("Dynamika", "Dynamic range", "Dynamik", "Динамика"), w2, 1U);
        S21_RysujWierszWynikow(3U, "M4", w3, 0U);
        S21_RysujWierszWynikow(4U, "DUT / KAL", w4, 1U);
    }
    else if (strona == 1U)
    {
        S21_EKSTREMA_LOKALNE_t e;
        uint8_t bw3_dotyczy_piku = 0U;

        (void)S21_ZnajdzEkstremaLokalne(&e);
        if (e.ma_p1 && e.ma_p2 && e.ma_dolek)
        {
            const float glebokosc_wzgledem_slabszego_piku =
                e.strata_dolka_db - fmaxf(e.strata_p1_db, e.strata_p2_db);
            bw3_dotyczy_piku = glebokosc_wzgledem_slabszego_piku >= 1.0f ? 1U : 0U;
        }

        snprintf(w0, sizeof(w0), "%.6f MHz   %.3f dB",
                 (double)g_s21_analiza.f_min_hz / 1000000.0,
                 (double)(-g_s21_analiza.strata_min_db));
        snprintf(w1, sizeof(w1), "%.3f dB", (double)(-g_s21_analiza.prog_3db_db));
        if (g_s21_analiza.ma_lewy_3db)
            snprintf(w2, sizeof(w2), "fL %.6f MHz", (double)g_s21_analiza.f1_3db_hz / 1000000.0);
        else
            snprintf(w2, sizeof(w2), "fL --");
        if (g_s21_analiza.ma_prawy_3db)
            snprintf(w3, sizeof(w3), "fH %.6f MHz", (double)g_s21_analiza.f2_3db_hz / 1000000.0);
        else
            snprintf(w3, sizeof(w3), "fH --");
        if (g_s21_analiza.ma_lewy_3db && g_s21_analiza.ma_prawy_3db && g_s21_analiza.pasmo_3db_hz > 0.0f)
            snprintf(w4, sizeof(w4), "BW3 %.2f kHz   f0 %.6f MHz   Q %.1f",
                     (double)g_s21_analiza.pasmo_3db_hz / 1000.0,
                     (double)g_s21_analiza.f_srodek_3db_hz / 1000000.0,
                     (double)g_s21_analiza.q_3db);
        else
            snprintf(w4, sizeof(w4), "%s",
                     JEZYK_Wybierz("BW3/Q: brak pełnego przecięcia", "BW3/Q: incomplete crossing",
                                   "BW3/Q: Schnitt unvollständig", "BW3/Q: неполное пересечение"));
        S21_RysujWierszWynikow(0U, "MAX |S21|", w0, 0U);
        S21_RysujWierszWynikow(1U, JEZYK_Wybierz("Próg -3 dB", "-3 dB threshold", "-3-dB-Schwelle", "Порог -3 дБ"), w1, 0U);
        S21_RysujWierszWynikow(2U, "-3 dB L", w2, 0U);
        S21_RysujWierszWynikow(3U, "-3 dB H", w3, 0U);
        S21_RysujWierszWynikow(4U,
            bw3_dotyczy_piku
                ? JEZYK_Wybierz("BW3 piku / f0 / Q", "Peak BW3 / f0 / Q",
                                "Peak-BW3 / f0 / Q", "BW3 пика / f0 / Q")
                : "BW3 / f0 / Q",
            w4, 1U);
    }
    else if (strona == 2U)
    {
        S21_FormatujPasmo(w0, sizeof(w0), g_s21_analiza.ma_6db, g_s21_analiza.f1_6db_hz, g_s21_analiza.f2_6db_hz, g_s21_analiza.pasmo_6db_hz);
        S21_FormatujPasmo(w1, sizeof(w1), g_s21_analiza.ma_10db, g_s21_analiza.f1_10db_hz, g_s21_analiza.f2_10db_hz, g_s21_analiza.pasmo_10db_hz);
        S21_FormatujPasmo(w2, sizeof(w2), g_s21_analiza.ma_20db, g_s21_analiza.f1_20db_hz, g_s21_analiza.f2_20db_hz, g_s21_analiza.pasmo_20db_hz);
        S21_FormatujPasmo(w3, sizeof(w3), g_s21_analiza.ma_40db, g_s21_analiza.f1_40db_hz, g_s21_analiza.f2_40db_hz, g_s21_analiza.pasmo_40db_hz);
        S21_FormatujPasmo(w4, sizeof(w4), g_s21_analiza.ma_60db, g_s21_analiza.f1_60db_hz, g_s21_analiza.f2_60db_hz, g_s21_analiza.pasmo_60db_hz);
        S21_RysujWierszWynikow(0U, "-6 dB", w0, 0U);
        S21_RysujWierszWynikow(1U, "-10 dB", w1, 0U);
        S21_RysujWierszWynikow(2U, "-20 dB", w2, 0U);
        S21_RysujWierszWynikow(3U, "-40 dB", w3, 0U);
        S21_RysujWierszWynikow(4U, "-60 dB", w4, 0U);
    }
    else if (strona == 3U)
    {
        snprintf(w0, sizeof(w0), "20/3 %s%.2f   40/3 %s%.2f   60/3 %s%.2f",
                 g_s21_analiza.ma_20db ? "" : "--", g_s21_analiza.ma_20db ? (double)g_s21_analiza.shape_20_3 : 0.0,
                 g_s21_analiza.ma_40db ? "" : "--", g_s21_analiza.ma_40db ? (double)g_s21_analiza.shape_40_3 : 0.0,
                 g_s21_analiza.ma_60db ? "" : "--", g_s21_analiza.ma_60db ? (double)g_s21_analiza.shape_60_3 : 0.0);
        snprintf(w1, sizeof(w1), "40/6 %s%.2f   60/6 %s%.2f",
                 (g_s21_analiza.ma_40db && g_s21_analiza.ma_6db) ? "" : "--",
                 (g_s21_analiza.ma_40db && g_s21_analiza.ma_6db) ? (double)g_s21_analiza.shape_40_6 : 0.0,
                 (g_s21_analiza.ma_60db && g_s21_analiza.ma_6db) ? "" : "--",
                 (g_s21_analiza.ma_60db && g_s21_analiza.ma_6db) ? (double)g_s21_analiza.shape_60_6 : 0.0);
        snprintf(w2, sizeof(w2), "%.2f dB", (double)g_s21_analiza.zafalowanie_srodka_db);
        snprintf(w3, sizeof(w3), "asym %.1f%%   L %.0f dB/oct   P %.0f dB/oct",
                 (double)g_s21_analiza.asymetria_3db_proc,
                 (double)g_s21_analiza.nachylenie_lewe_db_na_oktawe,
                 (double)g_s21_analiza.nachylenie_prawe_db_na_oktawe);
        snprintf(w4, sizeof(w4), "brzegi %.1f / %.1f dB   stop %.1f dB",
                 (double)(-g_s21_analiza.strata_lewego_brzegu_db),
                 (double)(-g_s21_analiza.strata_prawego_brzegu_db),
                 (double)g_s21_analiza.tlumienie_stop_db);
        S21_RysujWierszWynikow(0U, "Shape factor", w0, 0U);
        S21_RysujWierszWynikow(1U, "Shape 40/6 60/6", w1, 0U);
        S21_RysujWierszWynikow(2U, JEZYK_Wybierz("Zafalowanie", "Ripple", "Welligkeit", "Неравномерность"), w2, 1U);
        S21_RysujWierszWynikow(3U, JEZYK_Wybierz("Asym. / zbocza", "Asym. / slopes", "Asym. / Flanken", "Асимм. / склоны"), w3, 0U);
        S21_RysujWierszWynikow(4U, JEZYK_Wybierz("Brzegi / stop", "Edges / stop", "Ränder / Sperre", "Края / подавление"), w4, 0U);
    }
    else if (strona == 4U || strona == 5U)
    {
        S21_EKSTREMA_LOKALNE_t e;
        (void)S21_ZnajdzEkstremaLokalne(&e);
        if (strona == 4U)
        {
            if (e.ma_p1)
                snprintf(w0, sizeof(w0), "%.6f MHz   %.3f dB", (double)S21_CzestotliwoscDlaIndeksu(e.indeks_p1) / 1000000.0, (double)(-e.strata_p1_db));
            else snprintf(w0, sizeof(w0), "--");
            if (e.ma_p2)
                snprintf(w1, sizeof(w1), "%.6f MHz   %.3f dB", (double)S21_CzestotliwoscDlaIndeksu(e.indeks_p2) / 1000000.0, (double)(-e.strata_p2_db));
            else snprintf(w1, sizeof(w1), "--");
            if (e.ma_p1 && e.ma_p2)
            {
                snprintf(w2, sizeof(w2), "%.2f kHz", ((double)S21_CzestotliwoscDlaIndeksu(e.indeks_p2) - (double)S21_CzestotliwoscDlaIndeksu(e.indeks_p1)) / 1000.0);
                snprintf(w3, sizeof(w3), "%.2f dB", (double)fabsf(e.strata_p1_db - e.strata_p2_db));
            }
            else { snprintf(w2, sizeof(w2), "--"); snprintf(w3, sizeof(w3), "--"); }
            snprintf(w4, sizeof(w4), "%s", JEZYK_Wybierz("Maksima lokalne nie sterują Auto", "Local peaks do not control Auto", "Lokale Maxima steuern Auto nicht", "Локальные максимумы не управляют Auto"));
            S21_RysujWierszWynikow(0U, "P1", w0, 0U);
            S21_RysujWierszWynikow(1U, "P2", w1, 0U);
            S21_RysujWierszWynikow(2U, "dF", w2, 1U);
            S21_RysujWierszWynikow(3U, "dA", w3, 1U);
            S21_RysujWierszWynikow(4U, JEZYK_Wybierz("Znaczenie", "Meaning", "Bedeutung", "Значение"), w4, 0U);
        }
        else
        {
            if (e.ma_dolek)
            {
                snprintf(w0, sizeof(w0), "%.6f MHz   %.3f dB", (double)S21_CzestotliwoscDlaIndeksu(e.indeks_dolka) / 1000000.0, (double)(-e.strata_dolka_db));
                snprintf(w1, sizeof(w1), "%.2f dB", (double)(e.strata_dolka_db - e.strata_p1_db));
                snprintf(w2, sizeof(w2), "%.2f dB", (double)(e.strata_dolka_db - e.strata_p2_db));
                snprintf(w3, sizeof(w3), "P1 %.3f dB   P2 %.3f dB", (double)(-e.strata_p1_db), (double)(-e.strata_p2_db));
                snprintf(w4, sizeof(w4), "%s", JEZYK_Wybierz("D opisuje ślad; nie zmienia zakresu Auto", "D describes the trace; it does not alter Auto span", "D beschreibt die Kurve; Auto bleibt unverändert", "D описывает кривую; диапазон Auto не меняется"));
            }
            else
            {
                snprintf(w0, sizeof(w0), "%s", JEZYK_Wybierz("brak istotnego dołka", "no significant dip", "keine deutliche Senke", "нет выраженного провала"));
                snprintf(w1, sizeof(w1), "--"); snprintf(w2, sizeof(w2), "--"); snprintf(w3, sizeof(w3), "--");
                snprintf(w4, sizeof(w4), "%s", JEZYK_Wybierz("D wymaga dwóch rozdzielonych maksimów P1/P2", "D requires two separated peaks P1/P2", "D benötigt zwei getrennte Maxima P1/P2", "D требует двух раздельных максимумов P1/P2"));
            }
            S21_RysujWierszWynikow(0U, "D", w0, 0U);
            S21_RysujWierszWynikow(1U, "D - P1", w1, 1U);
            S21_RysujWierszWynikow(2U, "D - P2", w2, 1U);
            S21_RysujWierszWynikow(3U, "P1 / P2", w3, 0U);
            S21_RysujWierszWynikow(4U, JEZYK_Wybierz("Znaczenie", "Meaning", "Bedeutung", "Значение"), w4, 0U);
        }
    }
    else if (strona == 6U)
    {
        const float rozrzut_i = (s21_rozrzut_i_proc != 0 && isfinite(s21_rozrzut_i_proc[cursorPos])) ? s21_rozrzut_i_proc[cursorPos] : NAN;
        (void)OSL_S21_PobierzPewnosc(f_cursor, &pewnosc, &status_jakosci);
        if (s21_ostatni_skan_auto)
            snprintf(w0, sizeof(w0), "AUTO x%u   krok %.0f Hz   real %u/%u",
                     (unsigned int)autoScanFactor, (double)g_s21_analiza.krok_hz,
                     (unsigned int)g_s21_analiza.punkty_zmierzone,
                     (unsigned int)g_s21_analiza.punkty_poprawne);
        else
            snprintf(w0, sizeof(w0), "%s   krok %.0f Hz   real %u/%u",
                     JEZYK_Wybierz("POMIAR", "MEASURE", "MESSUNG", "ИЗМЕР."),
                     (double)g_s21_analiza.krok_hz,
                     (unsigned int)g_s21_analiza.punkty_zmierzone,
                     (unsigned int)g_s21_analiza.punkty_poprawne);
        snprintf(w1, sizeof(w1), "BW3 %u pkt   rozdz. ~%.0f Hz",
                 (unsigned int)g_s21_analiza.punkty_zmierzone_bw3,
                 (double)g_s21_analiza.rozdzielczosc_bw3_hz);
        snprintf(w2, sizeof(w2), "%u%%   %s",
                 (unsigned int)g_s21_analiza.jakosc_probkowania_proc,
                 s21_ostatni_skan_auto
                    ? JEZYK_Wybierz("podgląd AUTO", "AUTO preview", "AUTO-Vorschau", "просмотр AUTO")
                    : S21_TekstJakosciProbkowania(g_s21_analiza.punkty_zmierzone_bw3));
        if (s21_punkt_poprawny != 0 && s21_punkt_poprawny[cursorPos] && isfinite(valuesmI[cursorPos]))
            snprintf(w3, sizeof(w3), "%.6f MHz  %.3f dB  J%u%% %s",
                     (double)f_cursor / 1000000.0, (double)(-valuesmI[cursorPos]),
                     (unsigned int)pewnosc, S21_StatusJakosciSkrot(status_jakosci));
        else
            snprintf(w3, sizeof(w3), "%.6f MHz  --", (double)f_cursor / 1000000.0);
        if (CFG_GetParam(CFG_PARAM_DIAGNOSTYKA_VI) != 0U && s21_napiecie_v_mv != 0 && s21_napiecie_i_mv != 0 &&
            isfinite(s21_napiecie_v_mv[cursorPos]) && isfinite(s21_napiecie_i_mv[cursorPos]))
            snprintf(w4, sizeof(w4), "V %.2f mV  I %.2f mV  R%s%.1f%%",
                     (double)s21_napiecie_v_mv[cursorPos], (double)s21_napiecie_i_mv[cursorPos],
                     isfinite(rozrzut_i) ? "" : "--", isfinite(rozrzut_i) ? (double)rozrzut_i : 0.0);
        else if (s21_tlo_rx_wazne && isfinite(s21_tlo_rx_db))
            snprintf(w4, sizeof(w4),
                     JEZYK_Wybierz("tło RX %.1f dB   pewny ~%.0f dB",
                                   "RX floor %.1f dB   trusted ~%.0f dB",
                                   "RX-Boden %.1f dB   sicher ~%.0f dB",
                                   "фон RX %.1f дБ   надёжн. ~%.0f дБ"),
                     (double)(-s21_tlo_rx_db), (double)S21_PobierzOrientacyjnyLimitDb());
        else
            snprintf(w4, sizeof(w4), "V/I OFF   tło RX %s", CFG_GetParam(CFG_PARAM_S21_TLO_RX) ? "--" : "OFF");
        S21_RysujWierszWynikow(0U, JEZYK_Wybierz("Próbkowanie", "Sampling", "Abtastung", "Дискретизация"), w0, 0U);
        S21_RysujWierszWynikow(1U, "BW3", w1, 0U);
        S21_RysujWierszWynikow(2U, JEZYK_Wybierz("Jakość", "Quality", "Qualität", "Качество"), w2, 1U);
        S21_RysujWierszWynikow(3U, "M4", w3, 0U);
        S21_RysujWierszWynikow(4U, "V / I / RX", w4, 0U);
    }
    else
    {
        OSL_S21_LINIOWOSC_t lin;
        OSL_S21_KOREKCJA_LINIOWOSCI_t kor;
        S21_PROFIL_SPEC_t profil;
        char wiek_kal[20];
        float delta_ref_db = NAN;
        const uint8_t ref_ten_sam_zakres = (uint8_t)(s21_ref_aktywny && s21_ref_fstart == fstart && s21_ref_span == span21);
        const uint8_t ma_profil = S21_PobierzProfilSpec(&profil);

        OSL_S21_PobierzOceneLiniowosci(&lin);
        OSL_S21_PobierzKorekcjeLiniowosci(&kor);
        S21_FormatujWiekKalibracji(wiek_kal, sizeof(wiek_kal));
        snprintf(w0, sizeof(w0),
                 JEZYK_Wybierz("%.0f dB   %s   pewny ~%.0f dB",
                               "%.0f dB   %s   trusted ~%.0f dB",
                               "%.0f dB   %s   sicher ~%.0f dB",
                               "%.0f дБ   %s   надёжн. ~%.0f дБ"),
                 (double)tlumik_kal_db, wiek_kal, (double)S21_PobierzOrientacyjnyLimitDb());
        if (lin.wazna)
            snprintf(w1, sizeof(w1), "%u wz.  max %.2f dB  RMS %.2f  J%u%%",
                     (unsigned int)lin.liczba_wzorcow, (double)lin.odchylka_max_abs_db,
                     (double)lin.odchylka_rms_max_db, (unsigned int)lin.jakosc_min_proc);
        else
            snprintf(w1, sizeof(w1), "%s", JEZYK_Wybierz("brak serii 29/40/60 dB", "no 29/40/60 dB series", "keine 29/40/60-dB-Serie", "нет серии 29/40/60 дБ"));
        snprintf(w2, sizeof(w2), "%s", kor.aktywna ? "ON" : "OFF");
        if (ref_ten_sam_zakres && s21_ref_poprawny != 0 && s21_punkt_poprawny != 0 &&
            s21_punkt_poprawny[cursorPos] && s21_ref_poprawny[cursorPos] &&
            isfinite(valuesmI[cursorPos]) && isfinite(s21_ref_wartosci[cursorPos]))
            delta_ref_db = -(valuesmI[cursorPos] - s21_ref_wartosci[cursorPos]);
        snprintf(w3, sizeof(w3), "%s   dS@M4 %s%.2f dB",
                 ref_ten_sam_zakres ? "ON" : "--",
                 isfinite(delta_ref_db) ? "" : "--", isfinite(delta_ref_db) ? (double)delta_ref_db : 0.0);
        if (ma_profil)
            snprintf(w4, sizeof(w4), "%s  NOM %.4f MHz  [%s]", profil.nazwa,
                     (double)profil.f_nom_hz / 1000000.0, S21_NazwaStatusuSpec(S21_OcenProfilSpec(&profil)));
        else
            snprintf(w4, sizeof(w4), "%s", JEZYK_Wybierz("profil odniesienia: OFF", "reference profile: OFF", "Referenzprofil: AUS", "профиль: ВЫКЛ"));
        S21_RysujWierszWynikow(0U, "KAL", w0, 0U);
        S21_RysujWierszWynikow(1U, "LIN", w1, 0U);
        S21_RysujWierszWynikow(2U, JEZYK_Wybierz("Korekcja", "Correction", "Korrektur", "Коррекция"), w2, 1U);
        S21_RysujWierszWynikow(3U, "DUT / REF", w3, 0U);
        S21_RysujWierszWynikow(4U, JEZYK_Wybierz("Profil", "Profile", "Profil", "Профиль"), w4, 0U);
    }

    UI_RysujPrzycisk(S21_WYNIKI_BACK_X, S21_WYNIKI_BACK_Y,
                     S21_WYNIKI_BACK_W, S21_WYNIKI_BACK_H,
                     JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT,
                     UI_FontDlaRoli(UI_ROLA_TEKSTU_PRZYCISK));
    S21_UtworzOpcjeStron(opcje);
    UI_RysujWyborMetody(S21_WYNIKI_NAV_X, S21_WYNIKI_NAV_Y,
                        S21_WYNIKI_NAV_W, S21_WYNIKI_NAV_H,
                        JEZYK_Wybierz("Strona wyników", "Results page", "Ergebnisseite", "Страница результатов"),
                        opcje, S21_LICZBA_STRON_WYNIKOW, strona);
}

static void S21_PokazGotowaKlatke(void)
{
    LCD_BuforKlatkiPokaz(&s21_bufor_klatki);
    activeLayerS21 = s21_bufor_klatki.warstwa_widoczna;
}

static void S21_RysujWynikiBezMigania(uint8_t strona)
{
    LCD_BuforKlatkiRozpocznij(&s21_bufor_klatki);
    S21_RysujStroneWynikow(strona);
    S21_PokazGotowaKlatke();
}

static void S21_RysujGlownyEkranBezMigania(void)
{
    LCD_BuforKlatkiRozpocznij(&s21_bufor_klatki);
    /* Po powrocie z pełnoekranowych wyników nie mogą zostać ich ramki w marginesach wykresu. */
    LCD_FillAll(BackGrColor);
    RedrawWindowS21(0);
    S21_PokazGotowaKlatke();
}

static void S21_WyswietlWyniki(void)
{
    uint8_t strona = 0U;
    const uint32_t auto_przed_wejsciem = autofast;
    uint8_t wyjscie = 0U;

    if (!isMeasured)
        return;

    /* Wyniki sa widokiem danych z ostatniego zakonczonego skanu. */
    autofast = 0U;
    WEJSCIA_WyczyscZdarzenia();
    S21_RysujWynikiBezMigania(strona);

    while (!wyjscie)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;

        Sleep(0U);
        WEJSCIA_Aktualizuj();
        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
        {
            strona = (strona == 0U) ? (S21_LICZBA_STRON_WYNIKOW - 1U) : (uint8_t)(strona - 1U);
            S21_RysujWynikiBezMigania(strona);
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            strona = (uint8_t)((strona + 1U) % S21_LICZBA_STRON_WYNIKOW);
            S21_RysujWynikiBezMigania(strona);
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
        {
            wyjscie = 1U;
        }

        if (TOUCH_Poll(&punkt))
        {
            if (punkt.x < (S21_WYNIKI_BACK_X + S21_WYNIKI_BACK_W) &&
                punkt.y >= S21_WYNIKI_BACK_Y)
            {
                wyjscie = 1U;
            }
            else
            {
                UI_METODA_OPCJA_t opcje[S21_LICZBA_STRON_WYNIKOW];
                uint8_t nowa;
                S21_UtworzOpcjeStron(opcje);
                nowa = UI_WyborMetodyPoDotyku(punkt,
                                              S21_WYNIKI_NAV_X, S21_WYNIKI_NAV_Y,
                                              S21_WYNIKI_NAV_W, S21_WYNIKI_NAV_H,
                                              opcje, S21_LICZBA_STRON_WYNIKOW, strona);
                if (nowa != strona)
                {
                    strona = nowa;
                    S21_RysujWynikiBezMigania(strona);
                }
            }
            while (TOUCH_IsPressed())
                Sleep(1U);
        }
    }

    s21_widok_info = 0U;
    autofast = auto_przed_wejsciem;
    S21_RysujGlownyEkranBezMigania();
}

bool IsInverted;

static void DrawCursorS21(int offset)
{
    int i;
    int x = X0 + cursorPos + offset;
    if ((x < X0) || (x > X0 + WWIDTH))
        return;
    LCDPoint p;
    //Draw cursor line in TextColor
    p = LCD_MakePoint(x, Y0);

    i = 0;
    for (p.y = Y0; p.y < Y0 + WHEIGHT; p.y++)
    {
        if (i < 10)
            LCD_SetPixel(p, TextColor); //was: WK_InvertPixel(p.x,p.y);
        i++;
        if (i >= 20)
            i = 0;
    }
    if (offset == 0)
    {
        LCD_FillRect((LCDPoint){X0 + cursorPos - 3, Y0 + WHEIGHT + 2}, (LCDPoint){X0 + cursorPos + 3, Y0 + WHEIGHT + 4}, BackGrColor);
        LCD_FillRect((LCDPoint){X0 + cursorPos - 2, Y0 + WHEIGHT + 2}, (LCDPoint){X0 + cursorPos + 2, Y0 + WHEIGHT + 4}, TextColor);
    }
    Sleep(1);
}

static LCDColor c, c31, c35;
static int x, pos;

static void StrictDelCursor1(int offset)
{
    int x_lokalny;
    int y;
    int k;
    const float skala_db = S21_PobierzSkaleDb();

    x = X0 + cursorPos + offset;
    if ((x < X0) || (x > X0 + WWIDTH))
        return;

    /*
     * Przywracamy siatkę deterministycznie zamiast kopiować kolor z
     * przypadkowego piksela. Dzięki temu działa to również dla skal
     * 20/40/60/80 dB i widoku znormalizowanego.
     */
    x_lokalny = x - X0;
    if ((x_lokalny % 50) == 0)
        LCD_VLine(LCD_MakePoint(x, Y0), WHEIGHT + 1, S21_KolorSiatkiGlownej());
    else if ((x_lokalny % 10) == 0)
        LCD_VLine(LCD_MakePoint(x, Y0), WHEIGHT + 1, S21_KolorSiatkiDrobnej());
    else
        LCD_VLine(LCD_MakePoint(x, Y0), WHEIGHT + 1, BackGrColor);

    for (k = 0; k <= 10; ++k)
    {
        y = Y0 + (k * WHEIGHT) / 10;
        LCD_SetPixel(LCD_MakePoint(x, y),
                     (k % 2 == 0) ? S21_KolorSiatkiGlownej() : S21_KolorSiatkiDrobnej());
        if ((k % 2) == 0 && y < Y0 + WHEIGHT)
            LCD_SetPixel(LCD_MakePoint(x, y + 1), S21_KolorSiatkiGlownej());
    }

    if (s21_punkt_poprawny != 0 && s21_punkt_poprawny[cursorPos + offset] &&
        isfinite(valuesmI[cursorPos + offset]))
    {
        const float strata_ekran = S21_StrataDoWyswietlenia(valuesmI[cursorPos + offset]);
        pos = Y0 + (int)(strata_ekran / skala_db * WHEIGHT);
        if (pos < Y0) pos = Y0;
        if (pos > Y0 + WHEIGHT) pos = Y0 + WHEIGHT;
        LCD_SetPixel(LCD_MakePoint(x, pos), CurvColor);
        if (FatLines)
        {
            if (pos > Y0) LCD_SetPixel(LCD_MakePoint(x, pos - 1), CurvColor);
            if (pos < Y0 + WHEIGHT) LCD_SetPixel(LCD_MakePoint(x, pos + 1), CurvColor);
        }
    }
}

static void StrictDelCursor(void)
{
    StrictDelCursor1(0);
    if (FatLines)
    {
        StrictDelCursor1(-1);
        StrictDelCursor1(1);
    }
}

static void StrictDrawCursor(void)
{
    DrawCursorS21(0);
    if (FatLines)
    {
        DrawCursorS21(-1);
        DrawCursorS21(1);
    }
}

static void MoveCursor(int moveDirection)
{
    int NewPosition = cursorPos + moveDirection;

    if (!isMeasured)
        return;

    if ((NewPosition > WWIDTH) || (NewPosition < 0))
        return;

    StrictDelCursor(); // delete the old cursor
    cursorPos = NewPosition;
    StrictDrawCursor(); // set the new cursor

    if (cursorChangeCount++ < 5)
    {
        Sleep(20); //Slow down at first steps
    }
    DrawMeasuredValues(); //DH1AKF  25.10.2020
    Sleep(2);
}

#define linediv 10

static void Track_DrawGrid(int justGraphDraw)
{
    char buf[40];
    int i;
    const uint32_t zakres_start = fstart;
    const float skala_db = S21_PobierzSkaleDb();

    if (0 == CFG_GetParam(CFG_PARAM_PAN_CENTER_F))
        snprintf(buf, sizeof(buf), "%.3f MHz +%s", (double)f1 / 1000000.0, BSSTR_TRACK[span21]);
    else
        snprintf(buf, sizeof(buf), "%.3f MHz +/- %s", (double)f1 / 1000000.0, BSSTR_TRACK_HALF[span21]);

    LCD_FillRect(LCD_MakePoint(40, 15), LCD_MakePoint(459, 225), BackGrColor);
    cursorVisibleS21 = 0;

    /*
     * Oś Y jest parametryczna. Skala 20/40/60/80 dB zmienia wyłącznie
     * prezentację; nie wpływa na wartości używane przez analizę.
     */
    {
        int db;
        const int skala_i = (int)skala_db;
        for (db = 0; db <= skala_i; db += 5)
        {
            const int y = Y0 + (int)((float)db / skala_db * (float)WHEIGHT);
            const int major = (db == 0 || db == skala_i || (db % 10) == 0);

            LCD_HLine(LCD_MakePoint(X0 - 5, y), WWIDTH + 5,
                      major ? S21_KolorSiatkiGlownej() : S21_KolorSiatkiDrobnej());
            if (major && y < Y0 + WHEIGHT)
                LCD_HLine(LCD_MakePoint(X0 - 5, y + 1), WWIDTH + 5,
                          S21_KolorSiatkiGlownej());

            if (major)
            {
                char db_txt[12];
                if (db == 0)
                    snprintf(db_txt, sizeof(db_txt), "0dB");
                else
                    snprintf(db_txt, sizeof(db_txt), "-%d", db);
                FONT_Write(FONT_FRAN, S21_KolorTekstuWykresu(), BackGrColor,
                           db == 0 ? 12 : 8, y > 4 ? y - 4 : y, db_txt);
            }
        }
    }

    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_AKCENT), BackGrColor,
               52, 0, JEZYK_Tekst(TEKST_S21_TYTUL));
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 260, 0, buf);
    if (CFG_GetParam(CFG_PARAM_S21_NORMALIZUJ) != 0U)
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_AKCENT), BackGrColor, 213, 0, "NORM");

    /*
     * S21 służy do oceny dwuwrotnika, dlatego nie nanosimy tutaj pasm
     * krótkofalarskich. Pionowe tło pasm było mylące przy badaniu filtrów.
     */
    for (i = 0; i <= WWIDTH / linediv; i++)
    {
        const int x_grid = X0 + i * linediv;
        const int lmod = 5;

        if ((i % lmod) == 0 || i == WWIDTH / linediv)
        {
            float flabel = ((float)(zakres_start / 1000.0 +
                             i * BSVALUES_TRACK[span21] / (WWIDTH / linediv))) / 1000.0f;
            int w;
            if (flabel * 1000000.0f > (float)(CFG_GetParam(CFG_PARAM_BAND_FMAX) + 1U))
                continue;
            if (flabel > 999.99f)
                snprintf(buf, sizeof(buf), "%.1f", (double)flabel);
            else if (flabel > 99.99f)
                snprintf(buf, sizeof(buf), "%.2f", (double)flabel);
            else
                snprintf(buf, sizeof(buf), "%.3f", (double)flabel);

            w = FONT_GetStrPixelWidth(FONT_SDIGITS, buf);
            FONT_Write(FONT_FRAN, TextColor, BackGrColor,
                       x_grid - 8 - w / 2, Y0 + WHEIGHT + 3, buf);
            LCD_VLine(LCD_MakePoint(x_grid, Y0), WHEIGHT + 1, S21_KolorSiatkiGlownej());
            if (x_grid < X0 + WWIDTH)
                LCD_VLine(LCD_MakePoint(x_grid + 1, Y0), WHEIGHT + 1, S21_KolorSiatkiGlownej());
        }
        else
        {
            LCD_VLine(LCD_MakePoint(x_grid, Y0), WHEIGHT + 1, S21_KolorSiatkiDrobnej());
        }
    }

    LCD_Rectangle(LCD_MakePoint(X0, Y0),
                  LCD_MakePoint(X0 + WWIDTH, Y0 + WHEIGHT),
                  S21_KolorSiatkiGlownej());

    if (justGraphDraw == 2)
        TRACK_DrawFootText();
}

//static uint32_t Fs, Fp;// in Hz

static int S21_PomiarPunktu(uint32_t frequency, uint32_t nScanCount, float *wynik,
                            float *v_mv, float *i_mv, float *rozrzut_i_proc)
{
    float wartosc;

    if (wynik == 0)
        return 0;

    if (v_mv != 0)
        *v_mv = NAN;
    if (i_mv != 0)
        *i_mv = NAN;
    if (rozrzut_i_proc != 0)
        *rozrzut_i_proc = NAN;

    wartosc = CalcBin107(frequency, (int)nScanCount, &value1, &value2, &value3);
    if (!isfinite(wartosc))
        return 0;

    *wynik = wartosc;
    if (v_mv != 0)
        *v_mv = DSP_OstatniTrackVmv();
    if (i_mv != 0)
        *i_mv = DSP_OstatniTrackImv();
    if (rozrzut_i_proc != 0)
        *rozrzut_i_proc = DSP_OstatniTrackRozrzutIProc();
    return 1;
}

static uint32_t S21_PobierzLiczbePowtorzen(void)
{
    uint32_t n = CFG_GetParam(CFG_PARAM_PAN_NSCANS);
    const uint32_t jakosc = CFG_GetParam(CFG_PARAM_S21_JAKOSC_SKANU);

    if (n < 1U)
        n = 1U;
    if (n > 20U)
        n = 20U;

    if (jakosc == 0U)
    {
        n /= 2U;
        if (n < 1U)
            n = 1U;
    }
    else if (jakosc >= 2U)
    {
        n *= 2U;
        if (n < 6U)
            n = 6U;
        if (n > 20U)
            n = 20U;
    }
    return n;
}

static uint8_t s21_postep_ostatni_proc = 255U;
static uint32_t s21_postep_ostatni_ms;
#define S21_POSTEP_MIN_ODSWIEZENIE_MS 120U

static void S21_RysujPostepSkanu(uint32_t punkt, uint32_t czestotliwosc_hz,
                                  uint8_t wymus_odswiezenie)
{
    char tekst[80];
    uint32_t procent;
    uint16_t szerokosc;
    const LCDColor kolor = UI_KolorTekstu(UI_STYL_AKCENT);
    const uint16_t x0 = 6U;
    const uint16_t x1 = 388U;
    const uint16_t y0 = 226U;
    const uint16_t y1 = 237U;
    const uint32_t teraz_ms = HAL_GetTick();

    if (punkt > WWIDTH)
        punkt = WWIDTH;
    procent = (punkt * 100U) / WWIDTH;

    /*
     * LCD nie powinien wyznaczac tempa pomiaru. Odswiezamy nie czesciej niz
     * co 120 ms i tylko po przejsciu do kolejnego przedzialu 5%. Koniec skanu
     * (100%) jest zawsze pokazywany. Ograniczenie dotyczy wyłącznie grafiki;
     * wszystkie punkty RF są mierzone z dotychczasowym tempem.
     */
    if (!wymus_odswiezenie && s21_postep_ostatni_proc != 255U && procent < 100U)
    {
        if ((procent / 5U) == (s21_postep_ostatni_proc / 5U))
            return;
        if ((uint32_t)(teraz_ms - s21_postep_ostatni_ms) < S21_POSTEP_MIN_ODSWIEZENIE_MS)
            return;
    }

    s21_postep_ostatni_proc = (uint8_t)procent;
    s21_postep_ostatni_ms = teraz_ms;

    /*
     * Nie czyścimy już całego pasa 480x31 przy każdym odświeżeniu. Prawa
     * część (przycisk Wyniki) pozostaje nieruszona, a wewnątrz paska postępu
     * kasujemy tylko zmieniane piksele. To usuwa dodatkowe mruganie UI.
     */
    LCD_FillRect(LCD_MakePoint(0U, 209U), LCD_MakePoint(393U, 224U), BackGrColor);

    snprintf(tekst, sizeof(tekst),
             autofast
                 ? JEZYK_Wybierz("AUTO %lu%%  %.3f MHz  zakres stały",
                                 "AUTO %lu%%  %.3f MHz  fixed span",
                                 "AUTO %lu%%  %.3f MHz  Bereich fest",
                                 "AUTO %lu%%  %.3f МГц  диапазон фикс.")
                 : JEZYK_Wybierz("POMIAR %lu%%  %.3f MHz",
                                 "MEASURE %lu%%  %.3f MHz",
                                 "MESSUNG %lu%%  %.3f MHz",
                                 "ИЗМЕРЕНИЕ %lu%%  %.3f МГц"),
             (unsigned long)procent,
             (double)czestotliwosc_hz / 1000000.0);
    FONT_Write(FONT_FRAN, kolor, BackGrColor, 4U, 210U, tekst);

    LCD_HLine(LCD_MakePoint(x0, y0), (uint16_t)(x1 - x0 + 1U), TextColor);
    LCD_HLine(LCD_MakePoint(x0, y1), (uint16_t)(x1 - x0 + 1U), TextColor);
    LCD_VLine(LCD_MakePoint(x0, y0), (uint16_t)(y1 - y0 + 1U), TextColor);
    LCD_VLine(LCD_MakePoint(x1, y0), (uint16_t)(y1 - y0 + 1U), TextColor);

    szerokosc = (uint16_t)(((uint32_t)(x1 - x0 - 3U) * procent) / 100U);
    LCD_FillRect(LCD_MakePoint((uint16_t)(x0 + 2U), (uint16_t)(y0 + 2U)),
                 LCD_MakePoint((uint16_t)(x1 - 2U), (uint16_t)(y1 - 2U)),
                 BackGrColor);
    if (szerokosc > 0U)
    {
        LCD_FillRect(LCD_MakePoint((uint16_t)(x0 + 2U), (uint16_t)(y0 + 2U)),
                     LCD_MakePoint((uint16_t)(x0 + 1U + szerokosc),
                                   (uint16_t)(y1 - 2U)),
                     kolor);
    }
}

static int S21_SkanDoBufora(uint32_t krok, int mozna_przerwac)
{
    float *nowe_wartosci;
    float *nowe_v_mv;
    float *nowe_i_mv;
    float *nowe_rozrzut_i_proc;
    uint8_t *nowe_poprawne;
    uint8_t *nowe_zmierzone;
    uint32_t nScanCount;
    uint32_t deltaF;
    uint32_t poprzedni = 0U;
    uint32_t nastepny;
    uint32_t i;
    LCDPoint pt;
    int przerwany = 0;
    const uint8_t diagnostyka_vi =
        CFG_GetParam(CFG_PARAM_DIAGNOSTYKA_VI) != 0U ? 1U : 0U;

    if (krok < 1U)
        krok = 1U;
    if (krok > 20U)
        krok = 20U;

    nowe_wartosci = (float *)SDRH_malloc(sizeof(float) * (WWIDTH + 1U));
    nowe_v_mv = (float *)SDRH_malloc(sizeof(float) * (WWIDTH + 1U));
    nowe_i_mv = (float *)SDRH_malloc(sizeof(float) * (WWIDTH + 1U));
    nowe_rozrzut_i_proc = (float *)SDRH_malloc(sizeof(float) * (WWIDTH + 1U));
    nowe_poprawne = (uint8_t *)SDRH_malloc(sizeof(uint8_t) * (WWIDTH + 1U));
    nowe_zmierzone = (uint8_t *)SDRH_malloc(sizeof(uint8_t) * (WWIDTH + 1U));
    if (nowe_wartosci == 0 || nowe_v_mv == 0 || nowe_i_mv == 0 ||
        nowe_rozrzut_i_proc == 0 || nowe_poprawne == 0 || nowe_zmierzone == 0)
    {
        if (nowe_wartosci != 0) SDRH_free(nowe_wartosci);
        if (nowe_v_mv != 0) SDRH_free(nowe_v_mv);
        if (nowe_i_mv != 0) SDRH_free(nowe_i_mv);
        if (nowe_rozrzut_i_proc != 0) SDRH_free(nowe_rozrzut_i_proc);
        if (nowe_poprawne != 0) SDRH_free(nowe_poprawne);
        if (nowe_zmierzone != 0) SDRH_free(nowe_zmierzone);
        GEN_SetTXFreq(0);
        return 0;
    }

    for (i = 0U; i <= WWIDTH; ++i)
    {
        nowe_wartosci[i] = NAN;
        nowe_v_mv[i] = NAN;
        nowe_i_mv[i] = NAN;
        nowe_rozrzut_i_proc[i] = NAN;
        nowe_poprawne[i] = 0U;
        nowe_zmierzone[i] = 0U;
    }

    deltaF = (BSVALUES_TRACK[span21] * 1000U) / WWIDTH;
    if (deltaF == 0U)
        deltaF = 1U;
    nScanCount = S21_PobierzLiczbePowtorzen();

    CLK2_drive = 0;
    HS_SetPower(2, 0, 1);
    DSP_UstawDiagnostykeTrack(diagnostyka_vi);
    s21_postep_ostatni_proc = 255U;
    s21_postep_ostatni_ms = 0U;
    S21_RysujPostepSkanu(0U, fstart, 1U);

    /*
     * Pierwszy punkt jest zawsze rzeczywiście mierzony. Fizyczny przycisk
     * enkodera odczytujemy przed i po pomiarze, ale nie wywołujemy Sleep().
     * Dzięki temu krótkie naciśnięcie zostaje zapamiętane jako żądanie zrzutu,
     * natomiast kosztowny zapis SD następuje dopiero poza krytyczną pętlą RF.
     */
    freq1 = fstart;
    WEJSCIA_Aktualizuj();
    nowe_zmierzone[0] = 1U;
    nowe_poprawne[0] = (uint8_t)S21_PomiarPunktu(freq1, nScanCount,
                                                  &nowe_wartosci[0],
                                                  &nowe_v_mv[0], &nowe_i_mv[0],
                                                  &nowe_rozrzut_i_proc[0]);
    WEJSCIA_Aktualizuj();

    while (poprzedni < WWIDTH)
    {
        uint32_t m;
        nastepny = poprzedni + krok;
        if (nastepny > WWIDTH)
            nastepny = WWIDTH;

        WEJSCIA_Aktualizuj();
        if (mozna_przerwac && TOUCH_Poll(&pt))
        {
            przerwany = 1;
            break;
        }

        freq1 = fstart + nastepny * deltaF;
        nowe_zmierzone[nastepny] = 1U;
        nowe_poprawne[nastepny] = (uint8_t)S21_PomiarPunktu(freq1, nScanCount,
                                                             &nowe_wartosci[nastepny],
                                                             &nowe_v_mv[nastepny],
                                                             &nowe_i_mv[nastepny],
                                                             &nowe_rozrzut_i_proc[nastepny]);
        WEJSCIA_Aktualizuj();
        S21_RysujPostepSkanu(nastepny, freq1, nastepny == WWIDTH ? 1U : 0U);

        /*
         * Szybki skan może uzupełniać punkty wyłącznie do rysowania i wstępnej
         * analizy. Maska nowe_zmierzone[] pozostaje zerowa dla interpolacji,
         * więc automat zna rzeczywistą gęstość danych i nie myli gładkiej
         * polilinii z pomiarem.
         */
        for (m = poprzedni + 1U; m < nastepny; ++m)
        {
            if (nowe_poprawne[poprzedni] && nowe_poprawne[nastepny])
            {
                const float udzial =
                    (float)(m - poprzedni) / (float)(nastepny - poprzedni);
                nowe_wartosci[m] = nowe_wartosci[poprzedni] +
                    (nowe_wartosci[nastepny] - nowe_wartosci[poprzedni]) * udzial;
                if (isfinite(nowe_v_mv[poprzedni]) && isfinite(nowe_v_mv[nastepny]))
                    nowe_v_mv[m] = nowe_v_mv[poprzedni] +
                        (nowe_v_mv[nastepny] - nowe_v_mv[poprzedni]) * udzial;
                if (isfinite(nowe_i_mv[poprzedni]) && isfinite(nowe_i_mv[nastepny]))
                    nowe_i_mv[m] = nowe_i_mv[poprzedni] +
                        (nowe_i_mv[nastepny] - nowe_i_mv[poprzedni]) * udzial;
                /* Rozrzutu nie interpolujemy - tylko realne próbki mają taką informację. */
                nowe_poprawne[m] = 1U;
            }
        }

        poprzedni = nastepny;
    }

    DSP_UstawDiagnostykeTrack(0U);
    GEN_SetTXFreq(0);
    if (!przerwany)
    {
        memcpy(valuesmI, nowe_wartosci, sizeof(float) * (WWIDTH + 1U));
        memcpy(s21_napiecie_v_mv, nowe_v_mv, sizeof(float) * (WWIDTH + 1U));
        memcpy(s21_napiecie_i_mv, nowe_i_mv, sizeof(float) * (WWIDTH + 1U));
        memcpy(s21_rozrzut_i_proc, nowe_rozrzut_i_proc, sizeof(float) * (WWIDTH + 1U));
        memcpy(s21_punkt_poprawny, nowe_poprawne, sizeof(uint8_t) * (WWIDTH + 1U));
        memcpy(s21_punkt_zmierzony, nowe_zmierzone, sizeof(uint8_t) * (WWIDTH + 1U));

        (void)S21_AnalizujFiltr(valuesmI, s21_punkt_poprawny, s21_punkt_zmierzony,
                               (uint16_t)(WWIDTH + 1U), fstart, (float)deltaF,
                               &g_s21_analiza);
        isMeasured = 1;
    }

    SDRH_free(nowe_wartosci);
    SDRH_free(nowe_v_mv);
    SDRH_free(nowe_i_mv);
    SDRH_free(nowe_rozrzut_i_proc);
    SDRH_free(nowe_poprawne);
    SDRH_free(nowe_zmierzone);
    return przerwany ? 0 : 1;
}

static int S21_CzyRefTenSamZakres(void)
{
    return s21_ref_aktywny && s21_ref_fstart == fstart && s21_ref_span == span21;
}

static void S21_ZachowajSladReferencyjny(void)
{
    if (CFG_GetParam(CFG_PARAM_S21_POROWNAJ_POPRZEDNI) == 0U ||
        !isMeasured || s21_ref_wartosci == 0 || s21_ref_poprawny == 0)
        return;

    memcpy(s21_ref_wartosci, valuesmI, sizeof(float) * (WWIDTH + 1U));
    memcpy(s21_ref_poprawny, s21_punkt_poprawny, sizeof(uint8_t) * (WWIDTH + 1U));
    s21_ref_fstart = fstart;
    s21_ref_span = span21;
    memset(&g_s21_ref_analiza, 0, sizeof(g_s21_ref_analiza));
    (void)S21_AnalizujFiltr(s21_ref_wartosci, s21_ref_poprawny, 0,
                           (uint16_t)(WWIDTH + 1U), fstart,
                           (float)((BSVALUES_TRACK[span21] * 1000U) / WWIDTH),
                           &g_s21_ref_analiza);
    s21_ref_aktywny = 1U;
}

static void Scan21(int selector)
{
    (void)selector;
    S21_ZachowajSladReferencyjny();
    if (S21_SkanDoBufora(1U, 0))
    {
        s21_ostatni_skan_auto = 0U;
        S21_AktualizujTloRX(1U);
    }
}

//=========================================================================
//                        NOT REMOVE BELOW LINES
//=========================================================================
////For Version 0.5
////Check memory interference and open
//int offsetIndex = 0;
//static void Scan21Fast_v05()
//{
//    float inputMI;
//    uint32_t i, nScanCount;
//    uint32_t fstart, freq1, deltaF;
//
//    f1=CFG_GetParam(CFG_PARAM_S21_F1);
//    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
//        fstart = f1;
//    else
//        fstart = f1 - 500*BSVALUES_TRACK[span]; // 2;
//
//    freq1 = fstart - 150000;
//    if (freq1 < 100000)
//        freq1 = 100000;
//
//    DSP_MeasureTrack(freq1, 1, 1, 1); //Fake initial run to let the circuit stabilize
//    inputMI = DSP_MeasuredTrackValue();
//    //Sleep(20);
//
//    DSP_MeasureTrack(freq1, 1, 1, 1);
//    inputMI = DSP_MeasuredTrackValue();
//
//    deltaF=(BSVALUES_TRACK[span] * 1000) / WWIDTH;
//    nScanCount = CFG_GetParam(CFG_PARAM_PAN_NSCANS);
//
///*
//#define MEASURE_INTERVAL_FAST 32
//#define USEHIST_INTERVAL_FAST 16
//#define MEASURE_MAX_INDEX 1
//*/
//#define MEASURE_INTERVAL_FAST 32
//#define USEHIST_INTERVAL_FAST 8
//#define MEASURE_MAX_INDEX 3
//
//    const int FAST_SCAN_OFFSET[] = {0, 16, 8, 24};
//
//    int fastOffset = FAST_SCAN_OFFSET[offsetIndex++];
//
//    if (offsetIndex > MEASURE_MAX_INDEX)
//        offsetIndex = 0;
//
//    for(i = 0; i <= WWIDTH; i++)
//    {
//        freq1 = fstart + (i + fastOffset) * deltaF;
//        if (freq1 == 0) //To overcome special case in DSP_Measure, where 0 is valid value
//            freq1 = 1;
//
//        int drawX = i + fastOffset;
//
//
//        if (i % MEASURE_INTERVAL_FAST != 0)
//        {
//            continue;
//        }
//#ifdef _DEBUG_UART
////    DBG_Printf("PROCESS: OFST:%d, IDX:%d, drawX:%d, Freq:%u", fastOffset, i, drawX, freq1);
//#endif
//
//        //DSP_MeasureTrack(freq1, 1, 1, nScanCount);
//        DSP_MeasureTrack(freq1, 1, 1, 1);
//        valuesdBI[drawX] = OSL_TXTodB(freq1, DSP_MeasuredTrackValue());
//
//            /*
//        //Break by Touch
//        if (autofast)
//        {
//            if (TOUCH_Poll(&pt))
//            {
//                if (pt.y > 230 && pt.x > 442)
//                {
//                    autofast = 0;
//                    TRACK_Beep(0);
//                    TRACK_DrawFootText();
//                    return;
//                }
//            }
//        }
//            */
//    }
//
//    //FONT_Write(FONT_FRAN, LCD_RED, LCD_BLACK, 420, 0, "     ");
//    GEN_SetMeasurementFreq(0);
//    isMeasured = 1;
//
///*
//    //fastOffset
//    //First Interpolate
//    for(i = fastOffset; i < WWIDTH - MEASURE_INTERVAL_FAST; i += USEHIST_INTERVAL_FAST)
//    {
//        uint32_t fr = i % MEASURE_INTERVAL_FAST;
//
//        if (fastOffset == fr)
//            continue;
//
//        int fi0, fi2;
//
//        //17 : 16, 48
//
//        fi0 = i / MEASURE_INTERVAL_FAST;
//        fi2 = fi0 + 1;
//
//        fi0 *= MEASURE_INTERVAL_FAST;
//        fi2 *= MEASURE_INTERVAL_FAST;
//
//        fi0 += fastOffset;
//        fi2 += fastOffset;
//
//        if (fi2 > WWIDTH)
//            continue;
//
//#ifdef _DEBUG_UART
//    //DBG_Printf("PROCESS: OFST:%d, IDX:%d, f0:%d, f2:%u", fastOffset, i, fi0, fi2);
//#endif
//
//        float Yi = valuesdBI[fi0];
//        float Xi = fi0;
//        float Yi1 = valuesdBI[fi2];
//        float Xi1 = fi2;
//        float Xb = i;
//
//        float Yb = Yi + (Yi1 - Yi) * (Xb - Xi) / (Xi1 - Xi);
//        valuesdBI[i] = Yb * 0.3 + valuesdBI[i] * 0.7;
//    }
//*/
//
//
//    //Interpolate intermediate values
//    for(i = 1; i <= WWIDTH; i++)
//    {
//        uint32_t fr = i % USEHIST_INTERVAL_FAST;
//
//        if (0 == fr)
//            continue;
//
//        int fi0, fi1, fi2;
//
//        fi0 = i / USEHIST_INTERVAL_FAST;
//        fi2 = fi0 + 1;
//
//        fi0 *= USEHIST_INTERVAL_FAST;
//        fi2 *= USEHIST_INTERVAL_FAST;
//
//
//        float Yi = valuesdBI[fi0];
//        float Xi = fi0;
//        float Yi1 = valuesdBI[fi2];
//        float Xi1 = fi2;
//        float Xb = i;
//
//        float Yb = Yi + (Yi1 - Yi) * (Xb - Xi) / (Xi1 - Xi);
//        valuesdBI[i] = Yb;
//    }
//
///*
//    //Full Interpolate by Latest Measurement data
//    for(i = fastOffset; i <= WWIDTH; i++)
//    {
//        uint32_t fr = i % MEASURE_INTERVAL_FAST;
//
//        if (fastOffset == fr)
//            continue;
//
//        int fi0, fi2;
//        fi0 = i / MEASURE_INTERVAL_FAST;
//        fi2 = fi0 + 1;
//
//        fi0 *= MEASURE_INTERVAL_FAST;
//        fi2 *= MEASURE_INTERVAL_FAST;
//
//        fi0 += fastOffset;
//        fi2 += fastOffset;
//
//
//        //if (fi2 > WWIDTH)
//        //    continue;
//
//#ifdef _DEBUG_UART
//    //DBG_Printf("PROCESS: OFST:%d, IDX:%d, f0:%d, f2:%u", fastOffset, i, fi0, fi2);
//#endif
//
//        float Yi = valuesdBI[fi0];
//        float Xi = fi0;
//        float Yi1 = valuesdBI[fi2];
//        float Xi1 = fi2;
//        float Xb = i;
//
//        float Yb = Yi + (Yi1 - Yi) * (Xb - Xi) / (Xi1 - Xi);
//        //valuesdBI[i] = Yb * 0.3 + valuesdBI[i] * 0.7;
//
//        if (fabs(valuesdBI[i] - Yb) > 20)
//        {
//            valuesdBI[i] = Yb ;
//        }
//        else
//        {
//            valuesdBI[i] = Yb * 0.3 + valuesdBI[i] * 0.7;
//        }
//        //valuesdBI[i] = Yb;
//    }
//*/
//
//}

//Version 0.35

static void MakeFstart(void)
{
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    uint64_t span_hz;
    uint64_t start64;

    f1 = CFG_GetParam(CFG_PARAM_S21_F1);
    span21 = (BANDSPAN)CFG_GetParam(CFG_PARAM_S21_SPAN);
    if ((uint32_t)span21 > (uint32_t)BS100M)
        span21 = BS400;

    span_hz = (uint64_t)BSVALUES_TRACK[span21] * 1000ULL;
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
        start64 = f1;
    else
    {
        const uint64_t polowa = span_hz / 2ULL;
        start64 = (uint64_t)f1 > polowa ? (uint64_t)f1 - polowa : (uint64_t)fmin;
    }

    if (start64 < fmin)
        start64 = fmin;
    if (fmax > fmin && span_hz <= (uint64_t)(fmax - fmin) && start64 + span_hz > fmax)
        start64 = (uint64_t)fmax - span_hz;
    if (start64 > UINT32_MAX)
        start64 = UINT32_MAX;

    fstart = (uint32_t)start64;
    freq1 = fstart > 150000U ? fstart - 150000U : fmin;
    if (freq1 < fmin)
        freq1 = fmin;
}

//static LCDPoint    pt1;
static int touchIndex;

void TrackTouchExecute(LCDPoint pt)
{

    UI_KONTROLKA_t kontrolki[trackMenu_Length];
    TRACK_WypelnijKontrolki(kontrolki);
    touchIndex = UI_ZnajdzKontrolke(pt, kontrolki, trackMenu_Length);
    if (touchIndex < 9)
    { // Beep
        TRACK_Beep(1);
        while (TOUCH_IsPressed())
            ;
        Sleep(100);
    }

    switch (touchIndex)
    { // Exit
    case 0:
    {
        exitScan = 1;
        break;
    }
    case 1:
    { //SCAN
        if (autofast)
            break;

        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_AKCENT), BackGrColor, 170, 100, JEZYK_Tekst(TEKST_TRWA_POMIAR));
        redrawRequired = 1;
        break;
    }
    case 2:
    { // węższy zakres
        if (span21 > 0)
            span21--;
        CFG_SetParam(CFG_PARAM_S21_SPAN, span21);
        CFG_Flush();
        MakeFstart();
        redrawRequired = 1;
        break;
    }
    case 3:
    { // szerszy zakres
        if (span21 < BS100M)
            span21++;
        CFG_SetParam(CFG_PARAM_S21_SPAN, span21);
        CFG_Flush();
        MakeFstart();
        redrawRequired = 1;
        break;
    }
    case 4: // Input Freq
    {
        break;
    }
    case 5:
    {
        /* Jawny, pelnoekranowy widok ośmiu stron wyników. */
        S21_WyswietlWyniki();
        break;
    }
    case 6:
    { //Capture
        LCD_FillRect((LCDPoint){0, 248}, (LCDPoint){50, 271}, BackGrColor);
        save_snapshot();
        //Redraw
        //redrawRequired = 1;
        RedrawWindowS21(0);
        //DrawCursorS
        TRACK_DrawFootText();
        DrawAutoText();
        break;
    }
    case 7:
    { // SetCursor
        //if(autofast!=0) break;
        uint16_t cursorNew = pt.x;
        if (pt.x < X0)
            cursorNew = X0;
        if (pt.x > X0 + WWIDTH)
            cursorNew = WWIDTH;
        MoveCursor(cursorNew - cursorPos - X0);
        break;
    } //end of case
    }
    if (touchIndex == 8)
    {
        // Button Auto
        if (autofast == 0)
        {
            autofast = 1;
                cursorChangeCount = 0;

            /*
             * Auto nie czyści istniejącego wykresu. Poprzedni ślad pozostaje
             * widoczny do chwili zakończenia następnego skanu, a postęp jest
             * pokazywany w dolnym polu informacji.
             */
            DrawAutoText();
        }
        else
        {
            autofast = 0;
            StrictDrawCursor();
            TRACK_DrawFootText();
        }
        return;
    }
}

static int S21_PobierzKalibracjeDb(uint32_t frequency, float *log0, float *log_att)
{
    int idx;
    uint32_t fr1, fr2;
    float prop = 0.0f;
    float v0a, v0b, vatta, vattb;
    float l0a, l0b, latta, lattb;

    if (log0 == 0 || log_att == 0)
        return 0;

    idx = GetIndexForFreq(frequency);
    if (idx < 0)
        return 0;

    fr1 = OSL_GetCalFreqByIdx(idx);
    if (fr1 == 0U)
        return 0;

    osl_txCorr = (OSL_ERRCORR *)WORK_Ptr;
    v0a = osl_txCorr[idx].val0;
    vatta = osl_txCorr[idx].valAtt;
    if (!isfinite(v0a) || !isfinite(vatta) || v0a <= 0.0f || vatta <= 0.0f || vatta >= v0a)
        return 0;

    l0a = 10.0f * log10f(v0a);
    latta = 10.0f * log10f(vatta);
    if (!isfinite(l0a) || !isfinite(latta))
        return 0;

    fr2 = OSL_GetCalFreqByIdx(idx + 1);
    if (fr2 != 0U && frequency > fr1 && frequency < fr2)
    {
        v0b = osl_txCorr[idx + 1].val0;
        vattb = osl_txCorr[idx + 1].valAtt;
        if (!isfinite(v0b) || !isfinite(vattb) || v0b <= 0.0f || vattb <= 0.0f || vattb >= v0b)
            return 0;
        l0b = 10.0f * log10f(v0b);
        lattb = 10.0f * log10f(vattb);
        if (!isfinite(l0b) || !isfinite(lattb))
            return 0;

        /*
         * Charakterystyka transmisji jest wielkością multiplikatywną,
         * dlatego pomiędzy punktami kalibracji interpolujemy poziom w
         * dziedzinie logarytmicznej (dB), a nie surową amplitudę ADC.
         */
        prop = (float)(frequency - fr1) / (float)(fr2 - fr1);
        l0a += (l0b - l0a) * prop;
        latta += (lattb - latta) * prop;
    }

    if (latta >= l0a)
        return 0;
    *log0 = l0a;
    *log_att = latta;
    return 1;
}

float CalcBin107(uint32_t frequency, int iterations, float *val1, float *val2, float *val3)
{
    float sygnal;
    float log_sygnal;
    float log0;
    float log_att;
    float mianownik;
    const float tlumik_db = CFG_GetS21TlumikDb();

    if (val1 != 0)
        *val1 = NAN;
    if (val2 != 0)
        *val2 = NAN;
    if (val3 != 0)
        *val3 = NAN;

    if (frequency < CFG_GetParam(CFG_PARAM_BAND_FMIN) ||
        frequency > CFG_GetParam(CFG_PARAM_BAND_FMAX) ||
        !S21_PobierzKalibracjeDb(frequency, &log0, &log_att))
        return NAN;

    sygnal = DSP_GetValue(frequency, iterations);
    if (!DSP_CzyOstatniPomiarTrackPoprawny() || !isfinite(sygnal) || sygnal <= 0.0f)
        return NAN;

    /*
     * Operujemy w dziedzinie logarytmicznej, a dwa zmierzone poziomy
     * kalibracyjne przypisujemy do 0 dB i wartości znanego tłumika.
     * Dzięki temu nie zakładamy liniowej charakterystyki amplitudowej ADC.
     */
    log_sygnal = 10.0f * log10f(sygnal);
    mianownik = log_att - log0;

    if (!isfinite(log_sygnal) || !isfinite(log0) || !isfinite(log_att) ||
        fabsf(mianownik) < 0.05f || tlumik_db <= 0.0f)
        return NAN;

    if (val1 != 0)
        *val1 = log0;
    if (val2 != 0)
        *val2 = log_att;

    {
        const float wynik_bazowy = tlumik_db * (log_sygnal - log0) / mianownik;
        return OSL_S21_KorygujLiniowoscDb(wynik_bazowy);
    }
}

/*
 * Przeliczenie surowego poziomu FFT przez tę samą dwupunktową kalibrację,
 * której używa właściwy pomiar S21. Funkcja jest używana do oszacowania tła
 * odbiornika przy wyłączonym TX. Tła nie odejmujemy od śladu, ponieważ bez
 * informacji fazowej takie odejmowanie byłoby metrologicznie niepoprawne.
 */
static float S21_SurowyPoziomNaStrateDb(uint32_t frequency, float sygnal)
{
    float log_sygnal;
    float log0;
    float log_att;
    float mianownik;
    const float tlumik_db = CFG_GetS21TlumikDb();

    if (!isfinite(sygnal) || sygnal <= 0.0f || tlumik_db <= 0.0f ||
        !S21_PobierzKalibracjeDb(frequency, &log0, &log_att))
        return NAN;

    log_sygnal = 10.0f * log10f(sygnal);
    mianownik = log_att - log0;
    if (!isfinite(log_sygnal) || fabsf(mianownik) < 0.05f)
        return NAN;

    return OSL_S21_KorygujLiniowoscDb(
        tlumik_db * (log_sygnal - log0) / mianownik);
}

static void S21_AktualizujTloRX(uint8_t wymus)
{
    uint32_t czestotliwosci[3];
    uint32_t span_hz;
    uint32_t n;
    uint8_t i;
    uint8_t znaleziono = 0U;
    float najgorsze_tlo_db = NAN;
    float najgorszy_rozrzut = NAN;
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    if (!isMeasured)
        return;

    if (CFG_GetParam(CFG_PARAM_S21_TLO_RX) == 0U)
    {
        s21_tlo_rx_wazne = 0U;
        s21_tlo_rx_db = NAN;
        s21_tlo_rx_rozrzut_proc = NAN;
        return;
    }

    if (!wymus && s21_tlo_rx_wazne && s21_tlo_fstart == fstart &&
        s21_tlo_span == span21 && s21_tlo_licznik_odswiezania < 7U)
    {
        ++s21_tlo_licznik_odswiezania;
        return;
    }

    s21_tlo_licznik_odswiezania = 0U;
    s21_tlo_rx_wazne = 0U;
    s21_tlo_rx_db = NAN;
    s21_tlo_rx_rozrzut_proc = NAN;

    span_hz = BSVALUES_TRACK[span21] * 1000U;
    czestotliwosci[0] = fstart;
    czestotliwosci[1] = fstart + span_hz / 2U;
    czestotliwosci[2] = fstart + span_hz;
    n = S21_PobierzLiczbePowtorzen();
    if (n < 6U)
        n = 6U;

    for (i = 0U; i < 3U; ++i)
    {
        float rozrzut = NAN;
        float surowy;
        float strata_db;
        uint32_t f_hz = czestotliwosci[i];

        if (f_hz < fmin)
            f_hz = fmin;
        if (f_hz > fmax)
            f_hz = fmax;

        surowy = DSP_MeasureTrackTlo(f_hz, (int)n, &rozrzut);
        strata_db = S21_SurowyPoziomNaStrateDb(f_hz, surowy);
        if (!isfinite(strata_db) || strata_db < 0.0f)
            continue;

        /*
         * Mniejsza dodatnia strata oznacza wyższe tło, czyli gorszy przypadek.
         * Pokazujemy wartość konserwatywną z początku, środka i końca zakresu.
         */
        if (!znaleziono || strata_db < najgorsze_tlo_db)
        {
            najgorsze_tlo_db = strata_db;
            najgorszy_rozrzut = rozrzut;
        }
        znaleziono = 1U;
    }

    GEN_SetTXFreq(0U);
    if (znaleziono)
    {
        s21_tlo_rx_db = najgorsze_tlo_db;
        s21_tlo_rx_rozrzut_proc = najgorszy_rozrzut;
        s21_tlo_rx_wazne = 1U;
        s21_tlo_fstart = fstart;
        s21_tlo_span = span21;
    }
}

/*
void SetMeasPointS21B(uint32_t i, int pos, LCDColor col){
int lmod = 5;
int k, length;
    if(pos<Y0) pos=Y0;
    if(pos>Y0+WHEIGHT) pos=Y0+WHEIGHT;

    LCD_SetPixel(LCD_MakePoint(i+X0, pos), col);// set the new pixel(s)
    if(FatLines){
        LCD_SetPixel(LCD_MakePoint(i+X0, pos-1), col);// WK
    }
}
*/

void SetMeasPointS21(uint32_t i, int pos)
{
    int r;
    int k; //, length;
    if (pos < Y0)
        pos = Y0;
    if (pos > Y0 + WHEIGHT)
        pos = Y0 + WHEIGHT;

    if (i == cursorPos)
        StrictDelCursor();

    c31 = LCD_ReadPixel(LCD_MakePoint(i + X0, Y0 + 31)); // no horizontal line between 30..39
    c35 = LCD_ReadPixel(LCD_MakePoint(i + X0, Y0 + 35));
    c = c31;
    if (c31 == CurvColor)
        c = c35;
    LCD_VLine(LCD_MakePoint(i + X0, Y0), WHEIGHT + 2, c); //draw original
    r = 10 * Y0;
    for (k = 0; k < 14; k++)
    { // horizontal lines
        LCD_SetPixel(LCD_MakePoint(i + X0, r / 10),
                     (k % 2 == 0) ? S21_KolorSiatkiGlownej() : S21_KolorSiatkiDrobnej());
        if (k % 2 == 0)
            LCD_SetPixel(LCD_MakePoint(i + X0, r / 10 + 1), S21_KolorSiatkiGlownej());
        r += 146;
    }
    if (i == cursorPos)
        StrictDrawCursor();
    LCD_SetPixel(LCD_MakePoint(i + X0, pos), CurvColor); // set the new pixel(s) over cursor line
    if (FatLines)
    {
        LCD_SetPixel(LCD_MakePoint(i + X0, pos - 1), CurvColor);
        LCD_SetPixel(LCD_MakePoint(i + X0, pos + 1), CurvColor);
    }
}

static void Track_DrawCurve(void);

static void Scan21Fast(void)
{
    uint32_t krok = (uint32_t)autoScanFactor;

    /*
     * Auto oznacza wyłącznie ciągły, szybszy pomiar w zakresie ustawionym
     * przez użytkownika. Nie centruje filtru, nie zmienia f1 ani span i nie
     * uruchamia kolejnych skanów na podstawie analizy -3 dB. Dzięki temu
     * zachowuje się przewidywalnie i nadaje się do strojenia na żywo.
     */
    if (krok < 4U || krok > 20U)
        krok = 8U;

    if (!S21_SkanDoBufora(krok, 1))
        return;

    s21_ostatni_skan_auto = 1U;
    S21_AktualizujTloRX(0U);
    /*
     * Nie rysujemy tutaj bezpośrednio na warstwie widocznej. Po zakończeniu
     * skanu glowna petla sklada kompletna klatke w buforze ukrytym i dopiero
     * potem przelacza LTDC przy wygaszaniu pionowym.
     */
}

int lastoffset;

static int S21_PrzeliczDbNaY(float tlumienie_db)
{
    const float skala_db = S21_PobierzSkaleDb();
    const float strata_ekran = S21_StrataDoWyswietlenia(tlumienie_db);
    int y = (int)(strata_ekran / skala_db * (float)WHEIGHT) + Y0;

    if (y < Y0)
        y = Y0;
    else if (y > Y0 + WHEIGHT)
        y = Y0 + WHEIGHT;

    return y;
}

static int S21_CzestotliwoscDoX(float f_hz)
{
    const float span_hz = (float)BSVALUES_TRACK[span21] * 1000.0f;
    float udzial;

    if (!isfinite(f_hz) || span_hz <= 0.0f ||
        f_hz < (float)fstart || f_hz > (float)fstart + span_hz)
        return -1;

    udzial = (f_hz - (float)fstart) / span_hz;
    return X0 + (int)(udzial * (float)WWIDTH + 0.5f);
}

static void S21_RysujZnacznik(float f_hz, const char *nazwa)
{
    const int x_m = S21_CzestotliwoscDoX(f_hz);
    const LCDColor kolor = UI_KolorTekstu(UI_STYL_AKCENT);
    int x_txt;

    if (x_m < X0 || x_m > X0 + WWIDTH || nazwa == 0)
        return;

    LCD_VLine(LCD_MakePoint(x_m, Y0), 9U, kolor);
    x_txt = x_m - FONT_GetStrPixelWidth(FONT_FRAN, nazwa) / 2;
    if (x_txt < X0)
        x_txt = X0;
    if (x_txt > X0 + WWIDTH - 20)
        x_txt = X0 + WWIDTH - 20;
    FONT_Write(FONT_FRAN, kolor, BackGrColor, x_txt, Y0 + 9, nazwa);
}

static void S21_RysujMarkeryAnalizy(void)
{
    if (!isMeasured || !g_s21_analiza.poprawny)
        return;

    /*
     * Markery P1/P2/D pozostają wyłącznie opisem lokalnych ekstremów.
     * ekstrema widocznej charakterystyki. Nie są używane przez automat.
     */
    if (s21_widok_info == 6U || s21_widok_info == 7U)
    {
        S21_EKSTREMA_LOKALNE_t e;
        (void)S21_ZnajdzEkstremaLokalne(&e);
        if (e.ma_p1)
            S21_RysujZnacznik((float)S21_CzestotliwoscDlaIndeksu(e.indeks_p1), "P1");
        if (e.ma_p2)
            S21_RysujZnacznik((float)S21_CzestotliwoscDlaIndeksu(e.indeks_p2), "P2");
        if (s21_widok_info == 7U && e.ma_dolek)
            S21_RysujZnacznik((float)S21_CzestotliwoscDlaIndeksu(e.indeks_dolka), "D");
        return;
    }

    if (g_s21_analiza.typ == S21_TYP_BPF)
    {
        S21_RysujZnacznik(g_s21_analiza.f_min_hz, "M1");
        if (g_s21_analiza.ma_lewy_3db)
            S21_RysujZnacznik(g_s21_analiza.f1_3db_hz, "M2");
        if (g_s21_analiza.ma_prawy_3db)
            S21_RysujZnacznik(g_s21_analiza.f2_3db_hz, "M3");
    }
    else if (g_s21_analiza.typ == S21_TYP_NOTCH)
    {
        S21_RysujZnacznik(g_s21_analiza.f_max_hz, "M1");
    }
}

static void S21_RysujOdcinekKrzywej(int x1, int y1, int x2, int y2)
{
    LCD_Line(LCD_MakePoint(x1, y1), LCD_MakePoint(x2, y2), CurvColor);

    if (!FatLines)
        return;

    /*
     * Druga linia daje czytelniejszy przebieg na 480x272. Rysujemy ją tylko
     * wtedy, gdy cały odcinek mieści się w polu wykresu, aby nie wejść na ramkę.
     */
    if (y1 > Y0 && y2 > Y0)
        LCD_Line(LCD_MakePoint(x1, y1 - 1), LCD_MakePoint(x2, y2 - 1), CurvColor);
    if (y1 < Y0 + WHEIGHT && y2 < Y0 + WHEIGHT)
        LCD_Line(LCD_MakePoint(x1, y1 + 1), LCD_MakePoint(x2, y2 + 1), CurvColor);
}

static void S21_RysujSladReferencyjny(void)
{
    const LCDColor kolor_ref = S21_KolorSiatkiGlownej();
    const float skala_db = S21_PobierzSkaleDb();
    int poprzedni_x = 0;
    int poprzedni_y = 0;
    int ma_poprzedni = 0;
    int i;

    if (!S21_CzyRefTenSamZakres() || s21_ref_wartosci == 0 || s21_ref_poprawny == 0)
        return;

    for (i = 0; i <= WWIDTH; ++i)
    {
        float strata;
        int x_ref;
        int y_ref;

        if (!s21_ref_poprawny[i] || !isfinite(s21_ref_wartosci[i]))
            continue;

        strata = s21_ref_wartosci[i];
        if (CFG_GetParam(CFG_PARAM_S21_NORMALIZUJ) != 0U &&
            g_s21_ref_analiza.poprawny && isfinite(g_s21_ref_analiza.strata_min_db))
        {
            strata -= g_s21_ref_analiza.strata_min_db;
            if (strata < 0.0f)
                strata = 0.0f;
        }

        x_ref = X0 + i;
        y_ref = Y0 + (int)(strata / skala_db * (float)WHEIGHT);
        if (y_ref < Y0) y_ref = Y0;
        if (y_ref > Y0 + WHEIGHT) y_ref = Y0 + WHEIGHT;

        if (ma_poprzedni && x_ref - poprzedni_x <= 32)
            LCD_Line(LCD_MakePoint(poprzedni_x, poprzedni_y),
                     LCD_MakePoint(x_ref, y_ref), kolor_ref);

        poprzedni_x = x_ref;
        poprzedni_y = y_ref;
        ma_poprzedni = 1;
    }

    FONT_Write(FONT_FRAN, kolor_ref, BackGrColor, X0 + WWIDTH - 34, Y0 + 22, "REF");
}

static int S21_SpecCzestotliwoscNaX(uint32_t f_hz, uint32_t span_hz)
{
    if (span_hz == 0U || f_hz < fstart || f_hz > fstart + span_hz)
        return -1;
    return X0 + (int)(((uint64_t)(f_hz - fstart) * (uint64_t)WWIDTH) / (uint64_t)span_hz);
}

static void S21_RysujPionSpec(int x, LCDColor kolor)
{
    int y;
    if (x < X0 || x > X0 + WWIDTH)
        return;
    for (y = Y0; y <= Y0 + WHEIGHT; y += 9)
        LCD_VLine(LCD_MakePoint(x, y), 5, kolor);
}

static void S21_RysujProfilSpec(void)
{
    S21_PROFIL_SPEC_t profil;
    const LCDColor kolor = UI_KolorTekstu(UI_STYL_NIEAKTYWNY);
    const LCDColor kolor_limit = UI_KolorTekstu(UI_STYL_OSTRZEZENIE);
    const uint32_t span_hz = BSVALUES_TRACK[span21] * 1000U;
    int x_nom;

    if (s21_widok_info != 11U || !S21_PobierzProfilSpec(&profil) || span_hz == 0U)
        return;

    x_nom = S21_SpecCzestotliwoscNaX(profil.f_nom_hz, span_hz);
    if (x_nom >= 0)
    {
        S21_RysujPionSpec(x_nom, kolor);
        FONT_Write(FONT_FRAN, kolor, BackGrColor,
                   x_nom > X0 + WWIDTH - 35 ? x_nom - 30 : x_nom + 3,
                   Y0 + 3, "NOM");
    }

    if (profil.id == 1U)
    {
        /*
         * SFE 5.5MB: karta rodziny podaje minimum +/-75 kHz dla pasma -3 dB
         * oraz maksymalnie 6 dB insertion loss. Linie są tylko wzorcem
         * porównawczym - nie zastępują fizycznego zakończenia 600 om.
         */
        const int x_l = S21_SpecCzestotliwoscNaX(profil.f_nom_hz - 75000U, span_hz);
        const int x_h = S21_SpecCzestotliwoscNaX(profil.f_nom_hz + 75000U, span_hz);
        if (x_l >= 0) S21_RysujPionSpec(x_l, kolor_limit);
        if (x_h >= 0) S21_RysujPionSpec(x_h, kolor_limit);

        if (CFG_GetParam(CFG_PARAM_S21_NORMALIZUJ) == 0U && profil.il_max_db > 0.0f)
        {
            const int y = S21_PrzeliczDbNaY(profil.il_max_db);
            int x;
            if (y > Y0 && y < Y0 + WHEIGHT)
            {
                for (x = X0; x <= X0 + WWIDTH; x += 10)
                    LCD_HLine(LCD_MakePoint(x, y), 5, kolor_limit);
                FONT_Write(FONT_FRAN, kolor_limit, BackGrColor, X0 + 3,
                           y > Y0 + 10 ? y - 9 : y + 2, "IL6");
            }
        }
    }
    else if (profil.id == 2U && profil.bw3_ref_hz > 0.0f)
    {
        /* Referencja pomiarowa ~15 kHz, nie oficjalny limit katalogowy. */
        const uint32_t polowa = (uint32_t)(profil.bw3_ref_hz * 0.5f + 0.5f);
        const int x_l = profil.f_nom_hz > polowa
            ? S21_SpecCzestotliwoscNaX(profil.f_nom_hz - polowa, span_hz) : -1;
        const int x_h = S21_SpecCzestotliwoscNaX(profil.f_nom_hz + polowa, span_hz);
        if (x_l >= 0) S21_RysujPionSpec(x_l, kolor);
        if (x_h >= 0) S21_RysujPionSpec(x_h, kolor);
    }
}

static void S21_RysujTloRX(void)
{
    int y;
    int x_lokalny;
    const LCDColor kolor = UI_KolorTekstu(UI_STYL_OSTRZEZENIE);

    if (s21_widok_info != 9U || !s21_tlo_rx_wazne || !isfinite(s21_tlo_rx_db))
        return;

    y = S21_PrzeliczDbNaY(s21_tlo_rx_db);
    if (y <= Y0 || y >= Y0 + WHEIGHT)
        return;

    for (x_lokalny = X0; x_lokalny <= X0 + WWIDTH; x_lokalny += 8)
        LCD_HLine(LCD_MakePoint(x_lokalny, y), 4, kolor);

    FONT_Write(FONT_FRAN, kolor, BackGrColor, X0 + WWIDTH - 20,
               y > Y0 + 10 ? y - 9 : y + 2, "RX");
}

static void S21_RysujLimitKalibracji(void)
{
    const float limit_db = CFG_GetS21TlumikDb() + 10.0f;
    const float skala_db = S21_PobierzSkaleDb();
    const LCDColor kolor = UI_KolorTekstu(UI_STYL_NIEAKTYWNY);
    int y;
    int x_lokalny;

    if (s21_widok_info != 9U || !isfinite(limit_db) || limit_db <= 0.0f ||
        limit_db >= skala_db)
        return;

    y = S21_PrzeliczDbNaY(limit_db);
    if (y <= Y0 || y >= Y0 + WHEIGHT)
        return;

    for (x_lokalny = X0; x_lokalny <= X0 + WWIDTH; x_lokalny += 12)
        LCD_HLine(LCD_MakePoint(x_lokalny, y), 6, kolor);

    FONT_Write(FONT_FRAN, kolor, BackGrColor, X0 + 3,
               y > Y0 + 10 ? y - 9 : y + 2, "KAL");
}

static void Track_DrawCurve(void) // SelQu=1, if quartz measurement  SelEqu=1, if equal scales
{
#define LimitR 1999.f
    /*
     * Nie zmieniamy danych pomiarowych ani analizy Q/BW. To jest wyłącznie
     * sposób prezentacji: kolejne wiarygodne próbki tworzą polilinię.
     *
     * W praktyce tor S21 może odrzucić pojedynczą próbkę jako niewiarygodną.
     * Poprzednia wersja zerowała wtedy ciągłość rysowania i na stromym zboczu
     * filtru zostawały samotne kropki. Teraz pomijamy brakującą próbkę i
     * łączymy dwa najbliższe poprawne punkty. Dopiero duża luka oznacza realny
     * brak danych i pozostaje widoczna jako przerwa.
     */
    const int maks_luka_px = 32;
    int poprzedni_x = 0;
    int poprzedni_y = 0;
    int ma_poprzedni = 0;
    int i;

    if (!isMeasured)
        return;

    Track_DrawGrid(1);
    S21_RysujSladReferencyjny();
    S21_RysujLimitKalibracji();
    S21_RysujTloRX();
    S21_RysujProfilSpec();

    for (i = 0; i <= WWIDTH; ++i)
    {
        int x;
        int y;

        if (s21_punkt_poprawny == 0 || !s21_punkt_poprawny[i] || !isfinite(valuesmI[i]))
            continue;

        x = X0 + i;
        y = S21_PrzeliczDbNaY(valuesmI[i]);

        if (ma_poprzedni && (x - poprzedni_x) <= maks_luka_px)
        {
            S21_RysujOdcinekKrzywej(poprzedni_x, poprzedni_y, x, y);
        }
        else
        {
            /* Pierwszy punkt po dużej luce pozostaje jawnie zaznaczony. */
            LCD_SetPixel(LCD_MakePoint(x, y), CurvColor);
            if (FatLines)
            {
                if (y > Y0)
                    LCD_SetPixel(LCD_MakePoint(x, y - 1), CurvColor);
                if (y < Y0 + WHEIGHT)
                    LCD_SetPixel(LCD_MakePoint(x, y + 1), CurvColor);
            }
        }

        poprzedni_x = x;
        poprzedni_y = y;
        ma_poprzedni = 1;
    }

    S21_RysujMarkeryAnalizy();
    StrictDrawCursor();
}

static void RedrawWindowS21(int justGraphDraw)
{
    Track_DrawGrid(justGraphDraw);
    Track_DrawCurve(); // 1

    if (!justGraphDraw)
    {
        TRACK_DrawFootText();
    }
    DrawMeasuredValues();
    DrawAutoText();
}

#define XX0 190
#define YY0 42

static void save_snapshot(void)
{
    //static const TCHAR *sndir = "/aa/snapshot";
    //char path[64];
    //char wbuf[256];
    char *fname = 0;
    //uint32_t i = 0;
    //FRESULT fr = FR_OK;

    if (!isMeasured)
        return;

    Date_Time_Stamp();

    fname = SCREENSHOT_SelectFileName();

    if (strlen(fname) == 0)
        return;

    SCREENSHOT_DeleteOldest();
    if (CFG_GetParam(CFG_PARAM_SCREENSHOT_FORMAT))
        SCREENSHOT_SavePNG(fname);
    else
        SCREENSHOT_Save(fname);

    //DrawSavedText();
    //static const char* txt = "  Snapshot saved  ";
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_AKTYWNY), BackGrColor, 165, Y0 + WHEIGHT + 16 + 16,
               JEZYK_Wybierz("Zrzut zapisany", "Snapshot saved", "Bild gespeichert", "Снимок сохранён"));
    TRACK_DrawFootText();
    DrawAutoText();

    return;
}

//=====================================================================
//TRACK PROC
//---------------------------------------------------------------------

void Track_Proc(void) // ==================== S21 screen ====================================
{
    uint32_t fxkHzs; //Scan range start frequency, in kHz
    const uint32_t poprzedni_backgr = BackGrColor;
    const uint32_t poprzedni_curv = CurvColor;
    const uint32_t poprzedni_text = TextColor;

    /* S21 potrzebuje niezaleznego CLK2 jako zrodla transmisyjnego. W wariancie
     * 2x ADF ten sam CLK2 jest referencja 27 MHz i nie wolno go przestrajac. */
    if (!GEN_CzyWyjscieDodatkoweObslugiwane())
    {
        UI_RysujEkranPrzejsciowy(
            JEZYK_Tekst(TEKST_S21_TYTUL),
            JEZYK_Wybierz("Niedostępne: CLK2 pracuje jako wzorzec ADF.",
                          "Unavailable: CLK2 is used as the ADF reference.",
                          "Nicht verfügbar: CLK2 ist ADF-Referenz.",
                          "Недоступно: CLK2 используется как опора ADF."));
        Sleep(1800U);
        return;
    }
    uint32_t fxs;
    //extern int OSL_ENTRIES;

    redrawRequired = exitScan = 0;
    IsInverted = false;
    // Bufor wyniku i osobna maska wiarygodności każdego punktu.
    valuesmI = (float *)SDRH_malloc(sizeof(float) * (WWIDTH + 1U));
    s21_napiecie_v_mv = (float *)SDRH_malloc(sizeof(float) * (WWIDTH + 1U));
    s21_napiecie_i_mv = (float *)SDRH_malloc(sizeof(float) * (WWIDTH + 1U));
    s21_rozrzut_i_proc = (float *)SDRH_malloc(sizeof(float) * (WWIDTH + 1U));
    s21_punkt_poprawny = (uint8_t *)SDRH_malloc(sizeof(uint8_t) * (WWIDTH + 1U));
    s21_punkt_zmierzony = (uint8_t *)SDRH_malloc(sizeof(uint8_t) * (WWIDTH + 1U));
    s21_ref_wartosci = (float *)SDRH_malloc(sizeof(float) * (WWIDTH + 1U));
    s21_ref_poprawny = (uint8_t *)SDRH_malloc(sizeof(uint8_t) * (WWIDTH + 1U));
    if (valuesmI == 0 || s21_napiecie_v_mv == 0 || s21_napiecie_i_mv == 0 ||
        s21_rozrzut_i_proc == 0 || s21_punkt_poprawny == 0 || s21_punkt_zmierzony == 0 ||
        s21_ref_wartosci == 0 || s21_ref_poprawny == 0)
    {
        if (valuesmI != 0)
            SDRH_free(valuesmI);
        if (s21_napiecie_v_mv != 0)
            SDRH_free(s21_napiecie_v_mv);
        if (s21_napiecie_i_mv != 0)
            SDRH_free(s21_napiecie_i_mv);
        if (s21_rozrzut_i_proc != 0)
            SDRH_free(s21_rozrzut_i_proc);
        if (s21_punkt_poprawny != 0)
            SDRH_free(s21_punkt_poprawny);
        if (s21_punkt_zmierzony != 0)
            SDRH_free(s21_punkt_zmierzony);
        if (s21_ref_wartosci != 0)
            SDRH_free(s21_ref_wartosci);
        if (s21_ref_poprawny != 0)
            SDRH_free(s21_ref_poprawny);
        valuesmI = 0;
        s21_napiecie_v_mv = 0;
        s21_napiecie_i_mv = 0;
        s21_rozrzut_i_proc = 0;
        s21_punkt_poprawny = 0;
        s21_punkt_zmierzony = 0;
        s21_ref_wartosci = 0;
        s21_ref_poprawny = 0;
        GEN_SetTXFreq(0);

        /* Brak pamięci nie może wyglądać jak zawieszenie lub czarny ekran. */
        BSP_LCD_SelectLayer(activeLayerS21);
        LCD_FillAll(BackGrColor);
        UI_RysujNaglowek(JEZYK_Tekst(TEKST_S21_TYTUL));
        UI_RysujPoleStatusu(55, 78, 370, 92,
                            JEZYK_Wybierz("Błąd pamięci S21", "S21 memory error",
                                          "S21-Speicherfehler", "Ошибка памяти S21"),
                            JEZYK_Wybierz("Brak pamięci roboczej dla buforów wykresu. Uruchom ponownie analizator; jeśli błąd wraca, zgłoś ten ekran.",
                                          "Not enough working memory for graph buffers. Restart the analyzer; if the error returns, report this screen.",
                                          "Nicht genügend Arbeitsspeicher für die Diagrammpuffer. Analysator neu starten; bei erneutem Fehler diesen Bildschirm melden.",
                                          "Недостаточно рабочей памяти для буферов графика. Перезапустите анализатор; если ошибка повторится, сообщите об этом экране."),
                            UI_STYL_OSTRZEZENIE);
        LCD_ShowActiveLayerOnly();
        Sleep(4500U);
        return;
    }
    {
        uint32_t i;
        for (i = 0U; i <= WWIDTH; ++i)
        {
            valuesmI[i] = NAN;
            s21_napiecie_v_mv[i] = NAN;
            s21_napiecie_i_mv[i] = NAN;
            s21_rozrzut_i_proc[i] = NAN;
            s21_punkt_poprawny[i] = 0U;
            s21_punkt_zmierzony[i] = 0U;
            s21_ref_wartosci[i] = NAN;
            s21_ref_poprawny[i] = 0U;
        }
    }
    memset(&g_s21_analiza, 0, sizeof(g_s21_analiza));
    memset(&g_s21_ref_analiza, 0, sizeof(g_s21_ref_analiza));
    s21_ref_aktywny = 0U;
    s21_widok_info = 0U;
    s21_tlo_rx_wazne = 0U;
    s21_tlo_rx_db = NAN;
    s21_tlo_rx_rozrzut_proc = NAN;
    s21_tlo_licznik_odswiezania = 0U;
    S21_AktualizujMetaKalibracji();
    activeLayerS21 = 1;
    BSP_LCD_SelectLayer(activeLayerS21);
    LCD_ShowActiveLayerOnly();
    cursorVisibleS21 = 0; // in the beginning not visible
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 1)
        cursorPos = WWIDTH / 2;
    else
        cursorPos = 0;
    /*
     * SetColours pozostaje źródłem ustawień globalnych innych ekranów. S21
     * nakłada na czas swojego działania spójną paletę Retro, a przy wyjściu
     * przywraca wcześniejsze kolory bez dodatkowego zapisu konfiguracji.
     */
    SetColours();
    S21_ZastosujPaleteWykresu();
    LCD_FillAll(BackGrColor);
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_S21_TYTUL));
    FONT_Write(FONT_FRAN, TextColor, BackGrColor,
               66, 36, JEZYK_Tekst(TEKST_S21_SKALARNE_INFO));
    while (TOUCH_IsPressed())
        ;
    //Load Calibration
    if (!OSL_IsTXCorrLoaded())
    {
        OSL_LoadTXCorr();
#ifdef _DEBUG_UART
        DBG_Str("Load VNA Calibration File \r\n");
#endif
    }
    if (!OSL_IsTXCorrLoaded())
    {
        // Brak pliku kalibracji S21 nie jest awaria calego przyrzadu.
        // Informujemy uzytkownika i wracamy do menu bez uruchamiania pomiaru.
        char pomoc_s21[128];
        UI_RysujPoleStatusu(65, 82, 350, 78, JEZYK_Tekst(TEKST_S21_TYTUL),
                            JEZYK_Tekst(TEKST_S21_BRAK_KALIBRACJI), UI_STYL_OSTRZEZENIE);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor,
                   38, 172, JEZYK_Wybierz("Kalibracja -> S21: krok 1 S2 -> S1 bez tłumika.",
                                          "Calibration -> S21: step 1 S2 -> S1, no attenuator.",
                                          "Kalibrierung -> S21: Schritt 1 S2 -> S1 ohne Dämpfer.",
                                          "Калибровка -> S21: шаг 1 S2 -> S1 без аттенюатора."));
        {
            char tlumik[32];
            CFG_FormatujS21Tlumik(tlumik, sizeof(tlumik));
            snprintf(pomoc_s21, sizeof(pomoc_s21), "%s %s",
                     JEZYK_Wybierz("Krok 2: wstaw tłumik 50 om:", "Step 2: insert 50-ohm attenuator:",
                                   "Schritt 2: 50-Ohm-Dämpfer einsetzen:", "Шаг 2: установите аттенюатор 50 Ом:"),
                     tlumik);
        }
        FONT_Write(FONT_FRAN, TextColor, BackGrColor,
                   38, 194, pomoc_s21);
        Sleep(4200);
        SDRH_free(valuesmI);
        SDRH_free(s21_napiecie_v_mv);
        SDRH_free(s21_napiecie_i_mv);
        SDRH_free(s21_rozrzut_i_proc);
        SDRH_free(s21_punkt_poprawny);
        SDRH_free(s21_punkt_zmierzony);
        SDRH_free(s21_ref_wartosci);
        SDRH_free(s21_ref_poprawny);
        valuesmI = 0;
        s21_napiecie_v_mv = 0;
        s21_napiecie_i_mv = 0;
        s21_rozrzut_i_proc = 0;
        s21_punkt_poprawny = 0;
        s21_punkt_zmierzony = 0;
        s21_ref_wartosci = 0;
        s21_ref_poprawny = 0;
        BackGrColor = poprzedni_backgr;
        CurvColor = poprzedni_curv;
        TextColor = poprzedni_text;
        return;
    }
    osl_txCorr = (OSL_ERRCORR *)WORK_Ptr;

    //Load saved frequency and span values from config file
    uint32_t fbkup = CFG_GetParam(CFG_PARAM_S21_F1);

    //2019.03.26
    //if (fbkup != 0 && fbkup >= BAND_FMIN && fbkup <= CFG_GetParam(CFG_PARAM_BAND_FMAX) && (fbkup % 100) == 0)
    if (fbkup != 0 && fbkup >= CFG_GetParam(CFG_PARAM_BAND_FMIN) &&
        fbkup <= CFG_GetParam(CFG_PARAM_BAND_FMAX) && (fbkup % 100) == 0)
    {
        f1 = fbkup;
    }
    else
        f1 = 14000000;
    CFG_SetParam(CFG_PARAM_S21_F1, f1);
    CFG_Flush();

    span21 = (BANDSPAN)CFG_GetParam(CFG_PARAM_S21_SPAN);
    if ((uint32_t)span21 > (uint32_t)BS100M)
        span21 = BS400;
    autoScanFactor = (char)CFG_GetParam(CFG_PARAM_S21_AUTOSPEED);
    if (autoScanFactor < 1 || autoScanFactor > 20)
        autoScanFactor = 8;
    MakeFstart();

    /*
     * Obie warstwy LTDC muszą zawierać kompletny ekran zanim pokażemy
     * warstwę aktywną. W poprzednim kodzie siatka była rysowana na warstwie
     * ukrytej, po czym program przełączał się z powrotem na wyczyszczoną
     * warstwę aktywną. Efektem był czarny ekran po komunikacie
     * „Przygotowanie funkcji”; dopiero późniejsza akcja użytkownika
     * przerysowywała widok.
     */
    BSP_LCD_SelectLayer(activeLayerS21);
    LCD_FillAll(BackGrColor);
    Track_DrawGrid(0);
    TRACK_DrawFootText();
    DrawAutoText();

    BSP_LCD_SelectLayer((1U + activeLayerS21) % 2U);
    LCD_FillAll(BackGrColor);
    Track_DrawGrid(0);
    TRACK_DrawFootText();
    DrawAutoText();

    BSP_LCD_SelectLayer(activeLayerS21);
    LCD_ShowActiveLayerOnly();
    LCD_BuforKlatkiInicjalizuj(&s21_bufor_klatki, (uint8_t)activeLayerS21);
    exitScan = 0;
    redrawRequired = 0;
    autofast = 0;

    for (;;)
    {
        Sleep(0);
        if (TOUCH_Poll(&pt0))
        { // touch check
            TrackTouchExecute(pt0);
            if (exitScan == 1)
                break;
        }
        else
        {
            cursorChangeCount = 0;
            trackbeep = 0;
        }                    //end of
        if (touchIndex == 4) // new frequency
        {
            fxs = CFG_GetParam(CFG_PARAM_S21_F1);
            fxkHzs = fxs / 1000;
            if (PanFreqWindow(&fxkHzs, (BANDSPAN *)&span21))
            {
                f1 = fxkHzs * 1000;
                CFG_SetParam(CFG_PARAM_S21_F1, f1);
                CFG_SetParam(CFG_PARAM_S21_SPAN, span21);
                CFG_Flush();
                MakeFstart(); // and span
                                    isMeasured = 0;
                Track_DrawGrid(2);
                /* BSP_LCD_SelectLayer((1+activeLayerS21)%2);
                Track_DrawGrid(2);
                BSP_LCD_SelectLayer(activeLayerS21);*/
                redrawRequired = 0;
                IsInverted = false;
            }
            touchIndex = 255;
        }

        if (autofast) //Auto Scan
        {
            /*
             * Pomiar odbywa sie bez czyszczenia wykresu. Po skanie skladamy
             * cala nowa klatke na warstwie niewidocznej i pokazujemy ja jednym
             * przelaczeniem LTDC. Dzięki temu Auto nie miga.
             */
            Scan21Fast();
            if (autofast && isMeasured)
                S21_RysujGlownyEkranBezMigania();
            redrawRequired = 0;
        }
        if (redrawRequired)
        {
            Scan21(0);
            if (isMeasured)
                S21_RysujGlownyEkranBezMigania();
            redrawRequired = 0;
        }
    }                   //end of for(;;)
    GEN_SetClk2Freq(0); // CLK2 off
    //Release Memory
    SDRH_free(valuesmI);
    SDRH_free(s21_napiecie_v_mv);
    SDRH_free(s21_napiecie_i_mv);
    SDRH_free(s21_rozrzut_i_proc);
    SDRH_free(s21_punkt_poprawny);
    SDRH_free(s21_punkt_zmierzony);
    SDRH_free(s21_ref_wartosci);
    SDRH_free(s21_ref_poprawny);
    valuesmI = 0;
    s21_napiecie_v_mv = 0;
    s21_napiecie_i_mv = 0;
    s21_rozrzut_i_proc = 0;
    s21_punkt_poprawny = 0;
    s21_punkt_zmierzony = 0;
    s21_ref_wartosci = 0;
    s21_ref_poprawny = 0;
    s21_ref_aktywny = 0U;
    isMeasured = 0;

    /* S21 używa własnej palety tylko lokalnie; nie zmieniamy kolorów innych ekranów. */
    BackGrColor = poprzedni_backgr;
    CurvColor = poprzedni_curv;
    TextColor = poprzedni_text;
}
