/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   Modified by KD8CEC
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "font.h"
#include "touch.h"
#include "mainwnd.h"
#include "diagnostyka.h"
#include "centrum_kalibracji.h"
#include "profil_uzytkownika.h"
#include "uzytkownik.h"
#include "jezyk.h"
#include "tryb_interfejsu.h"
#include "splash.h"

#include "textbox.h"
#include "config.h"
#include "generator.h"
#include "measurement.h"
#include "oslcal.h"
#include "oslfile.h"
#include "osl70cm.h"
#include "panvswr2.h"
#include "panfreq.h"
#include "main.h"
#include "usbd_storage.h"
#include "ff_gen_drv.h"
#include "sd_diskio.h"
#include "crash.h"
#include "dsp.h"
#include "gen.h"
#include "aauart.h"
#include "build_timestamp.h"
#include "tdr.h"
#include "screenshot.h"
#include "spectr.h"
#include "stm32_ub_adc3_single.h"
#include "DS3231.h"
#include "wejscia_uzytkownika.h"
#include "ui_wspolny.h"
#include "num_keypad.h"
#include "gpio_control.h"
#include "kalibracja_meta.h"
#include "bitmaps.h"
#include "komunikaty.h"
#include "audiodsp.h"
#include "menedzer_plikow.h"
#include "projektowanie_anten.h"
#include "lacznosc.h"
#include "wersja_projektu.h"
#if PROJEKT_BUILD_DOKUMENTACYJNY_REALNY
#include "dokumentacja_realna.h"
#endif

//KD8CEC
#include "guicontrol.h"
#include "aaprotocol.h"

//#include "trackspectr.h"
extern void Track_Proc(void);
extern void TRACK_Beep(int duration);
extern void Sleep(uint32_t);
extern void WeakSignal_Proc(void);
void DisplayVoltage();

uint32_t date, time;
uint32_t RTCpresent;

void Voltage(void);
void InitVoltage(void);

// MainWnd
static TEXTBOX_CTX_t main_ctx;

// Ustawienia
static void MenuJezyk(void);
static void MenuPoziomFunkcji(void);
static void MenuUstawieniaWyglad(void);
static void MenuIkony(void);
static void MenuRTC(void);
static void MenuOProgramie(void);
static void MenuAccu(void);
static void Rotate(void);

static TEXTBOX_CTX_t menu3_ctx;

// Ustawienia generatora

volatile int NoDate;

// Voltage
static int Volt_max_Display, Volt_min_Display, Volt_max_Factor;

int BattVoltage, Batt;
float VoltFloat, percent;
int cntr, CountMax;
static bool BateriaObecna;

static void RysujNaglowekGlownego(void);
static bool MAINWND_WypelnijStatusUI(UI_STATUS_t *status);


#define M_BGCOLOR LCD_RGB(0, 0, 64)    //Menu item background color
#define M_FGCOLOR LCD_RGB(255, 255, 0) //Menu item foreground color

#define COL1 10  //Column 1 x coordinate
#define COL2 230 //Column 2 x coordinate

static USBD_HandleTypeDef USBD_Device;
extern char SDPath[4];
extern FATFS SDFatFs;
extern uint8_t second;
static bool rqExit1;
static void USB_RysujStan(uint8_t karta_obecna, uint8_t usb_polaczone)
{
    const char *stan_karty;
    const char *stan_usb;
    UI_STYL_t styl_karty;
    UI_STYL_t styl_usb;

    if (karta_obecna)
    {
        stan_karty = JEZYK_Wybierz("Wykryta", "Detected", "Erkannt", "Обнаружена");
        styl_karty = UI_STYL_AKTYWNY;
    }
    else
    {
        stan_karty = JEZYK_Wybierz("Brak karty", "Not detected", "Nicht erkannt", "Не обнаружена");
        styl_karty = UI_STYL_OSTRZEZENIE;
    }

    if (usb_polaczone)
    {
        stan_usb = JEZYK_Wybierz("Połączono", "Connected", "Verbunden", "Подключено");
        styl_usb = UI_STYL_AKTYWNY;
    }
    else
    {
        stan_usb = JEZYK_Wybierz("Czekam na komputer", "Waiting for PC", "Warte auf PC", "Ожидание ПК");
        styl_usb = UI_STYL_NORMALNY;
    }

    UI_RysujPoleStatusu(25, 75, 205, 50,
                        JEZYK_Wybierz("Karta microSD", "microSD card", "microSD-Karte", "Карта microSD"),
                        stan_karty, styl_karty);
    UI_RysujPoleStatusu(250, 75, 205, 50,
                        "USB HS / CN12",
                        stan_usb, styl_usb);
}

static void USB_OdlaczFatFs(void)
{
    /*
     * Najpierw logicznie odmontowujemy wolumin, a dopiero później odpinamy
     * sterownik. W czasie pracy MSC firmware nie może wykonywać żadnych
     * operacji plikowych na tej samej karcie co komputer.
     */
    (void)f_mount(NULL, (TCHAR const *)SDPath, 0);
    CFG_UstawDostepnoscKartySD(false);
    (void)FATFS_UnLinkDriver(SDPath);
    BSP_SD_DeInit();
}

static uint8_t USB_PrzywrocFatFs(void)
{
    uint8_t polaczono;
    FRESULT wynik_montowania;

    BSP_SD_DeInit();
    Sleep(250);

    polaczono = FATFS_LinkDriver(&SD_Driver, SDPath);
    if (polaczono != 0U)
    {
        CFG_UstawDostepnoscKartySD(false);
        return 0U;
    }

    /*
     * Po pracy jako pamięć masowa komputer mógł zmienić FAT. Wymuszamy
     * rzeczywiste ponowne zamontowanie, zamiast tylko zarejestrować wolumin.
     */
    wynik_montowania = f_mount(&SDFatFs, (TCHAR const *)SDPath, 1);
    CFG_SD_UstawStanStartowy(BSP_SD_IsDetected() == SD_PRESENT,
                             (uint8_t)wynik_montowania);
    if (wynik_montowania != FR_OK)
    {
        (void)FATFS_UnLinkDriver(SDPath);
        return 0U;
    }

    return 1U;
}

static uint8_t USB_ZakonczBezRestartu(void)
{
    const USBD_StatusTypeDef stop_wynik = USBD_Stop(&USBD_Device);
    const USBD_StatusTypeDef deinit_wynik = USBD_DeInit(&USBD_Device);

    /* Dajemy komputerowi i kontrolerowi SD czas na zamknięcie ostatnich operacji. */
    Sleep(350);

    {
        const uint8_t sd_ok = USB_PrzywrocFatFs();
        return (stop_wynik == USBD_OK && deinit_wynik == USBD_OK && sd_ok) ? 1U : 0U;
    }
}

static void USB_ZakonczIZglosBlad(void)
{
    if (USB_ZakonczBezRestartu())
        return;

    /*
     * Nie ukrywamy bledu ponownego montowania karty. Po pracy jako MSC FAT
     * mogl zostac zmieniony przez komputer i dalszy zapis bez poprawnego
     * remountu bylby ryzykowny. Stan SD w konfiguracji jest juz oznaczony
     * przez USB_PrzywrocFatFs(); tutaj informujemy o tym uzytkownika.
     */
    KOMUNIKAT_PokazTekst(
        JEZYK_Wybierz("Powrot z USB", "USB exit", "USB beenden", "Выход из USB"),
        JEZYK_Wybierz(
            "Nie udalo sie calkowicie zakonczyc USB lub ponownie zamontowac karty SD. Wyjmij i wloz karte albo uruchom analizator ponownie przed zapisem plikow.",
            "USB could not be fully stopped or the SD card could not be remounted. Reinsert the card or restart the analyzer before writing files.",
            "USB konnte nicht vollstaendig beendet oder die SD-Karte nicht erneut eingebunden werden. Karte neu einsetzen oder den Analysator vor dem Schreiben neu starten.",
            "Не удалось полностью завершить USB или повторно подключить SD-карту. Перед записью файлов переустановите карту или перезапустите анализатор."));
}

static uint8_t USB_UruchomStos(void)
{
    /*
     * Każdy etap uruchamiania USB sprawdzamy osobno. Wcześniej błąd jednego
     * kroku był ignorowany i ekran mógł bez końca pokazywać oczekiwanie na PC.
     * Numer etapu pozwala odróżnić błąd rdzenia, klasy MSC, magazynu i startu.
     */
    if (USBD_Init(&USBD_Device, &MSC_Desc, 0) != USBD_OK)
        return 1U;
    if (USBD_RegisterClass(&USBD_Device, USBD_MSC_CLASS) != USBD_OK)
        return 2U;
    if (USBD_MSC_RegisterStorage(&USBD_Device, &USBD_DISK_fops) != USBD_OK)
        return 3U;
    if (USBD_Start(&USBD_Device) != USBD_OK)
        return 4U;
    return 0U;
}

static void USB_PokazBladStartu(uint8_t etap)
{
    char tekst[72];

    snprintf(tekst, sizeof(tekst),
             JEZYK_Wybierz("Błąd startu USB, etap %u. Zakończ USB i wróć do menu.",
                           "USB start error, stage %u. Exit USB mode and return to menu.",
                           "USB-Startfehler, Stufe %u. USB beenden und zum Menü zurückkehren.",
                           "Ошибка запуска USB, этап %u. Завершите USB и вернитесь в меню."),
             (unsigned int)etap);

    UI_RysujPoleStatusu(25, 75, 430, 60,
                        JEZYK_Wybierz("Uruchamianie USB", "USB startup", "USB-Start", "Запуск USB"),
                        tekst, UI_STYL_OSTRZEZENIE);
}

void MAINWND_USBKartaSDProc(void)
{
    uint8_t poprzedni_stan_karty = 0xFFU;
    uint8_t poprzedni_stan_usb = 0xFFU;

    while (TOUCH_IsPressed())
        ;

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Wybierz("Karta SD przez USB", "SD card via USB", "SD-Karte über USB", "Карта SD через USB"));
    UI_RysujPanel(15, 52, 450, 132,
                  JEZYK_Wybierz("Połączenie z komputerem", "Connection to computer", "Verbindung zum PC", "Подключение к компьютеру"),
                  UI_STYL_NORMALNY);

    FONT_Write(FONT_FRAN, LCD_WHITE, UI_KolorTlaEkranu(), 26, 135,
               JEZYK_Wybierz("Podłącz przewód do USB HS (CN12).",
                              "Connect the cable to USB HS (CN12).",
                              "Kabel an USB HS (CN12) anschließen.",
                              "Подключите кабель к USB HS (CN12)."));
    FONT_Write(FONT_FRAN, LCD_GRAY, UI_KolorTlaEkranu(), 26, 158,
               JEZYK_Wybierz("Nie używaj ST-LINK CN14. DISCO = ST-LINK.",
                              "Do not use ST-LINK CN14. DISCO = ST-LINK.",
                              "ST-LINK CN14 nicht verwenden. DISCO = ST-LINK.",
                              "Не используйте ST-LINK CN14. DISCO = ST-LINK."));

    /* W tym trybie Wstecz znaczy jednoznacznie: zakończ USB i wróć.
     * Nie potrzebujemy drugiego, niestandardowego przycisku wyjścia. */
    UI_RysujWsteczDolny(false);

    /*
     * FatFs nie może równocześnie korzystać z karty, kiedy komputer ma ją
     * zamontowaną jako pamięć masową USB. Najpierw odłączamy sterownik FatFs,
     * a następnie przekazujemy kartę wyłącznie klasie USB MSC.
     */
    USB_OdlaczFatFs();
    Sleep(100);

    {
        const uint8_t blad_startu_usb = USB_UruchomStos();
        if (blad_startu_usb != 0U)
        {
            USB_PokazBladStartu(blad_startu_usb);
            WEJSCIA_WyczyscZdarzenia();

            for (;;)
            {
                WEJSCIE_ZDARZENIE_t zdarzenie;
                LCDPoint coord;

                Sleep(50);
                zdarzenie = WEJSCIA_PobierzZdarzenie();
                if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                {
                    USB_ZakonczIZglosBlad();
                    return;
                }

                if (TOUCH_Poll(&coord))
                {
                    while (TOUCH_IsPressed())
                        ;
                    if (UI_CzyDotknietoWstecz(coord))
                    {
                        USB_ZakonczIZglosBlad();
                        return;
                    }
                }
            }
        }
    }

    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        uint8_t karta_obecna;
        uint8_t usb_polaczone;
        WEJSCIE_ZDARZENIE_t zdarzenie;

        Sleep(50);

        karta_obecna = (BSP_SD_IsDetected() != SD_NOT_PRESENT) ? 1U : 0U;
        usb_polaczone = (USBD_Device.dev_state == USBD_STATE_CONFIGURED) ? 1U : 0U;

        if (karta_obecna != poprzedni_stan_karty || usb_polaczone != poprzedni_stan_usb)
        {
            USB_RysujStan(karta_obecna, usb_polaczone);
            poprzedni_stan_karty = karta_obecna;
            poprzedni_stan_usb = usb_polaczone;
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
        {
            USB_ZakonczIZglosBlad();
            return;
        }

        LCDPoint coord;
        if (TOUCH_Poll(&coord))
        {
            while (TOUCH_IsPressed())
                ;
            if (UI_CzyDotknietoWstecz(coord))
            {
                USB_ZakonczIZglosBlad();
                return;
            }
        }
    }
}

/*
 * Zachowana jako stan zgodnosci dla kodu i testow starszych ekranow.
 * Nowy menedzer zrzutow uzywa g_zrzuty_wybrany, ale Line odzwierciedla
 * aktualny wiersz i nie steruje juz rysowaniem warstwy plikowej.
 */
static uint16_t Line;
static uint16_t rqExitR;

void Exit(void)
{
    rqExitR = 1U;
    InitVoltage();
}

#define ZRZUTY_WIERSZ_Y 56U
#define ZRZUTY_WIERSZ_H 21U
#define ZRZUTY_DOLNY_PASEK_Y 232U

uint16_t FileNo;
volatile int Page;

static SCREENSHOT_PLIK_t g_zrzuty[SCREENSHOT_PLIKOW_NA_STRONIE];
static uint16_t g_zrzuty_pierwszy = 0U;
static uint16_t g_zrzuty_liczba = 0U;
static uint16_t g_zrzuty_razem = 0U;
static uint16_t g_zrzuty_wybrany = 0U;

static void ZRZUTY_FormatujRozmiar(uint32_t rozmiar_b, char *bufor, size_t rozmiar_bufora)
{
    if (bufor == 0 || rozmiar_bufora == 0U)
        return;

    if (rozmiar_b >= (1024UL * 1024UL))
        snprintf(bufor, rozmiar_bufora, "%.1f MB", (double)rozmiar_b / (1024.0 * 1024.0));
    else
        snprintf(bufor, rozmiar_bufora, "%lu kB", (unsigned long)((rozmiar_b + 512UL) / 1024UL));
}

static uint16_t ZRZUTY_LiczbaStron(void)
{
    if (g_zrzuty_razem == 0U)
        return 1U;
    return (uint16_t)((g_zrzuty_razem + SCREENSHOT_PLIKOW_NA_STRONIE - 1U) /
                      SCREENSHOT_PLIKOW_NA_STRONIE);
}

static uint16_t ZRZUTY_BiezacaStrona(void)
{
    return (uint16_t)(g_zrzuty_pierwszy / SCREENSHOT_PLIKOW_NA_STRONIE + 1U);
}

static void ZRZUTY_Wczytaj(void)
{
    int16_t liczba = SCREENSHOT_WczytajStrone(g_zrzuty_pierwszy,
                                              g_zrzuty,
                                              SCREENSHOT_PLIKOW_NA_STRONIE,
                                              &g_zrzuty_razem);

    if (liczba < 0)
    {
        g_zrzuty_liczba = 0U;
        g_zrzuty_razem = 0U;
        g_zrzuty_wybrany = 0U;
        return;
    }

    g_zrzuty_liczba = (uint16_t)liczba;

    /*
     * Po usunieciu ostatniego pliku z ostatniej strony wracamy o jedna
     * strone. Uzytkownik nie trafia dzieki temu na pusty ekran.
     */
    if (g_zrzuty_liczba == 0U && g_zrzuty_pierwszy > 0U)
    {
        g_zrzuty_pierwszy = (uint16_t)(g_zrzuty_pierwszy - SCREENSHOT_PLIKOW_NA_STRONIE);
        liczba = SCREENSHOT_WczytajStrone(g_zrzuty_pierwszy,
                                         g_zrzuty,
                                         SCREENSHOT_PLIKOW_NA_STRONIE,
                                         &g_zrzuty_razem);
        g_zrzuty_liczba = liczba > 0 ? (uint16_t)liczba : 0U;
    }

    if (g_zrzuty_liczba == 0U)
        g_zrzuty_wybrany = 0U;
    else if (g_zrzuty_wybrany >= g_zrzuty_liczba)
        g_zrzuty_wybrany = (uint16_t)(g_zrzuty_liczba - 1U);

    FileNo = g_zrzuty_pierwszy;
    Page = (int)ZRZUTY_BiezacaStrona();
    Line = g_zrzuty_wybrany;
}

static void ZRZUTY_RysujWiersz(uint16_t indeks)
{
    const uint16_t y = (uint16_t)(ZRZUTY_WIERSZ_Y + indeks * ZRZUTY_WIERSZ_H);
    const uint8_t zaznaczony = (uint8_t)(indeks == g_zrzuty_wybrany && indeks < g_zrzuty_liczba);
    const LCDColor tlo = zaznaczony ? UI_KolorTlaPrzycisku(UI_STYL_AKCENT) : UI_KolorTlaPola();
    const LCDColor ramka = zaznaczony ? UI_KolorRamki(UI_STYL_AKCENT) : UI_KolorRamki(UI_STYL_NIEAKTYWNY);
    char rozmiar[16];

    LCD_FillRect(LCD_MakePoint(4, y), LCD_MakePoint(475, (uint16_t)(y + ZRZUTY_WIERSZ_H - 2U)), tlo);
    LCD_Rectangle(LCD_MakePoint(4, y), LCD_MakePoint(475, (uint16_t)(y + ZRZUTY_WIERSZ_H - 2U)), ramka);

    if (indeks >= g_zrzuty_liczba)
        return;

    ZRZUTY_FormatujRozmiar(g_zrzuty[indeks].rozmiar_b, rozmiar, sizeof(rozmiar));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), tlo, 9, (uint16_t)(y + 3U), g_zrzuty[indeks].nazwa);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), tlo, 128, (uint16_t)(y + 3U), g_zrzuty[indeks].data);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), tlo, 236, (uint16_t)(y + 3U), g_zrzuty[indeks].czas);
    FONT_Write_RightAlign(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), tlo,
                          330, (uint16_t)(y + 3U), 468, rozmiar);
}

