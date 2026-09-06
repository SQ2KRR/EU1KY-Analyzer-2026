#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "LCD.h"
#include "touch.h"
#include "font.h"
#include "jezyk.h"
#include "ui_wspolny.h"
#include "wejscia_uzytkownika.h"
#include "komunikaty.h"
#include "screenshot.h"
#include "gen.h"
#include "mainwnd.h"
#include "oslcal.h"
#include "oslfile.h"
#include "projektowanie_anten.h"
#include "panvswr2.h"
#include "panfreq.h"
#include "measurement.h"
#include "lacznosc.h"
#include "dokumentacja_realna.h"
#include "dokumentacja_przyklady.h"
#include "wersja_projektu.h"
#include "ff.h"

extern char SDPath[4];
extern void Sleep(uint32_t ms);

typedef struct
{
    uint32_t (*liczba_stron)(void);
    const char *(*nazwa_strony)(uint32_t strona);
    void (*rysuj_strone)(uint32_t strona);
    const char *prefiks;
} DOK_REAL_MODUL_UI_t;

typedef struct
{
    const char *nazwa;
    uint32_t start_hz;
    BANDSPAN zakres;
} DOK_REAL_ZAKRES_t;

typedef struct
{
    const char *nazwa;
    uint32_t czestotliwosc_hz;
} DOK_REAL_PUNKT_t;

typedef struct
{
    uint32_t pan_f1;
    uint32_t pan_span;
    uint32_t pan_center_f;
    uint32_t pan_nscans;
    uint32_t meas_f;
    uint32_t meas_nscans;
} DOK_REAL_KONFIG_t;

/*
 * Do tej listy trafiają tylko renderery interfejsu, które nie udają wyniku
 * pomiarowego. Stary PANVSWR_DokumentacjaRysujStrone() i cały moduł
 * Pierwsze strony DOK_PRZYKLADY są pominięte, bo dublowałyby rzeczywiste
 * pomiary. Od strony 40 deterministyczne ekrany funkcji (TDR, Laboratorium,
 * CW itd.) są dołączane później do TEJ SAMEJ serii jako „przyklad_ui”.
 */
static const DOK_REAL_MODUL_UI_t dok_real_moduly_ui[] = {
    {MAINWND_DokumentacjaLiczbaStron, MAINWND_DokumentacjaNazwaStrony, MAINWND_DokumentacjaRysujStrone, "glowny"},
    {CFG_DokumentacjaLiczbaParametrow, CFG_DokumentacjaNazwaParametru, CFG_DokumentacjaRysujParametr, "config"},
    {OSL_DokumentacjaLiczbaStron, OSL_DokumentacjaNazwaStrony, OSL_DokumentacjaRysujStrone, "osl"},
    {ANTENY_DokumentacjaLiczbaStron, ANTENY_DokumentacjaNazwaStrony, ANTENY_DokumentacjaRysujStrone, "anteny"},
    {LACZNOSC_DokumentacjaLiczbaStron, LACZNOSC_DokumentacjaNazwaStrony, LACZNOSC_DokumentacjaRysujStrone, "lacznosc"},
};

static const DOK_REAL_ZAKRES_t dok_real_zakresy[] = {
    {"swr_88_108", 88000000U, BS20M},
    {"swr_140_150", 140000000U, BS10M},
    {"swr_440_460", 440000000U, BS20M},
};

/*
 * Etapy z wymiana obciazenia na porcie. Sesja zatrzymuje sie przed kazdym
 * i czeka, az wkrecisz wskazany wzorzec — dzieki temu instrukcja dostaje
 * komplet rzeczywistych, łatwo powtarzalnych obciążeń rezystancyjnych. W sesji
 * dokumentacyjnej nie używamy już 5 Ohm ani rozwarcia: zamiast nich są 100, 200
 * i 500 Ohm, bo użytkownik ma przygotowane takie wzorce i można je jednoznacznie
 * podłączyć oraz powtórzyć pomiar bez zależności od pojemności otwartego złącza.
 */
typedef struct
{
    const char *prefiks;
    const char *polecenie;
} DOK_REAL_ETAP_WZORCA_t;

static const DOK_REAL_ETAP_WZORCA_t dok_real_etapy_wzorcow[] = {
    {"wz_50",   "Wkrec wzorzec 50 Ohm (dopasowanie, SWR 1.0)"},
    {"wz_72",   "Wkrec wzorzec 72 Ohm (kontrola, SWR ok. 1.44)"},
    {"wz_22",   "Wkrec wzorzec 22 Ohm (SWR ok. 2.3)"},
    {"wz_100",  "Wkrec wzorzec 100 Ohm (SWR ok. 2.0)"},
    {"wz_200",  "Wkrec wzorzec 200 Ohm (SWR ok. 4.0)"},
    {"wz_500",  "Wkrec wzorzec 500 Ohm (SWR ok. 10)"},
    {"antena",  "Podlacz antene - dalsza czesc sesji"},
};

#define DOK_REAL_LICZBA_ETAPOW_WZORCOW ((uint32_t)(sizeof(dok_real_etapy_wzorcow) / sizeof(dok_real_etapy_wzorcow[0])))

/* Zakres i punkt pokazywane dla kazdego wzorca — jeden przebieg i jeden odczyt. */
#define DOK_REAL_WZORZEC_F_START 14000000U
#define DOK_REAL_WZORZEC_PASMO   BS450M
#define DOK_REAL_WZORZEC_PUNKT   216500000U

static const char *const dok_real_nazwy_minimow[] = {
    "minimum_88_108",
    "minimum_140_150",
    "minimum_440_460",
};

static const PAN_DOK_WIDOK_t dok_real_widoki_pan[] = {
    PAN_DOK_WIDOK_SWR_MIN,
    PAN_DOK_WIDOK_SWR_LEWO,
    PAN_DOK_WIDOK_SWR_PRAWO,
    PAN_DOK_WIDOK_ANALIZA,
    PAN_DOK_WIDOK_RX,
    PAN_DOK_WIDOK_SMITH,
};

static const char *const dok_real_nazwy_widokow_pan[] = {
    "swr_min",
    "swr_lewo",
    "swr_prawo",
    "analiza",
    "rx",
    "smith",
};

#define DOK_REAL_LICZBA_MODULOW_UI ((uint32_t)(sizeof(dok_real_moduly_ui) / sizeof(dok_real_moduly_ui[0])))
#define DOK_REAL_LICZBA_ZAKRESOW ((uint32_t)(sizeof(dok_real_zakresy) / sizeof(dok_real_zakresy[0])))
#define DOK_REAL_LICZBA_MINIMOW ((uint32_t)(sizeof(dok_real_nazwy_minimow) / sizeof(dok_real_nazwy_minimow[0])))
#define DOK_REAL_LICZBA_WIDOKOW_PAN ((uint32_t)(sizeof(dok_real_widoki_pan) / sizeof(dok_real_widoki_pan[0])))
#define DOK_REAL_USREDNIANIE 1U
#define DOK_REAL_UZUPELNIENIA_PIERWSZA_STRONA 40U
#define DOK_REAL_CRT_REPREZENTATYWNE 10U

