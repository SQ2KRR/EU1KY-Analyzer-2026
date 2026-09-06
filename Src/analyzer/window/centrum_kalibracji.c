/*
 * EU1KY-PL 2026 - Ustawienia > Kalibracja
 *
 * Ten ekran zawiera wyłącznie czynności kalibracyjne i ich stan.
 * Diagnostyka sprzętu, karty SD oraz weryfikacja wzorcami należą do
 * Ustawienia > Diagnostyka i nie są dublowane w Kalibracji.
 *
 * Podstawowa zależność metrologiczna: HW -> OSL.
 * S21 i TDR Vf są kalibracjami opcjonalnymi. L/C korzysta ze wspólnej OSL S11.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "centrum_kalibracji.h"
#include "config.h"
#include "diagnostyka.h"
#include "font.h"
#include "gen.h"
#include "jezyk.h"
#include "kalibracja_meta.h"
#include "komunikaty.h"
#include "mainwnd.h"
#include "measurement.h"
#include "num_keypad.h"
#include "oslcal.h"
#include "oslfile.h"
#include "si5351.h"
#include "textbox.h"
#include "touch.h"
#include "tdr.h"
#include "ui_edytor_liczby.h"
#include "ui_wspolny.h"
#include "wejscia_uzytkownika.h"

extern void Sleep(uint32_t ms);
extern void Measure_LCR_Proc(void);

#define CK_FMIN_KAL_MIN_HZ 100000U
#define CK_FMIN_KAL_MAX_HZ 500000U
#define CK_FMIN_KAL_KROK_HZ 100000U
#define CK_FMAX_KAL_KROK_HZ 1000000U

typedef enum
{
    CK_EKRAN_GLOWNY = 0,
    CK_EKRAN_GENERATOR,
    CK_EKRAN_WYJSCIE
} CK_EKRAN_t;

typedef enum
{
    CK_AKCJA_BRAK = 0,
    CK_AKCJA_GENERATOR,
    CK_AKCJA_WSTECZ,
    CK_AKCJA_SI5351,
    CK_AKCJA_KONFIG_RF,
    CK_AKCJA_SPRAWDZ_IF,
    CK_AKCJA_LIMIT_BEZPOSR,
    CK_AKCJA_HARMONICZNA,
    CK_AKCJA_ZAKRES,
    CK_AKCJA_HW,
    CK_AKCJA_OSL,
    CK_AKCJA_S21,
    CK_AKCJA_TDR
} CK_AKCJA_t;

typedef enum
{
    CK_STAN_OK = 0,
    CK_STAN_BEZ_HISTORII,
    CK_STAN_UWAGA,
    CK_STAN_BRAK,
    CK_STAN_OPCJONALNY
} CK_STAN_t;

typedef struct
{
    uint8_t sd_ok;
    uint8_t si5351_ok;
    uint8_t konfiguracja_rf_ok;
    uint8_t sprawdzenie_if_ok;
    uint8_t sprawdzenie_if_meta_jest;
    uint8_t sprawdzenie_if_z_hw;
    uint8_t hw_zaladowana;
    uint8_t hw_meta_jest;
    uint8_t hw_aktualna;
    uint8_t osl_zaladowana;
    uint8_t osl_meta_jest;
    uint8_t osl_aktualna;
    uint8_t s21_zaladowana;
    uint8_t s21_meta_jest;
    uint8_t s21_aktualna;
    uint8_t tdr_meta_jest;
    int32_t profil_osl;
    uint32_t fmin_hz;
    uint32_t fmax_hz;
    uint32_t fmax_bezposrednia_hz;
    uint32_t fmax_efektywne_hz;
    uint32_t kwarc_hz;
    int32_t korekcja_hz;
    uint8_t hmax;
} CK_STAN_CALOSCI_t;

static volatile CK_AKCJA_t ck_akcja = CK_AKCJA_BRAK;
static uint8_t ck_sprawdzenie_if_sesja_ok = 0U;

/* Wspólny wybór tekstu dla czterech oficjalnych języków firmware. */
static const char *CK_T(const char *pl, const char *en, const char *de, const char *ru)
{
    return JEZYK_Wybierz(pl, en, de, ru);
}

static UI_STYL_t CK_Styl(CK_STAN_t stan)
{
    switch (stan)
    {
    case CK_STAN_OK: return UI_STYL_AKTYWNY;
    case CK_STAN_BEZ_HISTORII: return UI_STYL_NORMALNY;
    case CK_STAN_UWAGA: return UI_STYL_OSTRZEZENIE;
    case CK_STAN_BRAK: return UI_STYL_OSTRZEZENIE;
    case CK_STAN_OPCJONALNY:
    default: return UI_STYL_NIEAKTYWNY;
    }
}

static const char *CK_TekstStanu(CK_STAN_t stan)
{
    switch (stan)
    {
    case CK_STAN_OK: return JEZYK_Tekst(TEKST_STATUS_OK);
    case CK_STAN_BEZ_HISTORII: return CK_T("OK bez historii", "OK, no history", "OK, ohne Verlauf", "OK, без истории");
    case CK_STAN_UWAGA: return CK_T("SPRAWDŹ", "CHECK", "PRÜFEN", "ПРОВЕРИТЬ");
    case CK_STAN_BRAK: return JEZYK_Tekst(TEKST_STATUS_BRAK);
    case CK_STAN_OPCJONALNY:
    default: return CK_T("opcjonalna", "optional", "optional", "необяз.");
    }
}

/*
 * Główny ekran Kalibracji ma być równocześnie miejscem wykonania czynności
 * i krótką historią. Dzięki temu nie potrzebujemy drugiego, niemal takiego
 * samego ekranu „Stan metrologiczny”. Datę pokazujemy tylko wtedy, gdy
 * pochodzi z wiarygodnego RTC i istnieją metadane danej czynności.
 */
static void CK_FormatujStanZData(CK_STAN_t stan, KAL_META_TYP_t typ, int32_t profil,
                                 char *bufor, size_t rozmiar)
{
    KAL_META_DANE_t meta;
    const char *stan_tekst = CK_TekstStanu(stan);

    if (bufor == NULL || rozmiar == 0U)
        return;

    memset(&meta, 0, sizeof(meta));
    if ((stan == CK_STAN_OK || stan == CK_STAN_UWAGA) &&
        KAL_META_Pobierz(typ, profil, &meta) && meta.czas_z_rtc)
    {
        snprintf(bufor, rozmiar, "%s %02lu.%02lu %02lu:%02lu",
                 stan_tekst,
                 (unsigned long)(meta.data_yyyymmdd % 100U),
                 (unsigned long)((meta.data_yyyymmdd / 100U) % 100U),
                 (unsigned long)(meta.czas_hhmmss / 10000U),
                 (unsigned long)((meta.czas_hhmmss / 100U) % 100U));
        return;
    }

    snprintf(bufor, rozmiar, "%s", stan_tekst);
}

