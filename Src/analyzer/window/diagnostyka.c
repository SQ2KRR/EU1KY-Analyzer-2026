#include "diagnostyka.h"
#include "centrum_kalibracji.h"
#include "sprawdzenie_if.h"
#include "zgloszenie_bledu.h"
#include "menedzer_plikow.h"
#include "LCD.h"
#include "font.h"
#include "touch.h"
#include "textbox.h"
#include "config.h"
#include "format_konfiguracji.h"
#include "jezyk.h"
#include "si5351.h"
#include "mainwnd.h"
#include "measurement.h"
#include "main.h"
#include "oslfile.h"
#include "oslcal.h"
#include "kalibracja_meta.h"
#include "ui_wspolny.h"
#include "ui_edytor_liczby.h"
#include "num_keypad.h"
#include "komunikaty.h"
#include "DS3231.h"
#include "gen.h"
#include "wersja_projektu.h"
#include "build_timestamp.h"
#include "stm32_ub_adc3_single.h"
#include "wejscia_uzytkownika.h"
#include "sdram_heap.h"
#include "ff.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern void CAMERA_IO_Init(void);
extern uint8_t CAMERA_IO_IsDeviceReady(uint8_t addr);
extern char SDPath[4];
extern void Measure_LCR_Proc(void);

static volatile uint32_t i2c_wyjscie;
static volatile uint32_t i2c_skanuj_ponownie;
static volatile uint32_t si5351_wyjscie;
static volatile uint32_t si5351_odswiez;
static volatile uint32_t sprawdzenie_if_wyjscie;
static uint8_t sprawdzenie_if_ostatnie_spojne = 0U;
static volatile uint32_t sprawdzenie_if_powtorz;
static volatile uint32_t sprawdzenie_if_po_ustawieniach;
static volatile uint32_t sd_diag_wyjscie;

/*
 * Raport jest generowany wyłącznie z ekranu serwisowego, więc pojedynczy
 * współdzielony bufor jest wystarczający i nie obciąża małego stosu MCU.
 * Limit 1024 B daje zapas na rozbudowane sekcje (w tym sprawdzenie IF), ale nadal
 * jawnie ogranicza rozmiar pojedynczego rekordu.
 */
#define DIAGNOSTYKA_RAPORT_MAKS_REKORD_B 1024U
static char diagnostyka_raport_bufor[DIAGNOSTYKA_RAPORT_MAKS_REKORD_B];
static uint32_t diagnostyka_raport_ostatnie_bledy;

uint32_t DIAGNOSTYKA_OstatnieBledyRaportu(void)
{
    return diagnostyka_raport_ostatnie_bledy;
}

static void DIAGNOSTYKA_RaportDodajWynik(uint32_t *bledy, uint32_t maska, int poprawny)
{
    if (bledy != NULL && !poprawny)
        *bledy |= maska;
}

static void I2C_Wyjdz(void)
{
    i2c_wyjscie = 1U;
}

static void I2C_SkanujPonownie(void)
{
    i2c_skanuj_ponownie = 1U;
}

static void SI5351_Wyjdz(void)
{
    si5351_wyjscie = 1U;
}

static void SI5351_ZapiszKwarc(uint32_t hz)
{
    /*
     * Zmieniamy wyłącznie nominalną częstotliwość rezonatora. Generator
     * odczytuje tę wartość przy każdym ustawieniu częstotliwości, więc nie
     * trzeba restartować analizatora. Po zmianie użytkownik powinien jednak
     * ponownie sprawdzić częstotliwość wzorcową i wykonać kalibracje HW/OSL.
     */
    CFG_SetParam(CFG_PARAM_SI5351_XTAL_FREQ, hz);
    CFG_Flush();
    si5351_odswiez = 1U;
}

static void SI5351_Ustaw25MHz(void)
{
    SI5351_ZapiszKwarc(25000000U);
}

static void SI5351_Ustaw27MHz(void)
{
    SI5351_ZapiszKwarc(27000000U);
}

static void SI5351_UstawInna(void)
{
    uint32_t aktualna = CFG_GetParam(CFG_PARAM_SI5351_XTAL_FREQ);
    uint32_t nowa;

    while (TOUCH_IsPressed())
        Sleep(0);

    nowa = aktualna;
    if (UI_EdytujCzestotliwoscHzEx(aktualna, 20000000U, 40000000U, 1U,
                                    JEZYK_Tekst(TEKST_SI5351_WPROWADZ_HZ), &nowa) &&
        nowa != aktualna)
        SI5351_ZapiszKwarc(nowa);
    else
        si5351_odswiez = 1U;
}

static void SI5351_RysujUstawienia(void)
{
    char adres[24];
    char kwarc[32];
    char korekcja[32];
    char referencja[40];
    int32_t kor = (int32_t)CFG_GetParam(CFG_PARAM_SI5351_CORR);
    uint32_t xtal = CFG_GetParam(CFG_PARAM_SI5351_XTAL_FREQ);
    int64_t efektywna = (int64_t)xtal + (int64_t)kor;

    snprintf(adres, sizeof(adres), "0x%02X", (unsigned)(si5351_GetBusAddress() >> 1));
    snprintf(kwarc, sizeof(kwarc), "%.3f MHz", (double)xtal / 1000000.0);
    snprintf(korekcja, sizeof(korekcja), "%+ld Hz", (long)kor);
    snprintf(referencja, sizeof(referencja), "%.6f MHz", (double)efektywna / 1000000.0);

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_SI5351_USTAWIENIA));

    UI_RysujPoleStatusu(16, 42, 218, 52,
                        JEZYK_Tekst(TEKST_SI5351_ADRES_I2C), adres,
                        si5351_IsPresent() ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);
    UI_RysujPoleStatusu(246, 42, 218, 52,
                        JEZYK_Tekst(TEKST_SI5351_KWARC), kwarc,
                        (xtal == 25000000U || xtal == 27000000U) ? UI_STYL_AKCENT : UI_STYL_NORMALNY);

    UI_RysujPanel(16, 102, 448, 88, JEZYK_Tekst(TEKST_DIAGNOSTYKA_TORU_RF), UI_STYL_NORMALNY);
    FONT_SetAttributes(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola());
    FONT_Printf(28, 128, "%s: %s", JEZYK_Tekst(TEKST_SI5351_KOREKCJA), korekcja);
    FONT_Printf(28, 150, "%s: %s", JEZYK_Tekst(TEKST_SI5351_REFERENCJA), referencja);
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_OSTRZEZENIE), UI_KolorTlaPola(),
               28, 170, JEZYK_Tekst(TEKST_SI5351_PO_ZMIANIE_KALIBRUJ));
}


static void DIAGNOSTYKA_Si5351(void)
{
    TEXTBOX_CTX_t kontekst;
    TEXTBOX_t mhz25;
    TEXTBOX_t mhz27;
    TEXTBOX_t inna;
    TEXTBOX_t wstecz;

    while (TOUCH_IsPressed())
        Sleep(0);

    si5351_wyjscie = 0U;
    si5351_odswiez = 0U;

    /* Dolny pasek ma pięć rozłącznych pól; Wstecz nie nachodzi na ustawienia. */
    wstecz = (TEXTBOX_t){
        .x0 = 4, .y0 = 216,
        .tekst_id = TEXTBOX_TEKST(TEKST_WSTECZ),
        .rola = TEXTBOX_ROLA_WSTECZ,
        .font = FONT_FRAN, .width = 78, .height = 46,
        .center = 1, .border = 1,
        .fgcolor = UI_KolorTekstu(UI_STYL_POWROT), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_POWROT),
        .cb = SI5351_Wyjdz,
    };
    mhz25 = (TEXTBOX_t){
        .x0 = 86, .y0 = 216, .text = "25 MHz",
        .font = FONT_FRAN, .width = 86, .height = 46,
        .center = 1, .border = 1,
        .fgcolor = UI_KolorTekstu(UI_STYL_AKCENT), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKCENT),
        .cb = SI5351_Ustaw25MHz,
    };
    mhz27 = (TEXTBOX_t){
        .x0 = 176, .y0 = 216, .text = "27 MHz",
        .font = FONT_FRAN, .width = 86, .height = 46,
        .center = 1, .border = 1,
        .fgcolor = UI_KolorTekstu(UI_STYL_AKCENT), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKCENT),
        .cb = SI5351_Ustaw27MHz,
    };
    inna = (TEXTBOX_t){
        .x0 = 266, .y0 = 216,
        .tekst_id = TEXTBOX_TEKST(TEKST_SI5351_INNA_WARTOSC),
        .font = FONT_FRAN, .width = 210, .height = 46,
        .center = 1, .border = 1,
        .fgcolor = UI_KolorTekstu(UI_STYL_NORMALNY), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_NORMALNY),
        .cb = SI5351_UstawInna,
    };

    TEXTBOX_InitContext(&kontekst);
    TEXTBOX_Append(&kontekst, &wstecz);
    TEXTBOX_Append(&kontekst, &mhz25);
    TEXTBOX_Append(&kontekst, &mhz27);
    TEXTBOX_Append(&kontekst, &inna);

    SI5351_RysujUstawienia();
    TEXTBOX_DrawContext(&kontekst);

    while (!si5351_wyjscie)
    {
        TEXTBOX_HitTest(&kontekst);
        if (si5351_odswiez)
        {
            si5351_odswiez = 0U;
            SI5351_RysujUstawienia();
            TEXTBOX_DrawContext(&kontekst);
        }
        Sleep(10);
    }

    while (TOUCH_IsPressed())
        Sleep(0);
}

static const char *I2C_NazwaUrzadzenia(uint8_t adres_7bit)
{
    switch (adres_7bit)
    {
    case 0x60:
    case 0x62:
    case 0x67:
    case 0x6F:
        return "Si5351";
    case 0x68:
        return "DS3231 RTC";
    default:
        return JEZYK_Tekst(TEKST_I2C_NIEZNANE);
    }
}