typedef enum
{
    DOK_REAL_CRT_MODUL = 0,
    DOK_REAL_CRT_UZUPELNIENIE
} DOK_REAL_CRT_ZRODLO_t;

typedef struct
{
    DOK_REAL_CRT_ZRODLO_t zrodlo;
    const char *prefiks;
    const char *nazwa;
} DOK_REAL_CRT_EKRAN_t;

/* Tylko reprezentatywna próbka stylu CRT. Pełna dokumentacja pozostaje
 * klasyczna; nie dublujemy setek BMP tylko po to, by pokazać drugi motyw. */
static const DOK_REAL_CRT_EKRAN_t dok_real_crt_ekrany[DOK_REAL_CRT_REPREZENTATYWNE] = {
    {DOK_REAL_CRT_MODUL, "glowny", "menu_glowne"},
    {DOK_REAL_CRT_MODUL, "glowny", "menu_pomiar"},
    {DOK_REAL_CRT_MODUL, "glowny", "menu_analiza"},
    {DOK_REAL_CRT_MODUL, "glowny", "menu_kalibracja"},
    {DOK_REAL_CRT_MODUL, "glowny", "wyglad_i_obsluga"},
    {DOK_REAL_CRT_MODUL, "glowny", "menu_pliki"},
    {DOK_REAL_CRT_MODUL, "anteny", "anteny_wybor"},
    {DOK_REAL_CRT_MODUL, "anteny", "twinyagi_menu"},
    {DOK_REAL_CRT_UZUPELNIENIE, "uzup", "demo_tdr_discontinuity"},
    {DOK_REAL_CRT_UZUPELNIENIE, "uzup", "demo_lab_compare"},
};
#define DOK_REAL_POSTEP_MS 120U

/*
 * Na używanej karcie okresowe problemy pojawiały się bardzo regularnie przy
 * obrazach 44 i 88. Jeden BMP ma 391734 B, więc 40 obrazów to ok. 14,95 MiB,
 * a 44 obrazy ok. 16,44 MiB. To wskazuje na okresową latencję wewnętrznego
 * kasowania/porządkowania bloków karty. Dajemy kontrolowany czas bez poleceń
 * jeszcze przed tą granicą; koszt dla całej sesji jest pomijalny.
 */
#define DOK_REAL_SD_ODPOCZYNEK_CO_OBRAZOW 40U
#define DOK_REAL_SD_ODPOCZYNEK_MS 900U

/*
 * Komunikat "blad zapisu" bez numeru obrazu i kodu FatFS nie pozwala odroznic
 * zapelnionej karty od zajetej, a uszkodzonego katalogu od braku miejsca.
 * Sesja trwa kwadrans, wiec kazde zgadywanie kosztuje kolejne podejscie.
 */
static FRESULT dok_real_ostatni_kod = FR_OK;
static char dok_real_ostatni_etap[24] = "-";
static uint32_t dok_real_zapisano_b = 0U;
static uint32_t dok_real_oczekiwano_b = 0U;
static uint8_t dok_real_ma_licznik_bajtow = 0U;
static uint8_t dok_real_przerwane_przez_uzytkownika = 0U;

static void DOK_REAL_ZapamietajBlad(const char *etap, FRESULT kod)
{
    snprintf(dok_real_ostatni_etap, sizeof(dok_real_ostatni_etap), "%s", etap);
    dok_real_ostatni_kod = kod;
    dok_real_zapisano_b = 0U;
    dok_real_oczekiwano_b = 0U;
    dok_real_ma_licznik_bajtow = 0U;
}

static uint8_t DOK_REAL_KatalogIstniejeLubUtworz(const char *sciezka)
{
    const FRESULT wynik = f_mkdir(sciezka);
    return (wynik == FR_OK || wynik == FR_EXIST) ? 1U : 0U;
}


static uint8_t DOK_REAL_UtworzNowyKatalogSesji(const char *katalog_bazowy,
                                                char *katalog_sesji,
                                                uint32_t rozmiar)
{
    uint32_t numer;

    for (numer = 1U; numer <= 999U; ++numer)
    {
        FILINFO info = {0};
        FRESULT wynik;

        snprintf(katalog_sesji, rozmiar, "%s/S%03lu",
                 katalog_bazowy, (unsigned long)numer);
        wynik = f_stat(katalog_sesji, &info);
        if (wynik == FR_NO_FILE || wynik == FR_NO_PATH)
            return DOK_REAL_KatalogIstniejeLubUtworz(katalog_sesji);
        if (wynik != FR_OK)
            return 0U;
    }
    return 0U;
}

static uint8_t DOK_REAL_ZapiszPlikTekstowy(const char *sciezka, const char *dane,
                                              uint8_t dopisz, const char *etap_bazowy)
{
    FIL plik = {0};
    UINT zapisano = 0U;
    const UINT dlugosc = (UINT)strlen(dane);
    FRESULT wynik;
    uint8_t ok = 0U;

    wynik = f_open(&plik, sciezka,
                   dopisz ? (FA_OPEN_ALWAYS | FA_WRITE) : (FA_CREATE_ALWAYS | FA_WRITE));
    if (wynik != FR_OK)
    {
        char etap[24];
        snprintf(etap, sizeof(etap), "%s/open", etap_bazowy);
        DOK_REAL_ZapamietajBlad(etap, wynik);
        return 0U;
    }

    if (dopisz)
    {
        wynik = f_lseek(&plik, f_size(&plik));
        if (wynik != FR_OK)
        {
            {
                char etap[24];
                snprintf(etap, sizeof(etap), "%s/seek", etap_bazowy);
                DOK_REAL_ZapamietajBlad(etap, wynik);
            }
            goto koniec;
        }
    }

    wynik = f_write(&plik, dane, dlugosc, &zapisano);
    if (wynik != FR_OK || zapisano != dlugosc)
    {
        if (wynik == FR_OK)
            wynik = FR_DENIED;
        {
            char etap[24];
            snprintf(etap, sizeof(etap), "%s/write", etap_bazowy);
            DOK_REAL_ZapamietajBlad(etap, wynik);
        }
        dok_real_zapisano_b = zapisano;
        dok_real_oczekiwano_b = dlugosc;
        dok_real_ma_licznik_bajtow = 1U;
        goto koniec;
    }

    ok = 1U;

koniec:
    wynik = f_close(&plik);
    if (wynik != FR_OK)
    {
        {
            char etap[24];
            snprintf(etap, sizeof(etap), "%s/close", etap_bazowy);
            DOK_REAL_ZapamietajBlad(etap, wynik);
        }
        ok = 0U;
    }
    return ok;
}

/*
 * Bledy niskiego poziomu moga pozostawic sterownik SDMMC w stanie, w ktorym
 * natychmiastowe ponowienie f_open/f_write nie ma szans powodzenia. Po takim
 * bledzie wykonujemy jedna probe odtworzenia karty i dopiero potem ponawiamy
 * zapis tego samego obrazu. Nie robimy petli wielokrotnych prob.
 */