static uint8_t CK_MetaAktualna(KAL_META_TYP_t typ, int32_t profil, uint8_t zaladowana,
                               uint32_t dodatkowo_niedozwolone, uint8_t *meta_jest)
{
    KAL_META_DANE_t meta;
    KAL_META_OCENA_t ocena;

    if (meta_jest != 0)
        *meta_jest = 0U;
    if (!zaladowana)
        return 0U;

    memset(&meta, 0, sizeof(meta));
    memset(&ocena, 0, sizeof(ocena));

    /*
     * Stary profil bez KAL_META nadal może być poprawną historyczną kalibracją.
     * Nie wolno wymuszać jej skasowania ani ponowienia tylko dlatego, że
     * powstała przed dodaniem metadanych. Centrum oznacza taki stan jako zgodny, ale bez historii
     * i kieruje użytkownika do weryfikacji wzorcami zamiast do kalibracji od zera.
     */
    if (!KAL_META_Pobierz(typ, profil, &meta))
        return 0U;

    if (meta_jest != 0)
        *meta_jest = 1U;
    KAL_META_Ocen(typ, profil, &meta, &ocena);
    if (ocena.uwagi & (KAL_META_UWAGA_KONFIGURACJA |
                       KAL_META_UWAGA_BRAK_PLIKU |
                       KAL_META_UWAGA_PLIK_ZMIENIONY |
                       dodatkowo_niedozwolone))
        return 0U;

    return 1U;
}

/*
 * Sprawdzenie generatora/IF ma własny trwały zapis od V2.1. Dla starszych danych
 * potrafimy jednak bezpiecznie wykorzystać metadane HW: udany skan HW jest
 * możliwy dopiero po automatycznym, pozytywnym teście Si5351/IF. To pozwala
 * nie pokazywać fałszywego „nie sprawdzono” po aktualizacji firmware.
 */
static uint8_t CK_PobierzAktualneSprawdzenieIF(KAL_META_DANE_t *meta_wynik,
                                       uint8_t *meta_jest, uint8_t *z_hw)
{
    KAL_META_DANE_t meta;
    KAL_META_OCENA_t ocena;

    if (meta_wynik != 0)
        memset(meta_wynik, 0, sizeof(*meta_wynik));
    if (meta_jest != 0)
        *meta_jest = 0U;
    if (z_hw != 0)
        *z_hw = 0U;

    memset(&meta, 0, sizeof(meta));
    memset(&ocena, 0, sizeof(ocena));
    if (KAL_META_Pobierz(KAL_META_IF, -1, &meta))
    {
        KAL_META_Ocen(KAL_META_IF, -1, &meta, &ocena);
        if (meta_jest != 0)
            *meta_jest = 1U;
        if ((ocena.uwagi & KAL_META_UWAGA_KONFIGURACJA) == 0U)
        {
            if (meta_wynik != 0)
                *meta_wynik = meta;
            return 1U;
        }
    }

    memset(&meta, 0, sizeof(meta));
    memset(&ocena, 0, sizeof(ocena));
    if (KAL_META_Pobierz(KAL_META_HW, -1, &meta))
    {
        KAL_META_Ocen(KAL_META_HW, -1, &meta, &ocena);
        if ((ocena.uwagi & (KAL_META_UWAGA_KONFIGURACJA |
                            KAL_META_UWAGA_BRAK_PLIKU |
                            KAL_META_UWAGA_PLIK_ZMIENIONY)) == 0U)
        {
            if (meta_wynik != 0)
                *meta_wynik = meta;
            if (z_hw != 0)
                *z_hw = 1U;
            return 1U;
        }
    }

    return ck_sprawdzenie_if_sesja_ok;
}

static uint8_t CK_KonfiguracjaRFOk(void)
{
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    /*
     * To nie jest test jakości toru RF. Sprawdzamy wyłącznie, czy zapisane
     * parametry mają sens formalny. Legalny, lecz ambitny plan (np. 600 MHz)
     * nie może blokować wejścia do HW. Osiągalność częstotliwości sprawdza
     * dopiero rzeczywisty start skanu i podaje wtedy konkretną przyczynę.
     */
    if (typ > CFG_SYNTH_SI5338A)
        return 0U;
    if (fmin < BAND_FMIN || fmin >= fmax || fmax > MAX_BAND_FREQ)
        return 0U;

    if (typ == CFG_SYNTH_SI5351)
    {
        const uint32_t fmax_bezposrednia = CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);
        const uint32_t kwarc = CFG_GetParam(CFG_PARAM_SI5351_XTAL_FREQ);
        const uint8_t hmax = GEN_MaksHarmoniczna();

        if (fmax_bezposrednia != 160000000U && fmax_bezposrednia != 200000000U &&
            fmax_bezposrednia != 260000000U && fmax_bezposrednia != 270000000U &&
            fmax_bezposrednia != 280000000U && fmax_bezposrednia != 290000000U)
            return 0U;
        if (kwarc < 20000000U || kwarc > 40000000U)
            return 0U;
        if (hmax != 1U && hmax != 3U && hmax != 5U && hmax != 7U)
            return 0U;
    }
    return 1U;
}

static void CK_PobierzStan(CK_STAN_CALOSCI_t *stan)
{
    CFG_SD_DIAGNOSTYKA_t sd;
    KAL_META_DANE_t meta_tdr;

    memset(stan, 0, sizeof(*stan));
    memset(&sd, 0, sizeof(sd));
    CFG_SD_PobierzDiagnostyke(&sd);

    stan->sd_ok = (uint8_t)(CFG_CzyKartaSDDostepna() &&
                            sd.system_plikow != CFG_SD_STAN_BLAD &&
                            sd.zapis != CFG_SD_STAN_BLAD);
    stan->si5351_ok = si5351_IsPresent() ? 1U : 0U;
    stan->konfiguracja_rf_ok = CK_KonfiguracjaRFOk();
    stan->sprawdzenie_if_ok = CK_PobierzAktualneSprawdzenieIF(0,
                                                        &stan->sprawdzenie_if_meta_jest,
                                                        &stan->sprawdzenie_if_z_hw);

    stan->profil_osl = OSL_GetSelected();
    stan->hw_zaladowana = OSL_IsErrCorrLoaded() ? 1U : 0U;
    stan->osl_zaladowana = OSL_IsSelectedValid() ? 1U : 0U;
    stan->s21_zaladowana = OSL_IsTXCorrLoaded() ? 1U : 0U;

    stan->hw_aktualna = CK_MetaAktualna(KAL_META_HW, -1, stan->hw_zaladowana, 0U, &stan->hw_meta_jest);
    stan->osl_aktualna = CK_MetaAktualna(KAL_META_OSL, stan->profil_osl,
                                         stan->osl_zaladowana, KAL_META_UWAGA_HW_ZMIENIONE, &stan->osl_meta_jest);
    stan->s21_aktualna = CK_MetaAktualna(KAL_META_S21, -1, stan->s21_zaladowana, 0U, &stan->s21_meta_jest);

    memset(&meta_tdr, 0, sizeof(meta_tdr));
    stan->tdr_meta_jest = KAL_META_Pobierz(KAL_META_TDR, -1, &meta_tdr) ? 1U : 0U;

    stan->fmin_hz = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    stan->fmax_hz = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    stan->fmax_bezposrednia_hz = CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);
    stan->fmax_efektywne_hz = GEN_MaksCzestotliwoscEfektywna();
    stan->kwarc_hz = CFG_GetParam(CFG_PARAM_SI5351_XTAL_FREQ);
    stan->korekcja_hz = (int32_t)CFG_GetParam(CFG_PARAM_SI5351_CORR);
    stan->hmax = GEN_MaksHarmoniczna();
}

