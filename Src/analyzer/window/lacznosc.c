#include "lacznosc.h"

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "aauart.h"
#include "config.h"
#include "font.h"
#include "jezyk.h"
#include "komunikaty.h"
#include "touch.h"
#include "ui_wspolny.h"
#include "wejscia_uzytkownika.h"

extern void Sleep(uint32_t);

#define LACZ_LICZBA_KAFLI UI_SIATKA_KOMPAKT_NA_STRONE

typedef void (*LACZ_AKCJA_t)(void);
typedef struct { const char *tekst; UI_IKONA_MENU_t ikona; LACZ_AKCJA_t akcja; } LACZ_KAFEL_t;


static const char *LACZ_T(const char *pl, const char *en, const char *de, const char *ru)
{
    return JEZYK_Wybierz(pl, en, de, ru);
}

static const char *LACZ_NazwaProtokolu(uint32_t p)
{
    switch (p)
    {
    case CFG_PROTO_AA600: return "AA-600 / AntScope";
    case CFG_PROTO_LINSMITH: return "N2PK / LinSmith";
    case CFG_PROTO_NANOVNA: return "NanoVNA shell";
    case CFG_PROTO_MINIVNA: return "miniVNA / VNA-J (exp.)";
    default: return "?";
    }
}

static const char *LACZ_NazwaModulu(uint32_t m)
{
    switch (m)
    {
    case CFG_BT_MODUL_HC05: return "HC-05";
    case CFG_BT_MODUL_HC06: return "HC-06";
    default: return LACZ_T("Brak", "None", "Kein", "Нет");
    }
}

static void LACZ_CzekajNaWstecz(void)
{
    while (TOUCH_IsPressed()) Sleep(5U);
    WEJSCIA_WyczyscZdarzenia();
    for (;;)
    {
        LCDPoint p;
        if (TOUCH_Poll(&p))
        {
            if (UI_CzyDotknietoWstecz(p)) { TOUCH_CzekajNaPuszczenie(35U); break; }
            TOUCH_CzekajNaPuszczenie(20U);
        }
        if (WEJSCIA_PobierzZdarzenie() == WEJSCIE_ZDARZENIE_WSTECZ) break;
        Sleep(10U);
    }
    WEJSCIA_WyczyscZdarzenia();
}

static uint32_t LACZ_WybierzZTablicy(const char *tytul, const char *const *etykiety,
                                     const uint32_t *wartosci, uint8_t liczba, uint32_t biezaca)
{
    uint8_t fokus = 0U, i;
    while (fokus + 1U < liczba && wartosci[fokus] != biezaca) fokus++;
    if (wartosci[fokus] != biezaca) fokus = 0U;
    while (TOUCH_IsPressed()) Sleep(5U);
    WEJSCIA_WyczyscZdarzenia();
    for (;;)
    {
        LCDPoint p;
        WEJSCIE_ZDARZENIE_t z;
        UI_WyczyscEkran();
        UI_RysujPasekGorny(tytul, true, false, 0);
        for (i = 0U; i < liczba; ++i)
            UI_RysujKafelKompaktowy(i, UI_IKONA_URZADZENIE, etykiety[i], i == fokus, true);
        for (;;)
        {
            if (TOUCH_Poll(&p))
            {
                if (UI_CzyDotknietoWstecz(p)) { TOUCH_CzekajNaPuszczenie(35U); return biezaca; }
                {
                    const int16_t indeks = UI_KafelKompaktowyPoDotyku(p, liczba);
                    if (indeks >= 0)
                    {
                        TOUCH_CzekajNaPuszczenie(35U);
                        return wartosci[(uint8_t)indeks];
                    }
                }
            }
            z = WEJSCIA_PobierzZdarzenie();
            if (z == WEJSCIE_ZDARZENIE_WSTECZ) return biezaca;
            if (z == WEJSCIE_ZDARZENIE_OK) return wartosci[fokus];
            if (z == WEJSCIE_ZDARZENIE_OBROT_PRAWO || z == WEJSCIE_ZDARZENIE_OBROT_LEWO)
            {
                fokus = z == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? (uint8_t)((fokus + 1U) % liczba)
                                                            : (uint8_t)((fokus + liczba - 1U) % liczba);
                break;
            }
            Sleep(10U);
        }
    }
}