static uint8_t DOK_REAL_CzyOdtwarzacSD(FRESULT kod)
{
    return (kod == FR_DISK_ERR || kod == FR_INT_ERR || kod == FR_NOT_READY ||
            kod == FR_NOT_ENABLED || kod == FR_TIMEOUT) ? 1U : 0U;
}

static void DOK_REAL_ZapamietajBladBMP(const SCREENSHOT_DIAGNOSTYKA_ZAPISU_t *diagnostyka)
{
    char etap[24];

    if (diagnostyka == NULL)
    {
        DOK_REAL_ZapamietajBlad("bmp/?", FR_INT_ERR);
        return;
    }

    snprintf(etap, sizeof(etap), "bmp/%s",
             SCREENSHOT_NazwaEtapuZapisu(diagnostyka->etap));
    DOK_REAL_ZapamietajBlad(etap, (FRESULT)diagnostyka->kod_fatfs);
    dok_real_zapisano_b = diagnostyka->zapisano_b;
    dok_real_oczekiwano_b = diagnostyka->oczekiwano_b;
    dok_real_ma_licznik_bajtow = 1U;
}

static uint8_t DOK_REAL_ZapiszBMPZPonowieniem(const char *sciezka)
{
    SCREENSHOT_DIAGNOSTYKA_ZAPISU_t diagnostyka = {0};
    uint8_t proba;

    /*
     * Pierwsza próba jest normalnym zapisem. Dwie kolejne są wyłącznie drogą
     * odzyskiwania po błędach niskiego poziomu SDMMC/FatFs. Nie ponawiamy
     * błędów nazw, parametrów ani braku miejsca, bo tam ponowienie niczego nie
     * naprawi i tylko ukrywałoby prawdziwą przyczynę.
     */
    for (proba = 0U; proba < 3U; ++proba)
    {
        if (SCREENSHOT_ZapiszBMPDoPliku(sciezka))
            return 1U;

        SCREENSHOT_PobierzDiagnostykeZapisu(&diagnostyka);
        DOK_REAL_ZapamietajBladBMP(&diagnostyka);

        if (!DOK_REAL_CzyOdtwarzacSD((FRESULT)diagnostyka.kod_fatfs))
            return 0U;

        /*
         * Po awarii karta dostaje najpierw czas na zakończenie własnych
         * operacji wewnętrznych. Następnie odpinamy i inicjalizujemy wolumin
         * od nowa. Wszystkie pliki automatu są w tym miejscu już zamknięte.
         */
        Sleep(proba == 0U ? 300U : 900U);
        CFG_UstawDostepnoscKartySD(false);
        if (!CFG_SD_SprobujPrzywrocic())
            return 0U;
        Sleep(proba == 0U ? 250U : 600U);
    }

    return 0U;
}

static void DOK_REAL_OdpoczynekKartyPoSerii(uint32_t numer)
{
    if (numer == 0U ||
        (numer % DOK_REAL_SD_ODPOCZYNEK_CO_OBRAZOW) != 0U)
        return;

    /*
     * Nie ma tu otwartego BMP ani manifestu. Celowo nie odmontowujemy
     * poprawnie działającej karty; sama przerwa pozwala jej kontrolerowi
     * zakończyć kasowanie i przenoszenie bloków zanim zacznie się następny
     * pakiet kilkunastu megabajtów.
     */
    Sleep(DOK_REAL_SD_ODPOCZYNEK_MS);
}

static uint8_t DOK_REAL_RysujPostep(uint32_t numer, uint32_t razem, const char *nazwa)
{
    char linia[80];
    const uint32_t procent = razem == 0U ? 0U : (numer * 100U) / razem;
    const uint16_t szerokosc = (uint16_t)((430U * procent) / 100U);
    uint32_t czas_ms = 0U;

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Wybierz("Automat zrzutów", "Auto screenshots",
                                    "Auto-Screenshots", "Автоснимки"));

    snprintf(linia, sizeof(linia), "%lu / %lu   %lu%%",
             (unsigned long)numer, (unsigned long)razem, (unsigned long)procent);
    FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 24, 70, linia);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(),
               24, 105, nazwa != NULL ? nazwa : "-");

    LCD_Rectangle(LCD_MakePoint(24, 148), LCD_MakePoint(456, 176),
                  UI_KolorRamki(UI_STYL_NIEAKTYWNY));
    if (szerokosc > 0U)
        LCD_FillRect(LCD_MakePoint(25, 149), LCD_MakePoint((int)(25U + szerokosc), 175),
                     UI_KolorRamki(UI_STYL_AKCENT));

    UI_RysujWsteczDolny(false);

    /*
     * Automat nie może być pułapką. Po każdym zapisanym obrazie jest krótki
     * punkt bezpiecznego przerwania. Zdarzenie Wstecz nie trafia do renderera
     * kolejnego ekranu, tylko kończy sesję po domknięciu bieżącego pliku.
     */
    while (czas_ms < DOK_REAL_POSTEP_MS)
    {
        LCDPoint punkt;
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            return 0U;

        if (TOUCH_Poll(&punkt) && UI_CzyDotknietoWstecz(punkt))
        {
            TOUCH_CzekajNaPuszczenie(35U);
            return 0U;
        }

        Sleep(10U);
        czas_ms += 10U;
    }
    return 1U;
}

static void DOK_REAL_ZapiszKonfiguracje(DOK_REAL_KONFIG_t *zapis)
{
    zapis->pan_f1 = CFG_GetParam(CFG_PARAM_PAN_F1);
    zapis->pan_span = CFG_GetParam(CFG_PARAM_PAN_SPAN);
    zapis->pan_center_f = CFG_GetParam(CFG_PARAM_PAN_CENTER_F);
    zapis->pan_nscans = CFG_GetParam(CFG_PARAM_PAN_NSCANS);
    zapis->meas_f = CFG_GetParam(CFG_PARAM_MEAS_F);
    zapis->meas_nscans = CFG_GetParam(CFG_PARAM_MEAS_NSCANS);
}

static void DOK_REAL_PrzywrocKonfiguracje(const DOK_REAL_KONFIG_t *zapis)
{
    CFG_SetParam(CFG_PARAM_PAN_F1, zapis->pan_f1);
    CFG_SetParam(CFG_PARAM_PAN_SPAN, zapis->pan_span);
    CFG_SetParam(CFG_PARAM_PAN_CENTER_F, zapis->pan_center_f);
    CFG_SetParam(CFG_PARAM_PAN_NSCANS, zapis->pan_nscans);
    CFG_SetParam(CFG_PARAM_MEAS_F, zapis->meas_f);
    CFG_SetParam(CFG_PARAM_MEAS_NSCANS, zapis->meas_nscans);
}

static uint32_t DOK_REAL_UzupelnieniaLiczbaJednegoStylu(void);