static TEXTBOX_t CK_Przycisk(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                              const char *tekst, UI_STYL_t styl, void (*cb)(void), uint8_t rola)
{
    TEXTBOX_t p = {0};
    p.x0 = x;
    p.y0 = y;
    p.text = tekst;
    p.font = FONT_FRAN;
    p.width = w;
    p.height = h;
    p.center = 1;
    p.border = 1;
    p.fgcolor = UI_KolorTekstu(styl);
    p.bgcolor = UI_KolorTlaPrzycisku(styl);
    p.cb = cb;
    p.rola = rola;
    return p;
}

#define CK_CB(nazwa, wartosc) static void nazwa(void) { ck_akcja = (wartosc); }
CK_CB(CK_CB_Wstecz, CK_AKCJA_WSTECZ)
CK_CB(CK_CB_KonfigRF, CK_AKCJA_KONFIG_RF)
CK_CB(CK_CB_SprawdzIF, CK_AKCJA_SPRAWDZ_IF)
#undef CK_CB

static void CK_CzekajNaPuszczenie(void)
{
    while (TOUCH_IsPressed())
        Sleep(0);
}

static CK_AKCJA_t CK_AkcjaPolaGlownego(LCDPoint punkt)
{
    UI_PROSTOKAT_t pole;
#define CK_CZY_POLE(x_, y_, w_, h_) \
    (pole = (UI_PROSTOKAT_t){ (x_), (y_), (w_), (h_) }, UI_CzyPunktWObszarze(punkt, &pole))

    /* Generator/IF jest wspólnym warunkiem kalibracji RF, dlatego zajmuje
     * osobny, pełnoszeroki wiersz. Pozostałe pola uruchamiają już wyłącznie
     * właściwe procedury kalibracyjne. */
    if (CK_CZY_POLE(12U, 40U, 456U, 43U)) return CK_AKCJA_GENERATOR;
    if (CK_CZY_POLE(12U, 88U, 220U, 43U)) return CK_AKCJA_HW;
    if (CK_CZY_POLE(248U, 88U, 220U, 43U)) return CK_AKCJA_OSL;
    if (CK_CZY_POLE(12U, 136U, 220U, 43U)) return CK_AKCJA_S21;
    if (CK_CZY_POLE(248U, 136U, 220U, 43U)) return CK_AKCJA_TDR;
#undef CK_CZY_POLE
    return CK_AKCJA_BRAK;
}

static CK_AKCJA_t CK_EkranGlowny(void)
{
    CK_STAN_CALOSCI_t stan;
    TEXTBOX_CTX_t ctx;
    TEXTBOX_t b_back;
    char osl_label[32];
    char gen_status[48];
    char hw_status[40];
    char osl_status[40];
    char s21_status[40];
    char tdr_status[40];
    CK_STAN_t st_hw, st_osl, st_s21, st_tdr;

    CK_PobierzStan(&stan);
    st_hw = !stan.hw_zaladowana ? CK_STAN_BRAK : (!stan.hw_meta_jest ? CK_STAN_BEZ_HISTORII : (stan.hw_aktualna ? CK_STAN_OK : CK_STAN_UWAGA));
    st_osl = !stan.osl_zaladowana ? CK_STAN_BRAK : (!stan.osl_meta_jest ? CK_STAN_BEZ_HISTORII : (stan.osl_aktualna ? CK_STAN_OK : CK_STAN_UWAGA));
    st_s21 = !stan.s21_zaladowana ? CK_STAN_OPCJONALNY : (!stan.s21_meta_jest ? CK_STAN_BEZ_HISTORII : (stan.s21_aktualna ? CK_STAN_OK : CK_STAN_UWAGA));
    st_tdr = stan.tdr_meta_jest ? CK_STAN_OK : CK_STAN_OPCJONALNY;

    if (CFG_GetParam(CFG_PARAM_SYNTH_TYPE) == CFG_SYNTH_SI5351)
    {
        snprintf(gen_status, sizeof(gen_status), "%s / %s",
                 stan.si5351_ok ? JEZYK_Tekst(TEKST_STATUS_OK) : JEZYK_Tekst(TEKST_STATUS_BRAK),
                 stan.sprawdzenie_if_ok ? CK_T("IF OK", "IF OK", "ZF OK", "ПЧ OK")
                                         : CK_T("IF --", "IF --", "ZF --", "ПЧ --"));
    }
    else
    {
        snprintf(gen_status, sizeof(gen_status), "%s / %s", GEN_PobierzNazweSyntezera(),
                 CK_T("niezweryfikowany", "unverified", "nicht verifiziert", "не проверен"));
    }

    snprintf(osl_label, sizeof(osl_label), "OSL %s", stan.profil_osl >= 0 ? OSL_GetSelectedName() : "-");
    CK_FormatujStanZData(st_hw, KAL_META_HW, -1, hw_status, sizeof(hw_status));
    CK_FormatujStanZData(st_osl, KAL_META_OSL, stan.profil_osl, osl_status, sizeof(osl_status));
    CK_FormatujStanZData(st_s21, KAL_META_S21, -1, s21_status, sizeof(s21_status));
    CK_FormatujStanZData(st_tdr, KAL_META_TDR, -1, tdr_status, sizeof(tdr_status));

    UI_WyczyscEkran();
    UI_RysujNaglowek(CK_T("Kalibracja", "Calibration", "Kalibrierung", "Калибровка"));

    /* Kalibracja nie dubluje Diagnostyki. Karta SD jest sprawdzana dopiero
     * przed zapisem współczynników i w razie problemu podawana jest konkretna
     * ścieżka do Ustawienia > Diagnostyka > SD/FAT. */
    UI_RysujPoleStatusu(12, 40, 456, 43,
                        CFG_GetParam(CFG_PARAM_SYNTH_TYPE) == CFG_SYNTH_SI5351
                            ? CK_T("Generator / tor IF", "Generator / IF path", "Generator / ZF-Pfad", "Генератор / тракт ПЧ")
                            : CK_T("Generator RF", "RF generator", "HF-Generator", "RF генератор"),
                        gen_status,
                        CFG_GetParam(CFG_PARAM_SYNTH_TYPE) == CFG_SYNTH_SI5351
                            ? ((stan.si5351_ok && stan.konfiguracja_rf_ok) ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE)
                            : (stan.konfiguracja_rf_ok ? UI_STYL_NORMALNY : UI_STYL_OSTRZEZENIE));
    UI_RysujPoleStatusu(12, 88, 220, 43, "HW", hw_status, CK_Styl(st_hw));
    UI_RysujPoleStatusu(248, 88, 220, 43, osl_label, osl_status, CK_Styl(st_osl));
    UI_RysujPoleStatusu(12, 136, 220, 43, "S21", s21_status, CK_Styl(st_s21));
    UI_RysujPoleStatusu(248, 136, 220, 43, "TDR Vf", tdr_status, CK_Styl(st_tdr));

    UI_RysujPanel(12, 184, 456, 30,
                  CK_T("Kolejność podstawowa: HW -> OSL. S21 i TDR Vf są opcjonalne.",
                       "Basic order: HW -> OSL. S21 and TDR Vf are optional.",
                       "Grundreihenfolge: HW -> OSL. S21 und TDR Vf sind optional.",
                       "Основной порядок: HW -> OSL. S21 и TDR Vf необязательны."),
                  UI_STYL_NORMALNY);

    b_back = CK_Przycisk(0, 220, 70, 45, JEZYK_Tekst(TEKST_WSTECZ),
                         UI_STYL_POWROT, CK_CB_Wstecz, TEXTBOX_ROLA_WSTECZ);

    TEXTBOX_InitContext(&ctx);
    TEXTBOX_Append(&ctx, &b_back);
    TEXTBOX_DrawContext(&ctx);

    ck_akcja = CK_AKCJA_BRAK;
    while (ck_akcja == CK_AKCJA_BRAK)
    {
        LCDPoint punkt;
        if (!TEXTBOX_HitTest(&ctx) && TOUCH_Poll(&punkt))
        {
            CK_AKCJA_t a = CK_AkcjaPolaGlownego(punkt);
            if (a != CK_AKCJA_BRAK)
            {
                ck_akcja = a;
                TOUCH_CzekajNaPuszczenie(35U);
            }
        }
        Sleep(10);
    }
    return ck_akcja;
}