enum
{
    ZRZUTY_AKCJA_WSTECZ = 0,
    ZRZUTY_AKCJA_POKAZ,
    ZRZUTY_AKCJA_USUN,
    ZRZUTY_AKCJA_POPRZEDNIA,
    ZRZUTY_AKCJA_NASTEPNA
};

static void ZRZUTY_ZbudujAkcje(UI_AKCJA_t akcje[5])
{
    const bool ma_plik = g_zrzuty_liczba > 0U;
    const bool ma_poprzednia = g_zrzuty_pierwszy > 0U;
    const bool ma_nastepna = (uint32_t)g_zrzuty_pierwszy + SCREENSHOT_PLIKOW_NA_STRONIE < g_zrzuty_razem;

    akcje[0] = (UI_AKCJA_t){ ZRZUTY_AKCJA_WSTECZ, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false };
    akcje[1] = (UI_AKCJA_t){ ZRZUTY_AKCJA_POKAZ, JEZYK_Tekst(TEKST_POKAZ), UI_STYL_AKCENT, ma_plik, false };
    akcje[2] = (UI_AKCJA_t){ ZRZUTY_AKCJA_USUN, JEZYK_Tekst(TEKST_USUN), UI_STYL_OSTRZEZENIE, ma_plik, false };
    akcje[3] = (UI_AKCJA_t){ ZRZUTY_AKCJA_POPRZEDNIA, JEZYK_Tekst(TEKST_POPRZ_STRONA), UI_STYL_NORMALNY, ma_poprzednia, false };
    akcje[4] = (UI_AKCJA_t){ ZRZUTY_AKCJA_NASTEPNA, JEZYK_Tekst(TEKST_NAST_STRONA), UI_STYL_NORMALNY, ma_nastepna, false };
}

static void ZRZUTY_Rysuj(void)
{
    char naglowek[64];
    uint16_t i;
    UI_AKCJA_t akcje[5];

    UI_WyczyscEkran();
    BackGrColor = UI_KolorTlaEkranu();
    TextColor = UI_KolorTekstu(UI_STYL_NORMALNY);

    snprintf(naglowek, sizeof(naglowek), "%s  %u/%u",
             JEZYK_Tekst(TEKST_ZRZUTY_EKRANU),
             (unsigned)ZRZUTY_BiezacaStrona(),
             (unsigned)ZRZUTY_LiczbaStron());
    UI_RysujNaglowek(naglowek);

    LCD_FillRect(LCD_MakePoint(4, 34), LCD_MakePoint(475, 53), UI_KolorTlaPrzycisku(UI_STYL_NORMALNY));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPrzycisku(UI_STYL_NORMALNY),
               9, 36, JEZYK_Tekst(TEKST_NAZWA));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPrzycisku(UI_STYL_NORMALNY),
               128, 36, JEZYK_Tekst(TEKST_DATA));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPrzycisku(UI_STYL_NORMALNY),
               236, 36, JEZYK_Tekst(TEKST_CZAS));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPrzycisku(UI_STYL_NORMALNY),
               350, 36, JEZYK_Tekst(TEKST_ROZMIAR));

    for (i = 0U; i < SCREENSHOT_PLIKOW_NA_STRONIE; ++i)
        ZRZUTY_RysujWiersz(i);

    if (g_zrzuty_liczba == 0U)
    {
        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(),
                   92, 125, JEZYK_Tekst(TEKST_BRAK_ZAPISANYCH_ZRZUTOW));
    }

    /*
     * Pasek akcji korzysta z tej samej geometrii do rysowania i hit-testu.
     * Nieaktywne operacje (np. Usuń bez pliku) są od razu widoczne i nie
     * reagują na dotyk, więc ekran nie ma ukrytych wyjątków brzegowych.
     */
    ZRZUTY_ZbudujAkcje(akcje);
    UI_RysujPasekAkcji(ZRZUTY_DOLNY_PASEK_Y, 40U, akcje, 5U);
}

static void ZRZUTY_ZmienWybor(uint16_t nowy)
{
    uint16_t stary;

    if (g_zrzuty_liczba == 0U || nowy >= g_zrzuty_liczba || nowy == g_zrzuty_wybrany)
        return;

    stary = g_zrzuty_wybrany;
    g_zrzuty_wybrany = nowy;
    Line = nowy;
    ZRZUTY_RysujWiersz(stary);
    ZRZUTY_RysujWiersz(g_zrzuty_wybrany);
}

static void ZRZUTY_PokazWybrany(void)
{
    if (g_zrzuty_liczba == 0U || g_zrzuty_wybrany >= g_zrzuty_liczba)
        return;

    (void)SCREENSHOT_PokazPlik(g_zrzuty[g_zrzuty_wybrany].nazwa);
    ZRZUTY_Rysuj();
}

static void ZRZUTY_UsunWybrany(void)
{
    if (g_zrzuty_liczba == 0U || g_zrzuty_wybrany >= g_zrzuty_liczba)
        return;

    (void)SCREENSHOT_UsunPlik(g_zrzuty[g_zrzuty_wybrany].nazwa);
    ZRZUTY_Wczytaj();
    ZRZUTY_Rysuj();
}

static void ZRZUTY_PoprzedniaStrona(void)
{
    if (g_zrzuty_pierwszy == 0U)
        return;

    g_zrzuty_pierwszy = (uint16_t)(g_zrzuty_pierwszy - SCREENSHOT_PLIKOW_NA_STRONIE);
    g_zrzuty_wybrany = 0U;
    ZRZUTY_Wczytaj();
    ZRZUTY_Rysuj();
}

static void ZRZUTY_NastepnaStrona(void)
{
    if ((uint32_t)g_zrzuty_pierwszy + SCREENSHOT_PLIKOW_NA_STRONIE >= g_zrzuty_razem)
        return;

    g_zrzuty_pierwszy = (uint16_t)(g_zrzuty_pierwszy + SCREENSHOT_PLIKOW_NA_STRONIE);
    g_zrzuty_wybrany = 0U;
    ZRZUTY_Wczytaj();
    ZRZUTY_Rysuj();
}

void Reload_Proc(void)
{
    LCDPoint punkt;

    while (TOUCH_IsPressed())
        ;
    WEJSCIA_WyczyscZdarzenia();

    g_zrzuty_pierwszy = 0U;
    g_zrzuty_wybrany = 0U;
    ZRZUTY_Wczytaj();
    ZRZUTY_Rysuj();

    for (;;)
    {
        WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK || zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
            ZRZUTY_PokazWybrany();
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
        {
            if (g_zrzuty_wybrany > 0U)
                ZRZUTY_ZmienWybor((uint16_t)(g_zrzuty_wybrany - 1U));
            else if (g_zrzuty_pierwszy > 0U)
            {
                ZRZUTY_PoprzedniaStrona();
                if (g_zrzuty_liczba > 0U)
                    ZRZUTY_ZmienWybor((uint16_t)(g_zrzuty_liczba - 1U));
            }
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            if (g_zrzuty_wybrany + 1U < g_zrzuty_liczba)
                ZRZUTY_ZmienWybor((uint16_t)(g_zrzuty_wybrany + 1U));
            else if ((uint32_t)g_zrzuty_pierwszy + SCREENSHOT_PLIKOW_NA_STRONIE < g_zrzuty_razem)
                ZRZUTY_NastepnaStrona();
        }

        if (TOUCH_Poll(&punkt))
        {
            while (TOUCH_IsPressed())
                ;

            if (punkt.y >= ZRZUTY_WIERSZ_Y && punkt.y < ZRZUTY_DOLNY_PASEK_Y)
            {
                uint16_t indeks = (uint16_t)((punkt.y - ZRZUTY_WIERSZ_Y) / ZRZUTY_WIERSZ_H);
                if (indeks < g_zrzuty_liczba)
                    ZRZUTY_ZmienWybor(indeks);
            }
            else if (punkt.y >= ZRZUTY_DOLNY_PASEK_Y)
            {
                UI_AKCJA_t akcje[5];
                int16_t akcja;

                ZRZUTY_ZbudujAkcje(akcje);
                akcja = UI_ZnajdzAkcjePaska(punkt, ZRZUTY_DOLNY_PASEK_Y, 40U, akcje, 5U);
                if (akcja == ZRZUTY_AKCJA_WSTECZ)
                    break;
                if (akcja == ZRZUTY_AKCJA_POKAZ)
                    ZRZUTY_PokazWybrany();
                else if (akcja == ZRZUTY_AKCJA_USUN)
                    ZRZUTY_UsunWybrany();
                else if (akcja == ZRZUTY_AKCJA_POPRZEDNIA)
                    ZRZUTY_PoprzedniaStrona();
                else if (akcja == ZRZUTY_AKCJA_NASTEPNA)
                    ZRZUTY_NastepnaStrona();
            }
        }

        Sleep(15);
    }

    InitVoltage();
    UI_WyczyscEkran();
}

static uint8_t second1;

static uint8_t RTC_CzyRokPrzestepny(uint32_t rok)
{
    return (uint8_t)(((rok % 4U) == 0U && (rok % 100U) != 0U) || (rok % 400U) == 0U);
}

static uint8_t RTC_DniWMiesiacu(uint32_t rok, uint32_t miesiac)
{
    static const uint8_t dni[12] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};

    if (miesiac < 1U || miesiac > 12U)
        return 31U;
    if (miesiac == 2U && RTC_CzyRokPrzestepny(rok))
        return 29U;
    return dni[miesiac - 1U];
}

uint8_t RTC_CzyDataPoprawna(void)
{
    const uint32_t rok = date / 10000U;
    const uint32_t miesiac = (date / 100U) % 100U;
    const uint32_t dzien = date % 100U;

    /*
     * W starej wersji rok do testu lutego był omyłkowo pobierany z cyfr MMDD.
     * Tutaj walidujemy bez konwersji na tekst, więc 29 lutego jest rozpoznawany
     * zgodnie z kalendarzem gregoriańskim, także dla lat podzielnych przez 400.
     */
    if (rok < 1980U || rok > 2080U)
        return 7U;
    if (miesiac < 1U || miesiac > 12U)
        return 2U;
    if (dzien < 1U || dzien > RTC_DniWMiesiacu(rok, miesiac))
        return 1U;
    return 0U;
}

void SetInternTime(uint32_t timx)
{
    /* Ustawienie godziny przez użytkownika oznacza początek wybranej minuty. */
    secondsCounter = (timx % 100U) * 60U + (timx / 100U) * 3600U;
    second1 = 0U;
}
void NextDay(void)
{
    if (NoDate == 1)
        return;
    date += 1;
    if (RTC_CzyDataPoprawna() == 0)
        return;
    date = date - (date % 100) + 101; // month +1; day =1
    if (RTC_CzyDataPoprawna() == 0)
        return;
    date = date - (date % 10000) + 10101; // year +1; moth = day = 1
}

uint32_t GetInternTime(uint8_t *secondsx)
{
    uint32_t minutes;
    if (secondsCounter > 86400)
    { // 24 * 3600
        NextDay();
        CFG_SetParam(CFG_PARAM_Date, date);
        CFG_Flush();
        secondsCounter -= 86400;
    }
    minutes = secondsCounter / 60;
    *secondsx = (uint8_t)(secondsCounter % 60);
    return (minutes % 60 + (minutes / 60) * 100);
}

//KD8CEC : RESIZE FONT AND CHANGE POSITION
static void DateTime(void)
{
    uint32_t mon;
    short AMPM1;
    char text1[20];

    if (NoDate == 1)
        return;
    if (RTCpresent)
    {
        getTime(&time, &second1, &AMPM1, 0);
        getDate(&date);
    }
    else
    {

        time = GetInternTime(&second1);
        date = CFG_GetParam(CFG_PARAM_Date);
    }
    mon = date % 10000;
    (void)mon;
    (void)text1;

    /* Czas jest prezentowany we wspolnym pasku stanu u gory ekranu. */
    RysujNaglowekGlownego();
}

void Measure_LCR_Proc(void);
#ifdef _DEBUG_UART
extern void PrintOSLSize(void);
#endif


static void UruchomPozycjeMenuGlownego(int indeks)
{
    const char *opis_przygotowania = JEZYK_Tekst(TEKST_PRZYGOTOWANIE_FUNKCJI);

    switch (indeks)
    {
    case BTN_SINGLE:
        Single_Frequency_Proc();
        break;
    case BTN_SWEEP:
        UI_RysujEkranPrzejsciowy(JEZYK_Tekst(TEKST_MENU_WYKRES_SWR), opis_przygotowania);
        Sleep(1400);
        PANVSWR2_Proc();
        break;
    case BTN_MULTISWR:
        MultiSWR_Proc();
        break;
    case BTN_TUNESWR:
        UI_RysujEkranPrzejsciowy(JEZYK_Tekst(TEKST_MENU_STROJENIE), opis_przygotowania);
        Sleep(1400);
        Tune_SWR_Proc();
        break;
    case BTN_TRACKER:
        if (!GEN_CzyWyjscieDodatkoweObslugiwane())
        {
            UI_RysujEkranPrzejsciowy(
                JEZYK_Tekst(TEKST_MENU_S21),
                JEZYK_Wybierz("Niedostępne: CLK2 pracuje jako wzorzec ADF.",
                              "Unavailable: CLK2 is used as the ADF reference.",
                              "Nicht verfügbar: CLK2 ist ADF-Referenz.",
                              "Недоступно: CLK2 используется как опора ADF."));
            Sleep(1800);
            break;
        }
        UI_RysujEkranPrzejsciowy(JEZYK_Tekst(TEKST_MENU_S21), opis_przygotowania);
        Sleep(1400);
        Track_Proc();
        break;
    case BTN_FIND:
        SPECTR_FindFreq();
        break;
    case BTN_QUARTZ:
        Quartz_proc();
        break;
    case BTN_TDR:
        UI_RysujEkranPrzejsciowy(JEZYK_Tekst(TEKST_MENU_TDR), opis_przygotowania);
        Sleep(1400);
        TDR_Proc();
        break;
    case BTN_LC:
        Measure_LCR_Proc();
        break;
    case BTN_DIAGNOSTYKA:
        DIAGNOSTYKA_Otworz();
        break;
    case BTN_SNAPSHOT:
        Reload_Proc();
        break;
    case BTN_USB:
        MAINWND_USBKartaSDProc();
        break;
    case BTN_RFGEN:
        if (!GEN_CzyWyjscieDodatkoweObslugiwane())
        {
            UI_RysujEkranPrzejsciowy(
                JEZYK_Tekst(TEKST_MENU_GENERATOR),
                JEZYK_Wybierz("Niedostępne: CLK2 pracuje jako wzorzec ADF.",
                              "Unavailable: CLK2 is used as the ADF reference.",
                              "Nicht verfügbar: CLK2 ist ADF-Referenz.",
                              "Недоступно: CLK2 используется как опора ADF."));
            Sleep(1800);
            break;
        }
        GENERATOR_Window_Proc();
        break;
    case BTN_WSPR:
        WeakSignal_Proc();
        break;
    default:
        break;
    }

}

typedef struct
{
    UI_PROSTOKAT_t obszar;
    TEKST_ID_t tekst_id;
    UI_IKONA_MENU_t ikona;
} MAINWND_KAFEL_t;

#define MAINWND_KAFEL(x0_, y0_, x1_, y1_, tekst_, ikona_) \
    { { (uint16_t)(x0_), (uint16_t)(y0_), (uint16_t)((x1_) - (x0_) + 1), \
        (uint16_t)((y1_) - (y0_) + 1) }, (tekst_), (ikona_) }

/*
 * v2.03-test24: docelowe menu działów wariantu B.
 *
 * Po test22 launcher był już aktywny, ale część pozycji prowadziła jeszcze
 * do starych zbiorczych podmenu. Test24 kończy ten etap: każda funkcja
 * użytkowa ma jedno logiczne miejsce, a stare 15-kafelkowe menu nie jest
 * już dostępne jako alternatywna ścieżka. Matematyka pomiarów pozostaje
 * nietknięta; zmienia się wyłącznie nawigacja i punkty wejścia.
 */
typedef enum
{
    MAINWND_DZIAL_POMIAR = 0,
    MAINWND_DZIAL_ANALIZA,
    MAINWND_DZIAL_NARZEDZIA,
    MAINWND_DZIAL_USTAWIENIA,
    MAINWND_DZIAL_PLIKI,
    MAINWND_DZIAL_LICZBA
} MAINWND_DZIAL_t;

typedef enum
{
    MAINWND_WIDOK_DZIALY = 0,
    MAINWND_WIDOK_DZIAL
} MAINWND_WIDOK_t;

typedef struct
{
    TEKST_ID_t tekst_id;
    int16_t akcja;
    UI_IKONA_MENU_t ikona;
} MAINWND_POZYCJA_DZIALU_t;

enum
{
    MAINWND_AKCJA_KAL_CENTRUM = 100,
    MAINWND_AKCJA_KAL_OSL,
    MAINWND_AKCJA_KAL_HW,
    MAINWND_AKCJA_KAL_LC,
    MAINWND_AKCJA_KAL_S21,
    MAINWND_AKCJA_KAL_TDR_VF,
    MAINWND_AKCJA_KAL_AKUMULATOR,
    MAINWND_AKCJA_KAL_WZORCE_DC,
    MAINWND_AKCJA_KAL_WERYFIKACJA,
    MAINWND_AKCJA_KAL_STAN,
    MAINWND_AKCJA_KAL_METROLOGIA,

    MAINWND_AKCJA_UST_WYGLAD = 120,
    MAINWND_AKCJA_UST_STYL,
    MAINWND_AKCJA_UST_JEZYK,
    MAINWND_AKCJA_UST_POZIOM_FUNKCJI,
    MAINWND_AKCJA_UST_DATA_CZAS,
    MAINWND_AKCJA_UST_UZYTKOWNIK,
    MAINWND_AKCJA_UST_LACZNOSC,
    MAINWND_AKCJA_UST_OBROT,
    MAINWND_AKCJA_UST_DIAGNOSTYKA,
    MAINWND_AKCJA_UST_KONFIGURACJA,
    MAINWND_AKCJA_UST_O_PROGRAMIE,