/* Zwraca liczbę pełnych stron UI. Sesja zapisuje cały komplet klasyczny
 * oraz tylko 10 reprezentatywnych ekranów CRT. */
static uint32_t DOK_REAL_LiczbaStronUI(void)
{
    uint32_t razem = 0U;
    uint32_t i;

    for (i = 0U; i < DOK_REAL_LICZBA_MODULOW_UI; ++i)
        razem += dok_real_moduly_ui[i].liczba_stron();
    return razem;
}

static uint32_t DOK_REAL_LiczbaStronRazem(void)
{
    const uint32_t ui = DOK_REAL_LiczbaStronUI() + DOK_REAL_UzupelnieniaLiczbaJednegoStylu() + DOK_REAL_CRT_REPREZENTATYWNE; /* pełny klasyczny + 10 CRT */
    const uint32_t panoramy = DOK_REAL_LICZBA_ZAKRESOW * DOK_REAL_LICZBA_WIDOKOW_PAN;
    const uint32_t minima = DOK_REAL_LICZBA_MINIMOW * 2U;
    const uint32_t wzorce = (DOK_REAL_LICZBA_ETAPOW_WZORCOW - 1U) * 4U;
    return ui + 1U + wzorce + panoramy + minima;
}

static uint8_t DOK_REAL_CzyJestMiejsce(uint32_t liczba_bmp)
{
    DWORD wolne_klastry = 0U;
    FATFS *system_plikow = NULL;
    uint64_t wolne_bajty;
    uint64_t potrzebne_bajty = (uint64_t)liczba_bmp * (uint64_t)SCREENSHOT_FILE_SIZE;

    /* 5% zapasu na metadane FAT i fragmentację. */
    potrzebne_bajty += potrzebne_bajty / 20U;

    if (f_getfree(SDPath, &wolne_klastry, &system_plikow) != FR_OK || system_plikow == NULL)
        return 0U;

    wolne_bajty = (uint64_t)wolne_klastry * (uint64_t)system_plikow->csize * 512ULL;
    return wolne_bajty >= potrzebne_bajty ? 1U : 0U;
}

static uint8_t DOK_REAL_ZapiszManifest(const char *sciezka_manifestu,
                                       uint32_t numer,
                                       const char *plik,
                                       const char *modul,
                                       const char *nazwa,
                                       const char *rodzaj,
                                       uint32_t f_start_hz,
                                       uint32_t f_stop_hz,
                                       uint32_t f_marker_hz)
{
    char linia[224];
    const int dl = snprintf(linia, sizeof(linia),
                            "%lu;%s;%s;%s;%s;%s;%s;%s;%lu;%lu;%lu;%u\r\n",
                            (unsigned long)numer,
                            plik,
                            JEZYK_KodProjektu(),
                            PROJEKT_WERSJA,
                            PROJEKT_KOMPILACJA,
                            modul,
                            nazwa,
                            rodzaj,
                            (unsigned long)f_start_hz,
                            (unsigned long)f_stop_hz,
                            (unsigned long)f_marker_hz,
                            (unsigned)DOK_REAL_USREDNIANIE);

    if (dl <= 0 || (size_t)dl >= sizeof(linia))
        return 0U;
    return DOK_REAL_ZapiszPlikTekstowy(sciezka_manifestu, linia, 1U, "manifest");
}

static uint8_t DOK_REAL_ZapiszEkran(const char *katalog,
                                    const char *sciezka_manifestu,
                                    uint32_t *numer,
                                    uint32_t razem,
                                    const char *modul,
                                    const char *nazwa,
                                    const char *rodzaj,
                                    uint32_t f_start_hz,
                                    uint32_t f_stop_hz,
                                    uint32_t f_marker_hz)
{
    char plik[16];
    char sciezka[80];

    ++(*numer);
    snprintf(plik, sizeof(plik), "%03lu.bmp", (unsigned long)*numer);
    snprintf(sciezka, sizeof(sciezka), "%s/%s", katalog, plik);

    if (!DOK_REAL_ZapiszBMPZPonowieniem(sciezka))
        return 0U;

    if (!DOK_REAL_ZapiszManifest(sciezka_manifestu, *numer, plik, modul, nazwa, rodzaj,
                                 f_start_hz, f_stop_hz, f_marker_hz))
        return 0U;

    DOK_REAL_OdpoczynekKartyPoSerii(*numer);

    if (!DOK_REAL_RysujPostep(*numer, razem, nazwa))
    {
        dok_real_przerwane_przez_uzytkownika = 1U;
        return 0U;
    }
    return 1U;
}

/*
 * Style dokumentacyjne są wymuszane tylko w rendererze UI. Nie zmieniamy
 * CFG_PARAM_ZESTAW_IKON, nie wywołujemy CFG_Flush() i nie dotykamy globalnej
 * palety wykresów. Dzięki temu zrzuty Retro i Klasyczne nie mogą zmienić
 * ustawień użytkownika ani toru pomiarowego.
 */
/*
 * Pełny zestaw interfejsu zapisujemy w stylu Retro. Klasyczny niebieski jest
 * drugim finalnym wariantem: zapisujemy 10 reprezentatywnych ekranów, aby
 * instrukcja pokazała oba style bez dublowania setek plików BMP.
 *
 * Zestaw ikon jest wymuszany wyłącznie w rendererze. Ekrany UI rysują swój
 * rzeczywisty stan; moduł PANVSWR pozostaje z tej listy wykluczony, żeby
 * żaden zrzut interfejsu nie udawał wyniku pomiaru.
 */
static uint8_t DOK_REAL_ZapiszUzupelnieniaWStylu(const char *katalog,
                                                  const char *manifest,
                                                  uint32_t *numer,
                                                  uint32_t razem,
                                                  const char *sufiks);

static uint8_t DOK_REAL_ZapiszStronyUIWStylu(const char *katalog,
                                                const char *manifest,
                                                uint32_t *numer,
                                                uint32_t razem,
                                                const char *sufiks)
{
    uint32_t i;

    for (i = 0U; i < DOK_REAL_LICZBA_MODULOW_UI; ++i)
    {
        uint32_t strona;
        const uint32_t liczba = dok_real_moduly_ui[i].liczba_stron();

        for (strona = 0U; strona < liczba; ++strona)
        {
            char nazwa[48];

            snprintf(nazwa, sizeof(nazwa), "%s%s",
                     dok_real_moduly_ui[i].nazwa_strony(strona), sufiks);
            dok_real_moduly_ui[i].rysuj_strone(strona);
            if (!DOK_REAL_ZapiszEkran(katalog, manifest, numer, razem,
                                      dok_real_moduly_ui[i].prefiks,
                                      nazwa,
                                      "ui",
                                      0U, 0U, 0U))
                return 0U;
        }
    }
    return 1U;
}