static void I2C_RysujListe(void)
{
    uint8_t adres_7bit;
    uint8_t liczba = 0U;
    uint8_t pokazane = 0U;
    uint8_t aktywny_si = (uint8_t)(si5351_GetBusAddress() >> 1);
    char tekst_adresu[8];
    char podsumowanie[48];
    uint16_t y = 82U;
    const uint16_t x_adres = 30U;
    const uint16_t x_nazwa = 112U;
    const uint16_t x_status = 344U;
    const uint16_t krok_y = 28U;
    const uint8_t maks_wierszy = 5U;
    const LCDColor tlo_pola = UI_KolorTlaPola();
    const LCDColor kolor_opisu = UI_KolorTekstu(UI_STYL_NORMALNY);
    const LCDColor kolor_aktywny = UI_KolorRamki(UI_STYL_AKTYWNY);

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_I2C_SKANER));
    UI_RysujPanel(14, 42, 452, 166, JEZYK_Tekst(TEKST_I2C_ZNALEZIONO), UI_STYL_NORMALNY);

    /*
     * Wszystkie trzy kolumny używają tej samej czcionki i stałego kroku 28 px.
     * Poprzednio adres był rysowany FONT_FRANBIG co 24 px, więc kolejne
     * wiersze fizycznie nachodziły na siebie mimo poprawnych współrzędnych X.
     */
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), tlo_pola, x_adres, 56,
               JEZYK_Wybierz("Adres", "Address", "Adresse", "Адрес"));
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), tlo_pola, x_nazwa, 56,
               JEZYK_Wybierz("Urządzenie", "Device", "Gerät", "Устройство"));
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), tlo_pola, x_status, 56,
               JEZYK_Wybierz("Stan", "Status", "Status", "Состояние"));
    LCD_HLine(LCD_MakePoint(24U, 74U), 420U, UI_KolorRamki(UI_STYL_AKCENT));

    CAMERA_IO_Init();

    for (adres_7bit = 0x08U; adres_7bit <= 0x77U; ++adres_7bit)
    {
        const char *nazwa;
        const char *stan = "";
        LCDColor kolor = kolor_opisu;

        if (!CAMERA_IO_IsDeviceReady((uint8_t)(adres_7bit << 1)))
            continue;

        ++liczba;
        if (pokazane >= maks_wierszy)
            continue;

        nazwa = (adres_7bit == aktywny_si) ? "Si5351" : I2C_NazwaUrzadzenia(adres_7bit);
        if (adres_7bit == aktywny_si)
        {
            kolor = kolor_aktywny;
            stan = JEZYK_Wybierz("aktywny", "active", "aktiv", "активен");
        }

        snprintf(tekst_adresu, sizeof(tekst_adresu), "0x%02X", adres_7bit);
        FONT_Write(FONT_FRAN, kolor, tlo_pola, x_adres, y, tekst_adresu);
        FONT_Write(FONT_FRAN, kolor, tlo_pola, x_nazwa, y, nazwa);
        if (stan[0] != '\0')
            FONT_Write(FONT_FRAN, kolor, tlo_pola, x_status, y, stan);

        y = (uint16_t)(y + krok_y);
        ++pokazane;
    }

    if (liczba == 0U)
    {
        FONT_Write(FONT_FRANBIG, UI_KolorRamki(UI_STYL_OSTRZEZENIE), tlo_pola, 34, 104,
                   JEZYK_Tekst(TEKST_I2C_NIE_ZNALEZIONO));
    }
    else if (liczba > pokazane)
    {
        snprintf(podsumowanie, sizeof(podsumowanie), "+ %u", (unsigned)(liczba - pokazane));
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_OSTRZEZENIE), tlo_pola, 386, 190, podsumowanie);
    }
}

static void DIAGNOSTYKA_SkanerI2C(void)
{
    TEXTBOX_CTX_t kontekst;
    TEXTBOX_t skanuj;
    TEXTBOX_t wstecz;

    while (TOUCH_IsPressed())
        Sleep(0);

    i2c_wyjscie = 0U;
    i2c_skanuj_ponownie = 0U;

    wstecz = (TEXTBOX_t){
        .x0 = 0, .y0 = UI_DOLNY_PASEK_Y,
        .tekst_id = TEXTBOX_TEKST(TEKST_WSTECZ),
        .rola = TEXTBOX_ROLA_WSTECZ,
        .font = FONT_FRAN, .width = UI_DOLNY_PRZYCISK_SZEROKOSC,
        .height = UI_DOLNY_PRZYCISK_WYSOKOSC,
        .center = 1, .border = 1,
        .fgcolor = UI_KolorTekstu(UI_STYL_POWROT), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_POWROT),
        .cb = I2C_Wyjdz,
    };
    skanuj = (TEXTBOX_t){
        .x0 = UI_DOLNY_PRZYCISK_SZEROKOSC + UI_DOLNY_PRZYCISK_ODSTEP,
        .y0 = UI_DOLNY_PASEK_Y,
        .text = "Skanuj",
        .font = FONT_FRAN, .width = UI_DOLNY_PRZYCISK_SZEROKOSC,
        .height = UI_DOLNY_PRZYCISK_WYSOKOSC,
        .center = 1, .border = 1,
        .fgcolor = UI_KolorTekstu(UI_STYL_AKCENT), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKCENT),
        .cb = I2C_SkanujPonownie,
    };

    TEXTBOX_InitContext(&kontekst);
    TEXTBOX_Append(&kontekst, &wstecz);
    TEXTBOX_Append(&kontekst, &skanuj);

    I2C_RysujListe();
    TEXTBOX_DrawContext(&kontekst);

    while (!i2c_wyjscie)
    {
        TEXTBOX_HitTest(&kontekst);
        if (i2c_skanuj_ponownie)
        {
            i2c_skanuj_ponownie = 0U;
            I2C_RysujListe();
            TEXTBOX_DrawContext(&kontekst);
        }
        Sleep(10);
    }

    while (TOUCH_IsPressed())
        Sleep(0);
}

static void DIAGNOSTYKA_RaportNazwy(uint32_t data_rtc, uint32_t czas_rtc, uint8_t sekunda,
                                     char *sciezka_docelowa, size_t rozmiar_docelowej,
                                     char *sciezka_tmp, size_t rozmiar_tmp)
{
    if (sciezka_docelowa == NULL || rozmiar_docelowej == 0U ||
        sciezka_tmp == NULL || rozmiar_tmp == 0U)
        return;

    if (data_rtc >= 19800101U && data_rtc <= 20991231U && czas_rtc <= 2359U)
    {
        /*
         * FatFs pracuje bez LFN. Pełną datę zachowujemy więc jako katalog
         * YYYYMMDD, a czas jako plik HHMMSS.txt. Każdy składnik mieści się
         * w 8.3, a raporty nie kolidują między kolejnymi miesiącami.
         */
        snprintf(sciezka_docelowa, rozmiar_docelowej,
                 "/aa/diag/%08lu/%04lu%02u.txt",
                 (unsigned long)data_rtc, (unsigned long)czas_rtc, (unsigned)sekunda);
        snprintf(sciezka_tmp, rozmiar_tmp,
                 "/aa/diag/%08lu/%04lu%02u.tmp",
                 (unsigned long)data_rtc, (unsigned long)czas_rtc, (unsigned)sekunda);
    }
    else
    {
        snprintf(sciezka_docelowa, rozmiar_docelowej, "/aa/diag/last.txt");
        snprintf(sciezka_tmp, rozmiar_tmp, "/aa/diag/last.tmp");
    }
}

static int DIAGNOSTYKA_RaportPisz(FIL *plik, const char *format, ...)
{
    va_list argumenty;
    int dlugosc;
    UINT zapisano = 0U;

    if (plik == NULL || format == NULL)
        return 0;

    va_start(argumenty, format);
    dlugosc = vsnprintf(diagnostyka_raport_bufor,
                        sizeof(diagnostyka_raport_bufor), format, argumenty);
    va_end(argumenty);

    /*
     * vsnprintf zwraca wymaganą długość również wtedy, gdy bufor był za mały.
     * Dzięki temu nigdy nie zapisujemy uciętego rekordu i błąd jest jawny.
     */
    if (dlugosc < 0 || (size_t)dlugosc >= sizeof(diagnostyka_raport_bufor))
        return 0;

    return f_write(plik, diagnostyka_raport_bufor, (UINT)dlugosc, &zapisano) == FR_OK &&
           zapisano == (UINT)dlugosc;
}

static void DIAGNOSTYKA_RaportUwagi(uint32_t uwagi, char *bufor, size_t rozmiar)
{
    struct UWAGA_OPIS
    {
        uint32_t maska;
        const char *opis;
    };
    static const struct UWAGA_OPIS opisy[] = {
        {KAL_META_UWAGA_BRAK_META, "brak_meta"},
        {KAL_META_UWAGA_BRAK_RTC, "brak_rtc"},
        {KAL_META_UWAGA_STARY_FORMAT, "stary_format"},
        {KAL_META_UWAGA_KONFIGURACJA, "inna_konfiguracja_rf"},
        {KAL_META_UWAGA_BRAK_PLIKU, "brak_pliku"},
        {KAL_META_UWAGA_PLIK_ZMIENIONY, "plik_zmieniony"},
        {KAL_META_UWAGA_HW_ZMIENIONE, "hw_zmienione"},
        {KAL_META_UWAGA_RTC_COFNIETY, "rtc_cofniety"},
    };
    size_t i;
    size_t zajete = 0U;

    if (bufor == NULL || rozmiar == 0U)
        return;
    bufor[0] = '\0';

    if (uwagi == 0U)
    {
        snprintf(bufor, rozmiar, "brak");
        return;
    }

    for (i = 0U; i < sizeof(opisy) / sizeof(opisy[0]); ++i)
    {
        int dopisano;
        if ((uwagi & opisy[i].maska) == 0U)
            continue;
        dopisano = snprintf(bufor + zajete, rozmiar - zajete, "%s%s",
                           zajete != 0U ? "," : "", opisy[i].opis);
        if (dopisano < 0 || (size_t)dopisano >= rozmiar - zajete)
            break;
        zajete += (size_t)dopisano;
    }
}

static int DIAGNOSTYKA_RaportKalibracji(FIL *plik, KAL_META_TYP_t typ, int32_t profil)
{
    KAL_META_DANE_t dane = {0};
    KAL_META_OCENA_t ocena = {0};
    char czas[32];
    char uwagi[128];
    int poprawny = 1;

    (void)KAL_META_Pobierz(typ, profil, &dane);
    KAL_META_Ocen(typ, profil, &dane, &ocena);
    KAL_META_FormatujCzas(&dane, czas, sizeof(czas));
    DIAGNOSTYKA_RaportUwagi(ocena.uwagi, uwagi, sizeof(uwagi));

    poprawny &= DIAGNOSTYKA_RaportPisz(plik,
        "\r\n[kalibracja_%s]\r\nprofil=%ld\r\nmetadane=%u\r\nformat=%u\r\nczas=%s\r\n",
        KAL_META_Nazwa(typ), (long)profil, (unsigned)dane.istnieje,
        (unsigned)dane.format, czas);

    poprawny &= DIAGNOSTYKA_RaportPisz(plik,
        "firmware_kalibracji=%s\r\n",
        dane.firmware[0] != '\0' ? dane.firmware : "brak");

    poprawny &= DIAGNOSTYKA_RaportPisz(plik,
        "wiek_dni=%s%lu\r\ntemperatura_rtc_c=%s%.2f\r\n",
        ocena.wiek_dostepny ? "" : "brak;", (unsigned long)ocena.wiek_dni,
        dane.temperatura_rtc_dostepna ? "" : "brak;",
        dane.temperatura_rtc_dostepna ? (double)dane.temperatura_rtc_c_x100 / 100.0 : 0.0);

    poprawny &= DIAGNOSTYKA_RaportPisz(plik,
        "fmin_hz=%lu\r\nfmax_hz=%lu\r\ntyp_syntezy=%lu\r\nplan_harmoniczny=%lu\r\n"
        "harmoniczna_max=%lu\r\nsi5351_max_hz=%lu\r\n",
        (unsigned long)dane.fmin_hz, (unsigned long)dane.fmax_hz,
        (unsigned long)dane.typ_syntezy, (unsigned long)dane.plan_harmoniczny,
        (unsigned long)dane.harmoniczna_max, (unsigned long)dane.si5351_max_hz);

    poprawny &= DIAGNOSTYKA_RaportPisz(plik,
        "si5351_xtal_hz=%lu\r\nsi5351_korekcja_hz=%ld\r\n"
        "crc_pliku_zapisany=%s%08lX\r\ncrc_pliku_biezacy=%s%08lX\r\n",
        (unsigned long)dane.si5351_xtal_hz, (long)dane.si5351_korekcja_hz,
        dane.crc_pliku_dostepny ? "" : "brak;", (unsigned long)dane.crc_pliku,
        ocena.crc_pliku_biezacy_dostepny ? "" : "brak;", (unsigned long)ocena.crc_pliku_biezacy);

    poprawny &= DIAGNOSTYKA_RaportPisz(plik,
        "crc_hw_zaleznosc=%s%08lX\r\ncrc_hw_biezacy=%s%08lX\r\n"
        "uwagi_maska=0x%08lX\r\nuwagi=%s\r\n",
        dane.crc_hw_zaleznosc_dostepna ? "" : "brak;", (unsigned long)dane.crc_hw_zaleznosc,
        ocena.crc_hw_biezacy_dostepny ? "" : "brak;", (unsigned long)ocena.crc_hw_biezacy,
        (unsigned long)ocena.uwagi, uwagi);

    return poprawny;
}