    MAINWND_AKCJA_PLIKI_MENEDZER = 140,
    MAINWND_AKCJA_PLIKI_DOKUMENTACJA,

    MAINWND_AKCJA_NARZEDZIA_DSP = 150,
    MAINWND_AKCJA_NARZEDZIA_ANTENY
};

#define MAINWND_LISTA_X 8U
#define MAINWND_LISTA_Y 40U
#define MAINWND_LISTA_W 464U
#define MAINWND_LISTA_H 225U
#define MAINWND_LISTA_WIDOCZNYCH 5U
#define MAINWND_LISTA_MAKS_POZYCJI 8U

#define MAINWND_KAFEL_DZIALU_SZEROKOSC 150U
#define MAINWND_KAFEL_DZIALU_WYSOKOSC 88U
#define MAINWND_DZIAL_KOLUMNA_1_X 8U
#define MAINWND_DZIAL_KOLUMNA_2_X 165U
#define MAINWND_DZIAL_KOLUMNA_3_X 322U
#define MAINWND_DZIAL_DOLNY_1_X 83U
#define MAINWND_DZIAL_DOLNY_2_X 247U
#define MAINWND_DZIAL_WIERSZ_1_Y 60U
#define MAINWND_DZIAL_WIERSZ_2_Y 156U
#define MAINWND_KAFEL_ROZMIAR(x_, y_, w_, h_, tekst_, ikona_) \
    MAINWND_KAFEL((x_), (y_), (x_) + (w_) - 1U, (y_) + (h_) - 1U, (tekst_), (ikona_))

static MAINWND_WIDOK_t g_mainwnd_widok = MAINWND_WIDOK_DZIALY;
static MAINWND_DZIAL_t g_mainwnd_dzial = MAINWND_DZIAL_POMIAR;

/*
 * Ekran startowy świadomie wraca do symboli znanych ze starego menu.
 * Nie tworzymy dla działu drugiej, „podobnej” ikony, jeżeli historyczny
 * odpowiednik już istnieje. Dzięki temu aparat jest dokładnie aparatem ze
 * Zrzutów, koło zębate dokładnie ikoną Ustawień itd. Narzędzia używają
 * historycznej Diagnostyki (układ scalony), a nie ikony Generatora.
 */
static const MAINWND_KAFEL_t kafle_dzialow[MAINWND_DZIAL_LICZBA] =
{
    MAINWND_KAFEL_ROZMIAR(MAINWND_DZIAL_KOLUMNA_1_X, MAINWND_DZIAL_WIERSZ_1_Y, MAINWND_KAFEL_DZIALU_SZEROKOSC, MAINWND_KAFEL_DZIALU_WYSOKOSC, TEKST_DZIAL_POMIAR,     UI_IKONA_POJEDYNCZY),
    MAINWND_KAFEL_ROZMIAR(MAINWND_DZIAL_KOLUMNA_2_X, MAINWND_DZIAL_WIERSZ_1_Y, MAINWND_KAFEL_DZIALU_SZEROKOSC, MAINWND_KAFEL_DZIALU_WYSOKOSC, TEKST_DZIAL_ANALIZA,    UI_IKONA_DZIAL_ANALIZA),
    MAINWND_KAFEL_ROZMIAR(MAINWND_DZIAL_KOLUMNA_3_X, MAINWND_DZIAL_WIERSZ_1_Y, MAINWND_KAFEL_DZIALU_SZEROKOSC, MAINWND_KAFEL_DZIALU_WYSOKOSC, TEKST_DZIAL_NARZEDZIA,  UI_IKONA_DZIAL_NARZEDZIA),
    MAINWND_KAFEL_ROZMIAR(MAINWND_DZIAL_DOLNY_1_X, MAINWND_DZIAL_WIERSZ_2_Y, MAINWND_KAFEL_DZIALU_SZEROKOSC, MAINWND_KAFEL_DZIALU_WYSOKOSC, TEKST_DZIAL_USTAWIENIA, UI_IKONA_USTAWIENIA),
    MAINWND_KAFEL_ROZMIAR(MAINWND_DZIAL_DOLNY_2_X, MAINWND_DZIAL_WIERSZ_2_Y, MAINWND_KAFEL_DZIALU_SZEROKOSC, MAINWND_KAFEL_DZIALU_WYSOKOSC, TEKST_DZIAL_PLIKI,       UI_IKONA_DZIAL_PLIKI),
};

static const MAINWND_POZYCJA_DZIALU_t pozycje_pomiar[] =
{
    { TEKST_MENU_POJEDYNCZY, BTN_SINGLE, UI_IKONA_POJEDYNCZY },
    { TEKST_MENU_WYKRES_SWR, BTN_SWEEP, UI_IKONA_WYKRES_SWR },
    { TEKST_MENU_WIELE_PASM, BTN_MULTISWR, UI_IKONA_WIELE_PASM },
    { TEKST_MENU_STROJENIE, BTN_TUNESWR, UI_IKONA_STROJENIE },
    { TEKST_MENU_LC, BTN_LC, UI_IKONA_LC },
};

static const MAINWND_POZYCJA_DZIALU_t pozycje_analiza[] =
{
    { TEKST_MENU_S21, BTN_TRACKER, UI_IKONA_S21 },
    { TEKST_MENU_SZUKAJ_F, BTN_FIND, UI_IKONA_SZUKAJ_F },
    { TEKST_MENU_TDR, BTN_TDR, UI_IKONA_TDR },
    { TEKST_MENU_KWARC, BTN_QUARTZ, UI_IKONA_KWARC },
};

static const MAINWND_POZYCJA_DZIALU_t pozycje_narzedzia[] =
{
    { TEKST_MENU_GENERATOR, BTN_RFGEN, UI_IKONA_GENERATOR },
    { TEKST_MENU_CYFROWE, BTN_WSPR, UI_IKONA_CYFROWE },
    { TEKST_MENU_DSP_AUDIO, MAINWND_AKCJA_NARZEDZIA_DSP, UI_IKONA_DSP },
    { TEKST_MENU_PROJEKTOWANIE_ANTEN, MAINWND_AKCJA_NARZEDZIA_ANTENY, UI_IKONA_PROJEKTOWANIE_ANTEN },
};

static const MAINWND_POZYCJA_DZIALU_t pozycje_ustawienia[] =
{
    /*
     * Ustawienia są celowo płaskie. Czas, znak użytkownika i poziom funkcji
     * nie są „profilem urządzenia”, tylko zwykłymi ustawieniami użytkownika.
     * Druga strona zawiera funkcje rzadsze i techniczne.
     */
    { TEKST_WYGLAD_DZWIEK, MAINWND_AKCJA_UST_WYGLAD, UI_IKONA_USTAWIENIA },
    { TEKST_JEZYK, MAINWND_AKCJA_UST_JEZYK, UI_IKONA_POMOC },
    { TEKST_POZIOM_FUNKCJI, MAINWND_AKCJA_UST_POZIOM_FUNKCJI, UI_IKONA_METODY },
    { TEKST_DATA_CZAS, MAINWND_AKCJA_UST_DATA_CZAS, UI_IKONA_DATA_CZAS },
    { TEKST_AKUMULATOR, MAINWND_AKCJA_KAL_AKUMULATOR, UI_IKONA_AKUMULATOR },
    { TEKST_UZYTKOWNIK, MAINWND_AKCJA_UST_UZYTKOWNIK, UI_IKONA_UZYTKOWNIK },
    { TEKST_DZIAL_KALIBRACJA, MAINWND_AKCJA_KAL_CENTRUM, UI_IKONA_DZIAL_KALIBRACJA },

    { TEKST_MENU_LACZNOSC_VNA, MAINWND_AKCJA_UST_LACZNOSC, UI_IKONA_LACZNOSC_VNA },
    { TEKST_OBROC_EKRAN, MAINWND_AKCJA_UST_OBROT, UI_IKONA_WYGLAD_OBSLUGA },
    { TEKST_DIAGNOSTYKA, MAINWND_AKCJA_UST_DIAGNOSTYKA, UI_IKONA_SERWIS },
    { TEKST_KONFIGURACJA_ZAAWANSOWANA, MAINWND_AKCJA_UST_KONFIGURACJA, UI_IKONA_USTAWIENIA },
    { TEKST_O_PROGRAMIE, MAINWND_AKCJA_UST_O_PROGRAMIE, UI_IKONA_POMOC },
};

static const MAINWND_POZYCJA_DZIALU_t pozycje_pliki[] =
{
    { TEKST_MENU_ZRZUTY, BTN_SNAPSHOT, UI_IKONA_ZRZUTY },
    { TEKST_MENU_MENEDZER_PLIKOW, MAINWND_AKCJA_PLIKI_MENEDZER, UI_IKONA_MENEDZER },
    { TEKST_MENU_USB, BTN_USB, UI_IKONA_USB },
#if PROJEKT_BUILD_DOKUMENTACYJNY_REALNY
    { TEKST_MENU_GENERATOR_INSTRUKCJI, MAINWND_AKCJA_PLIKI_DOKUMENTACJA, UI_IKONA_RAPORT },
#endif
};

static const MAINWND_POZYCJA_DZIALU_t *MAINWND_PozycjeDzialu(MAINWND_DZIAL_t dzial,
                                                              uint16_t *liczba)
{
    if (liczba == 0)
        return 0;

    switch (dzial)
    {
    case MAINWND_DZIAL_POMIAR:
        *liczba = (uint16_t)(sizeof(pozycje_pomiar) / sizeof(pozycje_pomiar[0]));
        return pozycje_pomiar;
    case MAINWND_DZIAL_ANALIZA:
        *liczba = TRYB_CzyZaawansowany()
            ? (uint16_t)(sizeof(pozycje_analiza) / sizeof(pozycje_analiza[0]))
            : (uint16_t)(sizeof(pozycje_analiza) / sizeof(pozycje_analiza[0]) - 1U);
        return pozycje_analiza;
    case MAINWND_DZIAL_NARZEDZIA:
        *liczba = (uint16_t)(sizeof(pozycje_narzedzia) / sizeof(pozycje_narzedzia[0]));
        return pozycje_narzedzia;
    case MAINWND_DZIAL_USTAWIENIA:
        *liczba = (uint16_t)(sizeof(pozycje_ustawienia) / sizeof(pozycje_ustawienia[0]));
        return pozycje_ustawienia;
    case MAINWND_DZIAL_PLIKI:
        *liczba = (uint16_t)(sizeof(pozycje_pliki) / sizeof(pozycje_pliki[0]));
        return pozycje_pliki;
    default:
        *liczba = 0U;
        return 0;
    }
}

static int MAINWND_ZnajdzDzial(LCDPoint punkt)
{
    int i;
    for (i = 0; i < MAINWND_DZIAL_LICZBA; ++i)
    {
        if (UI_CzyPunktWObszarze(punkt, &kafle_dzialow[i].obszar))
            return i;
    }
    return -1;
}

static bool MAINWND_CzyKalibracjaGotowa(void)
{
    if (OSL_IsSelectedValid() != 0)
    {
        KAL_META_DANE_t meta;
        KAL_META_OCENA_t ocena;
        const int32_t profil = OSL_GetSelected();

        memset(&meta, 0, sizeof(meta));
        memset(&ocena, 0, sizeof(ocena));

        /* V2.1 wymaga metadanych zgodnych z aktualnym DSP. Profil bez
         * historii mógł powstać z innym oknem FFT lub planem generatora,
         * dlatego po aktualizacji bezpiecznie wymagamy ponownej OSL. */
        if (!KAL_META_Pobierz(KAL_META_OSL, profil, &meta))
            return false;

        KAL_META_Ocen(KAL_META_OSL, profil, &meta, &ocena);
        return (ocena.uwagi & (KAL_META_UWAGA_KONFIGURACJA |
                               KAL_META_UWAGA_BRAK_PLIKU |
                               KAL_META_UWAGA_PLIK_ZMIENIONY |
                               KAL_META_UWAGA_HW_ZMIENIONE)) == 0U;
    }

    return OSL70_IsValid();
}

/*
 * Zwraca true tylko dla kafli, które rzeczywiście reprezentują stan urządzenia.
 * Sam wybór/fokus kafla nie jest stanem i nie powinien zapalać „lampki hi-fi”.
 */
static bool MAINWND_PobierzStanPozycji(int16_t akcja, bool *wlaczona)
{
    if (wlaczona == 0)
        return false;

    switch (akcja)
    {
    case MAINWND_AKCJA_KAL_CENTRUM:
    case MAINWND_AKCJA_KAL_OSL:
    case MAINWND_AKCJA_KAL_STAN:
        *wlaczona = MAINWND_CzyKalibracjaGotowa();
        return true;
    case BTN_RFGEN:
        *wlaczona = GET_LED_STANU_RF() != 0U;
        return true;
    case BTN_USB:
        *wlaczona = USBD_Device.dev_state == USBD_STATE_CONFIGURED;
        return true;
    default:
        *wlaczona = false;
        return false;
    }
}

/*
 * Dolna podpowiedz jest chwilowo wylaczona: zabierala 44 px pod kaflami i to
 * ona tworzyla pusty pas na dole ekranu startowego. Funkcja zostaje razem
 * z przetlumaczonymi opisami dzialow, bo przywrocenie jej to jedno wywolanie
 * w RysujMenuDzialow2026 - trzeba wtedy podniesc kafle o okolo 40 px.
 */
static void __attribute__((unused))
MAINWND_RysujPodpowiedzGlownego(int wybrany_dzial, bool fokus_widoczny)
{
    /*
     * Dolny opis był zbędny i zabierał miejsce. Od wariantu HQ9 ekran główny
     * kończy się na kaflach, a cała dolna część pozostaje czystym tłem.
     */
    (void)wybrany_dzial;
    (void)fokus_widoczny;
}

static void MAINWND_RysujSciezkeOzdoby(uint16_t x, uint16_t y,
                                        bool prawa, bool dol,
                                        const int8_t punkty[][2], uint8_t liczba,
                                        LCDColor kolor)
{
    const int dx = prawa ? -1 : 1;
    const int dy = dol ? -1 : 1;
    uint8_t i;

    if (liczba < 2U)
        return;

    for (i = 1U; i < liczba; ++i)
    {
        LCD_Line(
            LCD_MakePoint((uint16_t)((int)x + dx * punkty[i - 1U][0]),
                          (uint16_t)((int)y + dy * punkty[i - 1U][1])),
            LCD_MakePoint((uint16_t)((int)x + dx * punkty[i][0]),
                          (uint16_t)((int)y + dy * punkty[i][1])),
            kolor);
    }
}

static void MAINWND_RysujRamkeRetroNaroznik(uint16_t x, uint16_t y, bool prawa, bool dol)
{
    const LCDColor zloto = UI_KolorRamki(UI_STYL_NORMALNY);
    const LCDColor jasne_zloto = UI_KolorRamki(UI_STYL_AKCENT);
    const LCDColor cien = UI_KolorRamki(UI_STYL_NIEAKTYWNY);
    const int dx = prawa ? -1 : 1;
    const int dy = dol ? -1 : 1;
    const int ax = (int)x;
    const int ay = (int)y;

    /*
     * Wzór narożnika został uproszczony z modelowej grafiki do natywnych
     * prymitywów LCD. Dwie woluty, listwa i nit tworzą czytelny ornament,
     * ale nie zabierają miejsca kaflom i nie wymagają dużej bitmapy ramy.
     */
    static const int8_t woluta_duza[][2] =
    {
        { 1, 12 }, { 4, 9 }, { 8, 8 }, { 12, 9 }, { 14, 12 },
        { 13, 15 }, { 10, 17 }, { 7, 16 }, { 6, 13 }, { 8, 11 },
        { 11, 12 }, { 11, 14 }
    };
    static const int8_t woluta_mala[][2] =
    {
        { 7, 4 }, { 10, 2 }, { 14, 2 }, { 17, 4 }, { 17, 7 },
        { 15, 9 }, { 12, 8 }, { 11, 6 }, { 13, 5 }, { 15, 6 }
    };
    static const int8_t lisc[][2] =
    {
        { 15, 10 }, { 20, 8 }, { 24, 10 }, { 20, 12 }, { 15, 10 }
    };

    /* Podwójna listwa narożna. */
    if (prawa)
    {
        LCD_HLine(LCD_MakePoint((uint16_t)(ax - 25), (uint16_t)ay), 26U, jasne_zloto);
        LCD_HLine(LCD_MakePoint((uint16_t)(ax - 22), (uint16_t)(ay + 2 * dy)), 23U, cien);
    }
    else
    {
        LCD_HLine(LCD_MakePoint((uint16_t)ax, (uint16_t)ay), 26U, jasne_zloto);
        LCD_HLine(LCD_MakePoint((uint16_t)(ax + 2), (uint16_t)(ay + 2 * dy)), 23U, cien);
    }

    if (dol)
    {
        LCD_VLine(LCD_MakePoint((uint16_t)ax, (uint16_t)(ay - 25)), 26U, jasne_zloto);
        LCD_VLine(LCD_MakePoint((uint16_t)(ax + 2 * dx), (uint16_t)(ay - 22)), 23U, cien);
    }
    else
    {
        LCD_VLine(LCD_MakePoint((uint16_t)ax, (uint16_t)ay), 26U, jasne_zloto);
        LCD_VLine(LCD_MakePoint((uint16_t)(ax + 2 * dx), (uint16_t)(ay + 2)), 23U, cien);
    }

    /* Nit i mała rozetka w samym zbiegu listew. */
    LCD_FillCircle(LCD_MakePoint((uint16_t)(ax + 4 * dx),
                                 (uint16_t)(ay + 4 * dy)), 2U, jasne_zloto);
    LCD_Circle(LCD_MakePoint((uint16_t)(ax + 4 * dx),
                             (uint16_t)(ay + 4 * dy)), 3U, cien);

    /* Dwie zwinięte woluty i liść — najważniejszy motyw modelowej ramy. */
    MAINWND_RysujSciezkeOzdoby(x, y, prawa, dol, woluta_duza,
                               (uint8_t)(sizeof(woluta_duza) / sizeof(woluta_duza[0])), zloto);
    MAINWND_RysujSciezkeOzdoby(x, y, prawa, dol, woluta_mala,
                               (uint8_t)(sizeof(woluta_mala) / sizeof(woluta_mala[0])), jasne_zloto);
    MAINWND_RysujSciezkeOzdoby(x, y, prawa, dol, lisc,
                               (uint8_t)(sizeof(lisc) / sizeof(lisc[0])), cien);

    /* Drobne zakończenia nadają ornamentowi wygląd grawerowanego mosiądzu. */
    LCD_FillCircle(LCD_MakePoint((uint16_t)(ax + 24 * dx),
                                 (uint16_t)(ay + 10 * dy)), 1U, zloto);
    LCD_FillCircle(LCD_MakePoint((uint16_t)(ax + 10 * dx),
                                 (uint16_t)(ay + 18 * dy)), 1U, jasne_zloto);
}