static uint8_t DOK_REAL_ZapiszCRTReprezentatywne(const char *katalog,
                                                     const char *manifest,
                                                     uint32_t *numer,
                                                     uint32_t razem)
{
    uint32_t e;

    for (e = 0U; e < DOK_REAL_CRT_REPREZENTATYWNE; ++e)
    {
        const DOK_REAL_CRT_EKRAN_t *wybor = &dok_real_crt_ekrany[e];
        char nazwa[64];
        uint8_t znaleziono = 0U;

        snprintf(nazwa, sizeof(nazwa), "%s_crt", wybor->nazwa);
        if (wybor->zrodlo == DOK_REAL_CRT_UZUPELNIENIE)
        {
            uint32_t strona;
            for (strona = DOK_REAL_UZUPELNIENIA_PIERWSZA_STRONA;
                 strona < DOK_PRZYKLADY_LiczbaStron(); ++strona)
            {
                if (strcmp(DOK_PRZYKLADY_NazwaStrony(strona), wybor->nazwa) == 0)
                {
                    DOK_PRZYKLADY_RysujStrone(strona);
                    znaleziono = 1U;
                    break;
                }
            }
            if (!znaleziono ||
                !DOK_REAL_ZapiszEkran(katalog, manifest, numer, razem,
                                      "uzup", nazwa, "przyklad_ui_crt", 0U, 0U, 0U))
                return 0U;
        }
        else
        {
            uint32_t i;
            for (i = 0U; i < DOK_REAL_LICZBA_MODULOW_UI && !znaleziono; ++i)
            {
                uint32_t strona;
                if (strcmp(dok_real_moduly_ui[i].prefiks, wybor->prefiks) != 0)
                    continue;
                for (strona = 0U; strona < dok_real_moduly_ui[i].liczba_stron(); ++strona)
                {
                    if (strcmp(dok_real_moduly_ui[i].nazwa_strony(strona), wybor->nazwa) == 0)
                    {
                        dok_real_moduly_ui[i].rysuj_strone(strona);
                        znaleziono = 1U;
                        break;
                    }
                }
            }
            if (!znaleziono ||
                !DOK_REAL_ZapiszEkran(katalog, manifest, numer, razem,
                                      wybor->prefiks, nazwa, "ui_crt", 0U, 0U, 0U))
                return 0U;
        }
    }
    return 1U;
}

static uint8_t DOK_REAL_ZapiszStronyUI(const char *katalog,
                                       const char *manifest,
                                       uint32_t *numer,
                                       uint32_t razem)
{
    uint8_t ok;

    GEN_WylaczTorPomiarowy();
    GEN_WylaczClk2();

    /* Pełny komplet tylko raz — w stylu klasycznym. */
    UI_UstawZestawIkonTymczasowy(0);
    ok = DOK_REAL_ZapiszStronyUIWStylu(katalog, manifest, numer, razem, "_classic");
    if (ok)
        ok = DOK_REAL_ZapiszUzupelnieniaWStylu(katalog, manifest, numer, razem, "_classic");

    /* Drugi styl jest próbką 10 ekranów, nie drugim pełnym przebiegiem. */
    if (ok)
    {
        UI_UstawZestawIkonTymczasowy(1);
        ok = DOK_REAL_ZapiszCRTReprezentatywne(katalog, manifest, numer, razem);
    }

    UI_UstawZestawIkonTymczasowy(-1);
    return ok;
}

static uint8_t DOK_REAL_ZapiszUstawienieZakresu(const char *katalog,
                                                 const char *manifest,
                                                 uint32_t *numer,
                                                 uint32_t razem)
{
    PANFREQ_DokumentacjaRealnaRysuj(140000000U, BS10M);
    return DOK_REAL_ZapiszEkran(katalog, manifest, numer, razem,
                                "pomiar_swr", "ustawienie_zakresu_140_150", "ui",
                                140000000U, 150000000U, 0U);
}

static uint8_t DOK_REAL_ZapiszPanoramy(const char *katalog,
                                       const char *manifest,
                                       uint32_t *numer,
                                       uint32_t razem,
                                       uint32_t minima_hz[DOK_REAL_LICZBA_ZAKRESOW])
{
    uint32_t i;

    for (i = 0U; i < DOK_REAL_LICZBA_ZAKRESOW; ++i)
    {
        uint32_t widok;
        const uint32_t stop_hz = dok_real_zakresy[i].start_hz +
                                 BSVALUES[dok_real_zakresy[i].zakres] * 1000U;

        if (!PANVSWR_DokumentacjaRealnaSkanuj(dok_real_zakresy[i].start_hz,
                                               dok_real_zakresy[i].zakres))
            return 0U;

        minima_hz[i] = PANVSWR_DokumentacjaRealnaMinimumHz();

        for (widok = 0U; widok < DOK_REAL_LICZBA_WIDOKOW_PAN; ++widok)
        {
            char nazwa[56];
            uint32_t marker_hz;

            PANVSWR_DokumentacjaRealnaRysuj(dok_real_widoki_pan[widok]);
            marker_hz = PANVSWR_DokumentacjaRealnaMarkerHz();
            snprintf(nazwa, sizeof(nazwa), "%s_%s",
                     dok_real_zakresy[i].nazwa, dok_real_nazwy_widokow_pan[widok]);

            if (!DOK_REAL_ZapiszEkran(katalog, manifest, numer, razem,
                                      "pomiar_swr", nazwa, "real_skan",
                                      dok_real_zakresy[i].start_hz, stop_hz, marker_hz))
                return 0U;
        }
    }
    return 1U;
}

static uint8_t DOK_REAL_ZapiszPunkt(const char *katalog,
                                    const char *manifest,
                                    uint32_t *numer,
                                    uint32_t razem,
                                    const char *nazwa_bazowa,
                                    uint32_t czestotliwosc_hz)
{
    char nazwa[56];

    if (!MEASUREMENT_DokumentacjaRealnaZmierz(czestotliwosc_hz, DOK_REAL_USREDNIANIE))
        return 0U;

    MEASUREMENT_DokumentacjaRealnaRysuj(POMIAR_DOK_WIDOK_WYNIK);
    snprintf(nazwa, sizeof(nazwa), "%s_wynik", nazwa_bazowa);
    if (!DOK_REAL_ZapiszEkran(katalog, manifest, numer, razem,
                              "pomiar_punkt", nazwa, "real_punkt",
                              czestotliwosc_hz, czestotliwosc_hz, czestotliwosc_hz))
        return 0U;

    MEASUREMENT_DokumentacjaRealnaRysuj(POMIAR_DOK_WIDOK_ANALIZA);
    snprintf(nazwa, sizeof(nazwa), "%s_analiza", nazwa_bazowa);
    return DOK_REAL_ZapiszEkran(katalog, manifest, numer, razem,
                                "pomiar_punkt", nazwa, "real_punkt",
                                czestotliwosc_hz, czestotliwosc_hz, czestotliwosc_hz);
}