static int DIAGNOSTYKA_RaportSystem(FIL *plik)
{
    FATFS *system_plikow = NULL;
    DWORD wolne_klastry = 0U;
    uint64_t bajty_wolne = 0ULL;
    uint64_t bajty_calkowite = 0ULL;
    uint32_t uid0 = *(const uint32_t *)(UID_BASE + 0U);
    uint32_t uid1 = *(const uint32_t *)(UID_BASE + 4U);
    uint32_t uid2 = *(const uint32_t *)(UID_BASE + 8U);
    uint32_t odczyt_adc = 0U;
    uint32_t skala_adc = CFG_GetParam(CFG_PARAM_Volt_max_Factor);
    CFG_SD_DIAGNOSTYKA_t sd_diag;
    uint32_t i;
    int poprawny = 1;

    CFG_SD_PobierzDiagnostyke(&sd_diag);

    if (skala_adc < 100U || skala_adc > 2000U)
        skala_adc = 614U;

    /* Odczyt baterii jest wykonywany tylko podczas ręcznego generowania raportu. */
    for (i = 0U; i < 4U; ++i)
        odczyt_adc += UB_ADC3_SINGLE_Read_MW(ADC_PF8);
    odczyt_adc /= 4U;

    if (CFG_CzyKartaSDDostepna() &&
        f_getfree(SDPath, &wolne_klastry, &system_plikow) == FR_OK &&
        system_plikow != NULL)
    {
        const uint64_t bajtow_na_klaster = (uint64_t)system_plikow->csize * 512ULL;
        bajty_wolne = (uint64_t)wolne_klastry * bajtow_na_klaster;
        if (system_plikow->n_fatent >= 2U)
            bajty_calkowite = (uint64_t)(system_plikow->n_fatent - 2U) * bajtow_na_klaster;
    }

    poprawny &= DIAGNOSTYKA_RaportPisz(plik,
        "\r\n[system]\r\nuid=%08lX-%08lX-%08lX\r\nkompilacja=%s\r\n"
        /*
         * %llu wypisywalo sie doslownie jako "lu": newlib w konfiguracji
         * nano nie obsluguje modyfikatora dlugiego long, a przy niezgodnosci
         * formatu z argumentem rozjezdza sie CALA dalsza lista — stad smieci
         * w polu sd_blad_nazwa. Dzielimy na megabajty i zostajemy przy %lu.
         */
        "karta_sd=%u\r\nsd_calkowite_mb=%lu\r\nsd_wolne_mb=%lu\r\n"
        "sd_karta_fizyczna=%u\r\nsd_system_plikow=%u\r\nsd_odczyt=%u\r\n"
        "sd_zapis=%u\r\nsd_katalog_aa=%u\r\nsd_konfiguracja=%u\r\n"
        "sd_blad_fatfs=%u\r\nsd_blad_nazwa=%s\r\nsd_operacja=%s\r\n",
        (unsigned long)uid0, (unsigned long)uid1, (unsigned long)uid2,
        BUILD_TIMESTAMP_US,
        (unsigned)(CFG_CzyKartaSDDostepna() ? 1U : 0U),
        (unsigned long)(bajty_calkowite / 1048576ULL),
        (unsigned long)(bajty_wolne / 1048576ULL),
        (unsigned)sd_diag.karta_fizyczna,
        (unsigned)sd_diag.system_plikow,
        (unsigned)sd_diag.odczyt,
        (unsigned)sd_diag.zapis,
        (unsigned)sd_diag.katalog_aa,
        (unsigned)sd_diag.konfiguracja,
        (unsigned)sd_diag.ostatni_blad_fatfs,
        CFG_SD_NazwaBleduFatFs(sd_diag.ostatni_blad_fatfs),
        CFG_SD_NazwaOperacji(sd_diag.ostatnia_operacja));

    poprawny &= DIAGNOSTYKA_RaportPisz(plik,
        "bateria_adc=%lu\r\nbateria_skala_adc_na_v=%lu\r\nbateria_v=%.3f\r\n"
        "i2c_0x55=%u\r\ni2c_si5351_0x%02X=%u\r\ni2c_0x68=%u\r\n",
        (unsigned long)odczyt_adc, (unsigned long)skala_adc,
        skala_adc != 0U ? (double)odczyt_adc / (double)skala_adc : 0.0,
        (unsigned)(CAMERA_IO_IsDeviceReady((uint8_t)(0x55U << 1)) ? 1U : 0U),
        (unsigned)(si5351_GetBusAddress() >> 1),
        (unsigned)(si5351_IsPresent() ? 1U : 0U),
        (unsigned)(CAMERA_IO_IsDeviceReady((uint8_t)(0x68U << 1)) ? 1U : 0U));

    return poprawny;
}


static int DIAGNOSTYKA_ZapiszRaportSciezka(const char *sciezka_docelowa,
                                             const char *sciezka_tmp,
                                             uint32_t data_rtc,
                                             uint32_t czas_rtc,
                                             unsigned char sekunda,
                                             float temperatura)
{
    FIL plik = {0};
    const int32_t profil_osl = OSL_GetSelected();
    uint32_t bledy = 0U;

    diagnostyka_raport_ostatnie_bledy = 0U;

    if (sciezka_docelowa == NULL || sciezka_tmp == NULL)
    {
        diagnostyka_raport_ostatnie_bledy = DIAGNOSTYKA_RAPORT_BLAD_SCIEZKA;
        return 0;
    }
    if (!CFG_CzyKartaSDDostepna())
    {
        diagnostyka_raport_ostatnie_bledy = DIAGNOSTYKA_RAPORT_BLAD_BRAK_SD;
        return 0;
    }

    (void)f_unlink(sciezka_tmp);
    if (f_open(&plik, sciezka_tmp, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        diagnostyka_raport_ostatnie_bledy = DIAGNOSTYKA_RAPORT_BLAD_OTWARCIE;
        return 0;
    }

    {
        char nazwa_projektu[32];
        JEZYK_FormatujNazweProjektu(nazwa_projektu, sizeof(nazwa_projektu));
        DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_NAGLOWEK,
            DIAGNOSTYKA_RaportPisz(&plik,
                "%s - raport diagnostyczny\r\nformat=EU1KY-DIAG-6\r\n"
                "firmware=%s\r\nzasada=diagnostyka_stanu_nie_blokuje_pomiarow\r\n"
                "config_format=CFG1;wersja=%lu;parametry=%u;legacy_parametry=%u\r\n",
                nazwa_projektu, PROJEKT_WERSJA,
                (unsigned long)FORMAT_KONFIG_WERSJA,
                (unsigned)CFG_NUM_PARAMS, (unsigned)CFG_LEGACY_NUM_PARAMS));
    }

    DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_RTC,
        DIAGNOSTYKA_RaportPisz(&plik,
            "rtc_dostepny=%u\r\nrtc_data=%08lu\r\nrtc_czas=%04lu%02u\r\n"
            "temperatura_ds3231_c=%s%.2f\r\n",
            (unsigned)(RTCpresent ? 1U : 0U), (unsigned long)data_rtc,
            (unsigned long)czas_rtc, (unsigned)sekunda,
            RTCpresent ? "" : "brak;", RTCpresent ? (double)temperatura : 0.0));

    DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_PASMO,
        DIAGNOSTYKA_RaportPisz(&plik,
            "band_fmin_hz=%lu\r\nband_fmax_hz=%lu\r\ntyp_syntezy=%lu\r\n"
            "plan_harmoniczny=%u\r\nharmoniczna_max=%lu\r\n",
            (unsigned long)CFG_GetParam(CFG_PARAM_BAND_FMIN),
            (unsigned long)CFG_GetParam(CFG_PARAM_BAND_FMAX),
            (unsigned long)CFG_GetParam(CFG_PARAM_SYNTH_TYPE),
            (unsigned)GEN_PLAN_HARMONICZNY_WERSJA,
            (unsigned long)GEN_MaksHarmoniczna()));

    DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_SYNTEZA,
        DIAGNOSTYKA_RaportPisz(&plik,
            "si5351_max_hz=%lu\r\nsi5351_xtal_hz=%lu\r\nsi5351_korekcja_hz=%ld\r\n"
            "profil_osl=%ld\r\nlc_korekcja=wspolna_osl_s11\r\nrezystor_kontrolny_mohm=%lu\r\n",
            (unsigned long)CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ),
            (unsigned long)CFG_GetParam(CFG_PARAM_SI5351_XTAL_FREQ),
            (long)(int32_t)CFG_GetParam(CFG_PARAM_SI5351_CORR),
            (long)profil_osl,
            (unsigned long)CFG_GetParam(CFG_PARAM_WERYFIKACJA_R_KONTROLNY_MOHM)));

    DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_SYSTEM,
                                 DIAGNOSTYKA_RaportSystem(&plik));

    {
        SPRAWDZENIE_IF_WYNIK_t sprawdzenie_if;
        int sprawdzenie_if_ok;
        if (SPRAWDZENIE_IF_PobierzOstatni(&sprawdzenie_if))
        {
            sprawdzenie_if_ok = DIAGNOSTYKA_RaportPisz(&plik,
                "[sprawdzenie_if]\r\nstatus=%u\r\nf_pomiarowa_hz=%lu\r\nif_oczekiwana_hz=%.3f\r\n"
                "if_zmierzona_hz=%.3f\r\nodchylenie_proc=%.5f\r\njakosc_db=%.2f\r\n"
                "xtal_ustawiony_hz=%lu\r\nxtal_oszacowany_hz=%lu\r\nsugerowany_nominal_hz=%lu\r\n"
                "uwaga=sprawdzenie_if_nie_jest_kalibracja_ppm\r\n",
                (unsigned)sprawdzenie_if.status,
                (unsigned long)sprawdzenie_if.czestotliwosc_pomiarowa_hz,
                (double)sprawdzenie_if.oczekiwana_if_hz,
                (double)sprawdzenie_if.zmierzona_if_hz,
                (double)sprawdzenie_if.odchylenie_proc,
                (double)sprawdzenie_if.jakosc_db,
                (unsigned long)sprawdzenie_if.xtal_ustawiony_hz,
                (unsigned long)sprawdzenie_if.xtal_oszacowany_hz,
                (unsigned long)sprawdzenie_if.sugerowany_nominal_hz);
        }
        else
        {
            sprawdzenie_if_ok = DIAGNOSTYKA_RaportPisz(&plik,
                "[sprawdzenie_if]\r\nstatus=nie_wykonano\r\n");
        }
        DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_SPRAWDZENIE_IF, sprawdzenie_if_ok);
    }

    DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_KAL_HW,
                                 DIAGNOSTYKA_RaportKalibracji(&plik, KAL_META_HW, -1));
    DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_KAL_OSL,
                                 DIAGNOSTYKA_RaportKalibracji(&plik, KAL_META_OSL, profil_osl));
    DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_KAL_S21,
                                 DIAGNOSTYKA_RaportKalibracji(&plik, KAL_META_S21, -1));
    DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_KAL_TDR,
                                 DIAGNOSTYKA_RaportKalibracji(&plik, KAL_META_TDR, -1));
    DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_KAL_BATERIA,
                                 DIAGNOSTYKA_RaportKalibracji(&plik, KAL_META_BATERIA, -1));
    DIAGNOSTYKA_RaportDodajWynik(&bledy, DIAGNOSTYKA_RAPORT_BLAD_KAL_KWARC,
                                 DIAGNOSTYKA_RaportKalibracji(&plik, KAL_META_KWARC, -1));

    if (bledy == 0U && f_sync(&plik) != FR_OK)
        bledy |= DIAGNOSTYKA_RAPORT_BLAD_SYNCHRONIZACJA;
    if (f_close(&plik) != FR_OK)
        bledy |= DIAGNOSTYKA_RAPORT_BLAD_ZAMKNIECIE;

    if (bledy != 0U)
    {
        (void)f_unlink(sciezka_tmp);
        diagnostyka_raport_ostatnie_bledy = bledy;
        return 0;
    }

    (void)f_unlink(sciezka_docelowa);
    if (f_rename(sciezka_tmp, sciezka_docelowa) != FR_OK)
    {
        (void)f_unlink(sciezka_tmp);
        diagnostyka_raport_ostatnie_bledy = DIAGNOSTYKA_RAPORT_BLAD_PODMIANA;
        return 0;
    }

    diagnostyka_raport_ostatnie_bledy = 0U;
    return 1;
}