static void MAINWND_RysujRamkeRetroPelna(bool ekran_glowny)
{
    const LCDColor zloto = UI_KolorRamki(UI_STYL_NORMALNY);
    const LCDColor jasne_zloto = UI_KolorRamki(UI_STYL_AKCENT);
    const LCDColor cien = UI_KolorRamki(UI_STYL_NIEAKTYWNY);
    const uint16_t wew_x0 = 5U;
    const uint16_t wew_y0 = 39U;
    const uint16_t wew_x1 = 474U;
    const uint16_t wew_y1 = 266U;

    (void)ekran_glowny;

    if (CFG_GetParam(CFG_PARAM_ZESTAW_IKON) != 0U)
        return;

    /* Trzystopniowa rama zewnętrzna: światło, mosiądz i cień. */
    LCD_Rectangle(LCD_MakePoint(0U, 0U), LCD_MakePoint(479U, 271U), jasne_zloto);
    LCD_Rectangle(LCD_MakePoint(1U, 1U), LCD_MakePoint(478U, 270U), zloto);
    LCD_Rectangle(LCD_MakePoint(2U, 2U), LCD_MakePoint(477U, 269U), cien);

    /* Główne pole robocze pod paskiem statusu. */
    LCD_Rectangle(LCD_MakePoint(wew_x0, wew_y0),
                  LCD_MakePoint(wew_x1, wew_y1), jasne_zloto);
    LCD_Rectangle(LCD_MakePoint((uint16_t)(wew_x0 + 1U), (uint16_t)(wew_y0 + 1U)),
                  LCD_MakePoint((uint16_t)(wew_x1 - 1U), (uint16_t)(wew_y1 - 1U)), zloto);
    LCD_Rectangle(LCD_MakePoint((uint16_t)(wew_x0 + 2U), (uint16_t)(wew_y0 + 2U)),
                  LCD_MakePoint((uint16_t)(wew_x1 - 2U), (uint16_t)(wew_y1 - 2U)), cien);

    /* Listwy poziome wizualnie łączą pasek statusu z główną ramą. */
    LCD_HLine(LCD_MakePoint(30U, 36U), 420U, jasne_zloto);
    LCD_HLine(LCD_MakePoint(30U, 37U), 420U, cien);
    LCD_HLine(LCD_MakePoint(30U, 268U), 420U, cien);
    LCD_HLine(LCD_MakePoint(30U, 269U), 420U, jasne_zloto);

    /* Cztery duże woluty, jak w modelowej grafice przesłanej z ekranu. */
    MAINWND_RysujRamkeRetroNaroznik(7U, 41U, false, false);
    MAINWND_RysujRamkeRetroNaroznik(472U, 41U, true, false);
    MAINWND_RysujRamkeRetroNaroznik(7U, 265U, false, true);
    MAINWND_RysujRamkeRetroNaroznik(472U, 265U, true, true);
}

static void MAINWND_RysujRamkeStartowa(void)
{
    MAINWND_RysujRamkeRetroPelna(true);
}

static void MAINWND_RysujRamkePodmenu(void)
{
    MAINWND_RysujRamkeRetroPelna(false);
}

static void MAINWND_RysujKafelDzialu(int indeks, bool zaznaczony)
{
    const MAINWND_KAFEL_t *kafel;

    if (indeks < 0 || indeks >= MAINWND_DZIAL_LICZBA)
        return;

    kafel = &kafle_dzialow[indeks];
    UI_RysujKafelMenuGlownego(kafel->obszar.x, kafel->obszar.y,
                              kafel->obszar.szerokosc, kafel->obszar.wysokosc,
                              kafel->ikona, JEZYK_Tekst(kafel->tekst_id), zaznaczony);

}


static void MAINWND_WykonajAkcjeNowegoMenu(int16_t akcja)
{
    if (akcja >= 0 && akcja < BUTTON_COUNT)
    {
        UruchomPozycjeMenuGlownego(akcja);
        return;
    }

    switch (akcja)
    {
    case MAINWND_AKCJA_NARZEDZIA_DSP:
        AudioDSP_Proc();
        break;
    case MAINWND_AKCJA_NARZEDZIA_ANTENY:
        ANTENY_Otworz();
        break;
    case MAINWND_AKCJA_KAL_CENTRUM:
        CENTRUM_KALIBRACJI_Otworz();
        break;
    case MAINWND_AKCJA_KAL_OSL:
        CENTRUM_KALIBRACJI_OtworzOSL();
        break;
    case MAINWND_AKCJA_KAL_HW:
        CENTRUM_KALIBRACJI_OtworzHW();
        break;
    case MAINWND_AKCJA_KAL_LC:
        CENTRUM_KALIBRACJI_OtworzLC();
        break;
    case MAINWND_AKCJA_KAL_S21:
        CENTRUM_KALIBRACJI_OtworzS21();
        break;
    case MAINWND_AKCJA_KAL_TDR_VF:
        CENTRUM_KALIBRACJI_OtworzTDRVf();
        break;
    case MAINWND_AKCJA_KAL_AKUMULATOR:
        MenuAccu();
        break;
    case MAINWND_AKCJA_KAL_STAN:
        CENTRUM_KALIBRACJI_OtworzStan();
        break;
    case MAINWND_AKCJA_KAL_WERYFIKACJA:
        CENTRUM_KALIBRACJI_OtworzWeryfikacje();
        break;
    case MAINWND_AKCJA_KAL_WZORCE_DC:
        CENTRUM_KALIBRACJI_UstawDokladneWzorceDC();
        break;
    case MAINWND_AKCJA_KAL_METROLOGIA:
        DIAGNOSTYKA_OtworzStanMetrologiczny();
        break;

    case MAINWND_AKCJA_UST_WYGLAD:
        MenuUstawieniaWyglad();
        break;
    case MAINWND_AKCJA_UST_STYL:
        MenuIkony();
        break;
    case MAINWND_AKCJA_UST_JEZYK:
        MenuJezyk();
        break;
    case MAINWND_AKCJA_UST_POZIOM_FUNKCJI:
        MenuPoziomFunkcji();
        break;
    case MAINWND_AKCJA_UST_DATA_CZAS:
        MenuRTC();
        break;
    case MAINWND_AKCJA_UST_UZYTKOWNIK:
        PROFIL_UZYTKOWNIKA_Otworz();
        break;
    case MAINWND_AKCJA_UST_LACZNOSC:
        LACZNOSC_Otworz();
        break;
    case MAINWND_AKCJA_UST_OBROT:
        Rotate();
        break;
    case MAINWND_AKCJA_UST_DIAGNOSTYKA:
        DIAGNOSTYKA_Otworz();
        break;
    case MAINWND_AKCJA_UST_KONFIGURACJA:
        CFG_ParamWnd();
        break;
    case MAINWND_AKCJA_UST_O_PROGRAMIE:
        MenuOProgramie();
        break;
    case MAINWND_AKCJA_PLIKI_MENEDZER:
        PLIKI_Otworz();
        break;
#if PROJEKT_BUILD_DOKUMENTACYJNY_REALNY
    case MAINWND_AKCJA_PLIKI_DOKUMENTACJA:
        DOKUMENTACJA_REALNA_Uruchom();
        break;
#endif
    default:
        break;
    }
}

/* Podmenu działów korzystają ze wspólnej, dotykowej siatki 3 x 2 z ui_wspolny. */
static int16_t MAINWND_PodkafelPoDotyku(LCDPoint punkt, uint16_t liczba)
{
    return UI_KafelKompaktowyPoDotyku(punkt, liczba);
}

static void RysujMenuDzialow2026(int wybrany_dzial)
{
    int i;

    if (wybrany_dzial < 0 || wybrany_dzial >= MAINWND_DZIAL_LICZBA)
        wybrany_dzial = MAINWND_DZIAL_POMIAR;

    UI_WyczyscEkran();
    for (i = 0; i < MAINWND_DZIAL_LICZBA; ++i)
        MAINWND_RysujKafelDzialu(i, i == wybrany_dzial);
    RysujNaglowekGlownego();
    MAINWND_RysujRamkeStartowa();
}

static uint16_t MAINWND_PierwszyIndeksStrony(uint16_t zaznaczony)
{
    return (uint16_t)((zaznaczony / UI_SIATKA_KOMPAKT_NA_STRONE) *
                      UI_SIATKA_KOMPAKT_NA_STRONE);
}

static uint16_t MAINWND_LiczbaStron(uint16_t liczba)
{
    if (liczba == 0U)
        return 1U;
    return (uint16_t)((liczba + UI_SIATKA_KOMPAKT_NA_STRONE - 1U) /
                      UI_SIATKA_KOMPAKT_NA_STRONE);
}

static void MAINWND_RysujNawigacjeStron(uint16_t liczba, uint16_t zaznaczony)
{
    const uint16_t liczba_stron = MAINWND_LiczbaStron(liczba);
    const uint16_t strona = (uint16_t)(zaznaczony / UI_SIATKA_KOMPAKT_NA_STRONE);
    const UI_PROSTOKAT_t poprzednia = UI_ObszarPrzyciskuDolnego(1U);
    const UI_PROSTOKAT_t informacja = UI_ObszarPrzyciskuDolnego(2U);
    const UI_PROSTOKAT_t nastepna = UI_ObszarPrzyciskuDolnego(3U);
    char tekst[12];

    if (liczba_stron <= 1U)
        return;

    UI_RysujPrzycisk(poprzednia.x, poprzednia.y, poprzednia.szerokosc, poprzednia.wysokosc,
                     "<", strona > 0U ? UI_STYL_NORMALNY : UI_STYL_NIEAKTYWNY, FONT_FRANBIG);
    snprintf(tekst, sizeof(tekst), "%u/%u", (unsigned)(strona + 1U), (unsigned)liczba_stron);
    UI_RysujPrzycisk(informacja.x, informacja.y, informacja.szerokosc, informacja.wysokosc,
                     tekst, UI_STYL_NORMALNY, FONT_FRAN);
    UI_RysujPrzycisk(nastepna.x, nastepna.y, nastepna.szerokosc, nastepna.wysokosc,
                     ">", strona + 1U < liczba_stron ? UI_STYL_NORMALNY : UI_STYL_NIEAKTYWNY,
                     FONT_FRANBIG);
}

static void RysujDzial2026(MAINWND_DZIAL_t dzial, uint16_t zaznaczony, bool fokus_widoczny)
{
    const MAINWND_POZYCJA_DZIALU_t *pozycje;
    uint16_t liczba = 0U;
    uint16_t poczatek;
    uint16_t koniec;
    uint16_t i;

    pozycje = MAINWND_PozycjeDzialu(dzial, &liczba);
    if (liczba != 0U && zaznaczony >= liczba)
        zaznaczony = (uint16_t)(liczba - 1U);

    poczatek = MAINWND_PierwszyIndeksStrony(zaznaczony);
    koniec = (uint16_t)(poczatek + UI_SIATKA_KOMPAKT_NA_STRONE);
    if (koniec > liczba)
        koniec = liczba;

    UI_WyczyscEkran();
    MAINWND_RysujRamkePodmenu();
    if (pozycje != 0)
    {
        for (i = poczatek; i < koniec; ++i)
        {
            bool wlaczona = false;
            if (MAINWND_PobierzStanPozycji(pozycje[i].akcja, &wlaczona))
            {
                UI_RysujKafelKompaktowyZeStanem((uint16_t)(i - poczatek), pozycje[i].ikona,
                                                JEZYK_Tekst(pozycje[i].tekst_id),
                                                fokus_widoczny && i == zaznaczony, true,
                                                wlaczona);
            }
            else
            {
                UI_RysujKafelKompaktowy((uint16_t)(i - poczatek), pozycje[i].ikona,
                                         JEZYK_Tekst(pozycje[i].tekst_id),
                                         fokus_widoczny && i == zaznaczony, true);
            }
        }
    }
    RysujNaglowekGlownego();
    MAINWND_RysujNawigacjeStron(liczba, zaznaczony);
}

static void MAINWND_ObsluzDzial(MAINWND_DZIAL_t dzial)
{
    const MAINWND_POZYCJA_DZIALU_t *pozycje;
    uint16_t liczba = 0U;
    uint16_t zaznaczony = 0U;
    bool fokus_widoczny = false;
    int licznik_czasu = 98;

    g_mainwnd_widok = MAINWND_WIDOK_DZIAL;
    g_mainwnd_dzial = dzial;
    pozycje = MAINWND_PozycjeDzialu(dzial, &liczba);
    if (pozycje == 0 || liczba == 0U)
    {
        g_mainwnd_widok = MAINWND_WIDOK_DZIALY;
        return;
    }

    RysujDzial2026(dzial, zaznaczony, fokus_widoczny);
    WEJSCIA_WyczyscZdarzenia();

    while (TOUCH_IsPressed())
        Sleep(0);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;

        Voltage();
        if (++licznik_czasu > 100)
        {
            DateTime();
            licznik_czasu = 0;
        }

        if (TOUCH_Poll(&punkt))
        {
            int16_t indeks;

            if (UI_CzyDotknietoWstecz(punkt))
            {
                TRACK_Beep(1);
                TOUCH_CzekajNaPuszczenie(35U);
                break;
            }

            if (MAINWND_LiczbaStron(liczba) > 1U)
            {
                const uint16_t strona = (uint16_t)(zaznaczony / UI_SIATKA_KOMPAKT_NA_STRONE);
                const UI_PROSTOKAT_t poprzednia = UI_ObszarPrzyciskuDolnego(1U);
                const UI_PROSTOKAT_t nastepna = UI_ObszarPrzyciskuDolnego(3U);

                if (UI_CzyPunktWObszarze(punkt, &poprzednia) && strona > 0U)
                {
                    zaznaczony = (uint16_t)((strona - 1U) * UI_SIATKA_KOMPAKT_NA_STRONE);
                    fokus_widoczny = true;
                    TOUCH_CzekajNaPuszczenie(35U);
                    RysujDzial2026(dzial, zaznaczony, fokus_widoczny);
                    continue;
                }
                if (UI_CzyPunktWObszarze(punkt, &nastepna) &&
                    strona + 1U < MAINWND_LiczbaStron(liczba))
                {
                    zaznaczony = (uint16_t)((strona + 1U) * UI_SIATKA_KOMPAKT_NA_STRONE);
                    fokus_widoczny = true;
                    TOUCH_CzekajNaPuszczenie(35U);
                    RysujDzial2026(dzial, zaznaczony, fokus_widoczny);
                    continue;
                }
            }

            {
                const uint16_t poczatek = MAINWND_PierwszyIndeksStrony(zaznaczony);
                uint16_t na_stronie = (uint16_t)(liczba - poczatek);
                if (na_stronie > UI_SIATKA_KOMPAKT_NA_STRONE)
                    na_stronie = UI_SIATKA_KOMPAKT_NA_STRONE;
                indeks = MAINWND_PodkafelPoDotyku(punkt, na_stronie);
                if (indeks >= 0)
                    indeks = (int16_t)(poczatek + (uint16_t)indeks);
            }
            if (indeks >= 0 && (uint16_t)indeks < liczba)
            {
                zaznaczony = (uint16_t)indeks;
                TRACK_Beep(1);
                TOUCH_CzekajNaPuszczenie(35U);
                MAINWND_WykonajAkcjeNowegoMenu(pozycje[zaznaczony].akcja);
                g_mainwnd_widok = MAINWND_WIDOK_DZIAL;
                g_mainwnd_dzial = dzial;
                WEJSCIA_WyczyscZdarzenia();
                RysujDzial2026(dzial, zaznaczony, fokus_widoczny);
                PROTOCOL_Reset();
            }
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;

        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
            zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            if (!fokus_widoczny)
            {
                zaznaczony = zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? 0U : (uint16_t)(liczba - 1U);
                fokus_widoczny = true;
            }
            else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                zaznaczony = (uint16_t)((zaznaczony + 1U) % liczba);
            }
            else
            {
                zaznaczony = zaznaczony == 0U ? (uint16_t)(liczba - 1U) : (uint16_t)(zaznaczony - 1U);
            }
            RysujDzial2026(dzial, zaznaczony, fokus_widoczny);
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK && fokus_widoczny && zaznaczony < liczba)
        {
            MAINWND_WykonajAkcjeNowegoMenu(pozycje[zaznaczony].akcja);
            g_mainwnd_widok = MAINWND_WIDOK_DZIAL;
            g_mainwnd_dzial = dzial;
            WEJSCIA_WyczyscZdarzenia();
            RysujDzial2026(dzial, zaznaczony, fokus_widoczny);
            PROTOCOL_Reset();
        }

        PROTOCOL_Handler();
        Sleep(10);
    }

    WEJSCIA_WyczyscZdarzenia();
    g_mainwnd_widok = MAINWND_WIDOK_DZIALY;
}

void MAINWND_RysujMenuDemo(void)
{
    UI_STATUS_t status = {0};
    char nazwa_lokalna[32] = {0};

    /*
     * Renderer dokumentacyjny nie korzysta z bieżącej baterii, czasu ani RF.
     * Dzięki temu zrzuty są powtarzalne i nie sugerują rzeczywistego pomiaru.
     */
    g_mainwnd_widok = MAINWND_WIDOK_DZIALY;
    RysujMenuDzialow2026(MAINWND_DZIAL_NARZEDZIA);
    JEZYK_FormatujNazweProjektu(nazwa_lokalna, sizeof(nazwa_lokalna));
    status.nazwa = nazwa_lokalna;
    status.wersja = PROJEKT_WERSJA_KROTKA;
    status.znak = "";
    status.data = 20260819U;
    status.czas = 1200U;
    status.rtc_obecny = true;
    status.karta_sd_obecna = true;
    status.rf_aktywne = false;
    status.bateria_obecna = true;
    status.napiecie_baterii = 4.02f;
    status.procent_baterii = 76;
    UI_RysujPasekGorny(nazwa_lokalna, false, false, &status);
}