static uint8_t DOK_REAL_ZapiszPunkty(const char *katalog,
                                     const char *manifest,
                                     uint32_t *numer,
                                     uint32_t razem,
                                     const uint32_t minima_hz[DOK_REAL_LICZBA_ZAKRESOW])
{
    uint32_t i;

    for (i = 0U; i < DOK_REAL_LICZBA_MINIMOW; ++i)
    {
        if (minima_hz[i] == 0U)
            return 0U;

        if (!DOK_REAL_ZapiszPunkt(katalog, manifest, numer, razem,
                                  dok_real_nazwy_minimow[i], minima_hz[i]))
            return 0U;
    }
    return 1U;
}

/*
 * Czeka na przygotowanie portu przez uzytkownika. Sesja moze trwac kwadrans,
 * wiec instrukcja musi byc widoczna caly czas na ekranie, a nie mignac raz —
 * dlatego rysujemy ekran polecenia i czekamy na dotkniecie, zamiast odliczac
 * czas. Zwraca 0, gdy uzytkownik przerwal sesje przyciskiem Wstecz.
 */
#define DOK_REAL_ETAP_PRZERWIJ 0U
#define DOK_REAL_ETAP_WYKONAJ  1U
#define DOK_REAL_ETAP_POMIN    2U

static uint8_t DOK_REAL_CzekajNaWzorzec(const char *polecenie,
                                        uint32_t etap, uint32_t etapow)
{
    const LCDColor tlo = UI_KolorTlaEkranu();
    char naglowek[64];
    UI_AKCJA_t akcje[3];
    LCDPoint p;

    GEN_WylaczTorPomiarowy();
    GEN_WylaczClk2();

    snprintf(naglowek, sizeof(naglowek), "Wzorzec %lu z %lu",
             (unsigned long)etap, (unsigned long)etapow);

    /*
     * "Pomin" jest rownoprawnym wyjsciem, nie awaryjnym. Kompletu wzorcow nie
     * ma sie zawsze pod reka, a wymuszanie przelutowania rezystora w srodku
     * sesji konczyloby sie porzuceniem calej sesji.
     */
    akcje[0] = (UI_AKCJA_t){1U, "Gotowe", UI_STYL_AKCENT, true, false};
    akcje[1] = (UI_AKCJA_t){2U, "Pomin", UI_STYL_NORMALNY, true, false};
    akcje[2] = (UI_AKCJA_t){3U, "Przerwij", UI_STYL_POWROT, true, false};

    UI_WyczyscEkran();
    UI_RysujPasekGorny(naglowek, true, false, 0);
    UI_RysujPanel(12U, 60U, 456U, 90U, polecenie, UI_STYL_AKCENT);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), tlo, 22U, 168U,
               "Gotowe - zrzuty tego wzorca. Pomin - brak wzorca.");
    UI_RysujPasekAkcji(UI_DOLNY_PASEK_Y, UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 3U);

    while (TOUCH_IsPressed())
        Sleep(5U);

    for (;;)
    {
        if (TOUCH_Poll(&p))
        {
            const int16_t wybor = UI_ZnajdzAkcjePaska(p, UI_DOLNY_PASEK_Y,
                                                      UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 3U);
            if (wybor > 0)
            {
                TOUCH_CzekajNaPuszczenie(35U);
                if (wybor == 1)
                    return DOK_REAL_ETAP_WYKONAJ;
                if (wybor == 2)
                    return DOK_REAL_ETAP_POMIN;
                return DOK_REAL_ETAP_PRZERWIJ;
            }
        }
        Sleep(10U);
    }
}

/*
 * Dla kazdego wzorca jeden przebieg panoramy i jeden odczyt punktowy.
 * Wiecej nie ma sensu: rezystor jest plaski, wiec kolejne zakresy pokazalyby
 * to samo, a sesja z wymiana obciazenia i tak wymaga obecnosci czlowieka.
 */
static uint8_t DOK_REAL_ZapiszWzorce(const char *katalog,
                                     const char *manifest,
                                     uint32_t *numer,
                                     uint32_t razem)
{
    uint32_t i;

    for (i = 0U; i < DOK_REAL_LICZBA_ETAPOW_WZORCOW; ++i)
    {
        char nazwa[56];
        const uint32_t stop_hz = DOK_REAL_WZORZEC_F_START +
                                 BSVALUES[DOK_REAL_WZORZEC_PASMO] * 1000U;

        const uint8_t decyzja = DOK_REAL_CzekajNaWzorzec(dok_real_etapy_wzorcow[i].polecenie,
                                                         i + 1U, DOK_REAL_LICZBA_ETAPOW_WZORCOW);

        if (decyzja == DOK_REAL_ETAP_PRZERWIJ)
            return 0U;
        if (decyzja == DOK_REAL_ETAP_POMIN)
            continue;

        /* Ostatni etap to juz antena — dalsza czesc sesji zajmuje sie nia sama. */
        if (strcmp(dok_real_etapy_wzorcow[i].prefiks, "antena") == 0)
            return 1U;

        if (!PANVSWR_DokumentacjaRealnaSkanuj(DOK_REAL_WZORZEC_F_START,
                                               DOK_REAL_WZORZEC_PASMO))
            return 0U;

        PANVSWR_DokumentacjaRealnaRysuj(PAN_DOK_WIDOK_SWR_MIN);
        snprintf(nazwa, sizeof(nazwa), "%s_swr", dok_real_etapy_wzorcow[i].prefiks);
        if (!DOK_REAL_ZapiszEkran(katalog, manifest, numer, razem,
                                  "wzorzec", nazwa, "real_wzorzec",
                                  DOK_REAL_WZORZEC_F_START, stop_hz, 0U))
            return 0U;

        PANVSWR_DokumentacjaRealnaRysuj(PAN_DOK_WIDOK_SMITH);
        snprintf(nazwa, sizeof(nazwa), "%s_smith", dok_real_etapy_wzorcow[i].prefiks);
        if (!DOK_REAL_ZapiszEkran(katalog, manifest, numer, razem,
                                  "wzorzec", nazwa, "real_wzorzec",
                                  DOK_REAL_WZORZEC_F_START, stop_hz, 0U))
            return 0U;

        if (!DOK_REAL_ZapiszPunkt(katalog, manifest, numer, razem,
                                  dok_real_etapy_wzorcow[i].prefiks,
                                  DOK_REAL_WZORZEC_PUNKT))
            return 0U;
    }
    return 1U;
}