static void LACZ_RysujStan(void)
{
    char w1[96], w2[96], w3[96];
    const uint32_t modul = CFG_GetParam(CFG_PARAM_BT_MODUL);
    snprintf(w1, sizeof(w1), "%s / %s", CFG_GetParam(CFG_PARAM_COM_PORT) == COM2 ? "COM2" : "COM1", LACZ_NazwaModulu(modul));
    snprintf(w2, sizeof(w2), "%lu bit/s", (unsigned long)CFG_GetParam(CFG_PARAM_COM_SPEED));
    snprintf(w3, sizeof(w3), "%s", LACZ_NazwaProtokolu(CFG_GetParam(CFG_PARAM_SEREMUL)));
    UI_WyczyscEkran();
    UI_RysujPasekGorny(LACZ_T("Łączność z PC / VNA", "PC / VNA connectivity", "PC-/VNA-Verbindung", "Связь с ПК / VNA"), true, false, 0);
    /*
     * Dolny pas od y=220 jest zarezerwowany dla przycisku Wstecz.
     * Cztery pola mieszczą się nad nim z równym odstępem i nie przykrywają
     * przycisku ani w stylu klasycznym, ani CRT.
     */
    UI_RysujPoleInformacyjne(12U, 42U, 456U, 40U, LACZ_T("Transport", "Transport", "Transport", "Транспорт"), w1);
    UI_RysujPoleInformacyjne(12U, 84U, 456U, 40U, LACZ_T("UART", "UART", "UART", "UART"), w2);
    UI_RysujPoleInformacyjne(12U, 126U, 456U, 40U, LACZ_T("Protokół", "Protocol", "Protokoll", "Протокол"), w3);
    UI_RysujPoleInformacyjne(12U, 168U, 456U, 40U,
        LACZ_T("Dostępne", "Available", "Verfügbar", "Доступно"),
        LACZ_T("USB oraz HC-05 / HC-06 przez UART.",
               "USB and HC-05 / HC-06 over UART.",
               "USB und HC-05 / HC-06 über UART.",
               "USB и HC-05 / HC-06 через UART."));
}
static void LACZ_Stan(void) { LACZ_RysujStan(); LACZ_CzekajNaWstecz(); }

static void LACZ_USB(void)
{
    CFG_SetParam(CFG_PARAM_COM_PORT, COM1);
    CFG_SetParam(CFG_PARAM_COM_SPEED, 38400U);
    CFG_Flush();
    KOMUNIKAT_PokazTekst("USB / ST-Link", LACZ_T("COM1 38400 zapisany. Zrestartuj analizator.", "COM1 38400 saved. Restart the analyzer.", "COM1 38400 gespeichert. Neustart.", "COM1 38400 сохранён. Перезапустите."));
}

static void LACZ_Modul(void)
{
    static const char *const et[] = {"Brak / USB", "HC-05", "HC-06"};
    static const uint32_t wa[] = {CFG_BT_MODUL_BRAK, CFG_BT_MODUL_HC05, CFG_BT_MODUL_HC06};
    const uint32_t stary = CFG_GetParam(CFG_PARAM_BT_MODUL);
    const uint32_t nowy = LACZ_WybierzZTablicy(LACZ_T("Moduł bezprzewodowy", "Wireless module", "Funkmodul", "Беспроводной модуль"), et, wa, 3U, stary);
    if (nowy == stary) return;
    CFG_SetParam(CFG_PARAM_BT_MODUL, nowy);
    if (nowy == CFG_BT_MODUL_BRAK)
    {
        CFG_SetParam(CFG_PARAM_COM_PORT, COM1);
        CFG_SetParam(CFG_PARAM_COM_SPEED, 38400U);
    }
    else
    {
        CFG_SetParam(CFG_PARAM_COM_PORT, COM2);
        /* HC-05/06 występują z różnymi ustawieniami danych. Zaczynamy od
         * historycznego 9600 i pozostawiamy jawną zmianę prędkości. */
        CFG_SetParam(CFG_PARAM_BT_SPEED, 9600U);
        CFG_SetParam(CFG_PARAM_COM_SPEED, 9600U);
    }
    CFG_Flush();
    KOMUNIKAT_PokazTekst(LACZ_T("Moduł zapisany", "Module saved", "Modul gespeichert", "Модуль сохранён"),
                         LACZ_T("Zmiana UART obowiązuje po restarcie. HC-05/HC-06 wymagają zgodnej prędkości modułu.",
                                "Restart required. HC-05/HC-06 UART baud must match the module.",
                                "Neustart erforderlich. Die HC-05/HC-06-Baudrate muss passen.",
                                "Нужен перезапуск. Скорость HC-05/HC-06 должна совпадать."));
}