static bool MAINWND_WypelnijStatusUI(UI_STATUS_t *status)
{
    /*
     * Jedno źródło prawdy dla paska statusu. Bufory są statyczne, ponieważ
     * UI korzysta ze wskaźników tylko podczas bieżącego rysowania. Moduły
     * podrzędne nie muszą odczytywać RTC, ADC ani GPIO samodzielnie.
     */
    static char znak[UZYTKOWNIK_ZNAK_MAX + 1];
    static char nazwa_lokalna[32];

    if (status == 0)
        return false;

    memset(status, 0, sizeof(*status));
    memset(znak, 0, sizeof(znak));
    memset(nazwa_lokalna, 0, sizeof(nazwa_lokalna));
    UZYTKOWNIK_PobierzZnak(znak, sizeof(znak));
    JEZYK_FormatujNazweProjektu(nazwa_lokalna, sizeof(nazwa_lokalna));

    status->nazwa = nazwa_lokalna;
    status->wersja = PROJEKT_WERSJA_KROTKA;
    status->znak = znak;
    status->data = date;
    status->czas = time;
    status->rtc_obecny = RTCpresent != 0U;
    status->karta_sd_obecna = CFG_CzyKartaSDDostepna();
    status->rf_aktywne = GET_LED_STANU_RF() != 0U;
    status->bateria_obecna = BateriaObecna;
    status->napiecie_baterii = VoltFloat;
    status->procent_baterii = (int)percent;
    return true;
}

static void RysujNaglowekGlownego(void)
{
    UI_STATUS_t status = {0};

    if (!MAINWND_WypelnijStatusUI(&status))
        return;

    if (g_mainwnd_widok == MAINWND_WIDOK_DZIAL)
    {
        UI_RysujPasekGorny(JEZYK_Tekst(kafle_dzialow[g_mainwnd_dzial].tekst_id),
                           true, false, &status);
    }
    else
    {
        UI_RysujPasekGorny(status.nazwa, false, false, &status);
    }
}

// ================================================================================================
// Główne okno programu. Od test24 jedynym poziomem 0 jest launcher sześciu działów.
// Stare 15-kafelkowe menu nie jest już alternatywną ścieżką nawigacji.
void MainWnd(void)
{
    int counter11;
    int wybrany_dzial = MAINWND_DZIAL_POMIAR;
#ifdef _DEBUG_UART
    DBGUART_Init();
    DBG_Str("Debug Start for Antenna Analyzer!!! \\r\\n");
#endif

    SetColours();
    BSP_LCD_SelectLayer(0);
    SetColours();
    GetBS(CFG_GetParam(CFG_PARAM_PAN_F1) / 1000);
    BSP_LCD_SelectLayer(1);
    LCD_ShowActiveLayerOnly();

    g_mainwnd_widok = MAINWND_WIDOK_DZIALY;
    RysujMenuDzialow2026(wybrany_dzial);

    while (TOUCH_IsPressed())
        ;

    TEXTBOX_InitContext(&main_ctx);
    PROTOCOL_Reset();
    InitVoltage();
    UI_UstawDostawceStatusu(MAINWND_WypelnijStatusUI);
    RysujNaglowekGlownego();
    counter11 = 98;

    for (;;)
    {
        LCDPoint pt;
        WEJSCIE_ZDARZENIE_t zdarzenie;

        Voltage();
        if (++counter11 > 100)
        {
            DateTime();
            counter11 = 0;
        }
        Sleep(10);

        if (TOUCH_Poll(&pt))
        {
            const int dzial = MAINWND_ZnajdzDzial(pt);
            if (dzial >= 0)
            {
                TRACK_Beep(1);
                TOUCH_CzekajNaPuszczenie(35U);

                wybrany_dzial = dzial;
                MAINWND_ObsluzDzial((MAINWND_DZIAL_t)dzial);
                RysujMenuDzialow2026(wybrany_dzial);
                PROTOCOL_Reset();
                Voltage();
            }
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
        {
            /*
             * Poziom główny nie ma nadrzędnego ekranu. Wstecz nie usuwa też
             * podświetlenia, bo wybrany kafel jest stałym elementem nowej
             * szaty graficznej, a nie chwilowym „fokusem enkodera”.
             */
            WEJSCIA_WyczyscZdarzenia();
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
                 zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            /* Zawsze istnieje dokładnie jeden wybrany kafel. Najpierw gasimy
             * poprzedni, dopiero potem zapalamy następny. */
            MAINWND_RysujKafelDzialu(wybrany_dzial, false);

            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
                wybrany_dzial = (wybrany_dzial + 1) % MAINWND_DZIAL_LICZBA;
            else
                wybrany_dzial = (wybrany_dzial + MAINWND_DZIAL_LICZBA - 1) % MAINWND_DZIAL_LICZBA;

            MAINWND_RysujKafelDzialu(wybrany_dzial, true);
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            MAINWND_ObsluzDzial((MAINWND_DZIAL_t)wybrany_dzial);
            RysujMenuDzialow2026(wybrany_dzial);
            PROTOCOL_Reset();
            Voltage();
        }

        PROTOCOL_Handler();
    }
}

#define XXa 2

void SetColours(void)
{
    if (ColourSelection == 1)
    { // Daylight
        BackGrColor = LCD_WHITE;
        CurvColor = LCD_COLOR_DARKGREEN; // dark blue
        TextColor = LCD_BLACK;
        Color1 = LCD_COLOR_DARKGREEN; //LCD_COLOR_DARKBLUE;
        Color2 = LCD_COLOR_DARKGRAY;
        Color3 = LCD_YELLOW;                 // -ham bands area at daylight
        Color4 = LCD_MakeRGB(128, 255, 128); //Light green
    }
    else
    { // Inhouse
        BackGrColor = LCD_BLACK;
        CurvColor = LCD_COLOR_LIGHTGREEN;
        TextColor = LCD_WHITE;
        Color1 = LCD_COLOR_LIGHTBLUE;
        Color2 = LCD_COLOR_GRAY;
        Color3 = LCD_MakeRGB(0, 0, 64); // very dark blue -ham bands area
        Color4 = LCD_MakeRGB(0, 64, 0); // very dark green
    }
    CFG_SetParam(CFG_PARAM_Daylight, ColourSelection);
    CFG_Flush();
}

static void PrzelaczTloWykresow(void)
{
    /* Jedna funkcja = jeden przełącznik. Kontrolka na kaflu pokazuje stan
     * włączenia jasnego tła; zgaszona oznacza tło ciemne. */
    ColourSelection = ColourSelection ? 0U : 1U;
    SetColours(); /* SetColours zapisuje ustawienie. */
}

static void PrzelaczGrubeLinie(void)
{
    FatLines = !FatLines;
    CFG_SetParam(CFG_PARAM_Fatlines, FatLines ? 1U : 0U);
    CFG_Flush();
}

static void BeepOn(void)
{
    if (CFG_GetParam(CFG_PARAM_BeepOn) == 0)
    {
        BeepOn1 = 1;
        CFG_SetParam(CFG_PARAM_BeepOn, 1);
    }
    else
    {
        BeepOn1 = 0;
        CFG_SetParam(CFG_PARAM_BeepOn, 0);
    }
    CFG_Flush();
}

static void Rotate(void)
{
    if (LCD_Get_Orientation() == 0)
    {
        LCD_Set_Orientation(1);
    }
    else
    {
        LCD_Set_Orientation(0);
    }
    CFG_Flush();
    rqExit1 = 1; // leave the menue
}


#define COL3 380

void InitVoltage(void)
{

    Volt_max_Display = CFG_GetParam(CFG_PARAM_Volt_max_Display);
    Volt_min_Display = CFG_GetParam(CFG_PARAM_Volt_min_Display);
    Volt_max_Factor = CFG_GetParam(CFG_PARAM_Volt_max_Factor);
    if (Volt_max_Factor == 0)
        Volt_max_Factor = 614.f; // default factor
    if (Volt_max_Factor < 100 || Volt_max_Factor > 2000)
        Volt_max_Factor = 614;
    if (Volt_max_Display != 0 && (Volt_max_Display < 2000 || Volt_max_Display > 10000))
        Volt_max_Display = 4000;
    if (Volt_min_Display < 1000 || (Volt_max_Display != 0 && Volt_min_Display >= Volt_max_Display))
        Volt_min_Display = 3100;
    BattVoltage = 0;
    CountMax = 100;
    cntr = 0;
    VoltFloat = 0.0f;
    percent = 0.0f;
    BateriaObecna = false;
    SetColours();
}

//KD8CEC : for Visible Voltage indicator from hidden by other screen
void DisplayVoltage()
{
    /* Pasek stanu jest wspolny dla czasu, SD, RF i baterii. */
    RysujNaglowekGlownego();
}

//KD8CEC : RESIZE FONT AND CHANGE POSITION
void Voltage(void)
{
    int VoltRaw;
    if (Volt_max_Display == 0)
    {
        BateriaObecna = false;
        return;
    }

    cntr++;
    VoltRaw = UB_ADC3_SINGLE_Read_MW(ADC_PF8);

    if (VoltRaw <= 0)
    {
        BateriaObecna = false;
        percent = 0.0f;
        DisplayVoltage();
        return;
    }
    BattVoltage += VoltRaw;
    if (cntr >= CountMax)
    {
        Batt = BattVoltage / CountMax;             // value for calibration
        VoltFloat = (float)Batt / Volt_max_Factor; //614.f  563.0f
        cntr = 0;
        CountMax = 3200,
        BattVoltage = 0;

        /*
         * Wejscie PF8/A3 mierzy napiecie akumulatora przez istniejacy tor pomiarowy. Gdy analizator jest
         * zasilany tylko z USB, wejscie moze wisiec na niskim napieciu.
         * W takim przypadku nie pokazujemy ujemnego procentu, tylko brak
         * wykrytego akumulatora.
         */
        {
            float dolna_granica = (float)Volt_min_Display / 2000.0f;
            float gorna_granica = (float)Volt_max_Display / 800.0f;
            if (dolna_granica < 1.0f) dolna_granica = 1.0f;
            if (gorna_granica < 5.5f) gorna_granica = 5.5f;
            if (gorna_granica > 12.5f) gorna_granica = 12.5f;
            BateriaObecna = (VoltFloat >= dolna_granica && VoltFloat <= gorna_granica);
        }

        if (BateriaObecna)
        {
            float p = 100.0f * ((1000.0f * VoltFloat - (float)Volt_min_Display) /
                                (float)(Volt_max_Display - Volt_min_Display));
            if (p < 0.0f) p = 0.0f;
            if (p > 100.0f) p = 100.0f;
            percent = p;
        }
        else
        {
            percent = 0.0f;
        }

        DisplayVoltage();
    }
}

static uint16_t AkuOdczytajSuroweADC(void)
{
    uint32_t suma = 0U;
    const uint32_t liczba_probek = 8U;
    uint32_t i;

    /*
     * Pojedyncze wywolanie Read_MW juz usrednia 256 konwersji ADC.
     * Dodatkowe osiem probek daje spokojny odczyt do kalibracji bez
     * uzalezniania wyniku od dlugiego filtru paska stanu.
     */
    for (i = 0U; i < liczba_probek; ++i)
        suma += UB_ADC3_SINGLE_Read_MW(ADC_PF8);

    return (uint16_t)(suma / liczba_probek);
}

static float AkuPrzeliczNapiecie(uint16_t surowe_adc)
{
    int skala = CFG_GetParam(CFG_PARAM_Volt_max_Factor);

    if (skala < 100 || skala > 2000)
        skala = 614;

    return (float)surowe_adc / (float)skala;
}

static void AkuZapiszUstawienia(void)
{
    CFG_SetParam(CFG_PARAM_Volt_max_Display, Volt_max_Display);
    CFG_SetParam(CFG_PARAM_Volt_min_Display, Volt_min_Display);
    CFG_SetParam(CFG_PARAM_Volt_max_Factor, Volt_max_Factor);
    CFG_Flush();
    InitVoltage();
}

static const char *AkuKomunikat;
static LCDColor AkuKomunikatKolor;
static char AkuTekstPelne[32];
static char AkuTekstPuste[32];

static void AkuUstawKomunikat(const char *tekst, LCDColor kolor)
{
    AkuKomunikat = tekst;
    AkuKomunikatKolor = kolor;
}

static void AkuRysujKomunikat(void)
{
    const LCDColor tlo = UI_KolorTlaEkranu();

    LCD_FillRect(LCD_MakePoint(10, 184), LCD_MakePoint(469, 201), tlo);
    if (AkuKomunikat != 0 && AkuKomunikat[0] != '\0')
        FONT_Write(FONT_FRAN, AkuKomunikatKolor, tlo, 14, 185, AkuKomunikat);
}

static void AkuKalibrujWgWoltomierza(void)
{
    uint16_t surowe;
    uint32_t aktualne_mv;
    uint32_t wzorzec_mv;
    uint32_t nowa_skala;

    while (TOUCH_IsPressed())
        Sleep(0);

    surowe = AkuOdczytajSuroweADC();
    if (surowe < 50U)
    {
        AkuUstawKomunikat(JEZYK_Tekst(TEKST_AKU_KALIBRACJA_BLAD), UI_KolorRamki(UI_STYL_OSTRZEZENIE));
        return;
    }

    aktualne_mv = (uint32_t)(AkuPrzeliczNapiecie(surowe) * 1000.0f + 0.5f);
    if (aktualne_mv < 1500U || aktualne_mv > 10000U)
        aktualne_mv = 4000U;

    wzorzec_mv = NumKeypad(aktualne_mv, 1500U, 9999U,
                           JEZYK_Tekst(TEKST_AKU_WOLTOMIERZ_MV));
    if (wzorzec_mv == 0U)
        return;

    /* V = ADC / skala, zatem skala = ADC / V. */
    nowa_skala = ((uint32_t)surowe * 1000U + wzorzec_mv / 2U) / wzorzec_mv;
    if (nowa_skala < 100U || nowa_skala > 2000U)
    {
        AkuUstawKomunikat(JEZYK_Tekst(TEKST_AKU_KALIBRACJA_BLAD), UI_KolorRamki(UI_STYL_OSTRZEZENIE));
        return;
    }

    Volt_max_Factor = (int)nowa_skala;
    CFG_SetParam(CFG_PARAM_Volt_max_Factor, (uint32_t)Volt_max_Factor);
    CFG_Flush();
    InitVoltage();
    (void)KAL_META_Zapisz(KAL_META_BATERIA, -1);
    AkuUstawKomunikat(JEZYK_Tekst(TEKST_AKU_KALIBRACJA_OK), UI_KolorRamki(UI_STYL_AKTYWNY));
}

static void AkuUstawPelne(void)
{
    uint32_t wartosc;

    while (TOUCH_IsPressed())
        Sleep(0);

    wartosc = NumKeypad((Volt_max_Display != 0) ? (uint32_t)Volt_max_Display : 4200U,
                        2000U, 9999U,
                        JEZYK_Tekst(TEKST_AKU_NAPIECIE_PELNE_MV));
    if (wartosc == 0U)
        return;

    if (wartosc <= (uint32_t)Volt_min_Display + 100U)
        return;

    Volt_max_Display = (int)wartosc;
    AkuZapiszUstawienia();
}

static void AkuUstawPuste(void)
{
    uint32_t wartosc;
    uint32_t maksimum;

    while (TOUCH_IsPressed())
        Sleep(0);

    maksimum = (Volt_max_Display > 2000) ? (uint32_t)Volt_max_Display - 100U : 9900U;
    wartosc = NumKeypad((uint32_t)Volt_min_Display, 1500U, maksimum,
                        JEZYK_Tekst(TEKST_AKU_NAPIECIE_PUSTE_MV));
    if (wartosc == 0U)
        return;

    Volt_min_Display = (int)wartosc;
    AkuZapiszUstawienia();
}

static void AkuPrzelaczWskaznik(void)
{
    while (TOUCH_IsPressed())
        Sleep(0);

    if (Volt_max_Display == 0)
    {
        Volt_max_Display = 4200;
        if (Volt_min_Display < 1500 || Volt_min_Display >= Volt_max_Display)
            Volt_min_Display = 3200;
    }
    else
    {
        Volt_max_Display = 0;
    }

    AkuZapiszUstawienia();
}

static int rqExit3;
static uint8_t AkuOdswiezEkran;

static void Exit3(void)
{
    rqExit3 = 1;
}

static void AkuKalibrujCb(void)
{
    AkuKalibrujWgWoltomierza();
    AkuOdswiezEkran = 1U;
}

static void AkuPelneCb(void)
{
    AkuUstawPelne();
    AkuOdswiezEkran = 1U;
}

static void AkuPusteCb(void)
{
    AkuUstawPuste();
    AkuOdswiezEkran = 1U;
}

static void AkuWskaznikCb(void)
{
    AkuPrzelaczWskaznik();
    AkuOdswiezEkran = 1U;
}

static TEXTBOX_t volt_menu[] = {
    /* Progi sa od razu widoczne na przyciskach. Dotkniecie wartosci otwiera jej edycje. */
    (TEXTBOX_t){.x0 = 10, .y0 = 134, .text = AkuTekstPelne,
                .font = FONT_FRANBIG, .width = 220, .height = 44, .center = 1, .border = 1,
                .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR, .cb = AkuPelneCb, .cbparam = 1,
                .next = (void *)&volt_menu[1]},
    (TEXTBOX_t){.x0 = 250, .y0 = 134, .text = AkuTekstPuste,
                .font = FONT_FRANBIG, .width = 220, .height = 44, .center = 1, .border = 1,
                .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR, .cb = AkuPusteCb, .cbparam = 1,
                .next = (void *)&volt_menu[2]},
    (TEXTBOX_t){.x0 = 0, .y0 = UI_DOLNY_PASEK_Y, .text = "Back", .tekst_id = TEXTBOX_TEKST(TEKST_WSTECZ),
                .rola = TEXTBOX_ROLA_WSTECZ, .font = FONT_FRAN,
                .width = UI_DOLNY_PRZYCISK_SZEROKOSC, .height = UI_DOLNY_PRZYCISK_WYSOKOSC,
                .center = 1, .border = 1, .fgcolor = M_FGCOLOR, .bgcolor = LCD_RED,
                .cb = (void (*)(void))Exit3, .cbparam = 1, .next = (void *)&volt_menu[3]},
    (TEXTBOX_t){.x0 = UI_DOLNY_PRZYCISK_SZEROKOSC + UI_DOLNY_PRZYCISK_ODSTEP,
                .y0 = UI_DOLNY_PASEK_Y, .text = "Kalibruj",
                .font = FONT_FRAN, .width = UI_DOLNY_PRZYCISK_SZEROKOSC,
                .height = UI_DOLNY_PRZYCISK_WYSOKOSC, .center = 1, .border = 1,
                .fgcolor = M_FGCOLOR, .bgcolor = LCD_YELLOW, .cb = AkuKalibrujCb, .cbparam = 1,
                .next = (void *)&volt_menu[4]},
    (TEXTBOX_t){.x0 = 2 * (UI_DOLNY_PRZYCISK_SZEROKOSC + UI_DOLNY_PRZYCISK_ODSTEP),
                .y0 = UI_DOLNY_PASEK_Y, .text = "Wskaźnik", .font = FONT_FRAN,
                .width = UI_DOLNY_PRZYCISK_SZEROKOSC, .height = UI_DOLNY_PRZYCISK_WYSOKOSC,
                .center = 1, .border = 1, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR,
                .cb = AkuWskaznikCb, .cbparam = 1},
};

static uint16_t AkuOstatnieADC = 0xFFFFU;
static int32_t AkuOstatnieMilliV = -1;

static void AkuRysujPolaStale(void)
{
    const LCDColor pole_tlo = UI_KolorTlaPola();
    const LCDColor etykieta = UI_KolorRamki(UI_STYL_AKCENT);
    char wartosc[16];

    UI_RysujPanel(10, 40, 300, 86, JEZYK_Tekst(TEKST_AKU_NAPIECIE), UI_STYL_NORMALNY);
    UI_RysujPanel(320, 40, 150, 86, JEZYK_Tekst(TEKST_AKU_DANE_ADC), UI_STYL_NORMALNY);

    FONT_Write(FONT_FRAN, etykieta, pole_tlo, 328, 64,
               JEZYK_Tekst(TEKST_AKU_ODCZYT));
    FONT_Write(FONT_FRAN, etykieta, pole_tlo, 328, 99,
               JEZYK_Tekst(TEKST_AKU_SKALA));
    snprintf(wartosc, sizeof(wartosc), "%d", Volt_max_Factor);
    FONT_Write_RightAlign(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_NORMALNY), pole_tlo,
                          382, 91, 462, wartosc);

    AkuOstatnieADC = 0xFFFFU;
    AkuOstatnieMilliV = -1;
}