static int DIAGNOSTYKA_ZbudujSciezkeTmp(const char *sciezka_docelowa,
                                          char *sciezka_tmp, size_t rozmiar_tmp)
{
    const char *ukosnik;
    const char *kropka;
    size_t dlugosc_bazy;

    if (sciezka_docelowa == NULL || sciezka_tmp == NULL || rozmiar_tmp == 0U)
        return 0;

    ukosnik = strrchr(sciezka_docelowa, '/');
    kropka = strrchr(sciezka_docelowa, '.');
    if (kropka != NULL && (ukosnik == NULL || kropka > ukosnik))
        dlugosc_bazy = (size_t)(kropka - sciezka_docelowa);
    else
        dlugosc_bazy = strlen(sciezka_docelowa);

    if (dlugosc_bazy + 5U > rozmiar_tmp)
        return 0;

    memcpy(sciezka_tmp, sciezka_docelowa, dlugosc_bazy);
    memcpy(sciezka_tmp + dlugosc_bazy, ".tmp", 5U);
    return 1;
}

int DIAGNOSTYKA_ZapiszRaportDo(const char *sciezka_docelowa)
{
    uint32_t data_rtc = 0U;
    uint32_t czas_rtc = 0U;
    unsigned char sekunda = 0U;
    short ampm = 0;
    float temperatura = 0.0f;
    char sciezka_tmp[96];

    if (sciezka_docelowa == NULL || sciezka_docelowa[0] == '\0')
        return 0;
    if (!DIAGNOSTYKA_ZbudujSciezkeTmp(sciezka_docelowa, sciezka_tmp, sizeof(sciezka_tmp)))
        return 0;

    if (RTCpresent)
    {
        getDate(&data_rtc);
        getTime(&czas_rtc, &sekunda, &ampm, 0);
        temperatura = getTemperature();
    }

    return DIAGNOSTYKA_ZapiszRaportSciezka(sciezka_docelowa, sciezka_tmp,
                                            data_rtc, czas_rtc, sekunda, temperatura);
}

int DIAGNOSTYKA_ZapiszRaport(void)
{
    uint32_t data_rtc = 0U;
    uint32_t czas_rtc = 0U;
    unsigned char sekunda = 0U;
    short ampm = 0;
    float temperatura = 0.0f;
    char sciezka_docelowa[64];
    char sciezka_tmp[64];

    /* Raport jest dodatkiem serwisowym. Jego brak, błąd SD lub błąd RTC nie
     * może zmienić stanu kalibracji ani wstrzymać działania analizatora. */
    if (!CFG_CzyKartaSDDostepna())
        return 0;

    if (RTCpresent)
    {
        getDate(&data_rtc);
        getTime(&czas_rtc, &sekunda, &ampm, 0);
        temperatura = getTemperature();
    }

    DIAGNOSTYKA_RaportNazwy(data_rtc, czas_rtc, sekunda,
                            sciezka_docelowa, sizeof(sciezka_docelowa),
                            sciezka_tmp, sizeof(sciezka_tmp));
    (void)f_mkdir("/aa");
    (void)f_mkdir("/aa/diag");
    if (data_rtc >= 19800101U && data_rtc <= 20991231U && czas_rtc <= 2359U)
    {
        char katalog_daty[32];
        snprintf(katalog_daty, sizeof(katalog_daty), "/aa/diag/%08lu",
                 (unsigned long)data_rtc);
        (void)f_mkdir(katalog_daty);
    }
    return DIAGNOSTYKA_ZapiszRaportSciezka(sciezka_docelowa, sciezka_tmp,
                                            data_rtc, czas_rtc, sekunda, temperatura);
}









static void SPRAWDZENIE_IF_Wyjdz(void)
{
    sprawdzenie_if_wyjscie = 1U;
}

static void SPRAWDZENIE_IF_Powtorz(void)
{
    sprawdzenie_if_powtorz = 1U;
}

static void SPRAWDZENIE_IF_OtworzUstawienia(void)
{
    /*
     * Ekran Si5351 ma własny kontekst przycisków. Nie wolno po jego zamknięciu
     * dorysować samych przycisków rodzica na pozostawionym obrazie dziecka,
     * bo właśnie to powodowało nakładanie się „Sprawdź / Si5351 / 25 MHz...”.
     * Po powrocie żądamy pełnego odrysowania ekranu sprawdzenia IF.
     */
    DIAGNOSTYKA_Si5351();
    sprawdzenie_if_po_ustawieniach = 1U;
}