static CK_AKCJA_t CK_AkcjaPolaGeneratora(LCDPoint punkt)
{
    UI_PROSTOKAT_t pole;
#define CK_CZY_POLE_G(x_, y_, w_, h_) \
    (pole = (UI_PROSTOKAT_t){ (x_), (y_), (w_), (h_) }, UI_CzyPunktWObszarze(punkt, &pole))
    if (CK_CZY_POLE_G(12U, 40U, 220U, 43U))
        return CFG_GetParam(CFG_PARAM_SYNTH_TYPE) == CFG_SYNTH_SI5351 ? CK_AKCJA_SI5351 : CK_AKCJA_KONFIG_RF;
    if (CK_CZY_POLE_G(248U, 40U, 220U, 43U))
        return CFG_GetParam(CFG_PARAM_SYNTH_TYPE) == CFG_SYNTH_SI5351 ? CK_AKCJA_SI5351 : CK_AKCJA_KONFIG_RF;
    if (CK_CZY_POLE_G(12U, 88U, 220U, 43U)) return CK_AKCJA_LIMIT_BEZPOSR;
    if (CK_CZY_POLE_G(248U, 88U, 220U, 43U)) return CK_AKCJA_HARMONICZNA;
    if (CK_CZY_POLE_G(12U, 136U, 456U, 43U)) return CK_AKCJA_ZAKRES;
#undef CK_CZY_POLE_G
    return CK_AKCJA_BRAK;
}