static void LACZ_Protokol(void)
{
    static const char *const et[] = {"AA-600 / AntScope", "N2PK / LinSmith", "NanoVNA shell", "miniVNA / VNA-J (exp.)"};
    static const uint32_t wa[] = {CFG_PROTO_AA600, CFG_PROTO_LINSMITH, CFG_PROTO_NANOVNA, CFG_PROTO_MINIVNA};
    uint32_t nowy = LACZ_WybierzZTablicy(LACZ_T("Protokół programu PC", "PC software protocol", "PC-Software-Protokoll", "Протокол программы ПК"), et, wa, 4U, CFG_GetParam(CFG_PARAM_SEREMUL));
    CFG_SetParam(CFG_PARAM_SEREMUL, nowy); CFG_Flush();
}

static void LACZ_Predkosc(void)
{
    static const char *const et[] = {"9600", "19200", "38400", "57600", "115200"};
    static const uint32_t wa[] = {9600U,19200U,38400U,57600U,115200U};
    uint32_t n = LACZ_WybierzZTablicy("UART / COM baud", et, wa, 5U, CFG_GetParam(CFG_PARAM_COM_SPEED));
    CFG_SetParam(CFG_PARAM_COM_SPEED, n);
    if (CFG_GetParam(CFG_PARAM_COM_PORT) == COM2) CFG_SetParam(CFG_PARAM_BT_SPEED, n);
    CFG_Flush();
    KOMUNIKAT_PokazTekst("UART", LACZ_T("Prędkość zapisana. Zrestartuj analizator.", "Baud saved. Restart the analyzer.", "Baudrate gespeichert. Neustart.", "Скорость сохранена. Перезапустите."));
}

static void LACZ_RysujInformacje(void)
{
    UI_WyczyscEkran();
    UI_RysujPasekGorny(LACZ_T("Łączność - informacje", "Connectivity information",
                              "Verbindungsinfo", "Информация о связи"), true, false, 0);

    UI_RysujPoleInformacyjne(12U, 42U, 456U, 40U,
        "USB / ST-Link",
        LACZ_T("COM1, domyślnie 38400 bit/s.",
               "COM1, default 38400 bit/s.",
               "COM1, standardmäßig 38400 bit/s.",
               "COM1, по умолчанию 38400 бит/с."));
    UI_RysujPoleInformacyjne(12U, 84U, 456U, 40U,
        "HC-05 / HC-06",
        LACZ_T("Bluetooth SPP przez COM2; prędkość UART musi być zgodna z modułem.",
               "Bluetooth SPP over COM2; UART baud must match the module.",
               "Bluetooth-SPP über COM2; UART-Baudrate muss zum Modul passen.",
               "Bluetooth SPP через COM2; скорость UART должна совпадать с модулем."));
    UI_RysujPoleInformacyjne(12U, 126U, 456U, 40U,
        LACZ_T("Protokoły", "Protocols", "Protokolle", "Протоколы"),
        "AA-600 / LinSmith / NanoVNA / miniVNA");
    UI_RysujPoleInformacyjne(12U, 168U, 456U, 40U,
        LACZ_T("Zegar", "Clock", "Uhr", "Часы"),
        LACZ_T("RTC ustawiany lokalnie; bez automatycznej synchronizacji sieciowej.",
               "RTC is set locally; no automatic network time sync.",
               "RTC wird lokal gestellt; keine automatische Netzzeitsynchronisierung.",
               "RTC настраивается локально; сетевой автосинхронизации нет."));
}