static void DIAGNOSTYKA_SprawdzenieIF(void)
{
    SPRAWDZENIE_IF_WYNIK_t wynik;
    TEXTBOX_CTX_t kontekst;
    TEXTBOX_t powtorz;
    TEXTBOX_t ustawienia;
    TEXTBOX_t wstecz;
    char linia[96];
    char linia2[96];
    char sugestia[96];

    while (TOUCH_IsPressed())
        Sleep(0);

    sprawdzenie_if_wyjscie = 0U;
    sprawdzenie_if_powtorz = 1U;
    sprawdzenie_if_po_ustawieniach = 0U;
    sprawdzenie_if_ostatnie_spojne = 0U;

    {
        const UI_PROSTOKAT_t obszar_wstecz = UI_ObszarPrzyciskuDolnego(0U);
        const UI_PROSTOKAT_t obszar_test = UI_ObszarPrzyciskuDolnego(1U);
        const UI_PROSTOKAT_t obszar_si5351 = UI_ObszarPrzyciskuDolnego(2U);

        wstecz = (TEXTBOX_t){
            .x0 = obszar_wstecz.x, .y0 = obszar_wstecz.y,
            .tekst_id = TEXTBOX_TEKST(TEKST_WSTECZ),
            .rola = TEXTBOX_ROLA_WSTECZ,
            .font = FONT_FRAN, .width = obszar_wstecz.szerokosc, .height = obszar_wstecz.wysokosc,
            .center = 1, .border = 1,
            .fgcolor = UI_KolorTekstu(UI_STYL_POWROT), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_POWROT),
            .cb = SPRAWDZENIE_IF_Wyjdz,
        };
        powtorz = (TEXTBOX_t){
            .x0 = obszar_test.x, .y0 = obszar_test.y,
            .text = JEZYK_Wybierz("Sprawdź", "Check", "Prüfen", "Проверить"),
            .font = FONT_FRAN, .width = obszar_test.szerokosc, .height = obszar_test.wysokosc,
            .center = 1, .border = 1,
            .fgcolor = UI_KolorTekstu(UI_STYL_AKCENT), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKCENT),
            .cb = SPRAWDZENIE_IF_Powtorz,
        };
        ustawienia = (TEXTBOX_t){
            .x0 = obszar_si5351.x, .y0 = obszar_si5351.y,
            .text = "Si5351",
            .font = FONT_FRAN, .width = obszar_si5351.szerokosc, .height = obszar_si5351.wysokosc,
            .center = 1, .border = 1,
            .fgcolor = UI_KolorTekstu(UI_STYL_NORMALNY), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_NORMALNY),
            .cb = SPRAWDZENIE_IF_OtworzUstawienia,
        };
    }

    TEXTBOX_InitContext(&kontekst);
    TEXTBOX_Append(&kontekst, &powtorz);
    TEXTBOX_Append(&kontekst, &ustawienia);
    TEXTBOX_Append(&kontekst, &wstecz);

    while (!sprawdzenie_if_wyjscie)
    {
        if (sprawdzenie_if_powtorz)
        {
            sprawdzenie_if_powtorz = 0U;
            UI_WyczyscEkran();
            UI_RysujNaglowek(JEZYK_Wybierz("Sprawdzenie wzorca Si5351 / IF",
                                           "Si5351 / IF reference check",
                                           "Si5351-/IF-Referenzprüfung",
                                           "Тест опоры Si5351 / IF"));
            FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 20, 58,
                       JEZYK_Wybierz("Mierzę ton różnicowy w wejściu audio. Nie zmieniam ustawień.",
                                     "Measuring the beat tone at the audio input. Settings are not changed.",
                                     "Differenzton am Audioeingang wird gemessen. Keine Einstellungen werden geändert.",
                                     "Измеряется разностный тон на аудиовходе. Настройки не меняются."));
            LCD_ShowActiveLayerOnly();
            SPRAWDZENIE_IF_Wykonaj(&wynik);
            sprawdzenie_if_ostatnie_spojne = (uint8_t)(
                wynik.status == SPRAWDZENIE_IF_OK &&
                (!wynik.mozna_sugerowac_nominal ||
                 wynik.sugerowany_nominal_hz == wynik.xtal_ustawiony_hz));

            /*
             * Pozytywne sprawdzenie jest stanem metrologicznym, a nie tylko stanem
             * bieżącego okna. Zapisujemy jego datę i konfigurację RF tak samo
             * jak dla HW/OSL. Jeżeli później użytkownik zmieni XTAL, korekcję,
             * plan harmoniczny albo zakres, metadane same przestaną być zgodne
             * z konfiguracją i ekran Kalibracja poprosi o ponowne sprawdzenie.
             */
            if (sprawdzenie_if_ostatnie_spojne)
                (void)KAL_META_Zapisz(KAL_META_IF, -1);

            UI_WyczyscEkran();
            UI_RysujNaglowek(JEZYK_Wybierz("Sprawdzenie wzorca Si5351 / IF",
                                           "Si5351 / IF reference check",
                                           "Si5351-/IF-Referenzprüfung",
                                           "Тест опоры Si5351 / IF"));

            if (wynik.status == SPRAWDZENIE_IF_OK)
            {
                snprintf(linia, sizeof(linia), "IF: %.1f Hz   ocz.: %.1f Hz   Q: %.1f dB",
                         (double)wynik.zmierzona_if_hz, (double)wynik.oczekiwana_if_hz,
                         (double)wynik.jakosc_db);
                snprintf(linia2, sizeof(linia2), "XTAL menu: %.6f MHz   oszac.: %.6f MHz",
                         (double)wynik.xtal_ustawiony_hz / 1000000.0,
                         (double)wynik.xtal_oszacowany_hz / 1000000.0);
                if (wynik.mozna_sugerowac_nominal &&
                    wynik.sugerowany_nominal_hz != wynik.xtal_ustawiony_hz)
                {
                    snprintf(sugestia, sizeof(sugestia),
                             "Wskazówka: sprawdź ustawienie XTAL; wynik odpowiada ok. %.0f MHz.",
                             (double)wynik.sugerowany_nominal_hz / 1000000.0);
                }
                else
                {
                    snprintf(sugestia, sizeof(sugestia),
                             "Ustawienie nominalne wygląda spójnie. To nie jest kalibracja ppm.");
                }

                UI_RysujPoleStatusu(20, 52, 440, 56,
                                     JEZYK_Wybierz("Ton IF", "IF tone", "IF-Ton", "Тон IF"),
                                     linia, UI_STYL_AKTYWNY);
                UI_RysujPoleStatusu(20, 118, 440, 56,
                                     "Si5351 XTAL", linia2,
                                     wynik.mozna_sugerowac_nominal && wynik.sugerowany_nominal_hz != wynik.xtal_ustawiony_hz
                                         ? UI_STYL_OSTRZEZENIE : UI_STYL_NORMALNY);
                FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(),
                           22, 188, sugestia);
            }
            else
            {
                const char *blad = JEZYK_Wybierz("Nie udało się wiarygodnie zmierzyć tonu IF.",
                                                 "The IF tone could not be measured reliably.",
                                                 "Der IF-Ton konnte nicht zuverlässig gemessen werden.",
                                                 "Не удалось надёжно измерить тон IF.");
                UI_RysujPoleStatusu(20, 70, 440, 80,
                                     JEZYK_Tekst(TEKST_OSTRZEZENIE), blad,
                                     UI_STYL_OSTRZEZENIE);
                FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(),
                           24, 170,
                           JEZYK_Wybierz("Sprawdzenie wymaga użytecznego sygnału w torze audio. Pomiar nadal działa normalnie.",
                                         "The check needs a usable signal in the audio path. Normal measurement is unaffected.",
                                         "Die Prüfung benötigt ein nutzbares Signal im Audiopfad. Normale Messungen bleiben unverändert.",
                                         "Тесту нужен пригодный сигнал в аудиотракте. Обычные измерения не меняются."));
            }
            TEXTBOX_DrawContext(&kontekst);
        }

        if (TEXTBOX_HitTest(&kontekst))
        {
            if (sprawdzenie_if_po_ustawieniach)
            {
                sprawdzenie_if_po_ustawieniach = 0U;
                sprawdzenie_if_ostatnie_spojne = 0U;

                UI_WyczyscEkran();
                UI_RysujNaglowek(JEZYK_Wybierz("Sprawdzenie wzorca Si5351 / IF",
                                               "Si5351 / IF reference check",
                                               "Si5351-/IF-Referenzprüfung",
                                               "Тест опоры Si5351 / IF"));
                UI_RysujPoleStatusu(
                    20, 70, 440, 92,
                    JEZYK_Wybierz("Po ustawieniach Si5351",
                                  "After Si5351 settings",
                                  "Nach Si5351-Einstellungen",
                                  "После настроек Si5351"),
                    JEZYK_Wybierz("Naciśnij „Sprawdź”, aby wykonać nowy pomiar IF.",
                                  "Press “Check” to make a new IF measurement.",
                                  "„Prüfen“ drücken, um IF neu zu messen.",
                                  "Нажмите «Проверить», чтобы заново измерить IF."),
                    UI_STYL_AKCENT);
                TEXTBOX_DrawContext(&kontekst);
            }
            else if (!sprawdzenie_if_wyjscie && !sprawdzenie_if_powtorz)
            {
                TEXTBOX_DrawContext(&kontekst);
            }
        }
        Sleep(10);
    }

    GEN_SetMeasurementFreq(0U);
    while (TOUCH_IsPressed())
        Sleep(0);
}



static void DIAGNOSTYKA_DopiszBladPakietu(char *bufor, size_t rozmiar,
                                            const char *opis)
{
    const size_t zajete = strlen(bufor);
    if (zajete >= rozmiar - 1U || opis == NULL)
        return;

    (void)snprintf(bufor + zajete, rozmiar - zajete, "%s%s",
                   zajete != 0U ? ", " : "", opis);
}

static void DIAGNOSTYKA_PakietBleduUI(void)
{
    char folder[80];
    char tresc[220];
    char elementy[128] = {0};
    uint32_t bledy;

    if (ZGLOSZENIE_UtworzPakiet(folder, sizeof(folder)))
    {
        snprintf(tresc, sizeof(tresc),
                 "%s %s",
                 JEZYK_Wybierz("Pakiet zapisano w:",
                               "Package saved in:",
                               "Paket gespeichert in:",
                               "Пакет сохранён в:"),
                 folder);
        KOMUNIKAT_PokazTekst(JEZYK_Wybierz("Zgłoszenie błędu",
                                           "Bug report",
                                           "Fehlerbericht",
                                           "Отчёт об ошибке"),
                             tresc);
        return;
    }

    bledy = ZGLOSZENIE_OstatnieBledy();
    if ((bledy & ZGLOSZENIE_BLAD_BRAK_SD) != 0U)
    {
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_BRAK_KARTY_SD);
        return;
    }

    if ((bledy & ZGLOSZENIE_BLAD_KATALOG_AA) != 0U)
        DIAGNOSTYKA_DopiszBladPakietu(elementy, sizeof(elementy), "katalog /aa");
    if ((bledy & ZGLOSZENIE_BLAD_KATALOG_ZGLOSZEN) != 0U)
        DIAGNOSTYKA_DopiszBladPakietu(elementy, sizeof(elementy), "katalog zgloszen");
    if ((bledy & ZGLOSZENIE_BLAD_KATALOG_PAKIETU) != 0U)
        DIAGNOSTYKA_DopiszBladPakietu(elementy, sizeof(elementy), "katalog ZGL");
    if ((bledy & ZGLOSZENIE_BLAD_EKRAN) != 0U)
        DIAGNOSTYKA_DopiszBladPakietu(elementy, sizeof(elementy), "ekran.bmp");
    if ((bledy & ZGLOSZENIE_BLAD_RAPORT) != 0U)
        DIAGNOSTYKA_DopiszBladPakietu(elementy, sizeof(elementy), "raport.txt");
    if ((bledy & ZGLOSZENIE_BLAD_README) != 0U)
        DIAGNOSTYKA_DopiszBladPakietu(elementy, sizeof(elementy), "README.txt");

    snprintf(tresc, sizeof(tresc),
             JEZYK_Wybierz("Nie udało się zapisać: %s (kod 0x%02lX).",
                           "Could not save: %s (code 0x%02lX).",
                           "Speichern fehlgeschlagen: %s (Code 0x%02lX).",
                           "Не удалось сохранить: %s (код 0x%02lX)."),
             elementy[0] != '\0' ? elementy : "?", (unsigned long)bledy);
    KOMUNIKAT_PokazTekst(JEZYK_Tekst(TEKST_BLAD), tresc);
}

static const char *DIAGNOSTYKA_SD_StanTekst(CFG_SD_STAN_t stan)
{
    switch (stan)
    {
    case CFG_SD_STAN_OK:
        return JEZYK_Tekst(TEKST_STATUS_OK);
    case CFG_SD_STAN_BLAD:
        return JEZYK_Wybierz("BŁĄD", "ERROR", "FEHLER", "ОШИБКА");
    case CFG_SD_STAN_BRAK:
        return JEZYK_Tekst(TEKST_STATUS_BRAK);
    case CFG_SD_STAN_NIE_SPRAWDZONO:
    default:
        return "--";
    }
}

static UI_STYL_t DIAGNOSTYKA_SD_StanStyl(CFG_SD_STAN_t stan)
{
    if (stan == CFG_SD_STAN_OK)
        return UI_STYL_AKTYWNY;
    if (stan == CFG_SD_STAN_BLAD || stan == CFG_SD_STAN_BRAK)
        return UI_STYL_OSTRZEZENIE;
    return UI_STYL_NIEAKTYWNY;
}

static void DIAGNOSTYKA_SD_Wyjdz(void)
{
    sd_diag_wyjscie = 1U;
}

static void DIAGNOSTYKA_SD_PonowZapis(void)
{
    /*
     * „Ponów” musi umieć odzyskać kartę po FR_NOT_READY, a nie tylko ponownie
     * wywołać zapis na woluminie, który firmware wcześniej uznał za niedostępny.
     * Najpierw wykonujemy kontrolowane odmontowanie/reinicjalizację/mount,
     * dopiero potem uruchamiamy zwykłą transakcyjną ścieżkę zapisu configu.
     */
    if (!CFG_CzyKartaSDDostepna() && !CFG_SD_SprobujPrzywrocic())
        return;
    CFG_Flush();
}