static CK_AKCJA_t CK_EkranGeneratora(void)
{
    CK_STAN_CALOSCI_t stan;
    TEXTBOX_CTX_t ctx;
    TEXTBOX_t b_cfg, b_if, b_back;
    char kwarc[48], limit[48], harmoniczna[40], pasmo[56];
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);
    const uint8_t si5351 = typ == CFG_SYNTH_SI5351 ? 1U : 0U;

    CK_PobierzStan(&stan);
    snprintf(pasmo, sizeof(pasmo), "%.3f - %.0f MHz",
             (double)stan.fmin_hz / 1000000.0, (double)stan.fmax_hz / 1000000.0);

    if (si5351)
    {
        snprintf(kwarc, sizeof(kwarc), "%.3f MHz  %+ld Hz",
                 (double)stan.kwarc_hz / 1000000.0, (long)stan.korekcja_hz);
        snprintf(limit, sizeof(limit), "%lu MHz",
                 (unsigned long)(stan.fmax_bezposrednia_hz / 1000000U));
        snprintf(harmoniczna, sizeof(harmoniczna), "H%u  -> %lu MHz",
                 (unsigned)stan.hmax,
                 (unsigned long)(stan.fmax_efektywne_hz / 1000000U));
    }
    else
    {
        snprintf(kwarc, sizeof(kwarc), "%s",
                 CK_T("sterownik niezweryfikowany", "driver unverified",
                      "Treiber nicht verifiziert", "драйвер не проверен"));
        snprintf(limit, sizeof(limit), "%.0f - %.0f MHz",
                 (double)GEN_MinCzestotliwoscEfektywna() / 1000000.0,
                 (double)GEN_MaksCzestotliwoscEfektywna() / 1000000.0);
        snprintf(harmoniczna, sizeof(harmoniczna), "%s",
                 CK_T("nie dotyczy", "not applicable", "nicht zutreffend", "не применяется"));
    }

    UI_WyczyscEkran();
    UI_RysujNaglowek(CK_T("Generator i zakres kalibracji", "Generator and calibration range",
                          "Generator und Kalibrierbereich", "Генератор и диапазон калибровки"));

    if (si5351)
    {
        UI_RysujPoleStatusu(12, 40, 220, 43, "Si5351",
                            stan.si5351_ok ? JEZYK_Tekst(TEKST_STATUS_OK) : JEZYK_Tekst(TEKST_STATUS_BRAK),
                            stan.si5351_ok ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);
        UI_RysujPoleStatusu(248, 40, 220, 43,
                            CK_T("Kwarc / korekcja", "Crystal / correction", "Quarz / Korrektur", "Кварц / коррекция"),
                            kwarc, UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(12, 88, 220, 43,
                            CK_T("Limit bezpośredni", "Direct limit", "Direktlimit", "Прямой предел"),
                            limit, UI_STYL_AKCENT);
        UI_RysujPoleStatusu(248, 88, 220, 43,
                            CK_T("Harmoniczna maks.", "Maximum harmonic", "Max. Harmonische", "Макс. гармоника"),
                            harmoniczna, UI_STYL_AKCENT);
    }
    else
    {
        UI_RysujPoleStatusu(12, 40, 220, 43,
                            CK_T("Generator RF", "RF generator", "HF-Generator", "RF генератор"),
                            GEN_PobierzNazweSyntezera(), UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(248, 40, 220, 43,
                            CK_T("Weryfikacja", "Verification", "Verifizierung", "Проверка"),
                            kwarc, UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(12, 88, 220, 43,
                            CK_T("Zakres sterownika", "Driver range", "Treiberbereich", "Диапазон драйвера"),
                            limit, UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(248, 88, 220, 43,
                            CK_T("Harmoniczne", "Harmonics", "Harmonische", "Гармоники"),
                            harmoniczna, UI_STYL_NIEAKTYWNY);
    }

    UI_RysujPoleStatusu(12, 136, 456, 43,
                        CK_T("Zakres kalibracji", "Calibration range", "Kalibrierbereich", "Диапазон калибровки"),
                        pasmo,
                        (GEN_CzyCzestotliwoscObslugiwana(stan.fmin_hz) &&
                         GEN_CzyCzestotliwoscObslugiwana(stan.fmax_hz))
                            ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);

    b_cfg = CK_Przycisk(82, 181, 393, 34,
                        CK_T("Konfiguracja zaawansowana RF", "Advanced RF configuration",
                             "Erweiterte HF-Konfiguration", "Расширенные RF настройки"),
                        UI_STYL_NORMALNY, CK_CB_KonfigRF, TEXTBOX_ROLA_ZWYKLA);
    b_back = CK_Przycisk(0, 220, 70, 45, JEZYK_Tekst(TEKST_WSTECZ),
                         UI_STYL_POWROT, CK_CB_Wstecz, TEXTBOX_ROLA_WSTECZ);
    b_if = CK_Przycisk(82, 220, 393, 45,
                       si5351
                           ? CK_T("Sprawdź Si5351 / IF", "Check Si5351 / IF", "Si5351 / ZF prüfen", "Проверить Si5351 / ПЧ")
                           : CK_T("Brak automatycznego testu tego sterownika", "No automatic test for this driver",
                                  "Kein automatischer Test für diesen Treiber", "Нет автотеста для этого драйвера"),
                       si5351 ? UI_STYL_AKCENT : UI_STYL_NIEAKTYWNY,
                       si5351 ? CK_CB_SprawdzIF : 0,
                       si5351 ? TEXTBOX_ROLA_START_STOP : TEXTBOX_ROLA_ZWYKLA);

    TEXTBOX_InitContext(&ctx);
    TEXTBOX_Append(&ctx, &b_cfg);
    TEXTBOX_Append(&ctx, &b_back);
    TEXTBOX_Append(&ctx, &b_if);
    TEXTBOX_DrawContext(&ctx);

    ck_akcja = CK_AKCJA_BRAK;
    while (ck_akcja == CK_AKCJA_BRAK)
    {
        LCDPoint punkt;
        if (!TEXTBOX_HitTest(&ctx) && TOUCH_Poll(&punkt))
        {
            CK_AKCJA_t a = CK_AkcjaPolaGeneratora(punkt);
            if (a != CK_AKCJA_BRAK)
            {
                ck_akcja = a;
                TOUCH_CzekajNaPuszczenie(35U);
            }
        }
        Sleep(10);
    }
    return ck_akcja;
}

static int16_t CK_WybierzKafelki(const char *tytul,
                                  const char *const *etykiety,
                                  uint8_t liczba,
                                  uint8_t aktywny)
{
    uint8_t fokus = aktywny < liczba ? aktywny : 0U;
    uint8_t fokus_widoczny = 0U;

    if (etykiety == 0 || liczba == 0U || liczba > UI_SIATKA_KOMPAKT_NA_STRONE)
        return -1;

    while (TOUCH_IsPressed())
        Sleep(0U);
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        uint8_t i;

        UI_WyczyscEkran();
        UI_RysujPasekGorny(tytul, true, false, 0);
        for (i = 0U; i < liczba; ++i)
        {
            UI_RysujKafelKompaktowyZeStanem(i, UI_IKONA_GENERATOR, etykiety[i],
                                            fokus_widoczny && i == fokus,
                                            true, i == aktywny);
        }
        UI_RysujWsteczDolny(false);

        for (;;)
        {
            if (TOUCH_Poll(&punkt))
            {
                const int16_t wybor = UI_KafelKompaktowyPoDotyku(punkt, liczba);
                if (UI_CzyDotknietoWstecz(punkt))
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return -1;
                }
                if (wybor >= 0)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return wybor;
                }
            }

            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                return -1;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
                zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                fokus_widoczny = 1U;
                if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
                    fokus = (uint8_t)((fokus + 1U) % liczba);
                else
                    fokus = fokus == 0U ? (uint8_t)(liczba - 1U) : (uint8_t)(fokus - 1U);
                break;
            }
            if ((zdarzenie == WEJSCIE_ZDARZENIE_OK ||
                 zdarzenie == WEJSCIE_ZDARZENIE_START_STOP) && fokus_widoczny)
                return (int16_t)fokus;
            Sleep(10U);
        }
    }
}

static void CK_UstawLimitBezposredni(void)
{
    static const uint32_t wartosci[] =
    {
        160000000U, 200000000U, 260000000U,
        270000000U, 280000000U, 290000000U
    };
    static const char *const etykiety[] =
    {
        "160 MHz", "200 MHz", "260 MHz",
        "270 MHz", "280 MHz", "290 MHz"
    };
    const uint32_t aktualna = CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);
    uint8_t aktywny = 0U;
    uint8_t i;
    int16_t wybor;

    if (CFG_GetParam(CFG_PARAM_SYNTH_TYPE) != CFG_SYNTH_SI5351)
    {
        KOMUNIKAT_PokazTekst(CK_T("Parametr Si5351", "Si5351 parameter", "Si5351-Parameter", "Параметр Si5351"),
                             CK_T("Limit bezpośredni dotyczy tylko Si5351. Dla wybranego generatora zakres wynika z jego sterownika.",
                                  "The direct limit applies only to Si5351. For the selected synthesizer the range is defined by its driver.",
                                  "Das Direktlimit gilt nur für Si5351. Beim gewählten Synthesizer ergibt sich der Bereich aus dessen Treiber.",
                                  "Прямой предел относится только к Si5351. Для выбранного синтезатора диапазон задаёт его драйвер."));
        return;
    }

    for (i = 0U; i < (uint8_t)(sizeof(wartosci) / sizeof(wartosci[0])); ++i)
        if (wartosci[i] == aktualna)
            aktywny = i;

    wybor = CK_WybierzKafelki(CK_T("Limit bezpośredni Si5351", "Si5351 direct limit", "Si5351-Direktlimit", "Прямой предел Si5351"),
                              etykiety, (uint8_t)(sizeof(etykiety) / sizeof(etykiety[0])), aktywny);
    if (wybor >= 0 && wartosci[wybor] != aktualna)
    {
        CFG_SetParam(CFG_PARAM_SI5351_MAX_FREQ, wartosci[wybor]);
        CFG_Flush();
        ck_sprawdzenie_if_sesja_ok = 0U;
    }
}