static void AkuOdswiezPolaDynamiczne(uint8_t wymus)
{
    char liczba[24];
    char wartosc_adc[16];
    uint16_t surowe = AkuOdczytajSuroweADC();
    float napiecie = AkuPrzeliczNapiecie(surowe);
    int32_t napiecie_mv = (int32_t)(napiecie * 1000.0f + 0.5f);
    const LCDColor pole_tlo = UI_KolorTlaPola();

    /*
     * Na nagraniu z fizycznego LCD widać było miganie ekranu akumulatora.
     * Powodem nie był pomiar ADC, lecz ponowne czyszczenie dwóch całych paneli
     * co sekundę. Odświeżamy teraz wyłącznie prostokąty z liczbami i tylko gdy
     * ich wartość naprawdę się zmieniła.
     */
    if (wymus || napiecie_mv != AkuOstatnieMilliV)
    {
        int szerokosc;
        int szerokosc_v;
        int x;

        snprintf(liczba, sizeof(liczba), "%.3f", (double)napiecie);
        LCD_FillRect(LCD_MakePoint(18, 62), LCD_MakePoint(302, 118), pole_tlo);
        szerokosc = FONT_GetStrPixelWidth(FONT_BDIGITS, liczba);
        szerokosc_v = FONT_GetStrPixelWidth(FONT_FRANBIG, "V");
        x = 18 + (284 - szerokosc - szerokosc_v - 8) / 2;
        if (x < 20)
            x = 20;
        FONT_Write(FONT_BDIGITS, UI_KolorTekstu(UI_STYL_NORMALNY), pole_tlo,
                   (uint16_t)x, 67, liczba);
        FONT_Write(FONT_FRANBIG, UI_KolorRamki(UI_STYL_AKCENT), pole_tlo,
                   (uint16_t)(x + szerokosc + 8), 91, "V");
        AkuOstatnieMilliV = napiecie_mv;
    }

    if (wymus || surowe != AkuOstatnieADC)
    {
        snprintf(wartosc_adc, sizeof(wartosc_adc), "%u", (unsigned)surowe);
        LCD_FillRect(LCD_MakePoint(382, 59), LCD_MakePoint(462, 86), pole_tlo);
        FONT_Write_RightAlign(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_NORMALNY), pole_tlo,
                              382, 59, 462, wartosc_adc);
        AkuOstatnieADC = surowe;
    }
}

static void AkuRysujPola(void)
{
    AkuRysujPolaStale();
    AkuOdswiezPolaDynamiczne(1U);
}

static void AkuRysujPrzyciski(void)
{
    TEXTBOX_t *wskaznik = (TEXTBOX_t *)&volt_menu[4];

    if (Volt_max_Display != 0)
        snprintf(AkuTekstPelne, sizeof(AkuTekstPelne), "100%%: %.3f V", (float)Volt_max_Display / 1000.0f);
    else
        snprintf(AkuTekstPelne, sizeof(AkuTekstPelne), "100%%: --");

    snprintf(AkuTekstPuste, sizeof(AkuTekstPuste), "0%%: %.3f V", (float)Volt_min_Display / 1000.0f);

    wskaznik->text = (Volt_max_Display == 0) ? JEZYK_Tekst(TEKST_AKU_WSKAZNIK_WYLACZONY)
                                             : JEZYK_Tekst(TEKST_AKU_WSKAZNIK_WLACZONY);
    wskaznik->tekst_id = 0;
    wskaznik->bgcolor = (Volt_max_Display == 0) ? M_BGCOLOR : LCD_GREEN;

    TEXTBOX_DrawContext(&menu3_ctx);
}

static void MenuAccu(void)
{
    uint32_t licznik = 0U;

    while (TOUCH_IsPressed())
        Sleep(0);

    rqExit3 = 0;
    AkuOdswiezEkran = 0U;
    AkuKomunikat = 0;
    AkuKomunikatKolor = UI_KolorTekstu(UI_STYL_NORMALNY);
    Volt_max_Display = CFG_GetParam(CFG_PARAM_Volt_max_Display);
    Volt_min_Display = CFG_GetParam(CFG_PARAM_Volt_min_Display);
    Volt_max_Factor = CFG_GetParam(CFG_PARAM_Volt_max_Factor);
    if (Volt_max_Factor < 100 || Volt_max_Factor > 2000)
        Volt_max_Factor = 614;
    if (Volt_min_Display < 1500)
        Volt_min_Display = 3200;

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_AKUMULATOR));

    TEXTBOX_InitContext(&menu3_ctx);
    TEXTBOX_Append(&menu3_ctx, (TEXTBOX_t *)volt_menu);
    AkuRysujPrzyciski();
    AkuRysujPola();
    AkuRysujKomunikat();

    for (;;)
    {
        Sleep(10);
        licznik++;

        if (TEXTBOX_HitTest(&menu3_ctx))
        {
            if (rqExit3)
            {
                rqExit3 = 0;
                rqExitR = true;
                return;
            }
            AkuOdswiezEkran = 1U;
        }

        if (AkuOdswiezEkran)
        {
            UI_WyczyscEkran();
            UI_RysujNaglowek(JEZYK_Tekst(TEKST_AKUMULATOR));
            AkuRysujPrzyciski();
            AkuRysujPola();
            AkuRysujKomunikat();
            AkuOdswiezEkran = 0U;
            licznik = 0U;
        }
        else if (licznik >= 100U)
        {
            /* Odświeżamy same liczby, bez czyszczenia całych paneli. */
            AkuOdswiezPolaDynamiczne(0U);
            licznik = 0U;
        }
    }
}

void MAINWND_OtworzKalibracjeBaterii(void)
{
    MenuAccu();
    /* MenuAccu jest starszym ekranem i przy wyjściu ustawia globalną flagę
     * menu. Wywołanie z diagnostyki nie może przenosić tej flagi dalej. */
    rqExitR = false;
}



bool RTC_SprawdzObecnosc(void)
{
    RTCpresent = DS3231_IsPresent();
    return RTCpresent != 0;
}

typedef enum
{
    RTC_POLE_DZIEN = 0,
    RTC_POLE_MIESIAC,
    RTC_POLE_ROK,
    RTC_POLE_GODZINA,
    RTC_POLE_MINUTA,
    RTC_POLE_LICZBA
} RTC_POLE_t;

typedef enum
{
    RTC_AKCJA_WSTECZ = 1,
    RTC_AKCJA_MINUS,
    RTC_AKCJA_PLUS,
    RTC_AKCJA_ZAPISZ
} RTC_AKCJA_t;

static void RTC_PrzygotujAkcje(UI_AKCJA_t akcje[4])
{
    akcje[0] = (UI_AKCJA_t){RTC_AKCJA_WSTECZ, JEZYK_Tekst(TEKST_WSTECZ),
                             UI_STYL_POWROT, true, false};
    akcje[1] = (UI_AKCJA_t){RTC_AKCJA_MINUS, "-", UI_STYL_NORMALNY, true, false};
    akcje[2] = (UI_AKCJA_t){RTC_AKCJA_PLUS, "+", UI_STYL_NORMALNY, true, false};
    akcje[3] = (UI_AKCJA_t){RTC_AKCJA_ZAPISZ, JEZYK_Tekst(TEKST_ZAPISZ),
                             UI_STYL_AKTYWNY, true, false};
}

typedef struct
{
    uint16_t rok;
    uint8_t miesiac;
    uint8_t dzien;
    uint8_t godzina;
    uint8_t minuta;
} RTC_EDYCJA_t;

static void RTC_WycentrujTekst(uint32_t font, uint16_t x, uint16_t y, uint16_t szerokosc,
                              LCDColor kolor, LCDColor tlo, const char *tekst)
{
    int szerokosc_tekstu;
    int x_tekstu;

    if (tekst == 0)
        return;
    szerokosc_tekstu = FONT_GetStrPixelWidth((FONTS)font, tekst);
    x_tekstu = (int)x + ((int)szerokosc - szerokosc_tekstu) / 2;
    if (x_tekstu < (int)x)
        x_tekstu = x;
    FONT_Write((FONTS)font, kolor, tlo, (uint16_t)x_tekstu, y, tekst);
}

static void RTC_OgraniczDzien(RTC_EDYCJA_t *edycja)
{
    const uint8_t maksimum = RTC_DniWMiesiacu(edycja->rok, edycja->miesiac);
    if (edycja->dzien < 1U)
        edycja->dzien = 1U;
    if (edycja->dzien > maksimum)
        edycja->dzien = maksimum;
}

static void RTC_ZmienPole(RTC_EDYCJA_t *edycja, RTC_POLE_t pole, int8_t kierunek)
{
    int wartosc;
    int minimum = 0;
    int maksimum = 0;

    if (edycja == 0 || kierunek == 0)
        return;

    switch (pole)
    {
    case RTC_POLE_DZIEN:
        minimum = 1;
        maksimum = RTC_DniWMiesiacu(edycja->rok, edycja->miesiac);
        wartosc = edycja->dzien;
        break;
    case RTC_POLE_MIESIAC:
        minimum = 1;
        maksimum = 12;
        wartosc = edycja->miesiac;
        break;
    case RTC_POLE_ROK:
        minimum = 1980;
        maksimum = 2080;
        wartosc = edycja->rok;
        break;
    case RTC_POLE_GODZINA:
        minimum = 0;
        maksimum = 23;
        wartosc = edycja->godzina;
        break;
    case RTC_POLE_MINUTA:
        minimum = 0;
        maksimum = 59;
        wartosc = edycja->minuta;
        break;
    default:
        return;
    }

    wartosc += kierunek > 0 ? 1 : -1;
    if (wartosc > maksimum)
        wartosc = minimum;
    else if (wartosc < minimum)
        wartosc = maksimum;

    switch (pole)
    {
    case RTC_POLE_DZIEN: edycja->dzien = (uint8_t)wartosc; break;
    case RTC_POLE_MIESIAC:
        edycja->miesiac = (uint8_t)wartosc;
        RTC_OgraniczDzien(edycja);
        break;
    case RTC_POLE_ROK:
        edycja->rok = (uint16_t)wartosc;
        RTC_OgraniczDzien(edycja);
        break;
    case RTC_POLE_GODZINA: edycja->godzina = (uint8_t)wartosc; break;
    case RTC_POLE_MINUTA: edycja->minuta = (uint8_t)wartosc; break;
    default: break;
    }
}

static void RTC_RysujPole(uint16_t x, uint16_t y, uint16_t szerokosc,
                          const char *etykieta, const char *wartosc, uint8_t aktywne)
{
    const uint16_t wysokosc = 48U;
    const LCDColor tlo = aktywne ? UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY) : UI_KolorTlaPola();
    const LCDColor ramka = aktywne ? UI_KolorRamki(UI_STYL_AKTYWNY) : UI_KolorRamki(UI_STYL_NORMALNY);
    const LCDColor cyfry = aktywne ? UI_KolorTekstu(UI_STYL_AKTYWNY) : UI_KolorTekstu(UI_STYL_NORMALNY);
    const LCDColor kolor_etykiety = aktywne ? UI_KolorRamki(UI_STYL_AKTYWNY)
                                             : UI_KolorTekstu(UI_STYL_NIEAKTYWNY);
    const uint16_t wysokosc_fontu = (uint16_t)FONT_GetHeight(FONT_BDIGITS);
    uint16_t y_cyfr = y;

    RTC_WycentrujTekst(FONT_FRAN, x, (uint16_t)(y - 18U), szerokosc,
                       kolor_etykiety, UI_KolorTlaPola(), etykieta);

    /*
     * FONT_BDIGITS ma duże cyfry znane z WSPR/FT8. Nie używamy tu ogólnego
     * UI_RysujPrzycisk(), bo ten świadomie zawija tekst wielowierszowo i dla
     * dwóch cyfr w polu 100 px rozcinał datę na kilka linii. Pole daty/czasu
     * jest zawsze jednowierszowe.
     */
    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), ramka);
    if (aktywne && szerokosc > 4U && wysokosc > 4U)
        LCD_Rectangle(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                      LCD_MakePoint((uint16_t)(x + szerokosc - 2U), (uint16_t)(y + wysokosc - 2U)), ramka);

    if (wysokosc_fontu < wysokosc)
        y_cyfr = (uint16_t)(y + (wysokosc - wysokosc_fontu) / 2U);
    RTC_WycentrujTekst(FONT_BDIGITS, x, y_cyfr, szerokosc, cyfry, tlo, wartosc);
}

static void RTC_RysujEkran(const RTC_EDYCJA_t *edycja, RTC_POLE_t pole)
{
    char dzien[4];
    char miesiac[4];
    char rok[6];
    char godzina[4];
    char minuta[4];
    const LCDColor tlo_pola = UI_KolorTlaPola();

    if (edycja == 0)
        return;

    snprintf(dzien, sizeof(dzien), "%02u", (unsigned)edycja->dzien);
    snprintf(miesiac, sizeof(miesiac), "%02u", (unsigned)edycja->miesiac);
    snprintf(rok, sizeof(rok), "%04u", (unsigned)edycja->rok);
    snprintf(godzina, sizeof(godzina), "%02u", (unsigned)edycja->godzina);
    snprintf(minuta, sizeof(minuta), "%02u", (unsigned)edycja->minuta);

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_DATA_CZAS));

    UI_RysujPanel(10, 36, 460, 96, 0, UI_STYL_NORMALNY);
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), tlo_pola, 20, 40, JEZYK_Tekst(TEKST_DATA));
    RTC_RysujPole(20, 71, 102, JEZYK_Tekst(TEKST_DZIEN), dzien, pole == RTC_POLE_DZIEN);
    RTC_WycentrujTekst(FONT_BDIGITS, 122, 73, 16, UI_KolorTekstu(UI_STYL_NORMALNY), tlo_pola, ".");
    RTC_RysujPole(138, 71, 102, JEZYK_Tekst(TEKST_MIESIAC), miesiac, pole == RTC_POLE_MIESIAC);
    RTC_WycentrujTekst(FONT_BDIGITS, 240, 73, 16, UI_KolorTekstu(UI_STYL_NORMALNY), tlo_pola, ".");
    RTC_RysujPole(256, 71, 204, JEZYK_Tekst(TEKST_ROK), rok, pole == RTC_POLE_ROK);

    UI_RysujPanel(10, 136, 460, 76, 0, UI_STYL_NORMALNY);
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), tlo_pola, 20, 140, JEZYK_Tekst(TEKST_CZAS));
    RTC_RysujPole(124, 163, 100, JEZYK_Tekst(TEKST_GODZINA), godzina, pole == RTC_POLE_GODZINA);
    RTC_WycentrujTekst(FONT_BDIGITS, 224, 165, 32, UI_KolorTekstu(UI_STYL_NORMALNY), tlo_pola, ":");
    RTC_RysujPole(256, 163, 100, JEZYK_Tekst(TEKST_MINUTA), minuta, pole == RTC_POLE_MINUTA);

    {
        UI_AKCJA_t akcje[4];
        RTC_PrzygotujAkcje(akcje);
        UI_RysujPasekAkcji(UI_DOLNY_PASEK_Y, UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 4U);
    }
}

static uint8_t RTC_PunktWPolu(const LCDPoint *punkt, uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc)
{
    if (punkt == 0)
        return 0U;
    return (uint8_t)(punkt->x >= x && punkt->x < (uint16_t)(x + szerokosc) &&
                     punkt->y >= y && punkt->y < (uint16_t)(y + wysokosc));
}