static void DIAGNOSTYKA_SD_Wiersz(uint16_t y, const char *nazwa, CFG_SD_STAN_t stan)
{
    const UI_STYL_t styl = DIAGNOSTYKA_SD_StanStyl(stan);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(),
               28, y, nazwa);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(styl), UI_KolorTlaPola(),
               258, y, DIAGNOSTYKA_SD_StanTekst(stan));
}

static void DIAGNOSTYKA_SD_Rysuj(void)
{
    CFG_SD_DIAGNOSTYKA_t stan;
    char blad[96];
    char operacja[96];

    CFG_SD_PobierzDiagnostyke(&stan);

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Wybierz("Karta SD i system plików",
                                   "SD card and filesystem",
                                   "SD-Karte und Dateisystem",
                                   "SD-карта и файловая система"));
    UI_RysujPanel(14, 42, 452, 176, "microSD / FAT", UI_STYL_NORMALNY);

    DIAGNOSTYKA_SD_Wiersz(58, JEZYK_Wybierz("Karta fizyczna", "Physical card", "Physische Karte", "Физическая карта"), stan.karta_fizyczna);
    DIAGNOSTYKA_SD_Wiersz(80, JEZYK_Wybierz("System plików FAT", "FAT filesystem", "FAT-Dateisystem", "Файловая система FAT"), stan.system_plikow);
    DIAGNOSTYKA_SD_Wiersz(102, JEZYK_Wybierz("Odczyt", "Read", "Lesen", "Чтение"), stan.odczyt);
    DIAGNOSTYKA_SD_Wiersz(124, JEZYK_Wybierz("Zapis", "Write", "Schreiben", "Запись"), stan.zapis);
    DIAGNOSTYKA_SD_Wiersz(146, "Katalog /aa", stan.katalog_aa);
    DIAGNOSTYKA_SD_Wiersz(168, JEZYK_Wybierz("Konfiguracja", "Configuration", "Konfiguration", "Конфигурация"), stan.konfiguracja);

    snprintf(blad, sizeof(blad), "%s: %s (%u)",
             JEZYK_Wybierz("Ostatni błąd FatFs", "Last FatFs error", "Letzter FatFs-Fehler", "Последняя ошибка FatFs"),
             CFG_SD_NazwaBleduFatFs(stan.ostatni_blad_fatfs),
             (unsigned)stan.ostatni_blad_fatfs);
    if (stan.ostatni_blad_fatfs == (uint8_t)FR_INT_ERR &&
        stan.karta_fizyczna == CFG_SD_STAN_OK &&
        stan.system_plikow == CFG_SD_STAN_OK &&
        stan.odczyt == CFG_SD_STAN_OK &&
        stan.zapis == CFG_SD_STAN_BLAD)
    {
        /*
         * Taki układ stanów wystąpił na kartach FAT32 przygotowanych przez
         * jeden z czytników USB: wolumin dawał się zamontować i czytać, lecz
         * FatFs R0.11 zwracał FR_INT_ERR przy pierwszej alokacji danych pliku.
         * Nie nazywamy tego awarią karty — najpierw warto sprawdzić sposób
         * formatowania i inny czytnik.
         */
        snprintf(operacja, sizeof(operacja), "%s: %s; %s",
                 JEZYK_Wybierz("Operacja", "Operation", "Operation", "Операция"),
                 CFG_SD_NazwaOperacji(stan.ostatnia_operacja),
                 JEZYK_Wybierz("sprawdź format/czytnik",
                               "check format/reader",
                               "Format/Leser prüfen",
                               "проверьте формат/ридер"));
    }
    else
    {
        snprintf(operacja, sizeof(operacja), "%s: %s",
                 JEZYK_Wybierz("Operacja", "Operation", "Operation", "Операция"),
                 CFG_SD_NazwaOperacji(stan.ostatnia_operacja));
    }

    FONT_Write(FONT_FRAN, UI_KolorTekstu(stan.ostatni_blad_fatfs == 0U ? UI_STYL_NIEAKTYWNY : UI_STYL_OSTRZEZENIE),
               UI_KolorTlaPola(), 28, 190, blad);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY),
               UI_KolorTlaPola(), 28, 208, operacja);
}

static void DIAGNOSTYKA_SD_Otworz(void)
{
    TEXTBOX_CTX_t kontekst;
    TEXTBOX_t ponow;
    TEXTBOX_t wstecz;

    while (TOUCH_IsPressed())
        Sleep(0);

    sd_diag_wyjscie = 0U;

    {
        const UI_PROSTOKAT_t obszar_wstecz = UI_ObszarPrzyciskuDolnego(0U);
        const UI_PROSTOKAT_t obszar_ponow = UI_ObszarPrzyciskuDolnego(1U);

        wstecz = (TEXTBOX_t){
            .x0 = obszar_wstecz.x, .y0 = obszar_wstecz.y,
            .tekst_id = TEXTBOX_TEKST(TEKST_WSTECZ),
            .rola = TEXTBOX_ROLA_WSTECZ,
            .font = FONT_FRAN, .width = obszar_wstecz.szerokosc, .height = obszar_wstecz.wysokosc,
            .center = 1, .border = 1,
            .fgcolor = UI_KolorTekstu(UI_STYL_POWROT), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_POWROT),
            .cb = DIAGNOSTYKA_SD_Wyjdz,
        };
        ponow = (TEXTBOX_t){
            .x0 = obszar_ponow.x, .y0 = obszar_ponow.y,
            .text = JEZYK_Wybierz("Ponów", "Retry", "Nochmal", "Повтор"),
            .font = FONT_FRAN, .width = obszar_ponow.szerokosc, .height = obszar_ponow.wysokosc,
            .center = 1, .border = 1,
            .fgcolor = UI_KolorTekstu(UI_STYL_AKCENT), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKCENT),
            .cb = DIAGNOSTYKA_SD_PonowZapis,
        };
    }

    TEXTBOX_InitContext(&kontekst);
    TEXTBOX_Append(&kontekst, &ponow);
    TEXTBOX_Append(&kontekst, &wstecz);

    while (!sd_diag_wyjscie)
    {
        DIAGNOSTYKA_SD_Rysuj();
        TEXTBOX_DrawContext(&kontekst);

        while (!sd_diag_wyjscie)
        {
            if (TEXTBOX_HitTest(&kontekst))
                break;
            Sleep(10);
        }
    }

    while (TOUCH_IsPressed())
        Sleep(0);
}


static void DIAGNOSTYKA_PamiecOpis(uint8_t indeks, const char *wartosc)
{
    const char *tytul = "";
    const char *opis = "";
    char tresc[250];

    switch (indeks)
    {
    case 0U:
        tytul = JEZYK_Wybierz("RAM - zapas linkera", "RAM - linker headroom", "RAM - Linkerreserve", "RAM - запас компоновщика");
        opis = JEZYK_Wybierz("Pamięć RAM pozostająca poza obszarami zarezerwowanymi przez linker. Większy zapas oznacza większy margines dla statycznych danych firmware.",
                             "RAM left outside linker-reserved areas. More headroom means more margin for static firmware data.",
                             "RAM außerhalb der vom Linker reservierten Bereiche. Mehr Reserve bedeutet mehr Spielraum für statische Firmware-Daten.",
                             "RAM вне областей, зарезервированных компоновщиком. Больший запас даёт больше места для статических данных прошивки.");
        break;
    case 1U:
        tytul = JEZYK_Wybierz("Sterta C", "C heap", "C-Heap", "Куча C");
        opis = JEZYK_Wybierz("Klasyczna sterta malloc w wewnętrznym RAM. Pokazuje bieżące użycie i wolne miejsce. Mały zapas może powodować błędy alokacji.",
                             "Classic malloc heap in internal RAM. Shows current use and free space. Low headroom can cause allocation failures.",
                             "Klassischer malloc-Heap im internen RAM. Zeigt Nutzung und freien Platz. Wenig Reserve kann Allokationsfehler verursachen.",
                             "Обычная куча malloc во внутренней RAM. Показывает использование и свободное место. Малый запас может вызвать ошибки выделения памяти.");
        break;
    case 2U:
        tytul = JEZYK_Wybierz("SDRAM - dane stałe", "SDRAM - static data", "SDRAM - statische Daten", "SDRAM - статические данные");
        opis = JEZYK_Wybierz("Ilość zewnętrznej SDRAM zajęta na stałe przez duże bufory i tablice programu. To informacja o układzie pamięci, nie ocena dobra/zła.",
                             "External SDRAM permanently occupied by large firmware buffers and tables. This is layout information, not a pass/fail result.",
                             "Externe SDRAM, die dauerhaft durch große Puffer und Tabellen belegt ist. Dies ist Layout-Information, kein Gut/Schlecht-Ergebnis.",
                             "Объём внешней SDRAM, постоянно занятый крупными буферами и таблицами. Это информация о размещении, а не оценка исправности.");
        break;
    case 3U:
        tytul = JEZYK_Wybierz("Sterta SDRAM", "SDRAM heap", "SDRAM-Heap", "Куча SDRAM");
        opis = JEZYK_Wybierz("Pula dynamiczna w zewnętrznej SDRAM używana przez duże operacje. Zielony oznacza bezpieczny zapas; ostrzeżenie pojawia się przy małej ilości wolnej pamięci.",
                             "Dynamic pool in external SDRAM used by large operations. Green means healthy headroom; a warning appears when free memory is low.",
                             "Dynamischer Pool im externen SDRAM für große Operationen. Grün bedeutet ausreichende Reserve; bei wenig freiem Speicher erscheint eine Warnung.",
                             "Динамический пул во внешней SDRAM для крупных операций. Зелёный означает достаточный запас; предупреждение появляется при малом свободном объёме.");
        break;
    case 4U:
        tytul = JEZYK_Wybierz("Największy wolny blok", "Largest free block", "Größter freier Block", "Крупнейший свободный блок");
        opis = JEZYK_Wybierz("Największy ciągły blok dostępny w stercie SDRAM. Jest ważniejszy od samej sumy wolnej pamięci, gdy funkcja potrzebuje jednego dużego bufora.",
                             "Largest contiguous block available in the SDRAM heap. It matters when a function needs one large buffer, even if total free memory is high.",
                             "Größter zusammenhängender Block im SDRAM-Heap. Er ist wichtig, wenn eine Funktion einen einzelnen großen Puffer benötigt.",
                             "Крупнейший непрерывный блок в куче SDRAM. Важен, когда функции нужен один большой буфер, даже при большом суммарном свободном объёме.");
        break;
    case 5U:
    default:
        tytul = JEZYK_Wybierz("Tablice OSL", "OSL tables", "OSL-Tabellen", "Таблицы OSL");
        opis = JEZYK_Wybierz("Pokazuje, gdzie znajdują się duże tablice korekcji OSL. SDRAM jest stanem oczekiwanym, bo oszczędza wewnętrzny RAM mikrokontrolera.",
                             "Shows where the large OSL correction tables are stored. SDRAM is expected because it preserves internal MCU RAM.",
                             "Zeigt den Speicherort der großen OSL-Korrekturtabellen. SDRAM ist erwartet, da interner MCU-RAM geschont wird.",
                             "Показывает, где хранятся большие таблицы коррекции OSL. Ожидается SDRAM, чтобы экономить внутреннюю RAM микроконтроллера.");
        break;
    }
    snprintf(tresc, sizeof(tresc), "%s\n\n%s: %s", opis,
             JEZYK_Wybierz("Bieżąca wartość", "Current value", "Aktueller Wert", "Текущее значение"),
             wartosc != NULL ? wartosc : "-");
    KOMUNIKAT_PokazTekst(tytul, tresc);
}