static uint8_t DOK_REAL_ZapiszOpisSesji(const char *katalog, uint32_t razem)
{
    char sciezka[96];
    char tresc[896];

    snprintf(sciezka, sizeof(sciezka), "%s/SESJA.TXT", katalog);
    snprintf(tresc, sizeof(tresc),
             "EU1KY-PL 2026 - sesja zrzutow do instrukcji\r\n"
             "Firmware: %s, build: %s\r\n"
             "Rodzaj danych: rzeczywiste pomiary RF, bez danych DEMO.\r\n"
             "Obiekt: rzeczywista antena UKF/VHF/UHF petlowa z przeciwwagami.\r\n"
             "Usrednianie punktu dokumentacyjnego: %u; panorama: biezace ustawienie uzytkownika.\r\n"
             "Wzorce kontrolne: 50, 72, 22, 100, 200 i 500 Ohm; panorama 14-464 MHz, punkt 216.5 MHz.\r\n"
             "Zakresy anteny: 88-108 MHz, 140-150 MHz, 440-460 MHz.\r\n"
             "Punkty: automatyczne minima SWR z kazdego z trzech zakresow.\r\n"
             "Style UI: pelny zestaw Retro + 10 reprezentatywnych ekranow Klasyczny niebieski (pomiary RF tylko raz).\r\n"
             "Ta sama seria zawiera tez ekrany funkcji: DSP, Strojenie, TDR, S21, Elementy RF, Kwarc, Diagnostyka, Pliki/USB, Laboratorium, Wiele pasm, Szukaj F, Generator i WSPR/FT8.\r\n"
             "Liczba planowanych obrazow: %lu.\r\n",
             PROJEKT_WERSJA, PROJEKT_KOMPILACJA,
             (unsigned)DOK_REAL_USREDNIANIE, (unsigned long)razem);

    return DOK_REAL_ZapiszPlikTekstowy(sciezka, tresc, 0U, "sesja");
}

static uint8_t DOK_REAL_ZapiszZnacznikPomiarow(const char *katalog, uint32_t numer)
{
    char sciezka[96];
    char tresc[256];

    snprintf(sciezka, sizeof(sciezka), "%s/POMIARY.TXT", katalog);
    snprintf(tresc, sizeof(tresc),
             "Pomiary zakonczone poprawnie przed etapem zrzutow interfejsu.\r\n"
             "Build: %s / %s\r\n"
             "Liczba zapisanych obrazow pomiarowych: %lu\r\n",
             PROJEKT_WERSJA, PROJEKT_KOMPILACJA, (unsigned long)numer);
    return DOK_REAL_ZapiszPlikTekstowy(sciezka, tresc, 0U, "pomiarok");
}


/*
 * Druga tura materiałów do instrukcji. Pierwsze 40 stron modułu
 * dokumentacja_przyklady dotyczy pojedynczego pomiaru, SWR, ustawień i
 * kalibracji — te obszary główna sesja już pokrywa lepiej rzeczywistymi
 * zrzutami. Od strony 40 zaczynają się funkcje, których w pierwszej turze
 * brakowało: DSP, Strojenie, TDR, S21, Elementy RF, Kwarc,
 * Diagnostyka, Pliki/USB, Laboratorium, Wiele pasm, Szukaj F, Generator
 * oraz WSPR/FT8.
 */
static uint32_t DOK_REAL_UzupelnieniaLiczbaJednegoStylu(void)
{
    const uint32_t wszystkie = DOK_PRZYKLADY_LiczbaStron();
    return wszystkie > DOK_REAL_UZUPELNIENIA_PIERWSZA_STRONA
        ? wszystkie - DOK_REAL_UZUPELNIENIA_PIERWSZA_STRONA
        : 0U;
}

static uint8_t DOK_REAL_ZapiszUzupelnieniaWStylu(const char *katalog,
                                                  const char *manifest,
                                                  uint32_t *numer,
                                                  uint32_t razem,
                                                  const char *sufiks)
{
    uint32_t strona;

    for (strona = DOK_REAL_UZUPELNIENIA_PIERWSZA_STRONA;
         strona < DOK_PRZYKLADY_LiczbaStron(); ++strona)
    {
        char nazwa[64];
        snprintf(nazwa, sizeof(nazwa), "%s%s", DOK_PRZYKLADY_NazwaStrony(strona), sufiks);
        DOK_PRZYKLADY_RysujStrone(strona);
        if (!DOK_REAL_ZapiszEkran(katalog, manifest, numer, razem,
                                  "uzup", nazwa, "przyklad_ui", 0U, 0U, 0U))
            return 0U;
    }
    return 1U;
}

void DOKUMENTACJA_REALNA_Uruchom(void)
{
    DOK_REAL_KONFIG_t konfiguracja;
    uint32_t numer = 0U;
    uint32_t minima_hz[DOK_REAL_LICZBA_ZAKRESOW] = {0U};
    const uint32_t razem = DOK_REAL_LiczbaStronRazem();
    char katalog_bazowy[64];
    char katalog[72];
    char manifest[88];
    uint8_t konfiguracja_zapisana = 0U;
    uint8_t sukces = 0U;

    dok_real_przerwane_przez_uzytkownika = 0U;

    if (!CFG_CzyKartaSDDostepna() && !CFG_SD_SprobujPrzywrocic())
    {
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_BRAK_KARTY_SD);
        return;
    }

    /*
     * Nie tworzymy materiału instruktażowego z nieskalibrowanego toru.
     * To chroni przed przypadkowym utrwaleniem wykresów, które wyglądają
     * poprawnie graficznie, ale nie mają wartości metrologicznej.
     */
    if (!OSL_IsErrCorrLoaded() || OSL_GetSelected() < 0 || !OSL_IsSelectedValid())
    {
        KOMUNIKAT_PokazTekst("Zrzuty do instrukcji",
                             "Najpierw wykonaj i aktywuj poprawna kalibracje HW oraz OSL.");
        return;
    }

    if (!DOK_REAL_CzyJestMiejsce(razem))
    {
        KOMUNIKAT_PokazTekst("Za malo miejsca na karcie",
                             "Brakuje miejsca na pelna serie BMP. Zwolnij miejsce i sprobuj ponownie.");
        return;
    }

    DOK_REAL_ZapiszKonfiguracje(&konfiguracja);
    konfiguracja_zapisana = 1U;

    /*
     * Automat NIE zmienia uśredniania panoramy ani pomiaru pojedynczego.
     * To jest najważniejsza różnica względem poprzedniej wersji automatu:
     * normalny tor pomiarowy zachowuje dokładnie ustawienia użytkownika.
     * Zakresy i częstotliwości są zmieniane tylko roboczo w RAM i na końcu
     * wracają do zapisanej konfiguracji.
     */

    snprintf(katalog_bazowy, sizeof(katalog_bazowy), "/aa/manual/REAL/%s", JEZYK_KodProjektu());
    if (!DOK_REAL_KatalogIstniejeLubUtworz("/aa") ||
        !DOK_REAL_KatalogIstniejeLubUtworz("/aa/manual") ||
        !DOK_REAL_KatalogIstniejeLubUtworz("/aa/manual/REAL") ||
        !DOK_REAL_KatalogIstniejeLubUtworz(katalog_bazowy) ||
        !DOK_REAL_UtworzNowyKatalogSesji(katalog_bazowy, katalog, sizeof(katalog)))
    {
        KOMUNIKAT_PokazTekst("Zrzuty do instrukcji",
                             "Nie mozna utworzyc nowego katalogu sesji S001...S999.");
        goto koniec;
    }

    snprintf(manifest, sizeof(manifest), "%s/manifest.csv", katalog);
    if (!DOK_REAL_ZapiszPlikTekstowy(
            manifest,
            "numer;plik;jezyk;firmware;build;modul;nazwa;rodzaj_danych;f_start_hz;f_stop_hz;f_marker_hz;usrednianie\r\n",
            0U, "manifest"))
    {
        KOMUNIKAT_PokazTekst("Zrzuty do instrukcji", "Nie mozna utworzyc manifest.csv.");
        goto koniec;
    }

    if (!DOK_REAL_ZapiszOpisSesji(katalog, razem))
    {
        KOMUNIKAT_PokazTekst("Zrzuty do instrukcji", "Nie mozna zapisac SESJA.TXT.");
        goto koniec;
    }

    if (!DOK_REAL_RysujPostep(0U, razem, "Etap 1/2 - najpierw pomiary RF"))
        goto przerwane;

    /* Najpierw wszystkie rzeczywiste pomiary. Interfejs nie ma jeszcze prawa
       zmienic palety ani zapisac konfiguracji. */
    if (!DOK_REAL_ZapiszWzorce(katalog, manifest, &numer, razem))
        goto przerwane;

    if (!DOK_REAL_ZapiszPanoramy(katalog, manifest, &numer, razem, minima_hz))
    {
        if (dok_real_przerwane_przez_uzytkownika)
            goto przerwane;
        goto blad_pomiaru;
    }

    if (!DOK_REAL_ZapiszPunkty(katalog, manifest, &numer, razem, minima_hz))
    {
        if (dok_real_przerwane_przez_uzytkownika)
            goto przerwane;
        goto blad_pomiaru;
    }

    /*
     * Znacznik jest tylko metadana pomocnicza. Jego brak nie moze skasowac
     * udanej serii pomiarowej ani zatrzymac dalszych zrzutow interfejsu.
     */
    (void)DOK_REAL_ZapiszZnacznikPomiarow(katalog, numer);

    /* Od tej chwili automat nie korzysta juz z tymczasowych parametrow pomiaru.
       Przywracamy je i utrwalamy PRZED jakimkolwiek ekranem UI. */
    DOK_REAL_PrzywrocKonfiguracje(&konfiguracja);
    CFG_Flush();
    if (!DOK_REAL_RysujPostep(numer, razem, "Etap 2/2 - pełne UI: klasyczny + CRT"))
        goto przerwane;

    if (!DOK_REAL_ZapiszUstawienieZakresu(katalog, manifest, &numer, razem))
    {
        if (dok_real_przerwane_przez_uzytkownika)
            goto przerwane;
        goto blad_zapisu;
    }

    if (!DOK_REAL_ZapiszStronyUI(katalog, manifest, &numer, razem))
    {
        if (dok_real_przerwane_przez_uzytkownika)
            goto przerwane;
        goto blad_zapisu;
    }

    sukces = 1U;
    goto koniec;