static void MenuRTC(void)
{
    RTC_EDYCJA_t edycja;
    RTC_POLE_t pole = RTC_POLE_DZIEN;
    uint32_t data_poczatkowa;
    uint32_t czas_poczatkowy;
    uint8_t sekunda = 0U;
    short ampm = 0;
    uint8_t zapisz = 0U;
    uint8_t odswiez = 1U;

    while (TOUCH_IsPressed())
        Sleep(0);

    (void)RTC_SprawdzObecnosc();
    if (RTCpresent)
    {
        getDate(&data_poczatkowa);
        getTime(&czas_poczatkowy, &sekunda, &ampm, 0);
    }
    else
    {
        data_poczatkowa = CFG_GetParam(CFG_PARAM_Date);
        czas_poczatkowy = GetInternTime(&sekunda);
    }

    date = data_poczatkowa;
    if (RTC_CzyDataPoprawna() != 0U)
        data_poczatkowa = 20181001U;
    if ((czas_poczatkowy / 100U) > 23U || (czas_poczatkowy % 100U) > 59U)
        czas_poczatkowy = 1200U;

    edycja.rok = (uint16_t)(data_poczatkowa / 10000U);
    edycja.miesiac = (uint8_t)((data_poczatkowa / 100U) % 100U);
    edycja.dzien = (uint8_t)(data_poczatkowa % 100U);
    edycja.godzina = (uint8_t)(czas_poczatkowy / 100U);
    edycja.minuta = (uint8_t)(czas_poczatkowy % 100U);
    RTC_OgraniczDzien(&edycja);

    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        WEJSCIE_ZDARZENIE_t zdarzenie;
        LCDPoint punkt;

        if (odswiez)
        {
            RTC_RysujEkran(&edycja, pole);
            odswiez = 0U;
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
        {
            RTC_ZmienPole(&edycja, pole, -1);
            odswiez = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            RTC_ZmienPole(&edycja, pole, 1);
            odswiez = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            if (pole < RTC_POLE_MINUTA)
            {
                pole = (RTC_POLE_t)((uint32_t)pole + 1U);
                odswiez = 1U;
            }
            else
            {
                zapisz = 1U;
                break;
            }
        }

        if (TOUCH_Poll(&punkt))
        {
            while (TOUCH_IsPressed())
                Sleep(0);

            if (RTC_PunktWPolu(&punkt, 20, 50, 102, 72)) pole = RTC_POLE_DZIEN;
            else if (RTC_PunktWPolu(&punkt, 138, 50, 102, 72)) pole = RTC_POLE_MIESIAC;
            else if (RTC_PunktWPolu(&punkt, 256, 50, 204, 72)) pole = RTC_POLE_ROK;
            else if (RTC_PunktWPolu(&punkt, 124, 142, 100, 70)) pole = RTC_POLE_GODZINA;
            else if (RTC_PunktWPolu(&punkt, 256, 142, 100, 70)) pole = RTC_POLE_MINUTA;
            else
            {
                UI_AKCJA_t akcje[4];
                int16_t akcja;
                RTC_PrzygotujAkcje(akcje);
                akcja = UI_ZnajdzAkcjePaska(punkt, UI_DOLNY_PASEK_Y,
                                             UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 4U);
                if (akcja == RTC_AKCJA_WSTECZ)
                    break;
                if (akcja == RTC_AKCJA_MINUS)
                    RTC_ZmienPole(&edycja, pole, -1);
                else if (akcja == RTC_AKCJA_PLUS)
                    RTC_ZmienPole(&edycja, pole, 1);
                else if (akcja == RTC_AKCJA_ZAPISZ)
                {
                    zapisz = 1U;
                    break;
                }
            }
            odswiez = 1U;
        }
        Sleep(10);
    }

    if (zapisz)
    {
        date = (uint32_t)edycja.rok * 10000U + (uint32_t)edycja.miesiac * 100U + edycja.dzien;
        time = (uint32_t)edycja.godzina * 100U + edycja.minuta;
        second1 = 0U;
        NoDate = 0;

        if (RTCpresent)
        {
            setDate(date);
            setTime(time, 0, 0, 0);
        }
        else
        {
            SetInternTime(time);
        }

        CFG_SetParam(CFG_PARAM_Date, date);
        CFG_SetParam(CFG_PARAM_Time, time);
        CFG_Flush();
    }

    WEJSCIA_WyczyscZdarzenia();
    rqExitR = 1;
}

static uint32_t ustawienia_jezyk_generacja;

static void MenuJezykUstaw(JEZYK_t jezyk)
{
    const JEZYK_t poprzedni = JEZYK_Aktualny();
    CFG_SetParam(CFG_PARAM_JEZYK, (uint32_t)jezyk);
    CFG_Flush();
    if (poprzedni != jezyk)
        ++ustawienia_jezyk_generacja;
}

static const JEZYK_t menu_jezyki[] =
{
    JEZYK_POLSKI,
    JEZYK_ANGIELSKI,
    JEZYK_NIEMIECKI,
    JEZYK_ROSYJSKI,
};

static uint16_t MenuJezykIndeks(JEZYK_t jezyk)
{
    uint16_t i;

    for (i = 0U; i < (uint16_t)(sizeof(menu_jezyki) / sizeof(menu_jezyki[0])); ++i)
    {
        if (menu_jezyki[i] == jezyk)
            return i;
    }

    /* Nieznana wartość konfiguracji nie może pozostawić fokusu poza siatką. */
    return 0U;
}

static void MenuJezykRysuj(uint8_t fokus)
{
    const JEZYK_t aktualny = JEZYK_Aktualny();
    uint8_t i;

    UI_WyczyscEkran();
    UI_RysujPasekGorny(JEZYK_Tekst(TEKST_JEZYK), true, false, 0);

    for (i = 0U; i < 4U; ++i)
    {
        /*
         * Fokus i stan języka są dwiema różnymi informacjami. Zielona dioda
         * pokazuje język naprawdę aktywny, a ramka pokazuje tylko pozycję
         * wybraną enkoderem lub dotykiem. Nie dopisujemy słowa „Aktywny”.
         */
        UI_RysujKafelKompaktowyZeStanem(i, UI_IKONA_MENU_LICZBA,
                                        JEZYK_Nazwa(menu_jezyki[i]),
                                        fokus == i, true,
                                        menu_jezyki[i] == aktualny);
    }

}

static void MenuJezyk(void)
{
    uint8_t fokus = (uint8_t)MenuJezykIndeks(JEZYK_Aktualny());

    while (TOUCH_IsPressed())
        Sleep(0);

    WEJSCIA_WyczyscZdarzenia();
    MenuJezykRysuj(fokus);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int16_t wybor = -1;

        if (TOUCH_Poll(&punkt))
        {
            if (UI_CzyDotknietoWstecz(punkt))
            {
                TOUCH_CzekajNaPuszczenie(35U);
                break;
            }

            wybor = UI_KafelKompaktowyPoDotyku(punkt, 4U);
            if (wybor >= 0)
                TOUCH_CzekajNaPuszczenie(35U);
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
            zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            const int8_t krok = zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? 1 : -1;
            fokus = (uint8_t)((fokus + 4 + krok) % 4);
            MenuJezykRysuj(fokus);
            continue;
        }
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
            wybor = (int16_t)fokus;

        if (wybor >= 0 && wybor < 4)
        {
            fokus = (uint8_t)wybor;
            MenuJezykUstaw(menu_jezyki[fokus]);
            MenuJezykRysuj(fokus);
        }

        Sleep(10);
    }

    WEJSCIA_WyczyscZdarzenia();
    while (TOUCH_IsPressed())
        Sleep(0);
}

static void MenuPoziomFunkcjiRysuj(uint8_t fokus)
{
    const bool zaawansowany = TRYB_CzyZaawansowany();
    const uint8_t aktywny_indeks = zaawansowany ? 1U : 0U;
    const char *nazwy[2] =
    {
        JEZYK_Wybierz("Podstawowy", "Basic", "Basis", "Базовый"),
        JEZYK_Wybierz("Zaawansowany", "Advanced", "Erweitert", "Расширенный")
    };
    const char *opisy[2] =
    {
        JEZYK_Wybierz("Pokazuje tylko potrzebne funkcje codziennego pomiaru. Menu pozostaje krótsze i spokojniejsze.",
                      "Shows only the functions needed for everyday measurements. The menu stays shorter and calmer.",
                      "Zeigt nur die für den Alltag nötigen Messfunktionen. Das Menü bleibt kürzer und ruhiger.",
                      "Показывает только функции, нужные для повседневных измерений. Меню остаётся короче и спокойнее."),
        JEZYK_Wybierz("Odblokowuje RLC, model RF, Lorentz, Q-RLC, Q-circle QL oraz dodatkowe okna TDR. Cauer/Foster pozostaje w porównaniu modeli.",
                      "Unlocks RLC, RF model, Lorentz, Q-RLC, Q-circle QL and extra TDR windows. Cauer/Foster remains in model comparison.",
                      "Schaltet RLC, HF-Modell, Lorentz, Q-RLC, Q-circle QL und weitere TDR-Fenster frei. Cauer/Foster bleibt im Modellvergleich.",
                      "Открывает RLC, RF-модель, Lorentz, Q-RLC, Q-circle QL и дополнительные окна TDR. Cauer/Foster остаётся в сравнении моделей.")
    };
    char tytul_info[64];
    char opis_statusu[256];
    char tresc_info[512];
    uint8_t i;

    UI_WyczyscEkran();
    UI_RysujPasekGorny(JEZYK_Wybierz("Poziom funkcji", "Feature level", "Funktionsumfang", "Уровень функций"),
                       true, false, 0);

    for (i = 0U; i < 2U; ++i)
    {
        UI_RysujKafelKompaktowyZeStanem(i, UI_IKONA_WIELE_PASM, nazwy[i],
                                        fokus == i, true, i == aktywny_indeks);
    }

    snprintf(tytul_info, sizeof(tytul_info), "%s: %s",
             JEZYK_Wybierz("Wybrany tryb", "Selected mode", "Gewählter Modus", "Выбранный режим"),
             nazwy[fokus]);

    if (fokus == aktywny_indeks)
    {
        snprintf(opis_statusu, sizeof(opis_statusu), "%s",
                 JEZYK_Wybierz("Kontrolka oznacza aktywny poziom. Naciśnij OK, aby wrócić bez zmian albo wybierz drugi kafel.",
                               "The indicator marks the active level. Press OK to return unchanged or choose the other tile.",
                               "Die Anzeige markiert die aktive Stufe. Mit OK kehren Sie ohne Änderung zurück oder wählen die andere Kachel.",
                               "Индикатор показывает активный уровень. Нажмите OK, чтобы выйти без изменений, или выберите другой блок."));
    }
    else
    {
        snprintf(opis_statusu, sizeof(opis_statusu), "%s",
                 JEZYK_Wybierz("To nie jest jeszcze tryb aktywny. Naciśnij OK, aby przełączyć poziom funkcji na zaznaczony kafel.",
                               "This mode is not active yet. Press OK to switch the feature level to the highlighted tile.",
                               "Dieser Modus ist noch nicht aktiv. Mit OK schalten Sie den Funktionsumfang auf die markierte Kachel um.",
                               "Этот режим пока не активен. Нажмите OK, чтобы переключить уровень функций на выделенный блок."));
    }

    snprintf(tresc_info, sizeof(tresc_info), "%s\n%s", opisy[fokus], opis_statusu);
    UI_RysujPoleInformacyjne(8U, 128U, 464U, 84U, tytul_info, tresc_info);
}

static void MenuPoziomFunkcji(void)
{
    uint8_t fokus = TRYB_CzyZaawansowany() ? 1U : 0U;

    while (TOUCH_IsPressed())
        Sleep(0);
    WEJSCIA_WyczyscZdarzenia();
    MenuPoziomFunkcjiRysuj(fokus);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int16_t wybor = -1;

        if (TOUCH_Poll(&punkt))
        {
            if (UI_CzyDotknietoWstecz(punkt))
            {
                TOUCH_CzekajNaPuszczenie(35U);
                break;
            }
            wybor = UI_KafelKompaktowyPoDotyku(punkt, 2U);
            if (wybor >= 0)
                TOUCH_CzekajNaPuszczenie(35U);
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
            zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            fokus = (uint8_t)!fokus;
            MenuPoziomFunkcjiRysuj(fokus);
            continue;
        }
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
            wybor = (int16_t)fokus;

        if (wybor >= 0 && wybor < 2)
        {
            fokus = (uint8_t)wybor;
            TRYB_Ustaw(fokus == 1U ? TRYB_INTERFEJSU_ZAAWANSOWANY : TRYB_INTERFEJSU_PODSTAWOWY, true);
            MenuPoziomFunkcjiRysuj(fokus);
        }
        Sleep(10);
    }
    WEJSCIA_WyczyscZdarzenia();
}


static void USTAWIENIA_WyczyscFlagiPodmenu(void)
{
    /* Starsze ekrany używają globalnych flag rqExitR/rqExit1. W zagnieżdżonym
     * menu ich ustawienie oznacza tylko powrót z danego ekranu, nie wyjście
     * z całych Ustawień. */
    rqExitR = false;
    rqExit1 = false;
}

typedef struct
{
    const char *tekst;
    void (*akcja)(void);
    UI_IKONA_MENU_t ikona;
    bool aktywna;

    /*
     * Funkcja opisana kaflem jest wlaczona i dziala. Kontrolka mowi o stanie
     * przyrzadu, a nie o tym, gdzie stoi kursor — te dwie rzeczy sa rozne
     * i az do teraz ekran menu pokazywal tylko drugą.
     */
    bool wlaczona;
    bool ma_stan;
} USTAWIENIA_KAFEL_t;

static void USTAWIENIA_RysujKafle(const char *tytul, const USTAWIENIA_KAFEL_t *pozycje,
                                  uint8_t liczba, uint8_t fokus)
{
    uint8_t i;
    UI_WyczyscEkran();
    UI_RysujPasekGorny(tytul, true, false, 0);
    for (i = 0U; i < liczba; ++i)
    {
        if (pozycje[i].ma_stan)
            UI_RysujKafelKompaktowyZeStanem(i, pozycje[i].ikona, pozycje[i].tekst,
                                            pozycje[i].aktywna && fokus == i,
                                            pozycje[i].aktywna, pozycje[i].wlaczona);
        else
            UI_RysujKafelKompaktowy(i, pozycje[i].ikona, pozycje[i].tekst,
                                    pozycje[i].aktywna && fokus == i,
                                    pozycje[i].aktywna);
    }
}

static bool USTAWIENIA_OtworzKafle(const char *tytul, const USTAWIENIA_KAFEL_t *pozycje,
                                   uint8_t liczba)
{
    const uint32_t jezyk_start = ustawienia_jezyk_generacja;
    uint8_t fokus = 0U;
    bool przebuduj_po_jezyku = false;

    if (pozycje == 0 || liczba == 0U || liczba > UI_SIATKA_KOMPAKT_NA_STRONE)
        return false;

    while (TOUCH_IsPressed()) Sleep(0);
    WEJSCIA_WyczyscZdarzenia();
    USTAWIENIA_RysujKafle(tytul, pozycje, liczba, fokus);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int16_t wybor = -1;

        if (TOUCH_Poll(&punkt))
        {
            if (UI_CzyDotknietoWstecz(punkt))
            {
                TOUCH_CzekajNaPuszczenie(35U);
                break;
            }
            wybor = UI_KafelKompaktowyPoDotyku(punkt, liczba);
            if (wybor >= 0 && !pozycje[wybor].aktywna)
                wybor = -1;
            if (wybor >= 0)
                TOUCH_CzekajNaPuszczenie(35U);
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
            zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            const int8_t krok = zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? 1 : -1;
            uint8_t proby = liczba;
            do
            {
                fokus = (uint8_t)((fokus + liczba + krok) % liczba);
            } while (!pozycje[fokus].aktywna && --proby);
            USTAWIENIA_RysujKafle(tytul, pozycje, liczba, fokus);
            continue;
        }
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK && pozycje[fokus].aktywna)
            wybor = (int16_t)fokus;

        if (wybor >= 0 && (uint8_t)wybor < liczba)
        {
            if (pozycje[wybor].akcja != 0)
                pozycje[wybor].akcja();
            USTAWIENIA_WyczyscFlagiPodmenu();
            WEJSCIA_WyczyscZdarzenia();
            /*
             * Po każdej zmianie wracamy do funkcji nadrzędnej, która buduje
             * tablicę kafli od nowa. Dzięki temu kontrolki pokazują rzeczywisty
             * stan natychmiast, zamiast pozostawać przy wartości sprzed kliknięcia.
             */
            przebuduj_po_jezyku = true;
            (void)jezyk_start;
            break;
        }
        Sleep(10);
    }

    USTAWIENIA_WyczyscFlagiPodmenu();
    WEJSCIA_WyczyscZdarzenia();
    while (TOUCH_IsPressed()) Sleep(0);
    return przebuduj_po_jezyku;
}

static void IkonyUstaw(uint8_t zestaw)
{
    if (zestaw >= UI_LICZBA_ZESTAWOW_IKON)
        return;

    if (CFG_GetParam(CFG_PARAM_ZESTAW_IKON) != (uint32_t)zestaw)
    {
        CFG_SetParam(CFG_PARAM_ZESTAW_IKON, (uint32_t)zestaw);
        CFG_Flush();
    }
}

static void MenuIkonyRysuj(uint8_t fokus)
{
    const uint32_t zapisany = CFG_GetParam(CFG_PARAM_ZESTAW_IKON);
    const uint8_t aktywny = zapisany < UI_LICZBA_ZESTAWOW_IKON ? (uint8_t)zapisany : 0U;
    const char *nazwy[UI_LICZBA_ZESTAWOW_IKON] =
    {
        JEZYK_Wybierz("Retro", "Retro", "Retro", "Ретро")
    };
    uint8_t i;

    UI_WyczyscEkran();
    UI_RysujPasekGorny(JEZYK_Wybierz("Styl interfejsu", "Interface style", "Oberflächenstil", "Стиль интерфейса"),
                       true, false, 0);

    /* Finalny wariant pozostaje jeden: dopracowane Retro. */
    for (i = 0U; i < UI_LICZBA_ZESTAWOW_IKON; ++i)
    {
        UI_RysujKafelKompaktowyZestaw(i, UI_IKONA_STROJENIE, i, nazwy[i],
                                      fokus == i, true);
        {
            const UI_PROSTOKAT_t o = UI_ObszarKaflaKompaktowego(i);
            UI_RysujKontrolkeStanuKaflaZestawu(&o, i == aktywny, i);
        }
    }
}