static void DIAGNOSTYKA_Pamiec(void)
{
    SDRH_STATYSTYKA_t sdram;
    char ram_zapas[40];
    char sterta_c[48];
    char sdram_stala[48];
    char sdram_sterta[64];
    char sdram_blok[52];
    char osl[32];
    extern char __heap_start__;
    extern char __heap_end__;
    extern char __user_sdram_end__;
    extern char __sdram_heap_start__;
    extern char *heap_end;
    const uintptr_t ram_koniec = 0x20050000UL;
    const uintptr_t heap_start = (uintptr_t)&__heap_start__;
    const uintptr_t heap_end_limit = (uintptr_t)&__heap_end__;
    const uintptr_t heap_biezacy = (heap_end != 0) ? (uintptr_t)heap_end : heap_start;
    uint32_t ram_wolny_po_rezerwach = 0U;
    uint32_t heap_uzyte = 0U;
    uint32_t heap_wolne = 0U;
    uint32_t sdram_stale_b = 0U;

    if (ram_koniec > heap_end_limit)
        ram_wolny_po_rezerwach = (uint32_t)(ram_koniec - heap_end_limit);
    if (heap_biezacy >= heap_start && heap_biezacy <= heap_end_limit)
    {
        heap_uzyte = (uint32_t)(heap_biezacy - heap_start);
        heap_wolne = (uint32_t)(heap_end_limit - heap_biezacy);
    }
    if ((uintptr_t)&__user_sdram_end__ >= 0xC00FF000UL &&
        (uintptr_t)&__sdram_heap_start__ >= (uintptr_t)&__user_sdram_end__)
        sdram_stale_b = (uint32_t)((uintptr_t)&__user_sdram_end__ - 0xC00FF000UL);

    SDRH_PobierzStatystyke(&sdram);

    snprintf(ram_zapas, sizeof(ram_zapas), "%lu kB", (unsigned long)(ram_wolny_po_rezerwach / 1024U));
    snprintf(sterta_c, sizeof(sterta_c), "%lu użyte / %lu wolne kB",
             (unsigned long)(heap_uzyte / 1024U), (unsigned long)(heap_wolne / 1024U));
    snprintf(sdram_stala, sizeof(sdram_stala), "%lu kB", (unsigned long)(sdram_stale_b / 1024U));
    snprintf(sdram_sterta, sizeof(sdram_sterta), "%lu / %lu kB wolne",
             (unsigned long)(sdram.wolne_b / 1024U), (unsigned long)(sdram.pojemnosc_b / 1024U));
    snprintf(sdram_blok, sizeof(sdram_blok), "%lu kB", (unsigned long)(sdram.najwiekszy_wolny_blok_b / 1024U));
    snprintf(osl, sizeof(osl), "%s", OSL_CzyTabliceWSdram() ? "SDRAM" : "RAM wewnętrzny");

    while (TOUCH_IsPressed()) Sleep(0);
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        const UI_STYL_t styl_ram = ram_wolny_po_rezerwach < 16384U ? UI_STYL_OSTRZEZENIE
                                   : (ram_wolny_po_rezerwach >= 32768U ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY);
        const UI_STYL_t styl_heap = heap_wolne < 4096U ? UI_STYL_OSTRZEZENIE
                                    : (heap_wolne >= 8192U ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY);
        const UI_STYL_t styl_sdram = sdram.wolne_b < 262144U ? UI_STYL_OSTRZEZENIE : UI_STYL_AKTYWNY;
        const UI_STYL_t styl_blok = sdram.najwiekszy_wolny_blok_b < 131072U ? UI_STYL_OSTRZEZENIE : UI_STYL_AKTYWNY;

        UI_WyczyscEkran();
        UI_RysujNaglowek(JEZYK_Wybierz("Pamięć urządzenia", "Device memory", "Gerätespeicher", "Память устройства"));
        UI_RysujPoleStatusu(12, 42, 220, 52,
                            JEZYK_Wybierz("RAM - zapas linkera", "RAM - linker headroom", "RAM - Linkerreserve", "RAM - запас компоновщика"),
                            ram_zapas, styl_ram);
        UI_RysujPoleStatusu(248, 42, 220, 52,
                            JEZYK_Wybierz("Sterta C", "C heap", "C-Heap", "Куча C"),
                            sterta_c, styl_heap);
        UI_RysujPoleStatusu(12, 104, 220, 52,
                            JEZYK_Wybierz("SDRAM - dane stałe", "SDRAM - static data", "SDRAM - statische Daten", "SDRAM - статические данные"),
                            sdram_stala, UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(248, 104, 220, 52,
                            JEZYK_Wybierz("Sterta SDRAM", "SDRAM heap", "SDRAM-Heap", "Куча SDRAM"),
                            sdram_sterta, styl_sdram);
        UI_RysujPoleStatusu(12, 166, 220, 52,
                            JEZYK_Wybierz("Największy wolny blok", "Largest free block", "Größter freier Block", "Крупнейший свободный блок"),
                            sdram_blok, styl_blok);
        UI_RysujPoleStatusu(248, 166, 220, 52,
                            JEZYK_Wybierz("Tablice OSL", "OSL tables", "OSL-Tabellen", "Таблицы OSL"),
                            osl, OSL_CzyTabliceWSdram() ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);
        UI_RysujWsteczDolny(false);

        for (;;)
        {
            WEJSCIE_ZDARZENIE_t zd = WEJSCIA_PobierzZdarzenie();
            LCDPoint pkt;
            if (zd == WEJSCIE_ZDARZENIE_WSTECZ)
                return;
            if (TOUCH_Poll(&pkt))
            {
                static const UI_PROSTOKAT_t pola[6] = {
                    {12U,42U,220U,52U}, {248U,42U,220U,52U},
                    {12U,104U,220U,52U}, {248U,104U,220U,52U},
                    {12U,166U,220U,52U}, {248U,166U,220U,52U}
                };
                const char *wartosci[6] = { ram_zapas, sterta_c, sdram_stala, sdram_sterta, sdram_blok, osl };
                uint8_t i;
                if (UI_CzyDotknietoWstecz(pkt))
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return;
                }
                for (i = 0U; i < 6U; ++i)
                {
                    if (UI_CzyPunktWObszarze(pkt, &pola[i]))
                    {
                        TOUCH_CzekajNaPuszczenie(30U);
                        DIAGNOSTYKA_PamiecOpis(i, wartosci[i]);
                        break;
                    }
                }
                if (i < 6U) break;
            }
            Sleep(20);
        }
    }
}


/*
 * Poziomy w kanale napieciowym i pradowym w funkcji czestotliwosci.
 * Korekcja sprzetowa wylaczona — chcemy zobaczyc, ile sygnalu naprawde dociera
 * z mostka, a nie ile go zostaje po wyrownaniu kanalow.
 */




static void DIAGNOSTYKA_FormatujMetaKrotko(KAL_META_TYP_t typ, int32_t profil,
                                            char *bufor, size_t rozmiar)
{
    KAL_META_DANE_t meta = {0};
    if (bufor == NULL || rozmiar == 0U)
        return;
    if (!KAL_META_Pobierz(typ, profil, &meta) || !meta.czas_z_rtc)
    {
        snprintf(bufor, rozmiar, "--");
        return;
    }
    snprintf(bufor, rozmiar, "%02lu.%02lu.%02lu %02lu:%02lu",
             (unsigned long)(meta.data_yyyymmdd % 100U),
             (unsigned long)((meta.data_yyyymmdd / 100U) % 100U),
             (unsigned long)((meta.data_yyyymmdd / 10000U) % 100U),
             (unsigned long)(meta.czas_hhmmss / 10000U),
             (unsigned long)((meta.czas_hhmmss / 100U) % 100U));
}

static void DIAGNOSTYKA_RTCInfo(void)
{
    char opis[180];
    uint32_t data = 0U;
    uint32_t czas = 0U;
    unsigned char sekunda = 0U;
    short ampm = 0;
    float temperatura = 0.0f;

    if (!RTCpresent)
    {
        KOMUNIKAT_PokazTekst(JEZYK_Tekst(TEKST_ZEGAR_RTC),
                             JEZYK_Wybierz("Zegar RTC nie został wykryty na magistrali I2C.",
                                           "RTC was not detected on the I2C bus.",
                                           "Die RTC wurde am I2C-Bus nicht erkannt.",
                                           "RTC не обнаружены на шине I2C."));
        return;
    }
    getDate(&data);
    getTime(&czas, &sekunda, &ampm, 0);
    temperatura = getTemperature();
    snprintf(opis, sizeof(opis),
             JEZYK_Wybierz("Data %02lu.%02lu.%04lu, czas %02lu:%02lu:%02u. Temperatura DS3231: %.2f C. To kontrola obecności i poprawności odczytu RTC.",
                           "Date %02lu.%02lu.%04lu, time %02lu:%02lu:%02u. DS3231 temperature: %.2f C. This checks RTC presence and readable data.",
                           "Datum %02lu.%02lu.%04lu, Zeit %02lu:%02lu:%02u. DS3231-Temperatur: %.2f C. Dies prüft Anwesenheit und lesbare RTC-Daten.",
                           "Дата %02lu.%02lu.%04lu, время %02lu:%02lu:%02u. Температура DS3231: %.2f C. Проверяется наличие RTC и чтение данных."),
             (unsigned long)(data % 100U),
             (unsigned long)((data / 100U) % 100U),
             (unsigned long)(data / 10000U),
             (unsigned long)(czas / 100U), (unsigned long)(czas % 100U),
             (unsigned)sekunda, (double)temperatura);
    KOMUNIKAT_PokazTekst(JEZYK_Tekst(TEKST_ZEGAR_RTC), opis);
}

static uint8_t DIAGNOSTYKA_CzyMetaAktualne(KAL_META_TYP_t typ, int32_t profil)
{
    KAL_META_DANE_t dane = {0};
    KAL_META_OCENA_t ocena = {0};

    (void)KAL_META_Pobierz(typ, profil, &dane);
    KAL_META_Ocen(typ, profil, &dane, &ocena);
    return (uint8_t)(dane.istnieje && ocena.uwagi == KAL_META_UWAGA_BRAK);
}

void DIAGNOSTYKA_OtworzSi5351(void)
{
    DIAGNOSTYKA_Si5351();
}