static void CK_UstawHarmoniczna(void)
{
    static const uint32_t wartosci[] = { 1U, 3U, 5U, 7U };
    const char *etykiety[] =
    {
        CK_T("H1  bezpośrednio", "H1  direct", "H1  direkt", "H1  напрямую"),
        CK_T("H3  zalecana", "H3  recommended", "H3  empfohlen", "H3  рекомендуется"),
        CK_T("H5  eksperymentalna", "H5  experimental", "H5  experimentell", "H5  экспериментальная"),
        CK_T("H7  eksperymentalna", "H7  experimental", "H7  experimentell", "H7  экспериментальная")
    };
    const uint32_t aktualna = CFG_GetParam(CFG_PARAM_HARMONICZNA_MAX);
    uint8_t aktywny = 0U;
    uint8_t i;
    int16_t wybor;

    if (CFG_GetParam(CFG_PARAM_SYNTH_TYPE) != CFG_SYNTH_SI5351)
    {
        KOMUNIKAT_PokazTekst(CK_T("Harmoniczne Si5351", "Si5351 harmonics", "Si5351-Harmonische", "Гармоники Si5351"),
                             CK_T("Wybór H1/H3/H5/H7 dotyczy tylko toru z Si5351.",
                                  "H1/H3/H5/H7 selection applies only to the Si5351 path.",
                                  "Die Auswahl H1/H3/H5/H7 gilt nur für den Si5351-Pfad.",
                                  "Выбор H1/H3/H5/H7 относится только к тракту Si5351."));
        return;
    }

    for (i = 0U; i < (uint8_t)(sizeof(wartosci) / sizeof(wartosci[0])); ++i)
        if (wartosci[i] == aktualna)
            aktywny = i;

    wybor = CK_WybierzKafelki(CK_T("Maksymalna harmoniczna", "Maximum harmonic", "Maximale Harmonische", "Максимальная гармоника"),
                              etykiety, (uint8_t)(sizeof(etykiety) / sizeof(etykiety[0])), aktywny);
    if (wybor >= 0 && wartosci[wybor] != aktualna)
    {
        CFG_SetParam(CFG_PARAM_HARMONICZNA_MAX, wartosci[wybor]);
        CFG_Flush();
    }
}

static void CK_UstawZakresKalibracji(void)
{
    uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    uint32_t nowy_fmin = fmin;
    uint32_t nowy_fmax = fmax;

    /*
     * Zakres jest jedną parą danych. Zapisujemy go dopiero po zaakceptowaniu
     * obu granic, żeby anulowanie drugiego ekranu nie pozostawiało połowy zmiany.
     */
    /*
     * Dolny zakres produkcyjny ma tylko pięć dopuszczonych wartości.
     * Edytor pokazuje dokładnie te same ograniczenia, które później stosuje
     * CFG_Validate(), więc po restarcie ustawienie nie zostanie po cichu
     * zaokrąglone do innej częstotliwości.
     */
    if (!UI_EdytujCzestotliwoscHzEx(fmin, CK_FMIN_KAL_MIN_HZ, CK_FMIN_KAL_MAX_HZ,
                                    CK_FMIN_KAL_KROK_HZ,
                                    CK_T("Dolna granica kalibracji", "Calibration lower limit", "Untere Kalibriergrenze", "Нижняя граница калибровки"),
                                    &nowy_fmin))
        return;

    if (!UI_EdytujCzestotliwoscHzEx(fmax, nowy_fmin + CK_FMAX_KAL_KROK_HZ,
                                    MAX_BAND_FREQ, CK_FMAX_KAL_KROK_HZ,
                                    CK_T("Górna granica kalibracji", "Calibration upper limit", "Obere Kalibriergrenze", "Верхняя граница калибровки"),
                                    &nowy_fmax))
        return;

    if (nowy_fmin != fmin || nowy_fmax != fmax)
    {
        CFG_SetParam(CFG_PARAM_BAND_FMIN, nowy_fmin);
        CFG_SetParam(CFG_PARAM_BAND_FMAX, nowy_fmax);
        CFG_Flush();
    }
}

static uint8_t CK_KontrolaWstepnaKalibracji(void)
{
    CK_STAN_CALOSCI_t stan;
    CK_PobierzStan(&stan);

    if (!stan.sd_ok)
    {
        KOMUNIKAT_PokazTekst(CK_T("Najpierw karta SD", "SD card required first", "Zuerst SD-Karte", "Сначала SD-карта"),
                             CK_T("Kalibracja wymaga sprawnej karty SD do zapisu współczynników. Sprawdź Ustawienia > Diagnostyka > SD/FAT.", "Calibration requires a working SD card to save coefficients. Check Settings > Diagnostics > SD/FAT.", "Die Kalibrierung benötigt eine funktionierende SD-Karte. Prüfen Sie Einstellungen > Diagnose > SD/FAT.", "Для калибровки нужна исправная SD-карта. Проверьте Настройки > Диагностика > SD/FAT."));
        DIAGNOSTYKA_OtworzSD();
        return 0U;
    }
    if (CFG_GetParam(CFG_PARAM_SYNTH_TYPE) == CFG_SYNTH_SI5351 && !stan.si5351_ok)
    {
        KOMUNIKAT_PokazTekst(CK_T("Brak Si5351", "Si5351 missing", "Si5351 fehlt", "Нет Si5351"),
                             CK_T("Nie rozpoczynam kalibracji RF bez działającego generatora. Sprawdź I2C i ustawienia Si5351.", "RF calibration is not started without a working generator. Check I2C and Si5351 settings.", "HF-Kalibrierung startet nicht ohne Generator. I2C und Si5351 prüfen.", "RF калибровка не запускается без генератора. Проверьте I2C и Si5351."));
        DIAGNOSTYKA_OtworzSi5351();
        return 0U;
    }
    /*
     * Nie blokujemy wejścia do kalibracji HW na podstawie syntetycznej oceny
     * "spójności planu". Zakres, limit bezpośredni i harmoniczna są świadomymi
     * nastawami użytkownika. Ich rzeczywistą wykonalność sprawdzamy dopiero po
     * naciśnięciu START w oknie HW, gdzie można podać konkretną przyczynę i
     * częstotliwość, której generator nie potrafi wytworzyć.
     */
    return 1U;
}

static void CK_UruchomOSL(void);

static void CK_UruchomHW(void)
{
    if (!CK_KontrolaWstepnaKalibracji())
        return;

    /* Wejście do HW nie jest blokowane przez poprzedni stan testu IF ani
     * przez nietypowy, lecz legalny plan. Dopiero START w oknie HW sprawdza
     * osiągalność zakresu i — dla Si5351 — rzeczywisty tor IF. */
    {
        const uint8_t wynik_hw = OSL_CalErrCorr();
        if (wynik_hw == 1U)
        {
            KOMUNIKAT_PokazTekst(CK_T("HW zakończona", "HW complete", "HW fertig", "HW завершена"),
                                 CK_T("HW została zapisana. Zworka WORK została skontrolowana. OSL możesz uruchomić później w Ustawienia > Kalibracja.", "HW was saved and the WORK jumper state was checked. You may start OSL later in Settings > Calibration.", "HW wurde gespeichert und WORK wurde geprüft. OSL kann später unter Einstellungen > Kalibrierung gestartet werden.", "HW сохранена, положение WORK проверено. OSL можно запустить позже в Настройки > Калибровка."));
        }
        else if (wynik_hw == 2U)
        {
            CK_UruchomOSL();
        }
    }
}

static void CK_UruchomOSL(void)
{
    CK_STAN_CALOSCI_t stan;
    if (!CK_KontrolaWstepnaKalibracji())
        return;
    CK_PobierzStan(&stan);
    if (!stan.hw_zaladowana || (stan.hw_meta_jest && !stan.hw_aktualna))
    {
        KOMUNIKAT_PokazTekst(CK_T("Najpierw HW", "HW first", "Zuerst HW", "Сначала HW"),
                             CK_T("OSL wymaga aktualnej korekcji HW. Otwieram teraz kalibrację HW; po jej zakończeniu możesz przejść od razu do OSL.", "OSL requires current HW correction. HW calibration opens now; after it finishes you can continue directly to OSL.", "OSL benötigt eine aktuelle HW-Korrektur. Die HW-Kalibrierung wird jetzt geöffnet; danach kann direkt mit OSL fortgefahren werden.", "OSL требует актуальной HW-коррекции. Сейчас откроется HW-калибровка; после неё можно сразу перейти к OSL."));
        CK_UruchomHW();
        return;
    }
    OSL_CalWnd();
}