static void MenuIkony(void)
{
    const uint32_t zapisany = CFG_GetParam(CFG_PARAM_ZESTAW_IKON);
    uint8_t fokus = zapisany < UI_LICZBA_ZESTAWOW_IKON ? (uint8_t)zapisany : 0U;

    while (TOUCH_IsPressed())
        Sleep(0);
    WEJSCIA_WyczyscZdarzenia();
    MenuIkonyRysuj(fokus);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int16_t wybor = -1;

        if (TOUCH_Poll(&punkt))
        {
            if (UI_CzyDotknietoWstecz(punkt))
            {
                TOUCH_CzekajNaPuszczenie(35U);
                break;
            }
            wybor = UI_KafelKompaktowyPoDotyku(punkt, UI_LICZBA_ZESTAWOW_IKON);
            if (wybor >= 0)
                TOUCH_CzekajNaPuszczenie(35U);
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
            zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            {
                const int8_t krok = zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? 1 : -1;
                fokus = (uint8_t)((fokus + UI_LICZBA_ZESTAWOW_IKON + krok) %
                                  UI_LICZBA_ZESTAWOW_IKON);
            }
            MenuIkonyRysuj(fokus);
            continue;
        }
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
            wybor = (int16_t)fokus;

        if (wybor >= 0 && wybor < (int16_t)UI_LICZBA_ZESTAWOW_IKON)
        {
            fokus = (uint8_t)wybor;
            IkonyUstaw(fokus);
            /* Natychmiastowy redraw: użytkownik od razu widzi przesunięcie
             * zielonej kontrolki i oba rzeczywiste warianty ikon. */
            MenuIkonyRysuj(fokus);
        }
        Sleep(10);
    }

    WEJSCIA_WyczyscZdarzenia();
    while (TOUCH_IsPressed())
        Sleep(0);
}

static void MenuUstawieniaWyglad(void)
{
    bool przebuduj;
    do
    {
        USTAWIENIA_KAFEL_t pozycje[] =
        {
            { JEZYK_Tekst(TEKST_STYL_INTERFEJSU), MenuIkony, UI_IKONA_WYGLAD_OBSLUGA, true,
              CFG_GetParam(CFG_PARAM_ZESTAW_IKON) == 1U, true },
            { JEZYK_Wybierz("Jasne tło wykresów", "Light plot background", "Heller Diagrammhintergrund", "Светлый фон графиков"),
              PrzelaczTloWykresow, UI_IKONA_WYGLAD_OBSLUGA, true,
              ColourSelection != 0U, true },
            { JEZYK_Wybierz("Grube linie wykresów", "Thick plot lines", "Dicke Diagrammlinien", "Толстые линии графиков"),
              PrzelaczGrubeLinie, UI_IKONA_WYKRES_SWR, true,
              FatLines, true },
            { JEZYK_Tekst(TEKST_DZWIEK), BeepOn, UI_IKONA_DZWIEK, true,
              CFG_GetParam(CFG_PARAM_BeepOn) != 0U, true },
        };

        /*
         * Każdy kafel opisuje jedną funkcję. Kontrolka jest stanem funkcji,
         * nie kursorem ani osobnym polem tekstowym. Nie dublujemy wariantów
         * jasne/ciemne i cienkie/grube dwoma kaflami, bo tworzyło to pozorną
         * konfigurację bez czytelnego znaczenia kontrolki.
         */
        przebuduj = USTAWIENIA_OtworzKafle(JEZYK_Tekst(TEKST_WYGLAD_DZWIEK), pozycje,
                                            (uint8_t)(sizeof(pozycje) / sizeof(pozycje[0])));
    } while (przebuduj);
}

static void O_PROGRAMIE_RysujStroneStartowa(void)
{
    char wersja[64];
    char nazwa_lokalna[32];
    const LCDColor panel = LCD_RGB(4, 8, 12);
    const LCDColor ramka = UI_KolorRamki(UI_STYL_AKCENT);

    if (SCREENSHOT_RysujPNGZPamieci(logo_png, logo_png_size) != 0)
        LCD_FillAll(LCD_BLACK);
    JEZYK_FormatujNazweProjektu(nazwa_lokalna, sizeof(nazwa_lokalna));

    /*
     * Zdjęcie jest elementem strony, a nie tylko tłem. Poprzedni panel zajmował
     * całą dolną trzecią część ekranu i zasłaniał maszt oraz dolne ramiona
     * anteny. Informacje przenosimy na lewą część nieba, gdzie na oryginalnej
     * grafice nie ma głównej konstrukcji antenowej. Prawa strona obrazu
     * pozostaje widoczna od góry aż do dolnej krawędzi.
     */
    LCD_FillRect(LCD_MakePoint(8, 108), LCD_MakePoint(258, 214), panel);
    LCD_Rectangle(LCD_MakePoint(8, 108), LCD_MakePoint(258, 214), ramka);

    snprintf(wersja, sizeof(wersja), "%s", nazwa_lokalna);
    FONT_Write(FONT_FRANBIG, LCD_WHITE, panel, 18, 116, wersja);
    snprintf(wersja, sizeof(wersja), "v%s", PROJEKT_WERSJA);
    FONT_Write(FONT_FRANBIG, LCD_WHITE, panel, 18, 143, wersja);
    FONT_Write(FONT_FRAN, ramka, panel, 18, 174,
               JEZYK_Wybierz("Rozwinięcie otwartego projektu",
                             "Continuation of the open",
                             "Fortführung des offenen",
                             "Продолжение открытого проекта"));
    FONT_Write(FONT_FRAN, ramka, panel, 18, 194,
               JEZYK_Wybierz("analizatora EU1KY",
                             "EU1KY antenna analyzer project",
                             "EU1KY-Antennenanalysatorprojekts",
                             "анализатора антенн EU1KY"));

    UI_RysujWsteczDolny(false);
    {
        const UI_PROSTOKAT_t tworcy = UI_ObszarPrzyciskuDolnego(1U);
        UI_RysujPrzycisk(tworcy.x, tworcy.y, tworcy.szerokosc, tworcy.wysokosc,
                         JEZYK_Wybierz("Twórcy", "Credits", "Mitwirkende", "Авторы"),
                         UI_STYL_AKCENT, FONT_FRAN);
    }
}

static void O_PROGRAMIE_RysujWiersz(uint16_t y, const char *znak, const char *nazwisko,
                                    const char *opis, UI_STYL_t styl)
{
    const LCDColor tlo = UI_KolorTlaPola();
    const LCDColor ramka = UI_KolorRamki(styl);
    char naglowek[54];

    /*
     * Stała karta 34 px: pierwszy wiersz zawiera znak i nazwisko, drugi tylko
     * krótki opis wkładu. Żaden element nie konkuruje już o tę samą linię.
     */
    LCD_FillRect(LCD_MakePoint(10, y), LCD_MakePoint(470, (uint16_t)(y + 32U)), tlo);
    LCD_Rectangle(LCD_MakePoint(10, y), LCD_MakePoint(470, (uint16_t)(y + 32U)), ramka);
    LCD_FillRect(LCD_MakePoint(11, (uint16_t)(y + 1U)), LCD_MakePoint(14, (uint16_t)(y + 31U)), ramka);

    snprintf(naglowek, sizeof(naglowek), "%s  %s", znak, nazwisko);
    FONT_Write(FONT_FRAN, ramka, tlo, 22, (uint16_t)(y + 3U), naglowek);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo, 22, (uint16_t)(y + 17U), opis);
}

static void O_PROGRAMIE_RysujTworcow(void)
{
    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_TWORCY_I_WKLAD));

    O_PROGRAMIE_RysujWiersz(40,  "EU1KY",  "Yury Kuchura",    JEZYK_Tekst(TEKST_WKLAD_AUTOR_PROJEKTU), UI_STYL_AKCENT);
    O_PROGRAMIE_RysujWiersz(74,  "DH1AKF", "Wolfgang Kiefer", JEZYK_Tekst(TEKST_WKLAD_ROZWOJ_FIRMWARE), UI_STYL_NORMALNY);
    O_PROGRAMIE_RysujWiersz(108, "KD8CEC", "Ian Lee",         JEZYK_Tekst(TEKST_WKLAD_ROZSZERZENIA_CEC), UI_STYL_NORMALNY);
    O_PROGRAMIE_RysujWiersz(142, "SQ2KRR", "Marek",           JEZYK_Tekst(TEKST_WKLAD_EU1KY_PL), UI_STYL_AKTYWNY);
    O_PROGRAMIE_RysujWiersz(176, "LY2BOK", "HB9BRJ",         JEZYK_Tekst(TEKST_WKLAD_DOKUMENTACJA), UI_STYL_NORMALNY);

    UI_RysujWsteczDolny(false);
    {
        const UI_PROSTOKAT_t zamknij = UI_ObszarPrzyciskuDolnego(1U);
        UI_RysujPrzycisk(zamknij.x, zamknij.y, zamknij.szerokosc, zamknij.wysokosc,
                         JEZYK_Wybierz("Zamknij", "Close", "Schließen", "Закрыть"),
                         UI_STYL_NORMALNY, FONT_FRAN);
    }
}

static void MenuOProgramie(void)
{
    uint8_t strona = 0U;

    while (TOUCH_IsPressed())
        Sleep(0);

    WEJSCIA_WyczyscZdarzenia();
    O_PROGRAMIE_RysujStroneStartowa();

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;

        Sleep(10);
        if (TOUCH_Poll(&punkt))
        {
            while (TOUCH_IsPressed())
                Sleep(0);

            if (UI_CzyDotknietoWstecz(punkt))
            {
                if (strona == 0U)
                    break;
                strona = 0U;
                O_PROGRAMIE_RysujStroneStartowa();
            }
            else
            {
                const UI_PROSTOKAT_t akcja = UI_ObszarPrzyciskuDolnego(1U);
                if (UI_CzyPunktWObszarze(punkt, &akcja))
                {
                    if (strona == 0U)
                    {
                        strona = 1U;
                        O_PROGRAMIE_RysujTworcow();
                    }
                    else
                        break;
                }
            }
            continue;
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
        {
            if (strona == 0U)
                break;
            strona = 0U;
            O_PROGRAMIE_RysujStroneStartowa();
            continue;
        }
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK ||
            zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
            zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            strona = (uint8_t)!strona;
            if (strona == 0U)
                O_PROGRAMIE_RysujStroneStartowa();
            else
                O_PROGRAMIE_RysujTworcow();
        }
    }

    WEJSCIA_WyczyscZdarzenia();
    while (TOUCH_IsPressed())
        Sleep(0);
}


/* ========================================================================
 * Renderery dokumentacyjne ustawień. Nie uruchamiają callbacków i nie
 * zapisują konfiguracji. Każda pozycja odpowiada rzeczywistej stronie UI.
 * ======================================================================== */
static const char *const mainwnd_dok_nazwy[] = {
    "menu_glowne",
    "menu_pomiar",
    "menu_analiza",
    "menu_narzedzia",
    "menu_kalibracja",
    "menu_ustawienia",
    "menu_pliki",
    "ustawienia_strona_1",
    "ustawienia_strona_2",
    "wyglad_i_dzwiek",
    "wybor_jezyka",
    "data_czas_edytor_data",
    "data_czas_edytor_czas",
    "profil_uzytkownika",
    "o_programie",
    "tworcy_i_wklad",
    "konfiguracja_zaawansowana",
    "menu_kalibracji"
};

uint32_t MAINWND_DokumentacjaLiczbaStron(void)
{
    return (uint32_t)(sizeof(mainwnd_dok_nazwy) / sizeof(mainwnd_dok_nazwy[0]));
}

const char *MAINWND_DokumentacjaNazwaStrony(uint32_t strona)
{
    if (strona >= MAINWND_DokumentacjaLiczbaStron())
        return "";
    return mainwnd_dok_nazwy[strona];
}

static void MAINWND_DokRysujUstawieniaStrona(uint16_t zaznaczony)
{
    /* Ten sam renderer i ta sama tablica co w rzeczywistym menu Ustawienia. */
    RysujDzial2026(MAINWND_DZIAL_USTAWIENIA, zaznaczony, false);
}

static void MAINWND_DokRysujWyglad(void)
{
    const USTAWIENIA_KAFEL_t pozycje[] =
    {
        { JEZYK_Tekst(TEKST_STYL_INTERFEJSU), 0, UI_IKONA_WYGLAD_OBSLUGA, true,
          CFG_GetParam(CFG_PARAM_ZESTAW_IKON) == 1U, true },
        { JEZYK_Wybierz("Jasne tło wykresów", "Light plot background", "Heller Diagrammhintergrund", "Светлый фон графиков"),
          0, UI_IKONA_WYGLAD_OBSLUGA, true, ColourSelection != 0U, true },
        { JEZYK_Wybierz("Grube linie wykresów", "Thick plot lines", "Dicke Diagrammlinien", "Толстые линии графиков"),
          0, UI_IKONA_WYKRES_SWR, true, FatLines, true },
        { JEZYK_Tekst(TEKST_DZWIEK), 0, UI_IKONA_DZWIEK, true,
          CFG_GetParam(CFG_PARAM_BeepOn) != 0U, true },
    };
    USTAWIENIA_RysujKafle(JEZYK_Tekst(TEKST_WYGLAD_DZWIEK), pozycje,
                           (uint8_t)(sizeof(pozycje) / sizeof(pozycje[0])), 0xFFU);
}

static void MAINWND_DokRysujJezyk(void)
{
    /* Dokumentacja korzysta z tego samego renderera co rzeczywisty wybór. */
    MenuJezykRysuj((uint8_t)MenuJezykIndeks(JEZYK_Aktualny()));
}

static void MAINWND_DokRysujDate(void)
{
    const RTC_EDYCJA_t edycja = {
        .rok = 2026U,
        .miesiac = 8U,
        .dzien = 20U,
        .godzina = 6U,
        .minuta = 28U,
    };
    RTC_RysujEkran(&edycja, RTC_POLE_DZIEN);
}

static void MAINWND_DokRysujCzas(void)
{
    const RTC_EDYCJA_t edycja = {
        .rok = 2026U,
        .miesiac = 8U,
        .dzien = 20U,
        .godzina = 6U,
        .minuta = 28U,
    };
    RTC_RysujEkran(&edycja, RTC_POLE_GODZINA);
}

static void MAINWND_DokRysujProfil(void)
{
    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_UZYTKOWNIK));
    UI_RysujPanel(40, 62, 400, 102, JEZYK_Tekst(TEKST_ZNAK), UI_STYL_NORMALNY);
    FONT_Write(FONT_FRANBIG, UI_KolorRamki(UI_STYL_AKTYWNY), UI_KolorTlaPola(), 176, 105, "SQ2KRR");
    UI_RysujPrzycisk(90, 174, 300, 38, JEZYK_Tekst(TEKST_ZMIEN_ZNAK), UI_STYL_AKCENT, FONT_FRANBIG);
    UI_RysujWsteczDolny(false);
}

static void MAINWND_DokRysujZaawansowane(void)
{
    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_KONFIGURACJA_ZAAWANSOWANA));
    UI_RysujPoleInformacyjne(12U, 52U, 456U, 150U,
        JEZYK_Tekst(TEKST_KONFIGURACJA_ZAAWANSOWANA),
        JEZYK_Wybierz(
            "Jawny edytor parametrów sprzętu i funkcji zaawansowanych. Generator, zakres oraz adres I2C są wybierane z list opisanych wartości.",
            "Explicit editor for hardware and advanced parameters. Generator, range and I2C address are selected from described value lists.",
            "Expliziter Editor für Hardware- und erweiterte Parameter. Generator, Bereich und I2C-Adresse werden aus beschriebenen Wertelisten gewählt.",
            "Явный редактор аппаратных и расширенных параметров. Генератор, диапазон и адрес I2C выбираются из описанных списков."));
    UI_RysujWsteczDolny(false);
}

static void MAINWND_DokRysujKalibracje(void)
{
    /* Kalibracja jest teraz częścią Ustawień. Dokumentacyjny skrót pokazuje
     * rzeczywiste miejsce wejścia zamiast nieistniejącego działu głównego. */
    RysujDzial2026(MAINWND_DZIAL_USTAWIENIA, 2U, true);
}

void MAINWND_DokumentacjaRysujStrone(uint32_t strona)
{
    switch (strona)
    {
    /* Najpierw siedem ekranów nawigacyjnych. Generator instrukcji ma
     * dokumentować to, co użytkownik widzi po uruchomieniu urządzenia, a
     * nie zaczynać dopiero od głębokich ustawień. */
    case 0U: RysujMenuDzialow2026(MAINWND_DZIAL_NARZEDZIA); break;
    case 1U: RysujDzial2026(MAINWND_DZIAL_POMIAR, 0U, false); break;
    case 2U: RysujDzial2026(MAINWND_DZIAL_ANALIZA, 0U, false); break;
    case 3U: RysujDzial2026(MAINWND_DZIAL_NARZEDZIA, 0U, false); break;
    case 4U: RysujDzial2026(MAINWND_DZIAL_USTAWIENIA, 2U, true); break;
    case 5U: RysujDzial2026(MAINWND_DZIAL_USTAWIENIA, 0U, false); break;
    case 6U: RysujDzial2026(MAINWND_DZIAL_PLIKI, 0U, false); break;

    case 7U: MAINWND_DokRysujUstawieniaStrona(0U); break;
    case 8U: MAINWND_DokRysujUstawieniaStrona(6U); break;
    case 9U: MAINWND_DokRysujWyglad(); break;
    case 10U: MAINWND_DokRysujJezyk(); break;
    case 11U: MAINWND_DokRysujDate(); break;
    case 12U: MAINWND_DokRysujCzas(); break;
    case 13U: MAINWND_DokRysujProfil(); break;
    case 14U: O_PROGRAMIE_RysujStroneStartowa(); break;
    case 15U: O_PROGRAMIE_RysujTworcow(); break;
    case 16U: MAINWND_DokRysujZaawansowane(); break;
    case 17U: MAINWND_DokRysujKalibracje(); break;
    default: RysujMenuDzialow2026(MAINWND_DZIAL_NARZEDZIA); break;
    }
}