static void LACZ_Informacje(void)
{
    LACZ_RysujInformacje();
    LACZ_CzekajNaWstecz();
}

static void LACZ_RysujMenu(const LACZ_KAFEL_t *kafle, uint8_t fokus)
{
    uint8_t i;

    UI_WyczyscEkran();
    UI_RysujPasekGorny(LACZ_T("Łączność z PC / VNA", "PC / VNA connectivity",
                              "PC-/VNA-Verbindung", "Связь с ПК / VNA"),
                       true, false, 0);
    for (i = 0U; i < LACZ_LICZBA_KAFLI; ++i)
        UI_RysujKafelKompaktowy(i, kafle[i].ikona, kafle[i].tekst, i == fokus, true);
}

void LACZNOSC_Otworz(void)
{
    uint8_t fokus = 0U;

    for (;;)
    {
        LACZ_KAFEL_t kafle[LACZ_LICZBA_KAFLI] =
        {
            {"Stan łączności", UI_IKONA_STAN_SYSTEMU, LACZ_Stan},
            {"USB / ST-Link", UI_IKONA_USB, LACZ_USB},
            {"Moduł bezprzewodowy", UI_IKONA_CYFROWE, LACZ_Modul},
            {"Protokół PC", UI_IKONA_S21, LACZ_Protokol},
            {"UART / baud", UI_IKONA_URZADZENIE, LACZ_Predkosc},
            {LACZ_T("Informacje", "Information", "Information", "Информация"), UI_IKONA_I2C, LACZ_Informacje}
        };
        int16_t wybor = -1;

        LACZ_RysujMenu(kafle, fokus);
        while (TOUCH_IsPressed())
            Sleep(5U);
        WEJSCIA_WyczyscZdarzenia();

        for (;;)
        {
            LCDPoint punkt;
            WEJSCIE_ZDARZENIE_t zdarzenie;

            if (TOUCH_Poll(&punkt))
            {
                if (UI_CzyDotknietoWstecz(punkt))
                {
                    TOUCH_CzekajNaPuszczenie(35U);
                    return;
                }
                wybor = UI_KafelKompaktowyPoDotyku(punkt, LACZ_LICZBA_KAFLI);
                if (wybor >= 0)
                    TOUCH_CzekajNaPuszczenie(35U);
            }

            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                return;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
                wybor = (int16_t)fokus;

            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ||
                zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
            {
                fokus = zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO
                    ? (uint8_t)((fokus + 1U) % LACZ_LICZBA_KAFLI)
                    : (uint8_t)((fokus + LACZ_LICZBA_KAFLI - 1U) % LACZ_LICZBA_KAFLI);
                LACZ_RysujMenu(kafle, fokus);
            }

            if (wybor >= 0)
            {
                kafle[wybor].akcja();
                break;
            }
            Sleep(10U);
        }
    }
}

uint32_t LACZNOSC_DokumentacjaLiczbaStron(void) { return 3U; }

const char *LACZNOSC_DokumentacjaNazwaStrony(uint32_t s)
{
    static const char *const nazwy[] = {"lacznosc_menu", "lacznosc_stan", "lacznosc_info"};
    return s < 3U ? nazwy[s] : "lacznosc";
}

void LACZNOSC_DokumentacjaRysujStrone(uint32_t s)
{
    if (s == 1U)
    {
        LACZ_RysujStan();
    }
    else if (s == 2U)
    {
        LACZ_RysujInformacje();
    }
    else
    {
        LACZ_KAFEL_t kafle[LACZ_LICZBA_KAFLI] =
        {
            {"Stan łączności", UI_IKONA_STAN_SYSTEMU, 0},
            {"USB / ST-Link", UI_IKONA_USB, 0},
            {"Moduł bezprzewodowy", UI_IKONA_CYFROWE, 0},
            {"Protokół PC", UI_IKONA_S21, 0},
            {"UART / baud", UI_IKONA_URZADZENIE, 0},
            {"Informacje", UI_IKONA_I2C, 0}
        };
        LACZ_RysujMenu(kafle, 0U);
    }
}