static void CK_UruchomLC(void)
{
    /* Zachowany alias dla starych wywołań. L/C nie ma osobnej kalibracji,
     * dlatego nie pokazujemy ślepego ekranu ani komunikatu o wycofaniu. */
    CK_UruchomOSL();
}

static void CK_UruchomS21(void)
{
    if (!CK_KontrolaWstepnaKalibracji())
        return;
    if (CFG_GetParam(CFG_PARAM_ATTENUATOR) == 0U)
    {
        KOMUNIKAT_PokazTekst(CK_T("Tłumik S21 = 0 dB", "S21 attenuator = 0 dB", "S21-Dämpfer = 0 dB", "Аттенюатор S21 = 0 dB"),
                             CK_T("Dwupunktowa kalibracja S21 wymaga znanego, niezerowego tłumika. Otwieram właściwy parametr konfiguracji.", "Two-point S21 calibration needs a known non-zero attenuator. Opening the relevant setting.", "Die Zweipunkt-S21-Kalibrierung braucht einen bekannten Dämpfer ungleich 0 dB.", "Для двухточечной S21 нужен известный ненулевой аттенюатор."));
        CFG_ParamWndOdParametru(CFG_PARAM_ATTENUATOR);
        return;
    }
    OSL_CalTXCorr();
}

static CK_EKRAN_t CK_WykonajAkcje(CK_AKCJA_t akcja, CK_EKRAN_t skad)
{
    switch (akcja)
    {
    case CK_AKCJA_WSTECZ:
        return skad == CK_EKRAN_GLOWNY ? CK_EKRAN_WYJSCIE : CK_EKRAN_GLOWNY;
    case CK_AKCJA_GENERATOR: return CK_EKRAN_GENERATOR;
    case CK_AKCJA_SI5351: DIAGNOSTYKA_OtworzSi5351(); ck_sprawdzenie_if_sesja_ok = 0U; break;
    case CK_AKCJA_KONFIG_RF:
        CFG_ParamWndOdParametru(CFG_PARAM_SYNTH_TYPE);
        ck_sprawdzenie_if_sesja_ok = 0U;
        break;
    case CK_AKCJA_LIMIT_BEZPOSR: CK_UstawLimitBezposredni(); break;
    case CK_AKCJA_HARMONICZNA: CK_UstawHarmoniczna(); break;
    case CK_AKCJA_ZAKRES: CK_UstawZakresKalibracji(); break;
    case CK_AKCJA_SPRAWDZ_IF: ck_sprawdzenie_if_sesja_ok = DIAGNOSTYKA_OtworzSprawdzenieIF(); break;
    case CK_AKCJA_HW: CK_UruchomHW(); break;
    case CK_AKCJA_OSL: CK_UruchomOSL(); break;
    case CK_AKCJA_S21: CK_UruchomS21(); break;
    case CK_AKCJA_TDR: CENTRUM_KALIBRACJI_OtworzTDRVf(); break;
    default: break;
    }
    return skad;
}

static void CK_ZastosujZmianeWzorca(void)
{
    /* Zmiana któregokolwiek z trzech wzorców zmienia równania OSL.
       Plików nie kasujemy, ale aktywna korekcja nie może zostać w pamięci. */
    OSL_Select(-1);
    CFG_Flush();
}

static bool CK_WpiszWzorzecDC(uint32_t biezacy_mohm, uint32_t min_mohm,
                              uint32_t max_mohm, const char *tytul,
                              bool (*ustaw)(uint32_t))
{
    const uint32_t nowy_mohm = NumKeypadMiliohm(biezacy_mohm, min_mohm, max_mohm, tytul);
    if (nowy_mohm == 0U)
        return false;
    if (!ustaw(nowy_mohm))
    {
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Nie zapisano", "Not saved", "Nicht gespeichert", "Не сохранено"),
            JEZYK_Wybierz("Wartość jest poza zakresem tego wzorca.", "The value is outside this standard range.",
                          "Der Wert liegt außerhalb des Bereichs dieses Normals.", "Значение вне диапазона этого эталона."));
        return false;
    }
    CK_ZastosujZmianeWzorca();
    return true;
}


static uint8_t CK_WzorceZapisaneDalej(const char *niski, const char *srodkowy, const char *wysoki)
{
    UI_AKCJA_t akcje[2];
    uint8_t fokus = 1U;

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int16_t akcja;

        akcje[0] = (UI_AKCJA_t){ .id = 0, .tekst = JEZYK_Tekst(TEKST_WSTECZ),
                                 .styl = UI_STYL_POWROT, .aktywna = true,
                                 .zaznaczona = fokus == 0U };
        akcje[1] = (UI_AKCJA_t){ .id = 1,
                                 .tekst = JEZYK_Wybierz("Dalej: OSL", "Next: OSL", "Weiter: OSL", "Далее: OSL"),
                                 .styl = UI_STYL_AKCENT, .aktywna = true,
                                 .zaznaczona = fokus == 1U };

        UI_WyczyscEkran();
        UI_RysujPasekGorny(JEZYK_Wybierz("Wzorce zapisane", "Standards saved",
                                          "Normale gespeichert", "Эталоны сохранены"),
                            true, false, 0);
        UI_RysujPoleStatusu(14U, 44U, 452U, 40U,
                            JEZYK_Wybierz("Niski", "Low", "Niedrig", "Низкий"),
                            niski, UI_STYL_AKTYWNY);
        UI_RysujPoleStatusu(14U, 91U, 452U, 40U,
                            JEZYK_Wybierz("Środkowy", "Middle", "Mittel", "Средний"),
                            srodkowy, UI_STYL_AKTYWNY);
        UI_RysujPoleStatusu(14U, 138U, 452U, 40U,
                            JEZYK_Wybierz("Wysoki", "High", "Hoch", "Высокий"),
                            wysoki, UI_STYL_AKTYWNY);
        UI_RysujPoleStatusu(14U, 185U, 452U, 30U,
                            JEZYK_Wybierz("Następny krok", "Next step", "Nächster Schritt", "Следующий шаг"),
                            JEZYK_Wybierz("wybierz profil i wykonaj OSL", "select a profile and run OSL",
                                          "Profil wählen und OSL ausführen", "выберите профиль и выполните OSL"),
                            UI_STYL_AKCENT);
        UI_RysujPasekAkcji(222U, 40U, akcje, 2U);

        while (TOUCH_IsPressed())
            Sleep(10U);
        WEJSCIA_WyczyscZdarzenia();

        for (;;)
        {
            if (TOUCH_Poll(&punkt))
            {
                akcja = UI_ZnajdzAkcjePaska(punkt, 222U, 40U, akcje, 2U);
                if (UI_CzyDotknietoWstecz(punkt) || akcja == 0)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return 0U;
                }
                if (akcja == 1)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return 1U;
                }
            }

            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                return 0U;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
                zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                fokus = fokus ? 0U : 1U;
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
                return fokus == 1U ? 1U : 0U;
            Sleep(10U);
        }
    }
}