przerwane:
    if (numer > 0U)
    {
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Sesja przerwana", "Session interrupted", "Sitzung abgebrochen", "Сеанс прерван"),
            JEZYK_Wybierz("Zrzuty wykonane do tej pory pozostają na karcie.",
                          "Screenshots saved so far remain on the card.",
                          "Die bisher gespeicherten Screenshots bleiben auf der Karte.",
                          "Уже сохранённые снимки остаются на карте."));
        goto koniec;
    }
    goto koniec;

blad_zapisu:
    {
        char tresc[224];
        DWORD wolne_klastry = 0U;
        FATFS *fs = NULL;
        uint32_t wolne_mb = 0U;
        const FRESULT wynik_getfree = f_getfree(SDPath, &wolne_klastry, &fs);

        if (wynik_getfree == FR_OK && fs != NULL)
        {
            wolne_mb = (uint32_t)(((uint64_t)wolne_klastry *
                                    (uint64_t)fs->csize * 512ULL) / 1048576ULL);
            if (dok_real_ma_licznik_bajtow)
            {
                snprintf(tresc, sizeof(tresc),
                         "Obraz %lu z %lu. Etap: %s, FatFS: %d, "
                         "zapis: %lu/%lu B. Wolne: %lu MB.",
                         (unsigned long)numer, (unsigned long)razem,
                         dok_real_ostatni_etap, (int)dok_real_ostatni_kod,
                         (unsigned long)dok_real_zapisano_b,
                         (unsigned long)dok_real_oczekiwano_b,
                         (unsigned long)wolne_mb);
            }
            else
            {
                snprintf(tresc, sizeof(tresc),
                         "Obraz %lu z %lu. Etap: %s, FatFS: %d. Wolne: %lu MB.",
                         (unsigned long)numer, (unsigned long)razem,
                         dok_real_ostatni_etap, (int)dok_real_ostatni_kod,
                         (unsigned long)wolne_mb);
            }
        }
        else
        {
            /*
             * Nie wolno pokazywac 0 MB, gdy samo f_getfree() sie nie udalo.
             * Zero wyglada wtedy jak pelna karta i prowadzi diagnostyke w zla
             * strone. Pokazujemy osobno kod odczytu wolnego miejsca.
             */
            if (dok_real_ma_licznik_bajtow)
            {
                snprintf(tresc, sizeof(tresc),
                         "Obraz %lu z %lu. Etap: %s, FatFS: %d, "
                         "zapis: %lu/%lu B. Wolne: ? (getfree=%d).",
                         (unsigned long)numer, (unsigned long)razem,
                         dok_real_ostatni_etap, (int)dok_real_ostatni_kod,
                         (unsigned long)dok_real_zapisano_b,
                         (unsigned long)dok_real_oczekiwano_b,
                         (int)wynik_getfree);
            }
            else
            {
                snprintf(tresc, sizeof(tresc),
                         "Obraz %lu z %lu. Etap: %s, FatFS: %d. "
                         "Wolne: ? (getfree=%d).",
                         (unsigned long)numer, (unsigned long)razem,
                         dok_real_ostatni_etap, (int)dok_real_ostatni_kod,
                         (int)wynik_getfree);
            }
        }
        KOMUNIKAT_PokazTekst("Zrzuty przerwane - blad zapisu", tresc);
    }
    goto koniec;

blad_pomiaru:
    KOMUNIKAT_PokazTekst("Zrzuty do instrukcji przerwane",
                         "Nie udalo sie uzyskac wiarygodnych danych pomiarowych.");

koniec:
    GEN_SetMeasurementFreq(0U);
    GEN_WylaczTorPomiarowy();
    GEN_WylaczClk2();

    if (konfiguracja_zapisana)
    {
        DOK_REAL_PrzywrocKonfiguracje(&konfiguracja);
        CFG_Flush();
    }

    /* UI wylacza tor RF. Wracamy do tego samego stanu, jaki daje normalny start
       urzadzenia, bez zmiany kodu generatora ani algorytmu pomiarowego. */
    GEN_Init();

    if (sukces)
    {
        char tresc[128];
        snprintf(tresc, sizeof(tresc), "%lu obrazow zapisano w %s",
                 (unsigned long)numer, katalog);
        KOMUNIKAT_PokazTekst("Zrzuty do instrukcji gotowe", tresc);
    }
}