uint8_t DIAGNOSTYKA_OtworzSprawdzenieIF(void)
{
    DIAGNOSTYKA_SprawdzenieIF();
    return sprawdzenie_if_ostatnie_spojne;
}

void DIAGNOSTYKA_OtworzStanMetrologiczny(void)
{
    /* Dawny ekran „Stan metrologiczny” dublował główną Kalibrację.
     * Zachowujemy funkcję dla zgodności ze starszymi wywołaniami, ale
     * kierujemy do jedynego miejsca obsługi kalibracji. */
    CENTRUM_KALIBRACJI_Otworz();
}

void DIAGNOSTYKA_OtworzSD(void)
{
    DIAGNOSTYKA_SD_Otworz();
}

void DIAGNOSTYKA_Otworz(void)
{
    char adres[24];
    char status_si[80];
    char status_rtc[32];
    char status_sd[32];
    char status_kalibracji[96];
    char data_if[28];
    char data_hw[28];
    char data_osl[28];
    OSL_DIAGNOSTYKA_t d;
    CFG_SD_DIAGNOSTYKA_t sd;
    UI_STYL_t styl_sd;
    uint8_t generator_ok = 0U;
    uint8_t kalibracje_ok = 0U;
    UI_AKCJA_t akcje[6] =
    {
        { 0, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false },
        { 1, "I2C", UI_STYL_AKCENT, true, false },
        { 2, JEZYK_Wybierz("Pliki", "Files", "Dateien", "Файлы"), UI_STYL_NORMALNY, true, false },
        { 3, JEZYK_Wybierz("Pamięć", "Memory", "Speicher", "Память"), UI_STYL_NORMALNY, true, false },
        { 4, JEZYK_Wybierz("Weryfikacja", "Verify", "Prüfen", "Проверка"), UI_STYL_AKCENT, true, false },
        { 5, JEZYK_Wybierz("Raport", "Report", "Bericht", "Отчёт"), UI_STYL_NORMALNY, true, false },
    };
    uint8_t fokus = 0U;
    uint8_t wybor = 0U;

    while (TOUCH_IsPressed())
        Sleep(0);
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        OSL_PobierzDiagnostyke(&d);
        {
            const uint32_t typ_syntezy = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);
            if (typ_syntezy == CFG_SYNTH_SI5351)
            {
                const uint8_t if_ok = DIAGNOSTYKA_CzyMetaAktualne(KAL_META_IF, -1);
                DIAGNOSTYKA_FormatujMetaKrotko(KAL_META_IF, -1, data_if, sizeof(data_if));
                snprintf(adres, sizeof(adres), "0x%02X", (unsigned)(si5351_GetBusAddress() >> 1));
                generator_ok = (uint8_t)(si5351_IsPresent() && if_ok);
                if (si5351_IsPresent())
                    snprintf(status_si, sizeof(status_si), "Si5351 %s / IF %s\n%s",
                             adres, if_ok ? "OK" : "--", data_if);
                else
                    snprintf(status_si, sizeof(status_si), "Si5351 %s",
                             JEZYK_Tekst(TEKST_STATUS_BRAK));
            }
            else
            {
                generator_ok = 0U;
                snprintf(status_si, sizeof(status_si), "%s / %s",
                         GEN_PobierzNazweSyntezera(),
                         JEZYK_Wybierz("niezweryf.", "unverified", "ungeprüft", "не проверен"));
            }
        }
        if (RTCpresent)
        {
            uint32_t data = 0U, czas = 0U;
            unsigned char sekunda = 0U;
            short ampm = 0;
            getDate(&data);
            getTime(&czas, &sekunda, &ampm, 0);
            snprintf(status_rtc, sizeof(status_rtc), "OK\n%02lu.%02lu.%02lu %02lu:%02lu",
                     (unsigned long)(data % 100U),
                     (unsigned long)((data / 100U) % 100U),
                     (unsigned long)((data / 10000U) % 100U),
                     (unsigned long)(czas / 100U), (unsigned long)(czas % 100U));
        }
        else
            snprintf(status_rtc, sizeof(status_rtc), "%s", JEZYK_Tekst(TEKST_STATUS_BRAK));

        CFG_SD_PobierzDiagnostyke(&sd);
        if (sd.karta_fizyczna == CFG_SD_STAN_BRAK)
        {
            snprintf(status_sd, sizeof(status_sd), "%s", JEZYK_Tekst(TEKST_STATUS_BRAK));
            styl_sd = UI_STYL_OSTRZEZENIE;
        }
        else if (sd.system_plikow != CFG_SD_STAN_OK)
        {
            snprintf(status_sd, sizeof(status_sd), "%s",
                     JEZYK_Wybierz("FAT: BŁĄD", "FAT: ERROR", "FAT: FEHLER", "FAT: ОШИБКА"));
            styl_sd = UI_STYL_OSTRZEZENIE;
        }
        else if (sd.zapis == CFG_SD_STAN_BLAD)
        {
            snprintf(status_sd, sizeof(status_sd), "%s",
                     JEZYK_Wybierz("ZAPIS: BŁĄD", "WRITE: ERROR", "SCHREIBEN: FEHLER", "ЗАПИСЬ: ОШИБКА"));
            styl_sd = UI_STYL_OSTRZEZENIE;
        }
        else
        {
            snprintf(status_sd, sizeof(status_sd), "%s", JEZYK_Tekst(TEKST_STATUS_OK));
            styl_sd = UI_STYL_AKTYWNY;
        }

        {
            const uint8_t hw_ok = DIAGNOSTYKA_CzyMetaAktualne(KAL_META_HW, -1);
            const int32_t profil_osl = OSL_GetSelected();
            const uint8_t osl_ok = (uint8_t)(
                d.kalibracja_aktywna &&
                profil_osl >= 0 &&
                DIAGNOSTYKA_CzyMetaAktualne(KAL_META_OSL, profil_osl));

            kalibracje_ok = (uint8_t)(hw_ok && osl_ok);

            DIAGNOSTYKA_FormatujMetaKrotko(KAL_META_HW, -1, data_hw, sizeof(data_hw));
            if (profil_osl >= 0)
                DIAGNOSTYKA_FormatujMetaKrotko(KAL_META_OSL, profil_osl, data_osl, sizeof(data_osl));
            else
                snprintf(data_osl, sizeof(data_osl), "--");

            if (profil_osl >= 0)
            {
                snprintf(status_kalibracji, sizeof(status_kalibracji),
                         "HW %s %s\nOSL %c %s %s",
                         hw_ok ? "OK" : "--", data_hw,
                         (char)('A' + profil_osl),
                         osl_ok ? "OK" : "--", data_osl);
            }
            else
            {
                snprintf(status_kalibracji, sizeof(status_kalibracji),
                         "HW %s %s\nOSL --", hw_ok ? "OK" : "--", data_hw);
            }
        }

        UI_WyczyscEkran();
        UI_RysujPasekGorny(JEZYK_Tekst(TEKST_DIAGNOSTYKA), true, false, 0);
        UI_RysujPoleStatusu(18, 44, 214, 70,
                            JEZYK_Wybierz("Generator RF", "RF generator", "HF-Generator", "Генератор RF"),
                            status_si,
                            generator_ok ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);
        UI_RysujPoleStatusu(248, 44, 214, 70, JEZYK_Tekst(TEKST_ZEGAR_RTC), status_rtc,
                            RTCpresent ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);
        UI_RysujPoleStatusu(18, 122, 214, 70, JEZYK_Tekst(TEKST_KARTA_SD), status_sd, styl_sd);
        UI_RysujPoleStatusu(248, 122, 214, 70,
                            JEZYK_Wybierz("Kalibracje", "Calibrations", "Kalibrierungen", "Калибровки"),
                            status_kalibracji,
                            kalibracje_ok ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);
        UI_RysujPasekAkcji(218U, 42U, akcje, 6U);

        for (;;)
        {
            LCDPoint punkt;
            WEJSCIE_ZDARZENIE_t zdarzenie;
            int16_t akcja = -1;

            if (TOUCH_Poll(&punkt))
            {
                const UI_PROSTOKAT_t pole_generator = {18U, 44U, 214U, 70U};
                const UI_PROSTOKAT_t pole_rtc = {248U, 44U, 214U, 70U};
                const UI_PROSTOKAT_t pole_sd = {18U, 122U, 214U, 70U};
                const UI_PROSTOKAT_t pole_kalibracje = {248U, 122U, 214U, 70U};

                if (UI_CzyPunktWObszarze(punkt, &pole_generator))
                {
                    if (CFG_GetParam(CFG_PARAM_SYNTH_TYPE) == CFG_SYNTH_SI5351)
                        akcja = 6;
                    TOUCH_CzekajNaPuszczenie(35U);
                }
                else if (UI_CzyPunktWObszarze(punkt, &pole_rtc))
                {
                    akcja = 8;
                    TOUCH_CzekajNaPuszczenie(35U);
                }
                else if (UI_CzyPunktWObszarze(punkt, &pole_sd))
                {
                    akcja = 7;
                    TOUCH_CzekajNaPuszczenie(35U);
                }
                else if (UI_CzyPunktWObszarze(punkt, &pole_kalibracje))
                {
                    akcja = 9;
                    TOUCH_CzekajNaPuszczenie(35U);
                }
                else
                {
                    akcja = UI_ZnajdzAkcjePaska(punkt, 218U, 42U, akcje, 6U);
                    if (akcja >= 0)
                        TOUCH_CzekajNaPuszczenie(35U);
                }
            }

            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                return;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
                zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                uint8_t i;
                if (!fokus)
                {
                    fokus = 1U;
                    wybor = zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? 0U : 5U;
                }
                else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
                    wybor = (uint8_t)((wybor + 1U) % 6U);
                else
                    wybor = (uint8_t)((wybor + 5U) % 6U);

                for (i = 0U; i < 6U; ++i)
                    akcje[i].zaznaczona = i == wybor;
                UI_RysujPasekAkcji(218U, 42U, akcje, 6U);
                continue;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OK && fokus)
                akcja = (int16_t)wybor;

            if (akcja == 0)
                return;
            if (akcja == 1)
                DIAGNOSTYKA_SkanerI2C();
            else if (akcja == 2)
                PLIKI_OtworzKatalog("/aa");
            else if (akcja == 3)
                DIAGNOSTYKA_Pamiec();
            else if (akcja == 4)
                MEASUREMENT_WeryfikacjaWzorcami();
            else if (akcja == 5)
                DIAGNOSTYKA_PakietBleduUI();
            else if (akcja == 6)
                DIAGNOSTYKA_SprawdzenieIF();
            else if (akcja == 7)
                DIAGNOSTYKA_SD_Otworz();
            else if (akcja == 8)
                DIAGNOSTYKA_RTCInfo();
            else if (akcja == 9)
                CENTRUM_KALIBRACJI_Otworz();
            else
            {
                Sleep(10);
                continue;
            }

            WEJSCIA_WyczyscZdarzenia();
            break;
        }
    }
}