void CENTRUM_KALIBRACJI_UstawDokladneWzorceDC(void)
{
    char niski[32];
    char srodkowy[32];
    char wysoki[32];
    char wynik[140];

    CK_CzekajNaPuszczenie();
    KOMUNIKAT_PokazTekst(
        JEZYK_Wybierz("Dokładne wzorce OSL", "Exact OSL standards", "Exakte OSL-Normale", "Точные эталоны OSL"),
        JEZYK_Wybierz(
            "Zmierz omomierzem i wpisz kolejno trzy rzeczywiste rezystancje: niski około 5 Ohm, środkowy około 50 Ohm i wysoki około 500 Ohm. Nazwy SHORT/LOAD/OPEN są tylko historyczne.",
            "Measure with an ohmmeter and enter the three actual resistances in order: low about 5 ohm, middle about 50 ohm and high about 500 ohm. SHORT/LOAD/OPEN are historical names only.",
            "Mit dem Ohmmeter messen und nacheinander die drei tatsächlichen Widerstände eingeben: niedrig etwa 5 Ohm, mittel etwa 50 Ohm und hoch etwa 500 Ohm. SHORT/LOAD/OPEN sind nur historische Namen.",
            "Измерьте омметром и по очереди введите три фактических сопротивления: низкое около 5 Ом, среднее около 50 Ом и высокое около 500 Ом. SHORT/LOAD/OPEN — только исторические названия."));

    if (!CK_WpiszWzorzecDC(CFG_GetOslRshortMilliOhm(), 1000U, 20000U,
            JEZYK_Wybierz("1/3  Wzorzec niski ~5 Ohm", "1/3  Low standard ~5 ohm", "1/3  Niedriges Normal ~5 Ohm", "1/3  Низкий эталон ~5 Ом"),
            CFG_UstawOslRshortZPomiaruZewnetrznego))
        return;

    if (!CK_WpiszWzorzecDC(CFG_GetOslRloadMilliOhm(), 10000U, 200000U,
            JEZYK_Wybierz("2/3  Wzorzec środkowy ~50 Ohm", "2/3  Middle standard ~50 ohm", "2/3  Mittleres Normal ~50 Ohm", "2/3  Средний эталон ~50 Ом"),
            CFG_UstawOslRloadZPomiaruZewnetrznego))
        return;

    if (!CK_WpiszWzorzecDC(CFG_GetOslRopenMilliOhm(), 100000U, 2000000U,
            JEZYK_Wybierz("3/3  Wzorzec wysoki ~500 Ohm", "3/3  High standard ~500 ohm", "3/3  Hohes Normal ~500 Ohm", "3/3  Высокий эталон ~500 Ом"),
            CFG_UstawOslRopenZPomiaruZewnetrznego))
        return;

    CFG_FormatujOslRshort(niski, sizeof(niski), true);
    CFG_FormatujOslRload(srodkowy, sizeof(srodkowy), true);
    CFG_FormatujOslRopen(wysoki, sizeof(wysoki), true);
    snprintf(wynik, sizeof(wynik), "%s | %s | %s", niski, srodkowy, wysoki);
    (void)wynik;

    /* Po zmianie rezystancji stara OSL jest celowo unieważniona. Zamiast
       zostawiać użytkownika z komunikatem i ukrytym parametrem profilu,
       prowadzimy go bezpośrednio do wyboru profilu i następnie do OSL. */
    if (CK_WzorceZapisaneDalej(niski, srodkowy, wysoki))
        CENTRUM_KALIBRACJI_OtworzOSL();
    CK_CzekajNaPuszczenie();
}

void CENTRUM_KALIBRACJI_OtworzHW(void)
{
    CK_CzekajNaPuszczenie();
    CK_UruchomHW();
    CK_CzekajNaPuszczenie();
}

void CENTRUM_KALIBRACJI_OtworzOSL(void)
{
    CK_CzekajNaPuszczenie();
    CK_UruchomOSL();
    CK_CzekajNaPuszczenie();
}

void CENTRUM_KALIBRACJI_OtworzS21(void)
{
    CK_CzekajNaPuszczenie();
    CK_UruchomS21();
    CK_CzekajNaPuszczenie();
}

void CENTRUM_KALIBRACJI_OtworzLC(void)
{
    CK_CzekajNaPuszczenie();
    CK_UruchomLC();
    CK_CzekajNaPuszczenie();
}

void CENTRUM_KALIBRACJI_OtworzTDRVf(void)
{
    CK_CzekajNaPuszczenie();
    KOMUNIKAT_PokazTekst(
        CK_T("Kalibracja TDR Vf", "TDR Vf calibration", "TDR-Vf-Kalibrierung", "Калибровка TDR Vf"),
        CK_T("Do wyznaczenia Vf potrzebny jest kabel o znanej długości. W TDR wykonaj skan, ustaw kursor dokładnie na końcu kabla, wybierz Weryfikuj, wpisz zmierzoną długość i zapisz sugerowane Vf.",
             "A cable of known length is required. In TDR run a scan, place the cursor exactly at the cable end, select Verify, enter the measured cable length and save the suggested Vf.",
             "Für Vf wird ein Kabel bekannter Länge benötigt. TDR scannen, Cursor exakt auf das Kabelende setzen, Prüfen wählen, Länge eingeben und den vorgeschlagenen Vf speichern.",
             "Для Vf нужен кабель известной длины. Выполните скан TDR, поставьте курсор на конец кабеля, выберите Проверить, введите длину и сохраните предложенный Vf."));
    TDR_Proc();
    CK_CzekajNaPuszczenie();
}

void CENTRUM_KALIBRACJI_OtworzStan(void)
{
    /* Zgodność ze starszymi wywołaniami: nie otwieramy drugiego ekranu stanu. */
    CENTRUM_KALIBRACJI_Otworz();
}

void CENTRUM_KALIBRACJI_OtworzWeryfikacje(void)
{
    CK_CzekajNaPuszczenie();
    MEASUREMENT_WeryfikacjaWzorcami();
    CK_CzekajNaPuszczenie();
}

void CENTRUM_KALIBRACJI_Otworz(void)
{
    CK_EKRAN_t ekran = CK_EKRAN_GLOWNY;
    CK_AKCJA_t akcja;

    CK_CzekajNaPuszczenie();

    while (ekran != CK_EKRAN_WYJSCIE)
    {
        switch (ekran)
        {
        case CK_EKRAN_GENERATOR: akcja = CK_EkranGeneratora(); break;
        case CK_EKRAN_GLOWNY:
        default: akcja = CK_EkranGlowny(); break;
        }
        ekran = CK_WykonajAkcje(akcja, ekran);
        CK_CzekajNaPuszczenie();
    }

    UI_WyczyscEkran();
}
