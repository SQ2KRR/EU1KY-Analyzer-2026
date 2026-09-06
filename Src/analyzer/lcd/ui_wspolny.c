#include <stdio.h>
#include <string.h>

#include "ui_wspolny.h"
#include "font.h"
#include "config.h"
#include "jezyk.h"
#include "bitmaps/ikony_motywow.h"
#include "bitmaps/ikony_szczegolowe_makiety.h"
#include "bitmaps/ikony_szczegolowe_nowe.h"

typedef struct
{
    LCDColor tlo_ekranu;
    LCDColor tlo_przycisku;
    LCDColor tlo_akcentu;
    LCDColor tlo_powrotu;
    LCDColor tlo_aktywnego;
    LCDColor tlo_nieaktywnego;
    LCDColor tlo_ostrzezenia;

    LCDColor ramka_normalna;
    LCDColor ramka_akcentu;
    LCDColor ramka_powrotu;
    LCDColor ramka_aktywnego;
    LCDColor ramka_nieaktywna;
    LCDColor ramka_ostrzezenia;

    LCDColor tekst_normalny;
    LCDColor tekst_nieaktywny;
    LCDColor tekst_naglowka;

    LCDColor ikona_kolor;
    LCDColor ikona_akcent;
    LCDColor ikona_dodatkowy;

    LCDColor naglowek_tlo;
    LCDColor pasek_stanu_tlo;
    LCDColor pasek_stanu_linia;

    LCDColor kafel_tlo;
    LCDColor kafel_tlo_aktywny;
    LCDColor kafel_ramka;
    LCDColor kafel_ramka_aktywna;
    LCDColor kafel_etykieta_tlo;
    LCDColor kafel_etykieta_tlo_aktywny;
    LCDColor kafel_ramka_wewn;

    LCDColor pole_tlo;
    LCDColor pole_ramka;
    LCDColor pole_etykieta;
}
UI_PALETA_t;

typedef struct
{
    char nazwa[24];
    char wersja[16];
    char znak[24];
    uint32_t czas;
    uint8_t rtc_obecny;
    uint8_t karta_sd_obecna;
    uint8_t rf_aktywne;
    uint8_t bateria_obecna;
    uint8_t zestaw_ikon;
    int procent_baterii;
    int napiecie_centi_v;
} UI_STATUS_CACHE_t;

static UI_STATUS_CACHE_t g_poprzedni_status;
static uint8_t g_poprzedni_status_wazny;
static UI_DOSTAWCA_STATUSU_t g_dostawca_statusu;


static const UI_PALETA_t g_paleta_crt __attribute__((unused)) =
{
    /*
     * Drugi styl interfejsu inspirowany zielonym monitorem CRT i sprzętem
     * Hi-Fi z lat 80. Kształty ikon pozostają te same, zmienia się wyłącznie
     * sposób ich prezentacji. Czerwień jest zarezerwowana dla kontrolek,
     * ostrzeżeń i przycisku wyjścia.
     */
    .tlo_ekranu = LCD_RGB(0, 5, 1),
    .tlo_przycisku = LCD_RGB(0, 13, 4),
    .tlo_akcentu = LCD_RGB(0, 28, 7),
    .tlo_powrotu = LCD_RGB(72, 5, 6),
    .tlo_aktywnego = LCD_RGB(0, 34, 9),
    .tlo_nieaktywnego = LCD_RGB(7, 15, 8),
    .tlo_ostrzezenia = LCD_RGB(72, 5, 6),

    .ramka_normalna = LCD_RGB(36, 220, 62),
    .ramka_akcentu = LCD_RGB(108, 255, 124),
    .ramka_powrotu = LCD_RGB(255, 76, 70),
    .ramka_aktywnego = LCD_RGB(92, 255, 112),
    .ramka_nieaktywna = LCD_RGB(35, 82, 42),
    .ramka_ostrzezenia = LCD_RGB(255, 72, 66),

    .tekst_normalny = LCD_RGB(126, 255, 137),
    .tekst_nieaktywny = LCD_RGB(52, 112, 62),
    .tekst_naglowka = LCD_RGB(150, 255, 158),

    .ikona_kolor = LCD_RGB(80, 255, 98),
    .ikona_akcent = LCD_RGB(150, 255, 158),
    .ikona_dodatkowy = LCD_RGB(255, 72, 66),

    .naglowek_tlo = LCD_RGB(0, 8, 2),
    .pasek_stanu_tlo = LCD_RGB(0, 7, 2),
    .pasek_stanu_linia = LCD_RGB(26, 160, 45),

    .kafel_tlo = LCD_RGB(0, 10, 3),
    .kafel_tlo_aktywny = LCD_RGB(0, 24, 6),
    .kafel_ramka = LCD_RGB(32, 202, 54),
    .kafel_ramka_aktywna = LCD_RGB(100, 255, 118),
    .kafel_etykieta_tlo = LCD_RGB(0, 15, 4),
    .kafel_etykieta_tlo_aktywny = LCD_RGB(0, 28, 7),
    .kafel_ramka_wewn = LCD_RGB(17, 94, 30),

    .pole_tlo = LCD_RGB(0, 11, 3),
    .pole_ramka = LCD_RGB(26, 132, 40),
    .pole_etykieta = LCD_RGB(74, 186, 88),
};

/*
 * Trzy motywy przywrocone z v2.0-rc4. Kazdy ma wlasna palete interfejsu
 * dobrana do bitmapowych ikon z ikony_motywow.c - ikony sa nieprzezroczystymi
 * plytkami 70 x 30, wiec tlo kafla musi do nich pasowac, inaczej plytka
 * odcina sie od reszty ekranu.
 */
static const UI_PALETA_t g_paleta_cyberpunk __attribute__((unused)) =
{
    .tlo_ekranu = LCD_RGB(8, 4, 16),
    .tlo_przycisku = LCD_RGB(20, 12, 34),
    .tlo_akcentu = LCD_RGB(18, 34, 62),
    .tlo_powrotu = LCD_RGB(96, 20, 54),
    .tlo_aktywnego = LCD_RGB(10, 64, 66),
    .tlo_nieaktywnego = LCD_RGB(33, 26, 42),
    .tlo_ostrzezenia = LCD_RGB(110, 56, 12),
    .ramka_normalna = LCD_RGB(90, 245, 255),
    .ramka_akcentu = LCD_RGB(255, 90, 220),
    .ramka_powrotu = LCD_RGB(255, 120, 155),
    .ramka_aktywnego = LCD_RGB(95, 255, 205),
    .ramka_nieaktywna = LCD_RGB(98, 70, 118),
    .ramka_ostrzezenia = LCD_RGB(255, 210, 100),
    .tekst_normalny = LCD_RGB(240, 250, 255),
    .tekst_nieaktywny = LCD_RGB(150, 130, 170),
    .tekst_naglowka = LCD_RGB(255, 110, 225),
    .ikona_kolor = LCD_RGB(140, 255, 255),
    .ikona_akcent = LCD_RGB(255, 75, 210),
    .ikona_dodatkowy = LCD_RGB(255, 205, 95),
    .naglowek_tlo = LCD_RGB(14, 6, 26),
    .pasek_stanu_tlo = LCD_RGB(10, 7, 20),
    .pasek_stanu_linia = LCD_RGB(100, 225, 255),
    .kafel_tlo = LCD_RGB(12, 9, 25),
    .kafel_tlo_aktywny = LCD_RGB(18, 28, 50),
    .kafel_ramka = LCD_RGB(85, 240, 255),
    .kafel_ramka_aktywna = LCD_RGB(255, 90, 220),
    .kafel_etykieta_tlo = LCD_RGB(20, 15, 40),
    .kafel_etykieta_tlo_aktywny = LCD_RGB(54, 16, 58),
    .kafel_ramka_wewn = LCD_RGB(58, 54, 105),
    .pole_tlo = LCD_RGB(16, 10, 30),
    .pole_ramka = LCD_RGB(98, 80, 150),
    .pole_etykieta = LCD_RGB(175, 165, 215),
};

static const UI_PALETA_t g_paleta_retro =
{
    /*
     * Wiktoriański panel pomiarowy: czernione drewno / bakelit + mosiądz.
     * Wartości odpowiadają zatwierdzonej makiecie V2.1: pola przycisków i kafli
     * są prawie czarne, a cały kolor niesie mosiądz ramek i kremowy tekst.
     * Wcześniejsze brązowe tła (29,19,10) rozmywały kontrast - na matowym
     * ekranie 480 x 272 ramka gubiła się w tle przycisku.
     */
    .tlo_ekranu = LCD_RGB(6, 5, 3),
    .tlo_przycisku = LCD_RGB(10, 8, 4),
    .tlo_akcentu = LCD_RGB(74, 48, 16),
    .tlo_powrotu = LCD_RGB(118, 26, 17),
    .tlo_aktywnego = LCD_RGB(83, 56, 22),
    .tlo_nieaktywnego = LCD_RGB(30, 25, 19),
    .tlo_ostrzezenia = LCD_RGB(92, 60, 12),
    .ramka_normalna = LCD_RGB(216, 168, 92),
    .ramka_akcentu = LCD_RGB(247, 213, 132),
    /* Ramka Wstecz zostaje mosiężna - czerwone jest samo pole, jak w makiecie. */
    .ramka_powrotu = LCD_RGB(231, 182, 102),
    .ramka_aktywnego = LCD_RGB(255, 218, 112),
    .ramka_nieaktywna = LCD_RGB(105, 82, 52),
    .ramka_ostrzezenia = LCD_RGB(255, 206, 82),
    .tekst_normalny = LCD_RGB(247, 232, 196),
    .tekst_nieaktywny = LCD_RGB(149, 126, 91),
    .tekst_naglowka = LCD_RGB(252, 231, 186),
    .ikona_kolor = LCD_RGB(231, 184, 92),
    .ikona_akcent = LCD_RGB(255, 211, 111),
    .ikona_dodatkowy = LCD_RGB(212, 144, 57),
    .naglowek_tlo = LCD_RGB(5, 4, 2),
    .pasek_stanu_tlo = LCD_RGB(5, 4, 2),
    .pasek_stanu_linia = LCD_RGB(190, 140, 56),
    .kafel_tlo = LCD_RGB(10, 8, 4),
    .kafel_tlo_aktywny = LCD_RGB(56, 35, 12),
    .kafel_ramka = LCD_RGB(216, 168, 92),
    .kafel_ramka_aktywna = LCD_RGB(255, 211, 111),
    .kafel_etykieta_tlo = LCD_RGB(8, 7, 4),
    .kafel_etykieta_tlo_aktywny = LCD_RGB(63, 39, 13),
    .kafel_ramka_wewn = LCD_RGB(120, 82, 34),
    .pole_tlo = LCD_RGB(12, 10, 6),
    .pole_ramka = LCD_RGB(150, 106, 44),
    .pole_etykieta = LCD_RGB(214, 178, 112),
};

static int8_t g_zestaw_ikon_tymczasowy = -1;

static uint8_t UI_EfektywnyZestawIkon(void)
{
    /*
     * Finalna makieta V2.1 ma jeden dopracowany wariant Retro.
     * Zachowujemy tymczasowy podgląd tylko wtedy, gdy mieści się w realnej
     * liczbie dostępnych zestawów, ale w zwykłej pracy zawsze wracamy do 0.
     */
    if (g_zestaw_ikon_tymczasowy >= 0 &&
        g_zestaw_ikon_tymczasowy < (int8_t)UI_LICZBA_ZESTAWOW_IKON)
        return (uint8_t)g_zestaw_ikon_tymczasowy;

    if (CFG_GetParam(CFG_PARAM_ZESTAW_IKON) != 0U)
        CFG_SetParam(CFG_PARAM_ZESTAW_IKON, 0U);
    return 0U;
}

void UI_UstawZestawIkonTymczasowy(int8_t zestaw)
{
    if (zestaw >= 0 && zestaw < (int8_t)UI_LICZBA_ZESTAWOW_IKON)
        g_zestaw_ikon_tymczasowy = zestaw;
    else
        g_zestaw_ikon_tymczasowy = -1;
}

static const UI_PALETA_t *UI_PaletaDlaZestawu(uint8_t zestaw)
{
    /* Został jeden styl - retro. Parametr zostaje w sygnaturze, bo wywołania
       przekazują numer zestawu z konfiguracji. */
    (void)zestaw;
    return &g_paleta_retro;
}

static const UI_PALETA_t *UI_Paleta(void)
{
    return UI_PaletaDlaZestawu(UI_EfektywnyZestawIkon());
}

UI_MOTYW_t UI_PobierzMotyw(void)
{
    return UI_MOTYW_KLASYCZNY;
}

void UI_UstawMotyw(UI_MOTYW_t motyw)
{
    /*
     * v2.02-test4: firmware ma jeden zachowany, klasyczny wygląd.
     * Parametr CFG pozostaje w pliku konfiguracji dla zgodności wstecznej,
     * ale każda stara wartość jest sprowadzana do 0.
     */
    (void)motyw;
    if (CFG_GetParam(CFG_PARAM_UI_MOTYW) != 0U)
    {
        CFG_SetParam(CFG_PARAM_UI_MOTYW, 0U);
        CFG_Flush();
    }
}

UI_MOTYW_t UI_PrzelaczMotyw(void)
{
    UI_UstawMotyw(UI_MOTYW_KLASYCZNY);
    return UI_MOTYW_KLASYCZNY;
}

const char *UI_PobierzNazweMotywu(void)
{
    return JEZYK_Wybierz("Klasyczny", "Classic", "Klassisch", "Классический");
}

void UI_FormatujCzestotliwoscMHz(uint32_t czestotliwosc_hz, char *bufor, uint32_t rozmiar_bufora)
{
    char separator;
    uint32_t mhz;
    uint32_t reszta_hz;

    if (bufor == 0 || rozmiar_bufora == 0U)
        return;

    separator = JEZYK_CzySeparatorDziesietnyPrzecinek() ? ',' : '.';
    mhz = czestotliwosc_hz / 1000000U;
    reszta_hz = czestotliwosc_hz % 1000000U;

    snprintf(bufor, rozmiar_bufora, "%lu%c%06lu MHz",
             (unsigned long)mhz, separator, (unsigned long)reszta_hz);
}

void UI_FormatujCzestotliwoscMHzKrotko(uint32_t czestotliwosc_hz, char *bufor, uint32_t rozmiar_bufora)
{
    char separator;
    uint32_t mhz;
    uint32_t khz;

    if (bufor == 0 || rozmiar_bufora == 0U)
        return;

    /*
     * W kaflach i listach pokazujemy rozdzielczość 1 kHz. To odpowiada
     * wspólnemu edytorowi częstotliwości i nie sugeruje użytkownikowi
     * dokładności 1 Hz, której ten ekran nie ustawia. Pełny formatter
     * sześciocyfrowy pozostaje dostępny dla ekranów pomiarowych.
     */
    separator = JEZYK_CzySeparatorDziesietnyPrzecinek() ? ',' : '.';
    mhz = czestotliwosc_hz / 1000000U;
    khz = (czestotliwosc_hz % 1000000U) / 1000U;

    snprintf(bufor, rozmiar_bufora, "%lu%c%03lu MHz",
             (unsigned long)mhz, separator, (unsigned long)khz);
}

#define UI_TLO_EKRANU       (UI_Paleta()->tlo_ekranu)
#define UI_TLO_PRZYCISKU    (UI_Paleta()->tlo_przycisku)
#define UI_TLO_AKCENTU      (UI_Paleta()->tlo_akcentu)
#define UI_TLO_POWROTU      (UI_Paleta()->tlo_powrotu)
#define UI_TLO_AKTYWNEGO    (UI_Paleta()->tlo_aktywnego)
#define UI_TLO_NIEAKTYWNEGO (UI_Paleta()->tlo_nieaktywnego)
#define UI_TLO_OSTRZEZENIA  (UI_Paleta()->tlo_ostrzezenia)
#define UI_RAMKA_NORMALNA   (UI_Paleta()->ramka_normalna)
#define UI_RAMKA_AKCENTU    (UI_Paleta()->ramka_akcentu)
#define UI_RAMKA_POWROTU    (UI_Paleta()->ramka_powrotu)
#define UI_RAMKA_AKTYWNEGO  (UI_Paleta()->ramka_aktywnego)
#define UI_RAMKA_NIEAKTYWNA (UI_Paleta()->ramka_nieaktywna)
#define UI_RAMKA_OSTRZEZENIA (UI_Paleta()->ramka_ostrzezenia)
#define UI_TEKST_NORMALNY   (UI_Paleta()->tekst_normalny)
#define UI_TEKST_NIEAKTYWNY (UI_Paleta()->tekst_nieaktywny)
#define UI_TEKST_NAGLOWKA   (UI_Paleta()->tekst_naglowka)
#define UI_IKONA_KOLOR      (UI_Paleta()->ikona_kolor)
#define UI_IKONA_AKCENT     (UI_Paleta()->ikona_akcent)
#define UI_IKONA_DODATKOWY  (UI_Paleta()->ikona_dodatkowy)

static uint32_t UI_DobierzFont(uint32_t font, const char *tekst, uint16_t szerokosc)
{
    const uint32_t margines = 10U;

    if (tekst == 0)
        return font;

    if (FONT_GetStrPixelWidth(font, tekst) + margines <= szerokosc)
        return font;

    /*
     * BŁĄD ZNALEZIONY PRZY PRZEGLĄDZIE: FONT_KAFEL (PT Serif Bold) jest przy
     * tych samych napisach nawet o 60% szerszy niż FONT_FRAN - np. "Sterowanie
     * PTT" to 125 px zamiast 80 px. UI_ROLA_TEKSTU_PRZYCISK jest używana w
     * ponad 100 miejscach w całym firmware z przyciskami o rozmiarach
     * dobranych pod wąski FONT_FRAN. Bez tego warunku część z nich zaczęłaby
     * zawijać tekst do dwóch linii (albo się przycinać) tam, gdzie wcześniej
     * mieścił się w jednej. Stosujemy dokładnie ten sam, już sprawdzony
     * mechanizm co dla FONT_FRANBIG: jeśli nie mieści się, spadamy do
     * FONT_FRAN zamiast łamać układ ekranu.
     */
    if (font == FONT_FRANBIG || font == FONT_KAFEL)
        return FONT_FRAN;

    return font;
}

static uint32_t UI_DobierzFontDoPola(uint32_t font, const char *tekst,
                                     uint16_t szerokosc, uint16_t wysokosc)
{
    font = UI_DobierzFont(font, tekst, szerokosc);

    /*
     * Sama kontrola szerokości nie wystarcza. FONT_FRANBIG ma 32 px
     * wysokości, a wiele pól informacyjnych ma tylko 40 px razem z etykietą.
     * Jeżeli czcionka nie mieści się pionowo, przechodzimy na FONT_FRAN
     * zanim tekst zostanie narysowany. Zapobiega to nachodzeniu wartości na
     * ramkę i sąsiedni wiersz w całym firmware, nie tylko w ekranach anten.
     */
    if (FONT_GetHeight(font) > wysokosc && font == FONT_FRANBIG)
        font = FONT_FRAN;

    return font;
}

static void UI_RysujTekstWycentrowany(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                                      const char *tekst, uint32_t font, LCDColor kolor, LCDColor tlo)
{
    int px;
    int py;
    int w;
    int h;

    if (tekst == 0)
        tekst = "";

    font = UI_DobierzFontDoPola(font, tekst, szerokosc, wysokosc);
    w = (int)FONT_GetStrPixelWidth(font, tekst);
    h = (int)FONT_GetHeight(font);
    px = (int)x + ((int)szerokosc - w) / 2;
    py = (int)y + ((int)wysokosc - h) / 2;

    if (px < (int)x + 2)
        px = (int)x + 2;
    if (py < (int)y + 1)
        py = (int)y + 1;

    FONT_Write(font, kolor, tlo, (uint32_t)px, (uint32_t)py, tekst);
}

typedef struct
{
    char linia1[96];
    char linia2[96];
    uint8_t liczba_linii;
} UI_UKLAD_TEKSTU_t;

static uint8_t UI_CzySeparatorTekstu(char znak)
{
    return (uint8_t)(znak == ' ' || znak == '/' || znak == '-' || znak == '_' || znak == '\n');
}

static uint8_t UI_CzyPoczatekUTF8(const char *tekst, size_t indeks)
{
    const unsigned char b = (unsigned char)tekst[indeks];
    return (uint8_t)((b & 0xC0U) != 0x80U);
}

static size_t UI_UTF8_BezpiecznaGranica(const char *tekst, size_t maksimum)
{
    size_t dlugosc;

    if (tekst == 0)
        return 0U;

    dlugosc = strlen(tekst);
    if (maksimum > dlugosc)
        maksimum = dlugosc;

    while (maksimum > 0U && maksimum < dlugosc && !UI_CzyPoczatekUTF8(tekst, maksimum))
        --maksimum;
    return maksimum;
}

static void UI_KopiujUTF8(char *cel, size_t rozmiar, const char *zrodlo)
{
    size_t ile;

    if (cel == 0 || rozmiar == 0U)
        return;
    cel[0] = '\0';
    if (zrodlo == 0)
        return;

    ile = UI_UTF8_BezpiecznaGranica(zrodlo, rozmiar - 1U);
    memcpy(cel, zrodlo, ile);
    cel[ile] = '\0';
}

static size_t UI_PoprzedniaGranicaUTF8(const char *tekst, size_t indeks)
{
    if (tekst == 0 || indeks == 0U)
        return 0U;

    --indeks;
    while (indeks > 0U && (((unsigned char)tekst[indeks] & 0xC0U) == 0x80U))
        --indeks;
    return indeks;
}

static void UI_SkrocTekstDoSzerokosci(const char *tekst, uint32_t font,
                                      uint16_t szerokosc, char *bufor, size_t rozmiar_bufora)
{
    size_t dlugosc;

    if (bufor == 0 || rozmiar_bufora == 0U)
        return;
    bufor[0] = '\0';
    if (tekst == 0)
        return;

    UI_KopiujUTF8(bufor, rozmiar_bufora, tekst);
    if (FONT_GetStrPixelWidth(font, bufor) <= szerokosc)
        return;

    dlugosc = strlen(bufor);
    while (dlugosc > 0U)
    {
        size_t nowa_dlugosc = UI_PoprzedniaGranicaUTF8(bufor, dlugosc);
        bufor[nowa_dlugosc] = '\0';
        dlugosc = nowa_dlugosc;

        if (dlugosc + 4U < rozmiar_bufora)
        {
            char proba[96];
            size_t ile = dlugosc;
            if (ile > sizeof(proba) - 4U)
                ile = sizeof(proba) - 4U;
            memcpy(proba, bufor, ile);
            memcpy(proba + ile, "...", 4U);
            if (FONT_GetStrPixelWidth(font, proba) <= szerokosc)
            {
                UI_KopiujUTF8(bufor, rozmiar_bufora, proba);
                return;
            }
        }
    }

    if (rozmiar_bufora >= 4U)
        snprintf(bufor, rozmiar_bufora, "...");
}

static void UI_PrzytnijTekst(char *tekst)
{
    size_t dlugosc;

    if (tekst == 0)
        return;

    dlugosc = strlen(tekst);
    while (dlugosc > 0U && tekst[dlugosc - 1U] == ' ')
    {
        tekst[dlugosc - 1U] = '\0';
        --dlugosc;
    }
}

static void UI_ZbudujUkladTekstuDwuwierszowego(const char *tekst, uint32_t font,
                                               uint16_t maks_szerokosc, UI_UKLAD_TEKSTU_t *uklad)
{
    size_t dlugosc;
    size_t i;
    size_t polowa;
    size_t najlepszy = 0U;
    uint32_t najlepszy_wynik = 0xFFFFFFFFUL;
    char lewa[96];
    char prawa[96];

    if (uklad == 0)
        return;

    memset(uklad, 0, sizeof(*uklad));
    if (tekst == 0)
        tekst = "";

    /* Jawny znak nowej linii ma pierwszeństwo przed automatycznym łamaniem. */
    {
        const char *nowa_linia = strchr(tekst, '\n');
        if (nowa_linia != 0)
        {
            size_t pierwsza = (size_t)(nowa_linia - tekst);
            if (pierwsza >= sizeof(uklad->linia1))
                pierwsza = UI_UTF8_BezpiecznaGranica(tekst, sizeof(uklad->linia1) - 1U);
            memcpy(uklad->linia1, tekst, pierwsza);
            uklad->linia1[pierwsza] = '\0';
            UI_PrzytnijTekst(uklad->linia1);
            UI_KopiujUTF8(uklad->linia2, sizeof(uklad->linia2), nowa_linia + 1);
            UI_PrzytnijTekst(uklad->linia2);
            uklad->liczba_linii = uklad->linia2[0] != '\0' ? 2U : 1U;
            return;
        }
    }

    dlugosc = strlen(tekst);
    if (dlugosc >= sizeof(uklad->linia1))
        dlugosc = UI_UTF8_BezpiecznaGranica(tekst, sizeof(uklad->linia1) - 1U);

    memcpy(uklad->linia1, tekst, dlugosc);
    uklad->linia1[dlugosc] = '\0';
    UI_PrzytnijTekst(uklad->linia1);
    uklad->liczba_linii = 1U;

    if (FONT_GetStrPixelWidth(font, uklad->linia1) <= maks_szerokosc)
        return;

    polowa = dlugosc / 2U;
    for (i = 1U; i < dlugosc; ++i)
    {
        uint32_t w1;
        uint32_t w2;
        uint32_t kara;
        uint32_t odleglosc;
        uint32_t wynik;
        size_t start_prawej;

        if (!UI_CzyPoczatekUTF8(tekst, i))
            continue;
        if (!UI_CzySeparatorTekstu(tekst[i - 1U]) && !UI_CzySeparatorTekstu(tekst[i]))
            continue;

        memcpy(lewa, tekst, i);
        lewa[i] = '\0';
        UI_PrzytnijTekst(lewa);
        start_prawej = i;
        while (start_prawej < dlugosc && tekst[start_prawej] == ' ')
            ++start_prawej;
        UI_KopiujUTF8(prawa, sizeof(prawa), tekst + start_prawej);
        UI_PrzytnijTekst(prawa);
        if (lewa[0] == '\0' || prawa[0] == '\0')
            continue;

        w1 = FONT_GetStrPixelWidth(font, lewa);
        w2 = FONT_GetStrPixelWidth(font, prawa);
        kara = 0U;
        if (w1 > maks_szerokosc)
            kara += (w1 - maks_szerokosc) * 8U;
        if (w2 > maks_szerokosc)
            kara += (w2 - maks_szerokosc) * 8U;
        odleglosc = (i > polowa) ? (uint32_t)(i - polowa) : (uint32_t)(polowa - i);
        wynik = kara + odleglosc;
        if (wynik < najlepszy_wynik)
        {
            najlepszy_wynik = wynik;
            najlepszy = i;
        }
    }

    if (najlepszy == 0U)
    {
        size_t ostatni_dobry = 0U;
        for (i = 1U; i < dlugosc; ++i)
        {
            if (!UI_CzyPoczatekUTF8(tekst, i))
                continue;
            memcpy(lewa, tekst, i);
            lewa[i] = '\0';
            UI_PrzytnijTekst(lewa);
            if (FONT_GetStrPixelWidth(font, lewa) <= maks_szerokosc)
                ostatni_dobry = i;
            else
                break;
        }
        najlepszy = (ostatni_dobry > 0U) ? ostatni_dobry : UI_UTF8_BezpiecznaGranica(tekst, dlugosc / 2U);
        if (najlepszy == 0U)
            najlepszy = UI_UTF8_BezpiecznaGranica(tekst, dlugosc);
    }

    memcpy(uklad->linia1, tekst, najlepszy);
    uklad->linia1[najlepszy] = '\0';
    UI_PrzytnijTekst(uklad->linia1);
    while (najlepszy < dlugosc && tekst[najlepszy] == ' ')
        ++najlepszy;
    UI_KopiujUTF8(uklad->linia2, sizeof(uklad->linia2), tekst + najlepszy);
    UI_PrzytnijTekst(uklad->linia2);
    if (uklad->linia2[0] != '\0')
        uklad->liczba_linii = 2U;
}

static void UI_RysujTekstWielowierszowy(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                                        const char *tekst, uint32_t font, LCDColor kolor, LCDColor tlo)
{
    UI_UKLAD_TEKSTU_t uklad;
    uint16_t wysokosc_wiersza;
    int start_y;

    if (tekst == 0)
        tekst = "";

    font = UI_DobierzFontDoPola(font, tekst, szerokosc, wysokosc);
    UI_ZbudujUkladTekstuDwuwierszowego(tekst, font, (uint16_t)(szerokosc > 10U ? szerokosc - 10U : szerokosc), &uklad);

    if (uklad.liczba_linii <= 1U)
    {
        UI_RysujTekstWycentrowany(x, y, szerokosc, wysokosc, uklad.linia1, font, kolor, tlo);
        return;
    }

    wysokosc_wiersza = FONT_GetHeight(font);
    if ((uint16_t)(2U * wysokosc_wiersza) > wysokosc && font == FONT_FRANBIG)
    {
        font = FONT_FRAN;
        UI_ZbudujUkladTekstuDwuwierszowego(tekst, font, (uint16_t)(szerokosc > 10U ? szerokosc - 10U : szerokosc), &uklad);
        wysokosc_wiersza = FONT_GetHeight(font);
    }

    start_y = (int)y + ((int)wysokosc - (int)(2U * wysokosc_wiersza)) / 2;
    if (start_y < (int)y)
        start_y = (int)y;

    UI_RysujTekstWycentrowany(x, (uint16_t)start_y, szerokosc, wysokosc_wiersza,
                              uklad.linia1, font, kolor, tlo);
    UI_RysujTekstWycentrowany(x, (uint16_t)(start_y + wysokosc_wiersza), szerokosc, wysokosc_wiersza,
                              uklad.linia2, font, kolor, tlo);
}

static void UI_RysujEtykieteKafla(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                                  const char *tekst, LCDColor kolor, LCDColor tlo)
{
    /*
     * FONT_KAFEL (PT Serif Bold, wygenerowany z gotowego kroju OFL) zamiast
     * dotychczasowego FONT_FRAN - żeby podpisy pod ikonami wyglądały spójnie
     * z pogrubionym, szeryfowym stylem zatwierdzonej makiety. Wysokość komórki
     * (16 px) jest identyczna jak we FONT_FRAN, więc pasek etykiety nie
     * wymaga przebudowy geometrii.
     */
    UI_RysujTekstWielowierszowy(x, y, szerokosc, wysokosc, tekst, FONT_KAFEL, kolor, tlo);
}

/*
 * Wspolny pasek podpisu kafla.
 *
 * Wczesniej menu glowne mialo pionowy znacznik przy nazwie dzialu, a zwykle
 * kafle i kafle podmenu rysowaly ten sam obszar inaczej. Drobna roznica byla
 * bardzo widoczna na rzeczywistym LCD. Od UIFIX2 caly pasek podpisu — tlo,
 * separator, pionowy akcent i geometria tekstu — powstaje w jednym miejscu.
 */
static void UI_RysujPodpisKafla2026(uint16_t x, uint16_t y,
                                    uint16_t szerokosc, uint16_t wysokosc,
                                    uint16_t etykieta_h, const char *tekst,
                                    LCDColor etykieta_tlo, LCDColor ramka,
                                    LCDColor akcent, LCDColor kolor_tekstu)
{
    uint16_t y_etykiety;
    uint16_t y_znacznika_gora;
    uint16_t y_znacznika_dol;

    if (szerokosc < 18U || wysokosc < etykieta_h + 4U || etykieta_h < 12U)
        return;

    y_etykiety = (uint16_t)(y + wysokosc - etykieta_h - 1U);

    LCD_FillRect(LCD_MakePoint((uint16_t)(x + 2U), y_etykiety),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 3U),
                               (uint16_t)(y + wysokosc - 3U)), etykieta_tlo);
    LCD_HLine(LCD_MakePoint((uint16_t)(x + 2U), (uint16_t)(y_etykiety - 1U)),
              (uint16_t)(szerokosc - 4U), ramka);

    /*
     * Znacznik ma zawsze 3 px szerokosci i jest odsunięty o tyle samo od
     * ramki. Przy niskim pasku skracamy go symetrycznie, zamiast usuwac.
     */
    if (UI_EfektywnyZestawIkon() == 0U)
    {
        /* Retro: wycentrowana tabliczka znamionowa z mosiężnymi nitami. */
        LCD_Rectangle(LCD_MakePoint((uint16_t)(x + 4U), (uint16_t)(y_etykiety + 2U)),
                      LCD_MakePoint((uint16_t)(x + szerokosc - 5U),
                                    (uint16_t)(y + wysokosc - 5U)), akcent);
        LCD_FillCircle(LCD_MakePoint((uint16_t)(x + 8U), (uint16_t)(y_etykiety + etykieta_h / 2U)), 1U, akcent);
        LCD_FillCircle(LCD_MakePoint((uint16_t)(x + szerokosc - 9U), (uint16_t)(y_etykiety + etykieta_h / 2U)), 1U, akcent);
        UI_RysujEtykieteKafla((uint16_t)(x + 12U), y_etykiety,
                              (uint16_t)(szerokosc - 24U),
                              (uint16_t)(etykieta_h - 2U),
                              tekst, kolor_tekstu, etykieta_tlo);
    }
    else
    {
        y_znacznika_gora = (uint16_t)(y_etykiety + 5U);
        y_znacznika_dol = (uint16_t)(y + wysokosc - 9U);
        if (y_znacznika_dol < y_znacznika_gora)
            y_znacznika_dol = y_znacznika_gora;
        LCD_FillRect(LCD_MakePoint((uint16_t)(x + 4U), y_znacznika_gora),
                     LCD_MakePoint((uint16_t)(x + 6U), y_znacznika_dol), akcent);
        UI_RysujEtykieteKafla((uint16_t)(x + 9U), y_etykiety,
                              (uint16_t)(szerokosc - 13U),
                              (uint16_t)(etykieta_h - 2U),
                              tekst, kolor_tekstu, etykieta_tlo);
    }
}

static void UI_RysujRogiRetro(uint16_t x, uint16_t y, uint16_t w, uint16_t h, LCDColor kolor);

static void UI_RysujOzdobePaskaRetro(void)
{
    const LCDColor zloto = UI_RAMKA_NORMALNA;
    if (UI_EfektywnyZestawIkon() != 0U)
        return;

    LCD_HLine(LCD_MakePoint(2U, 1U), 476U, zloto);
    LCD_HLine(LCD_MakePoint(2U, UI_WYSOKOSC_PASKA_STANU - 3U), 476U, zloto);
    LCD_Circle(LCD_MakePoint(7U, 6U), 3U, zloto);
    LCD_Circle(LCD_MakePoint(472U, 6U), 3U, zloto);
    LCD_HLine(LCD_MakePoint(12U, 5U), 12U, zloto);
    LCD_HLine(LCD_MakePoint(456U, 5U), 12U, zloto);
}

static void UI_RysujBaterie(uint16_t x, uint16_t y, int procent, bool obecna)
{
    const uint16_t w = 39U;
    const uint16_t h = 16U;
    const bool klasyczny = UI_EfektywnyZestawIkon() == 1U;
    const LCDColor tlo = klasyczny ? LCD_RGB(0, 20, 65) : LCD_RGB(20, 15, 7);
    LCDColor kolor;
    int p = procent;
    uint8_t segmenty;
    uint8_t i;

    if (p < 0) p = 0;
    if (p > 100) p = 100;

    if (!obecna)
        kolor = klasyczny ? LCD_RGB(85, 125, 160) : LCD_RGB(95, 72, 34);
    else if (p <= 20)
        kolor = LCD_RGB(255, 72, 66);
    else if (klasyczny)
        kolor = p <= 50 ? LCD_RGB(255, 210, 90) : LCD_RGB(90, 255, 150);
    else
        kolor = p <= 50 ? LCD_RGB(214, 150, 48) : LCD_RGB(232, 190, 92);

    /* Retro udaje mosiężny wskaźnik z tablicy przyrządu, klasyczny pozostaje
     * prostym i bardzo kontrastowym wskaźnikiem segmentowym. */
    LCD_Rectangle(LCD_MakePoint(x, y), LCD_MakePoint(x + w, y + h), kolor);
    if (!klasyczny)
        LCD_Rectangle(LCD_MakePoint(x + 1U, y + 1U), LCD_MakePoint(x + w - 1U, y + h - 1U), LCD_RGB(116, 78, 30));
    LCD_FillRect(LCD_MakePoint(x + w + 1U, y + 5U), LCD_MakePoint(x + w + 4U, y + 11U), kolor);
    LCD_FillRect(LCD_MakePoint(x + 2U, y + 2U), LCD_MakePoint(x + w - 2U, y + h - 2U), tlo);

    segmenty = obecna ? (uint8_t)((p + 24) / 25) : 0U;
    if (segmenty > 4U) segmenty = 4U;
    for (i = 0U; i < 4U; ++i)
    {
        const uint16_t sx = (uint16_t)(x + 4U + i * 8U);
        LCD_Rectangle(LCD_MakePoint(sx, y + 4U), LCD_MakePoint(sx + 5U, y + 12U), kolor);
        if (i < segmenty)
            LCD_FillRect(LCD_MakePoint(sx + 1U, y + 5U), LCD_MakePoint(sx + 4U, y + 11U), kolor);
    }
    if (!klasyczny)
    {
        LCD_FillCircle(LCD_MakePoint(x + 2U, y + 2U), 1U, kolor);
        LCD_FillCircle(LCD_MakePoint(x + w - 2U, y + 2U), 1U, kolor);
    }
}

static void UI_RysujIkoneSD(uint16_t x, uint16_t y, bool obecna)
{
    const bool klasyczny = UI_EfektywnyZestawIkon() == 1U;
    LCDColor k = obecna
        ? (klasyczny ? LCD_RGB(100, 255, 170) : LCD_RGB(225, 184, 82))
        : (klasyczny ? LCD_RGB(80, 125, 165) : LCD_RGB(95, 72, 34));

    LCD_Line(LCD_MakePoint(x + 3, y), LCD_MakePoint(x + 14, y), k);
    LCD_Line(LCD_MakePoint(x + 14, y), LCD_MakePoint(x + 18, y + 4), k);
    LCD_Line(LCD_MakePoint(x + 18, y + 4), LCD_MakePoint(x + 18, y + 17), k);
    LCD_Line(LCD_MakePoint(x + 18, y + 17), LCD_MakePoint(x, y + 17), k);
    LCD_Line(LCD_MakePoint(x, y + 17), LCD_MakePoint(x, y + 3), k);
    LCD_Line(LCD_MakePoint(x, y + 3), LCD_MakePoint(x + 3, y), k);
    LCD_VLine(LCD_MakePoint(x + 4, y + 3), 5, k);
    LCD_VLine(LCD_MakePoint(x + 8, y + 3), 5, k);
    LCD_VLine(LCD_MakePoint(x + 12, y + 3), 5, k);
    if (!klasyczny)
        LCD_HLine(LCD_MakePoint(x + 3U, y + 14U), 12U, LCD_RGB(116, 78, 30));
}

static void UI_RysujIkoneZegara(uint16_t x, uint16_t y, bool rtc)
{
    const bool klasyczny = UI_EfektywnyZestawIkon() == 1U;
    LCDColor k = rtc
        ? (klasyczny ? LCD_RGB(110, 255, 180) : LCD_RGB(232, 190, 92))
        : LCD_RGB(255, 100, 75);

    if (!klasyczny)
    {
        /* Kieszonkowy zegar: korona, uchwyt i podwójna koperta. */
        LCD_Circle(LCD_MakePoint(x + 8, y + 9), 7, k);
        LCD_Circle(LCD_MakePoint(x + 8, y + 9), 6, LCD_RGB(116, 78, 30));
        LCD_Rectangle(LCD_MakePoint(x + 6U, y), LCD_MakePoint(x + 10U, y + 2U), k);
        LCD_Circle(LCD_MakePoint(x + 8U, y + 1U), 2U, k);
    }
    else
        LCD_Circle(LCD_MakePoint(x + 8, y + 8), 7, k);

    LCD_Line(LCD_MakePoint(x + 8, y + 8), LCD_MakePoint(x + 8, y + 4), k);
    LCD_Line(LCD_MakePoint(x + 8, y + 8), LCD_MakePoint(x + 12, y + 10), k);
}

static void UI_RysujIkoneRF(uint16_t x, uint16_t y, bool aktywne)
{
    const bool klasyczny = UI_EfektywnyZestawIkon() == 1U;
    LCDColor k = aktywne
        ? (klasyczny ? LCD_RGB(125, 235, 255) : LCD_RGB(232, 190, 92))
        : (klasyczny ? LCD_RGB(70, 105, 145) : LCD_RGB(95, 72, 34));
    LCD_FillCircle(LCD_MakePoint(x + 4, y + 8), 3, k);
    if (aktywne)
    {
        LCD_DrawArc(x + 4, y + 8, 7, -55.0f, 55.0f, k);
        LCD_DrawArc(x + 4, y + 8, 11, -55.0f, 55.0f, k);
    }
}

static void UI_RysujIkoneZastepcza(int x, int y, int w, int h);






static uint8_t UI_AktualnyZestawIkon(void)
{
    return UI_EfektywnyZestawIkon();
}





/* -------------------------------------------------------------------------
 * ANTICON1 - małe schematyczne ikony anten.
 *
 * Są celowo rysowane prostymi liniami zamiast bitmap. Dzięki temu zachowują
 * kształt w obu zestawach kolorystycznych i można łatwo poprawić pojedynczy
 * wymiar bez przebudowy zasobów graficznych. Pole robocze ma około 70 x 30 px.
 * ------------------------------------------------------------------------- */










static uint16_t UI_CzytajLE16(const uint8_t *dane)
{
    return (uint16_t)dane[0] | (uint16_t)((uint16_t)dane[1] << 8);
}

static uint32_t UI_CzytajLE32(const uint8_t *dane)
{
    return (uint32_t)dane[0] | ((uint32_t)dane[1] << 8) |
           ((uint32_t)dane[2] << 16) | ((uint32_t)dane[3] << 24);
}

static bool UI_RysujBitmapeBMP4Wypelnij(const uint8_t *bmp, uint32_t rozmiar,
                                        int x, int y, int w, int h)
{
    uint32_t offset;
    uint32_t szer;
    uint32_t wys;
    uint16_t bpp;
    uint32_t kompresja;
    uint32_t skok_wiersza;
    uint32_t paleta_ofs;
    uint32_t paleta_kolorow;
    int32_t crop_x = 0;
    int32_t crop_y = 0;
    uint32_t crop_w;
    uint32_t crop_h;
    int dx;
    int dy;

    if (bmp == 0 || rozmiar < 118U || w <= 0 || h <= 0)
        return false;
    if (bmp[0] != 'B' || bmp[1] != 'M')
        return false;

    offset = UI_CzytajLE32(bmp + 10);
    szer = UI_CzytajLE32(bmp + 18);
    wys = UI_CzytajLE32(bmp + 22);
    bpp = UI_CzytajLE16(bmp + 28);
    kompresja = UI_CzytajLE32(bmp + 30);
    paleta_ofs = 14U + UI_CzytajLE32(bmp + 14);
    paleta_kolorow = UI_CzytajLE32(bmp + 46);
    if (paleta_kolorow == 0U)
        paleta_kolorow = 16U;

    if (szer == 0U || wys == 0U || bpp != 4U || kompresja != 0U)
        return false;
    if (paleta_ofs + paleta_kolorow * 4U > rozmiar || offset >= rozmiar)
        return false;

    skok_wiersza = (uint32_t)((((szer * 4U) + 31U) / 32U) * 4U);
    if (offset + skok_wiersza * wys > rozmiar)
        return false;

    crop_w = szer;
    crop_h = wys;

    if ((uint64_t)w * (uint64_t)wys > (uint64_t)h * (uint64_t)szer)
    {
        crop_h = (uint32_t)(((uint64_t)szer * (uint64_t)h) / (uint64_t)w);
        if (crop_h == 0U || crop_h > wys)
            crop_h = wys;
        crop_y = (int32_t)(wys - crop_h) / 2;
    }
    else
    {
        crop_w = (uint32_t)(((uint64_t)wys * (uint64_t)w) / (uint64_t)h);
        if (crop_w == 0U || crop_w > szer)
            crop_w = szer;
        crop_x = (int32_t)(szer - crop_w) / 2;
    }

    for (dy = 0; dy < h; ++dy)
    {
        const uint32_t sy = (uint32_t)crop_y + (uint32_t)(((uint64_t)dy * crop_h) / (uint64_t)h);
        const uint32_t src_y_bmp = wys - 1U - sy;
        const uint8_t *wiersz = bmp + offset + src_y_bmp * skok_wiersza;

        for (dx = 0; dx < w; ++dx)
        {
            const uint32_t sx = (uint32_t)crop_x + (uint32_t)(((uint64_t)dx * crop_w) / (uint64_t)w);
            const uint8_t bajt = wiersz[sx >> 1U];
            const uint8_t indeks = (sx & 1U) == 0U ? (uint8_t)(bajt >> 4U) : (uint8_t)(bajt & 0x0FU);
            const uint8_t *wpis = bmp + paleta_ofs + (uint32_t)indeks * 4U;
            const LCDColor kolor = LCD_RGB(wpis[2], wpis[1], wpis[0]);
            LCD_SetPixel(LCD_MakePoint((uint16_t)(x + dx), (uint16_t)(y + dy)), kolor);
        }
    }
    return true;
}


static bool UI_RysujBitmapeBMP8Wypelnij(const uint8_t *bmp, uint32_t rozmiar,
                                        int x, int y, int w, int h)
{
    uint32_t offset;
    uint32_t szer;
    uint32_t wys;
    uint32_t skok_wiersza;
    uint32_t paleta_ofs;
    uint32_t paleta_kolorow;
    int32_t crop_x = 0;
    int32_t crop_y = 0;
    uint32_t crop_w;
    uint32_t crop_h;
    int dx;
    int dy;

    if (bmp == 0 || rozmiar < 310U || w <= 0 || h <= 0 || bmp[0] != 'B' || bmp[1] != 'M')
        return false;

    offset = UI_CzytajLE32(bmp + 10);
    szer = UI_CzytajLE32(bmp + 18);
    wys = UI_CzytajLE32(bmp + 22);
    if (szer == 0U || wys == 0U || UI_CzytajLE16(bmp + 28) != 8U || UI_CzytajLE32(bmp + 30) != 0U)
        return false;

    paleta_ofs = 14U + UI_CzytajLE32(bmp + 14);
    paleta_kolorow = UI_CzytajLE32(bmp + 46);
    if (paleta_kolorow == 0U)
        paleta_kolorow = 256U;
    if (paleta_kolorow > 256U || paleta_ofs + paleta_kolorow * 4U > rozmiar)
        return false;

    skok_wiersza = (szer + 3U) & ~3U;
    if (offset + skok_wiersza * wys > rozmiar)
        return false;

    crop_w = szer;
    crop_h = wys;
    if ((uint64_t)w * (uint64_t)wys > (uint64_t)h * (uint64_t)szer)
    {
        crop_h = (uint32_t)(((uint64_t)szer * (uint64_t)h) / (uint64_t)w);
        if (crop_h == 0U || crop_h > wys) crop_h = wys;
        crop_y = (int32_t)(wys - crop_h) / 2;
    }
    else
    {
        crop_w = (uint32_t)(((uint64_t)wys * (uint64_t)w) / (uint64_t)h);
        if (crop_w == 0U || crop_w > szer) crop_w = szer;
        crop_x = (int32_t)(szer - crop_w) / 2;
    }

    for (dy = 0; dy < h; ++dy)
    {
        const uint32_t sy = (uint32_t)crop_y + (uint32_t)(((uint64_t)dy * crop_h) / (uint64_t)h);
        const uint32_t src_y_bmp = wys - 1U - sy;
        const uint8_t *wiersz = bmp + offset + src_y_bmp * skok_wiersza;
        for (dx = 0; dx < w; ++dx)
        {
            const uint32_t sx = (uint32_t)crop_x + (uint32_t)(((uint64_t)dx * crop_w) / (uint64_t)w);
            const uint8_t indeks = wiersz[sx];
            const uint8_t *wpis;
            LCDColor kolor;
            if ((uint32_t)indeks >= paleta_kolorow)
                continue;
            wpis = bmp + paleta_ofs + (uint32_t)indeks * 4U;
            kolor = LCD_RGB(wpis[2], wpis[1], wpis[0]);
            LCD_SetPixel(LCD_MakePoint((uint16_t)(x + dx), (uint16_t)(y + dy)), kolor);
        }
    }
    return true;
}


static const UI_IKONA_SZCZEGOLOWA_t *UI_PobierzIkoneSzczegolowa(UI_IKONA_MENU_t ikona, uint8_t zestaw)
{
    const UI_IKONA_SZCZEGOLOWA_t *zrodlo = ui_ikony_szczegolowe_retro;
    const uint32_t indeks = (uint32_t)ikona;
    (void)zestaw;

    /*
     * Najpierw sprawdzamy nowe bitmapy Retro. Tablica jest rzadka, więc
     * funkcje bez nowej grafiki przechodzą bez kosztu do dotychczasowego
     * zestawu szczegółowego albo do kompletnej ikony 70 x 30.
     */
    if (indeks < UI_IKONY_SZCZEGOLOWE_NOWE_LICZBA &&
        ui_ikony_szczegolowe_nowe[indeks].dane != 0 &&
        ui_ikony_szczegolowe_nowe[indeks].rozmiar != 0U)
    {
        return &ui_ikony_szczegolowe_nowe[indeks];
    }

    switch (ikona)
    {
    case UI_IKONA_POJEDYNCZY:
    case UI_IKONA_DZIAL_POMIAR:
        return &zrodlo[UI_IKONA_SZCZEG_POMIAR];
    case UI_IKONA_DZIAL_ANALIZA:
        return &zrodlo[UI_IKONA_SZCZEG_ANALIZA];
    case UI_IKONA_DZIAL_NARZEDZIA:
        return &zrodlo[UI_IKONA_SZCZEG_NARZEDZIA];
    case UI_IKONA_USTAWIENIA:
    case UI_IKONA_DZIAL_USTAWIENIA:
        return &zrodlo[UI_IKONA_SZCZEG_USTAWIENIA];
    case UI_IKONA_ZRZUTY:
        return &zrodlo[UI_IKONA_SZCZEG_PLIKI];
    default:
        return 0;
    }
}

static bool UI_RysujIkoneSzczegolowa(UI_IKONA_MENU_t ikona, uint8_t zestaw,
                                     int x, int y, int w, int h)
{
    const UI_IKONA_SZCZEGOLOWA_t *bitmapa = UI_PobierzIkoneSzczegolowa(ikona, zestaw);

    if (bitmapa == 0 || bitmapa->dane == 0 || bitmapa->rozmiar == 0U)
        return false;

    /*
     * Te bitmapy pochodzą z zatwierdzonej makiety: mają już właściwe tło,
     * kreskę i kontrast dla obu wariantów, więc nie przepuszczamy ich przez
     * dodatkowe przemalowanie. Dzięki temu aparat i główne działy wyglądają
     * na urządzeniu tak samo jak na zaakceptowanym wzorze.
     */
    if (UI_CzytajLE16(bitmapa->dane + 28) == 8U)
        return UI_RysujBitmapeBMP8Wypelnij(bitmapa->dane, bitmapa->rozmiar, x, y, w, h);
    return UI_RysujBitmapeBMP4Wypelnij(bitmapa->dane, bitmapa->rozmiar, x, y, w, h);
}

static uint16_t UI_DobierzWysokoscPodpisuKafla(uint16_t szerokosc, const char *tekst,
                                               uint16_t jednowiersz, uint16_t dwuwiersz)
{
    uint16_t dostepna;
    int szer_tekstu;

    if (tekst == 0 || tekst[0] == '\0')
        return jednowiersz;

    dostepna = szerokosc > 16U ? (uint16_t)(szerokosc - 16U) : szerokosc;
    /*
     * Mierzymy tym samym fontem, którym UI_RysujEtykieteKafla faktycznie
     * rysuje podpis (FONT_KAFEL) - inaczej decyzja jedna/dwie linie byłaby
     * podjęta na podstawie metryk innego kroju i mogłaby się nie zgadzać
     * z rzeczywistą szerokością wyrenderowanego tekstu.
     */
    szer_tekstu = FONT_GetStrPixelWidth(FONT_KAFEL, tekst);
    return (szer_tekstu <= (int)dostepna) ? jednowiersz : dwuwiersz;
}


static bool __attribute__((unused)) UI_RysujIkoneMotywu(UI_IKONA_MENU_t ikona, uint8_t motyw,
                                int x, int y, int w, int h)
{
    const UI_IKONA_BITMAP_t *bitmapa;

    if (motyw >= UI_IKONA_BITMAP_MOTYWY)
        return false;
    if ((uint32_t)ikona >= UI_IKONA_BITMAP_LICZBA)
        return false;

    bitmapa = &ui_ikony_bitmap[motyw][ikona];
    if (bitmapa->dane == 0 || bitmapa->rozmiar == 0U)
        return false;

    return UI_RysujBitmapeBMP4Wypelnij(bitmapa->dane, bitmapa->rozmiar, x, y, w, h);
}

static bool UI_RysujIkoneBitmapZestawu(UI_IKONA_MENU_t ikona, uint8_t zestaw,
                                       int x, int y, int w, int h,
                                       LCDColor tlo, LCDColor kolor_symbolu)
{
    /*
     * Jeden styl, dwa poziomy jakosci. Najpierw szukamy bitmapy szczegolowej
     * 136 x 50; jesli dana funkcja jej jeszcze nie ma, rysujemy podstawowa
     * 70 x 30. Zestaw 70 x 30 pokrywa komplet 53 pozycji, wiec dalszej
     * sciezki awaryjnej juz nie ma - maski i rysunki wektorowe byly martwym
     * kodem i zostaly usuniete.
     */
    (void)zestaw;
    (void)tlo;
    (void)kolor_symbolu;

    if (UI_RysujIkoneSzczegolowa(ikona, 0U, x, y, w, h))
        return true;

    if ((uint32_t)ikona >= UI_IKONA_BITMAP_LICZBA)
        return false;

    return UI_RysujBitmapeBMP4Wypelnij(ui_ikony_bitmap[0U][ikona].dane,
                                       ui_ikony_bitmap[0U][ikona].rozmiar, x, y, w, h);
}

void UI_RysujIkoneMenuZestawu(UI_IKONA_MENU_t ikona, uint8_t zestaw,
                              uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc)
{
    if (zestaw >= UI_LICZBA_ZESTAWOW_IKON)
        zestaw = UI_AktualnyZestawIkon();
    if (!UI_RysujIkoneBitmapZestawu(ikona, zestaw, (int)x, (int)y,
                                    (int)szerokosc, (int)wysokosc,
                                    UI_TLO_PRZYCISKU, UI_IKONA_KOLOR))
        UI_RysujIkoneZastepcza((int)x, (int)y, (int)szerokosc, (int)wysokosc);
}

static void UI_RysujIkoneZastepcza(int x, int y, int w, int h)
{

    /*
     * Fallback jest celowo neutralny. Nie zawiera nowego projektu ikon,
     * aby błąd brakującej bitmapy był widoczny i nie maskował problemu zasobów.
     * W normalnej pracy nie jest używany, bo kompletny historyczny zestaw
     * ikon jest wbudowany w firmware.
     */
    const char *znak = "?";
    const int szer = FONT_GetStrPixelWidth(FONT_FRANBIG, znak);
    const int wys = (int)FONT_GetHeight(FONT_FRANBIG);
    const int px = x + (w - szer) / 2;
    const int py = y + (h - wys) / 2;
    FONT_Write(FONT_FRANBIG, UI_TEKST_NIEAKTYWNY, UI_TLO_EKRANU,
               (uint16_t)(px < x ? x : px), (uint16_t)(py < y ? y : py), znak);
}

LCDColor UI_KolorTlaEkranu(void)
{
    return UI_TLO_EKRANU;
}

LCDColor UI_KolorTlaPola(void)
{
    return UI_Paleta()->pole_tlo;
}

LCDColor UI_KolorTlaPrzycisku(UI_STYL_t styl)
{
    switch (styl)
    {
    case UI_STYL_AKCENT: return UI_TLO_AKCENTU;
    case UI_STYL_POWROT: return UI_TLO_POWROTU;
    case UI_STYL_AKTYWNY: return UI_TLO_AKTYWNEGO;
    case UI_STYL_NIEAKTYWNY: return UI_TLO_NIEAKTYWNEGO;
    case UI_STYL_OSTRZEZENIE: return UI_TLO_OSTRZEZENIA;
    default: return UI_TLO_PRZYCISKU;
    }
}

LCDColor UI_KolorRamki(UI_STYL_t styl)
{
    switch (styl)
    {
    case UI_STYL_AKCENT: return UI_RAMKA_AKCENTU;
    case UI_STYL_POWROT: return UI_RAMKA_POWROTU;
    case UI_STYL_AKTYWNY: return UI_RAMKA_AKTYWNEGO;
    case UI_STYL_NIEAKTYWNY: return UI_RAMKA_NIEAKTYWNA;
    case UI_STYL_OSTRZEZENIE: return UI_RAMKA_OSTRZEZENIA;
    default: return UI_RAMKA_NORMALNA;
    }
}

LCDColor UI_KolorTekstu(UI_STYL_t styl)
{
    return (styl == UI_STYL_NIEAKTYWNY) ? UI_TEKST_NIEAKTYWNY : UI_TEKST_NORMALNY;
}

void UI_UstawDostawceStatusu(UI_DOSTAWCA_STATUSU_t dostawca)
{
    g_dostawca_statusu = dostawca;
    g_poprzedni_status_wazny = 0U;
}

bool UI_PobierzBiezacyStatus(UI_STATUS_t *status)
{
    if (status == 0 || g_dostawca_statusu == 0)
        return false;

    memset(status, 0, sizeof(*status));
    return g_dostawca_statusu(status);
}

void UI_WyczyscEkran(void)
{
    /* Po wyczyszczeniu ekranu pasek stanu musi zostać narysowany ponownie,
     * nawet gdy jego dane nie zmieniły się od poprzedniej klatki. */
    g_poprzedni_status_wazny = 0U;
    LCD_FillAll(UI_TLO_EKRANU);
    if (UI_EfektywnyZestawIkon() == 0U)
    {
        const LCDColor zloto = UI_RAMKA_NORMALNA;
        const LCDColor braz = UI_Paleta()->kafel_ramka_wewn;
        LCD_Rectangle(LCD_MakePoint(1U, 33U), LCD_MakePoint(478U, 270U), zloto);
        LCD_Rectangle(LCD_MakePoint(3U, 35U), LCD_MakePoint(476U, 268U), braz);
        LCD_FillCircle(LCD_MakePoint(7U, 39U), 1U, zloto);
        LCD_FillCircle(LCD_MakePoint(472U, 39U), 1U, zloto);
        LCD_FillCircle(LCD_MakePoint(7U, 264U), 1U, zloto);
        LCD_FillCircle(LCD_MakePoint(472U, 264U), 1U, zloto);
    }
}

void UI_RysujNaglowek(const char *tytul)
{
    /*
     * Historyczna nazwa funkcji pozostaje dla zgodności ze starszymi
     * modułami, lecz rysowanie przechodzi przez ten sam pasek co nowe ekrany.
     * Dzięki temu zmiana koloru tytułu albo wyglądu statusu obejmuje także
     * stare ekrany bez ręcznego poprawiania każdego z nich.
     */
    UI_RysujPasekGorny(tytul, false, false, 0);
}

#define UI_PASEK_GORNY_OFFSET_Y 3U
#define UI_PASEK_GORNY_TYTUL_OFFSET_Y 3U

void UI_RysujPasekStanu(const UI_STATUS_t *status)
{
    UI_STATUS_CACHE_t biezacy;
    char txt[48];
    char czas_txt[16];
    uint32_t mon = 0;
    LCDColor czas_kolor;
    const LCDColor tlo = UI_Paleta()->pasek_stanu_tlo;

    if (status == 0)
        return;

    memset(&biezacy, 0, sizeof(biezacy));
    if (status->nazwa != 0)
        strncpy(biezacy.nazwa, status->nazwa, sizeof(biezacy.nazwa) - 1U);
    if (status->wersja != 0)
        strncpy(biezacy.wersja, status->wersja, sizeof(biezacy.wersja) - 1U);
    if (status->znak != 0)
        strncpy(biezacy.znak, status->znak, sizeof(biezacy.znak) - 1U);
    biezacy.czas = status->czas;
    biezacy.rtc_obecny = status->rtc_obecny ? 1U : 0U;
    biezacy.karta_sd_obecna = status->karta_sd_obecna ? 1U : 0U;
    biezacy.rf_aktywne = status->rf_aktywne ? 1U : 0U;
    biezacy.bateria_obecna = status->bateria_obecna ? 1U : 0U;
    biezacy.zestaw_ikon = UI_EfektywnyZestawIkon();
    biezacy.procent_baterii = status->procent_baterii;
    biezacy.napiecie_centi_v = (int)(status->napiecie_baterii * 100.0f + 0.5f);

    if (g_poprzedni_status_wazny && memcmp(&g_poprzedni_status, &biezacy, sizeof(biezacy)) == 0)
        return;

    g_poprzedni_status = biezacy;
    g_poprzedni_status_wazny = 1U;

    LCD_FillRect(LCD_MakePoint(0, 0), LCD_MakePoint(479, UI_WYSOKOSC_PASKA_STANU - 1U), tlo);
    LCD_FillRect(LCD_MakePoint(0, UI_WYSOKOSC_PASKA_STANU - 2U),
                 LCD_MakePoint(479, UI_WYSOKOSC_PASKA_STANU - 1U), UI_Paleta()->pasek_stanu_linia);
    UI_RysujOzdobePaskaRetro();

    /*
     * Ekran główny ma teraz własne, spokojniejsze rozłożenie paska.
     * Elementy są lekko opuszczone i od siebie odsunięte, aby lepiej wpisać
     * się w retro-ramkę zamiast wyglądać na stłoczone przy górnej krawędzi.
     */
    /*
     * POPRAWKA PO ZDJĘCIACH: pasek stanu rysował tekst starym FONT_FRAN,
     * podczas gdy podpisy kafli menu głównego mają już FONT_KAFEL - różnica
     * była widoczna na pierwszy rzut oka. Pasek ma 38 px wysokości, więc
     * ten sam co FONT_FRAN 16 px format wysokości mieści się bez zmian
     * geometrii; sprawdziłem też, że tytuł + znak wywoławczy nadal się
     * mieszczą obok siebie przy nowych, szerszych metrykach.
     */
    snprintf(txt, sizeof(txt), "%s %s", status->nazwa ? status->nazwa : "", status->wersja ? status->wersja : "");
    FONT_Write(FONT_KAFEL, UI_TEKST_NAGLOWKA, tlo, 12U, 15U, txt);

    if (status->znak != 0 && status->znak[0] != '\0')
    {
        const uint32_t szerokosc_nazwy = FONT_GetStrPixelWidth(FONT_KAFEL, txt);
        const uint32_t szerokosc_znaku = FONT_GetStrPixelWidth(FONT_KAFEL, status->znak);

        if (szerokosc_nazwy + szerokosc_znaku + 28U <= 286U)
            FONT_Write_RightAlign(FONT_KAFEL, UI_TEKST_NAGLOWKA, tlo, 190U, 15U, 272U, status->znak);
    }

    UI_RysujIkoneZegara(274U, 15U, status->rtc_obecny);
    snprintf(czas_txt, sizeof(czas_txt), "%02lu:%02lu", (unsigned long)(status->czas / 100U), (unsigned long)(status->czas % 100U));
    czas_kolor = status->rtc_obecny ? UI_TEKST_NORMALNY : UI_IKONA_DODATKOWY;
    FONT_Write(FONT_KAFEL, czas_kolor, tlo, 293U, 15U, czas_txt);

    UI_RysujIkoneSD(342U, 15U, status->karta_sd_obecna);
    UI_RysujIkoneRF(369U, 15U, status->rf_aktywne);

    if (status->bateria_obecna)
        snprintf(txt, sizeof(txt), "%.2fV", status->napiecie_baterii);
    else
        strcpy(txt, "--.--V");
    /*
     * POPRAWKA PO ZDJĘCIU: napięcie w FONT_KAFEL potrzebuje 43-44 px, a
     * dostępna szczelina między ikoną RF a ikoną baterii ma tylko 40 px -
     * cyfry nachodziły na baterię. To pole to surowy odczyt liczbowy, nie
     * podpis, więc zostaje przy węższym FONT_FRAN (mieści się z zapasem
     * 7-11 px) zamiast przesuwać sąsiednie ikony i ryzykować kolejne
     * kolizje gdzie indziej na tym samym, ciasno upakowanym pasku.
     */
    FONT_Write(FONT_FRAN, UI_TEKST_NORMALNY, tlo, 386U, 15U, txt);
    UI_RysujBaterie(426U, 15U, status->procent_baterii, status->bateria_obecna);

    (void)mon;
}

void UI_RysujPrzycisk(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                      const char *tekst, UI_STYL_t styl, uint32_t font)
{
    LCDColor tlo = UI_KolorTlaPrzycisku(styl);
    LCDColor ramka = UI_KolorRamki(styl);
    LCDColor tekst_kolor = UI_KolorTekstu(styl);

    if (szerokosc < 4U || wysokosc < 4U)
        return;

    /*
     * POPRAWKA PO ZDJĘCIACH: sporo ekranów (np. Strojenie w measurement.c,
     * generator.c, przyciski markera) rysuje własny dolny pasek wywołując
     * UI_RysujPrzycisk() bezpośrednio, z pominięciem UI_RysujKontrolke()
     * poprawionej wcześniej. Usuwając ozdobne rogi i podbijając font tutaj,
     * w jednym wspólnym miejscu, naprawiamy od razu wszystkie te ekrany,
     * zamiast gonić je pojedynczo.
     */
    LCD_FillRect(LCD_MakePoint(x, y), LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y), LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), ramka);
    LCD_Rectangle(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 2U), (uint16_t)(y + wysokosc - 2U)), UI_Paleta()->kafel_ramka_wewn);

    /*
     * FONT_FRAN -> FONT_KAFEL tylko dla przycisków (każdy wywołujący tę
     * funkcję rysuje właśnie przycisk, więc to bezpieczna, jednoznaczna
     * podmiana). FONT_FRANBIG zostaje bez zmian - to już świadomie duży
     * font używany np. do steperów +/-, nie chodzi tu o niego.
     * UI_RysujTekstWielowierszowy i tak wywołuje UI_DobierzFontDoPola, który
     * sam cofnie się do FONT_FRAN, jeśli KAFEL się nie zmieści - więc żaden
     * przycisk się nie rozjedzie.
     */
    if (font == FONT_FRAN)
        font = FONT_KAFEL;

    UI_RysujTekstWielowierszowy(x, y, szerokosc, wysokosc, tekst, font, tekst_kolor, tlo);
}

void UI_RysujPrzyciskZaznaczony(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                                const char *tekst, UI_STYL_t styl, uint32_t font, uint8_t zaznaczony)
{
    UI_RysujPrzycisk(x, y, szerokosc, wysokosc, tekst, zaznaczony ? UI_STYL_AKTYWNY : styl, font);
}


uint32_t UI_FontDlaRoli(UI_ROLA_TEKSTU_t rola)
{
    switch (rola)
    {
    case UI_ROLA_TEKSTU_TYTUL:
        return FONT_FRANBIG;
    case UI_ROLA_TEKSTU_WARTOSC_GLOWNA:
        return FONT_BDIGITS;
    case UI_ROLA_TEKSTU_WARTOSC:
        return FONT_FRANBIG;
    case UI_ROLA_TEKSTU_PRZYCISK:
        /*
         * Ten sam pogrubiony krój co podpisy kafli menu głównego (FONT_KAFEL).
         * Dotyczy tylko przycisków rysowanych przez UI_RysujKontrolke (pasek
         * akcji na dole ekranów pomiarowych) - reszta przycisków w aplikacji
         * (dialogi, steppery +/-, nawigacja) korzysta wprost z UI_RysujPrzycisk
         * i podaje swój font jawnie, więc ta zmiana ich nie dotyczy.
         */
        return FONT_KAFEL;
    case UI_ROLA_TEKSTU_ETYKIETA:
    case UI_ROLA_TEKSTU_POMOC:
    default:
        return FONT_FRAN;
    }
}

UI_PROSTOKAT_t UI_UtworzObszar(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc)
{
    UI_PROSTOKAT_t obszar = { x, y, szerokosc, wysokosc };
    return obszar;
}

UI_KONTROLKA_t UI_UtworzKontrolke(int16_t id, uint16_t x, uint16_t y,
                                   uint16_t szerokosc, uint16_t wysokosc,
                                   const char *tekst, UI_STYL_t styl,
                                   UI_ROLA_TEKSTU_t rola_tekstu,
                                   bool aktywna, bool zaznaczona)
{
    UI_KONTROLKA_t kontrolka;
    kontrolka.id = id;
    kontrolka.obszar = UI_UtworzObszar(x, y, szerokosc, wysokosc);
    kontrolka.tekst = tekst;
    kontrolka.styl = styl;
    kontrolka.rola_tekstu = rola_tekstu;
    kontrolka.aktywna = aktywna;
    kontrolka.zaznaczona = zaznaczona;
    return kontrolka;
}

bool UI_CzyPunktWObszarze(LCDPoint punkt, const UI_PROSTOKAT_t *obszar)
{
    uint32_t prawy;
    uint32_t dolny;

    if (obszar == 0 || obszar->szerokosc == 0U || obszar->wysokosc == 0U)
        return false;

    prawy = (uint32_t)obszar->x + (uint32_t)obszar->szerokosc;
    dolny = (uint32_t)obszar->y + (uint32_t)obszar->wysokosc;
    return (uint32_t)punkt.x >= obszar->x && (uint32_t)punkt.x < prawy &&
           (uint32_t)punkt.y >= obszar->y && (uint32_t)punkt.y < dolny;
}

static bool UI_CzyKontrolkaWstecz(const UI_KONTROLKA_t *kontrolka)
{
    const char *tekst;
    if (kontrolka == 0 || kontrolka->styl != UI_STYL_POWROT)
        return false;
    tekst = kontrolka->tekst;
    if (tekst == 0)
        return false;

    /*
     * Stare moduły przechowują podpis Wstecz zarówno przez tablicę języka,
     * jak i jako literał. Rozpoznanie jest celowo w warstwie wspólnej, aby
     * obraz i hit-test zawsze używały tej samej geometrii 0,220,70,45.
     */
    if (strcmp(tekst, JEZYK_Tekst(TEKST_WSTECZ)) == 0)
        return true;
    return strcmp(tekst, "Wstecz") == 0 || strcmp(tekst, "Back") == 0 ||
           strcmp(tekst, "Zurück") == 0 || strcmp(tekst, "Zurueck") == 0 ||
           strcmp(tekst, "Назад") == 0 || strcmp(tekst, "Exit") == 0;
}

static UI_PROSTOKAT_t UI_ObszarKontrolkiUjednolicony(const UI_KONTROLKA_t *kontrolka)
{
    if (UI_CzyKontrolkaWstecz(kontrolka))
        return UI_ObszarWsteczDolny();
    return kontrolka != 0 ? kontrolka->obszar : (UI_PROSTOKAT_t){0U, 0U, 0U, 0U};
}

int16_t UI_ZnajdzKontrolke(LCDPoint punkt, const UI_KONTROLKA_t *kontrolki, uint16_t liczba)
{
    uint16_t i;

    if (kontrolki == 0)
        return -1;

    /*
     * Szukamy od końca, bo ostatnia narysowana kontrolka jest wizualnie na
     * wierzchu. Dzięki temu ewentualne nakładanie obszarów nie daje losowego
     * wyniku zależnego od kolejności sprawdzania w module.
     */
    for (i = liczba; i > 0U; --i)
    {
        const UI_KONTROLKA_t *kontrolka = &kontrolki[i - 1U];
        const UI_PROSTOKAT_t obszar = UI_ObszarKontrolkiUjednolicony(kontrolka);
        if (kontrolka->aktywna && UI_CzyPunktWObszarze(punkt, &obszar))
            return kontrolka->id;
    }
    return -1;
}

void UI_RysujKontrolke(const UI_KONTROLKA_t *kontrolka)
{
    UI_STYL_t styl;

    if (kontrolka == 0)
        return;

    {
        const UI_PROSTOKAT_t obszar = UI_ObszarKontrolkiUjednolicony(kontrolka);
        const uint16_t x = obszar.x, y = obszar.y;
        const uint16_t szerokosc = obszar.szerokosc, wysokosc = obszar.wysokosc;
        LCDColor tlo;
        LCDColor ramka;
        LCDColor tekst_kolor;

        styl = kontrolka->aktywna ? kontrolka->styl : UI_STYL_NIEAKTYWNY;
        if (kontrolka->zaznaczona)
            styl = UI_STYL_AKTYWNY;
        tlo = UI_KolorTlaPrzycisku(styl);
        ramka = UI_KolorRamki(styl);
        tekst_kolor = UI_KolorTekstu(styl);

        if (szerokosc < 4U || wysokosc < 4U)
            return;

        /*
         * Pasek akcji (dolne przyciski ekranów pomiarowych) rysujemy prościej
         * niż zwykły UI_RysujPrzycisk: bez zawijasów w rogach (UI_RysujRogiRetro)
         * i większym, pogrubionym FONT_KAFEL zamiast FONT_FRAN - dokładnie tak
         * jak podpisy w menu głównym. Te przyciski są małe i ciasno upakowane
         * w jednym rzędzie, więc ornamenty tylko zjadały miejsce na tekst,
         * a nie dodawały czytelnej wartości.
         */
        LCD_FillRect(LCD_MakePoint(x, y),
                     LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), tlo);
        LCD_Rectangle(LCD_MakePoint(x, y),
                      LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), ramka);
        LCD_Rectangle(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                      LCD_MakePoint((uint16_t)(x + szerokosc - 2U), (uint16_t)(y + wysokosc - 2U)),
                      UI_Paleta()->kafel_ramka_wewn);

        UI_RysujTekstWielowierszowy(x, y, szerokosc, wysokosc, kontrolka->tekst,
                                    UI_FontDlaRoli(kontrolka->rola_tekstu), tekst_kolor, tlo);
    }
}

void UI_RysujKontrolki(const UI_KONTROLKA_t *kontrolki, uint16_t liczba)
{
    uint16_t i;
    if (kontrolki == 0)
        return;
    for (i = 0U; i < liczba; ++i)
        UI_RysujKontrolke(&kontrolki[i]);
}


#define UI_PASEK_AKCJI_MARGINES 4U
#define UI_PASEK_AKCJI_ODSTEP   4U

static UI_PROSTOKAT_t UI_ObszarAkcjiPaska(uint8_t indeks, uint8_t liczba,
                                          uint16_t y, uint16_t wysokosc)
{
    UI_PROSTOKAT_t obszar = {0U, y, 0U, wysokosc};
    uint16_t dostepna;
    uint16_t szerokosc;
    uint16_t x;

    if (liczba == 0U || indeks >= liczba)
        return obszar;

    dostepna = (uint16_t)(480U - 2U * UI_PASEK_AKCJI_MARGINES -
                          (uint16_t)(liczba - 1U) * UI_PASEK_AKCJI_ODSTEP);
    szerokosc = (uint16_t)(dostepna / liczba);
    x = (uint16_t)(UI_PASEK_AKCJI_MARGINES +
                   indeks * (szerokosc + UI_PASEK_AKCJI_ODSTEP));
    obszar.x = x;
    obszar.szerokosc = (indeks == liczba - 1U)
                           ? (uint16_t)(480U - UI_PASEK_AKCJI_MARGINES - x)
                           : szerokosc;
    return obszar;
}

/*
 * Dolne paski akcji mają jedną wspólną geometrię wyjścia. Jeżeli na pasku
 * występuje akcja o roli wizualnej POWRÓT, dostaje dokładnie ten sam duży
 * obszar co wygodny przycisk ze Strojenia: 0,220,70,45. Pozostałe akcje
 * dzielą miejsce od x=74 do prawej krawędzi. Dzięki temu Wstecz nie zmienia
 * położenia zależnie od modułu, liczby przycisków ani historycznej geometrii.
 *
 * Dotyczy to także krótkiego „Anuluj” w ekranach potwierdzeń: jest to ta sama
 * bezpieczna droga wyjścia i powinna być równie łatwa do trafienia.
 */
static int16_t UI_ZnajdzAkcjePowrotu(const UI_AKCJA_t *akcje, uint8_t liczba)
{
    uint8_t i;
    if (akcje == 0)
        return -1;
    for (i = 0U; i < liczba; ++i)
    {
        if (akcje[i].styl == UI_STYL_POWROT)
            return (int16_t)i;
    }
    return -1;
}

static UI_PROSTOKAT_t UI_ObszarAkcjiPaskaUjednolicony(uint8_t indeks,
                                                       const UI_AKCJA_t *akcje,
                                                       uint8_t liczba,
                                                       uint16_t y,
                                                       uint16_t wysokosc)
{
    const int16_t indeks_powrotu = UI_ZnajdzAkcjePowrotu(akcje, liczba);
    uint8_t i;
    uint8_t pozycja;

    /*
     * Dla przycisków wewnątrz treści zachowujemy dawny równy podział.
     * Dolna strefa od y=205 jest natomiast jednym, sztywnym interfejsem:
     * sześć miejsc 70 x 45 px. Rozmiar nie zależy od liczby funkcji.
     */
    if (y < 205U)
        return UI_ObszarAkcjiPaska(indeks, liczba, y, wysokosc);

    if (akcje == 0 || indeks >= liczba || liczba > UI_DOLNY_PASEK_MAKS_AKCJI)
        return (UI_PROSTOKAT_t){0U, 0U, 0U, 0U};

    if (indeks_powrotu >= 0)
    {
        if (indeks == (uint8_t)indeks_powrotu)
            return UI_ObszarPrzyciskuDolnego(0U);

        pozycja = 1U;
        for (i = 0U; i < indeks; ++i)
        {
            if (i != (uint8_t)indeks_powrotu)
                ++pozycja;
        }
    }
    else
    {
        pozycja = indeks;
    }

    return UI_ObszarPrzyciskuDolnego(pozycja);
}

void UI_RysujPasekAkcji(uint16_t y, uint16_t wysokosc,
                         const UI_AKCJA_t *akcje, uint8_t liczba)
{
    uint8_t i;
    if (akcje == 0 || liczba == 0U)
        return;
    for (i = 0U; i < liczba; ++i)
    {
        UI_KONTROLKA_t kontrolka;
        kontrolka.id = akcje[i].id;
        kontrolka.obszar = UI_ObszarAkcjiPaskaUjednolicony(i, akcje, liczba, y, wysokosc);
        kontrolka.tekst = akcje[i].tekst;
        kontrolka.styl = akcje[i].styl;
        kontrolka.rola_tekstu = UI_ROLA_TEKSTU_PRZYCISK;
        kontrolka.aktywna = akcje[i].aktywna;
        kontrolka.zaznaczona = akcje[i].zaznaczona;
        UI_RysujKontrolke(&kontrolka);
    }
}

int16_t UI_ZnajdzAkcjePaska(LCDPoint punkt, uint16_t y, uint16_t wysokosc,
                            const UI_AKCJA_t *akcje, uint8_t liczba)
{
    uint8_t i;
    if (akcje == 0 || liczba == 0U)
        return -1;
    for (i = 0U; i < liczba; ++i)
    {
        UI_PROSTOKAT_t obszar = UI_ObszarAkcjiPaskaUjednolicony(i, akcje, liczba, y, wysokosc);
        if (akcje[i].aktywna && UI_CzyPunktWObszarze(punkt, &obszar))
            return akcje[i].id;
    }
    return -1;
}

uint8_t UI_NastepnaDostepnaMetoda(const UI_METODA_OPCJA_t *opcje,
                                  uint8_t liczba_opcji, uint8_t wybrana,
                                  int8_t kierunek)
{
    uint8_t krok;
    int indeks;

    if (opcje == 0 || liczba_opcji == 0U)
        return 0U;
    if (wybrana >= liczba_opcji)
        wybrana = 0U;
    if (kierunek == 0)
        return wybrana;

    indeks = (int)wybrana;
    for (krok = 0U; krok < liczba_opcji; ++krok)
    {
        indeks += (kierunek > 0) ? 1 : -1;
        if (indeks < 0)
            indeks = (int)liczba_opcji - 1;
        else if (indeks >= (int)liczba_opcji)
            indeks = 0;
        if (opcje[indeks].dostepna)
            return (uint8_t)indeks;
    }
    return wybrana;
}

void UI_RysujWyborMetody(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                         const char *etykieta, const UI_METODA_OPCJA_t *opcje,
                         uint8_t liczba_opcji, uint8_t wybrana)
{
    const uint16_t szerokosc_strzalki = 44U;
    const uint16_t etykieta_h = 18U;
    const char *nazwa = "--";
    const char *opis = 0;
    UI_STYL_t styl_srodka = UI_STYL_AKCENT;
    LCDColor tlo;

    if (szerokosc < 150U || wysokosc < 42U || opcje == 0 || liczba_opcji == 0U)
        return;
    if (wybrana >= liczba_opcji)
        wybrana = 0U;

    if (opcje[wybrana].nazwa != 0)
        nazwa = opcje[wybrana].nazwa;
    opis = opcje[wybrana].opis;
    if (!opcje[wybrana].dostepna)
        styl_srodka = UI_STYL_NIEAKTYWNY;

    UI_RysujPrzycisk(x, y, szerokosc_strzalki, wysokosc,
                     "<", UI_STYL_NORMALNY, UI_FontDlaRoli(UI_ROLA_TEKSTU_TYTUL));
    UI_RysujPrzycisk((uint16_t)(x + szerokosc - szerokosc_strzalki), y,
                     szerokosc_strzalki, wysokosc,
                     ">", UI_STYL_NORMALNY, UI_FontDlaRoli(UI_ROLA_TEKSTU_TYTUL));

    tlo = UI_KolorTlaPrzycisku(styl_srodka);
    LCD_FillRect(LCD_MakePoint((uint16_t)(x + szerokosc_strzalki), y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - szerokosc_strzalki - 1U),
                               (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint((uint16_t)(x + szerokosc_strzalki), y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - szerokosc_strzalki - 1U),
                                (uint16_t)(y + wysokosc - 1U)),
                  UI_KolorRamki(styl_srodka));

    if (etykieta != 0 && etykieta[0] != '\0')
        UI_RysujTekstWycentrowany((uint16_t)(x + szerokosc_strzalki), (uint16_t)(y + 2U),
                                  (uint16_t)(szerokosc - 2U * szerokosc_strzalki), etykieta_h,
                                  etykieta, UI_FontDlaRoli(UI_ROLA_TEKSTU_ETYKIETA),
                                  UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo);

    UI_RysujTekstWycentrowany((uint16_t)(x + szerokosc_strzalki),
                              (uint16_t)(y + etykieta_h),
                              (uint16_t)(szerokosc - 2U * szerokosc_strzalki),
                              (uint16_t)(wysokosc - etykieta_h - ((opis != 0 && opis[0] != '\0') ? 14U : 0U)),
                              nazwa, UI_FontDlaRoli(UI_ROLA_TEKSTU_PRZYCISK),
                              UI_KolorTekstu(styl_srodka), tlo);

    if (opis != 0 && opis[0] != '\0' && wysokosc >= 58U)
        UI_RysujTekstWycentrowany((uint16_t)(x + szerokosc_strzalki),
                                  (uint16_t)(y + wysokosc - 16U),
                                  (uint16_t)(szerokosc - 2U * szerokosc_strzalki), 14U,
                                  opis, UI_FontDlaRoli(UI_ROLA_TEKSTU_POMOC),
                                  UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo);
}

uint8_t UI_WyborMetodyPoDotyku(LCDPoint punkt, uint16_t x, uint16_t y,
                               uint16_t szerokosc, uint16_t wysokosc,
                               const UI_METODA_OPCJA_t *opcje,
                               uint8_t liczba_opcji, uint8_t wybrana)
{
    const uint16_t szerokosc_strzalki = 44U;
    UI_PROSTOKAT_t lewa = {x, y, szerokosc_strzalki, wysokosc};
    UI_PROSTOKAT_t prawa = {(uint16_t)(x + szerokosc - szerokosc_strzalki), y,
                            szerokosc_strzalki, wysokosc};

    if (UI_CzyPunktWObszarze(punkt, &lewa))
        return UI_NastepnaDostepnaMetoda(opcje, liczba_opcji, wybrana, -1);
    if (UI_CzyPunktWObszarze(punkt, &prawa))
        return UI_NastepnaDostepnaMetoda(opcje, liczba_opcji, wybrana, 1);
    return wybrana;
}

void UI_RysujPolePasywne(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                         const char *tekst, uint32_t font)
{
    const LCDColor tlo = UI_Paleta()->pole_tlo;
    const LCDColor ramka = UI_KolorRamki(UI_STYL_NIEAKTYWNY);

    if (szerokosc < 4U || wysokosc < 4U)
        return;
    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                               (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                                (uint16_t)(y + wysokosc - 1U)), ramka);
    UI_RysujTekstWycentrowany(x, y, szerokosc, wysokosc,
                              tekst != 0 ? tekst : "", font,
                              UI_TEKST_NIEAKTYWNY, tlo);
}

void UI_RysujPoleInformacyjne(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                              const char *etykieta, const char *wartosc)
{
    const LCDColor tlo = UI_Paleta()->pole_tlo;
    const LCDColor ramka = UI_KolorRamki(UI_STYL_NIEAKTYWNY);
    const LCDColor pasek = UI_Paleta()->pole_ramka;

    if (szerokosc < 8U || wysokosc < 8U)
        return;

    /*
     * Pole danych jest celowo spokojniejsze od przycisku: jedna przygaszona
     * ramka i cienki pasek po lewej. Brak podwójnej ramki oraz brak akcentu
     * sygnalizują, że dotyk nie uruchamia akcji.
     */
    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), ramka);
    if (wysokosc >= 12U)
        LCD_FillRect(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                     LCD_MakePoint((uint16_t)(x + 3U), (uint16_t)(y + wysokosc - 2U)), pasek);

    if (etykieta != 0)
        FONT_Write(UI_FontDlaRoli(UI_ROLA_TEKSTU_ETYKIETA), UI_Paleta()->pole_etykieta,
                   tlo, x + 7U, y + 4U, etykieta);
    if (wartosc != 0)
        UI_RysujTekstWielowierszowy((uint16_t)(x + 6U), (uint16_t)(y + 18U),
                                    (uint16_t)(szerokosc - 10U), (uint16_t)(wysokosc - 22U),
                                    wartosc, UI_FontDlaRoli(UI_ROLA_TEKSTU_WARTOSC),
                                    UI_TEKST_NORMALNY, tlo);
}

void UI_RysujPoleWartosci(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                          const char *etykieta, const char *wartosc)
{
    UI_RysujPoleInformacyjne(x, y, szerokosc, wysokosc, etykieta, wartosc);
}

void UI_RysujWierszDanych(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                         const char *etykieta, const char *wartosc, UI_STYL_t styl)
{
    const LCDColor tlo = UI_Paleta()->pole_tlo;
    const LCDColor ramka = UI_KolorRamki(styl);
    const LCDColor wartosc_kolor = (styl == UI_STYL_NORMALNY) ? UI_TEKST_NORMALNY : ramka;
    uint16_t szer_etykiety;
    uint16_t x_wartosci;
    uint16_t szer_wartosci;
    uint32_t font_wartosci;

    if (szerokosc < 120U || wysokosc < 28U)
        return;

    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), ramka);
    LCD_FillRect(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                 LCD_MakePoint((uint16_t)(x + 4U), (uint16_t)(y + wysokosc - 2U)), ramka);

    /*
     * Wiersz danych ma dwa niezależne obszary: etykietę i wartość. Dzięki
     * temu nawet dwuwierszowy opis nie konkuruje pionowo z etykietą. Jest to
     * podstawowy wzorzec dla ekranów z wieloma parametrami (anteny, testy,
     * diagnostyka) i usuwa klasę błędów "tekst na ramce / tekst na tekście".
     */
    szer_etykiety = (uint16_t)(szerokosc * 36U / 100U);
    if (szer_etykiety < 96U) szer_etykiety = 96U;
    if (szer_etykiety > 170U) szer_etykiety = 170U;
    if (szer_etykiety + 40U > szerokosc)
        szer_etykiety = (uint16_t)(szerokosc / 3U);

    x_wartosci = (uint16_t)(x + szer_etykiety + 6U);
    szer_wartosci = (uint16_t)(szerokosc - szer_etykiety - 10U);

    if (etykieta != 0)
        UI_RysujTekstWielowierszowy((uint16_t)(x + 8U), (uint16_t)(y + 2U),
                                    (uint16_t)(szer_etykiety - 10U), (uint16_t)(wysokosc - 4U),
                                    etykieta, FONT_FRAN, UI_Paleta()->pole_etykieta, tlo);

    if (wartosc != 0)
    {
        font_wartosci = UI_DobierzFontDoPola(FONT_FRANBIG, wartosc,
                                             szer_wartosci, (uint16_t)(wysokosc - 4U));
        UI_RysujTekstWielowierszowy(x_wartosci, (uint16_t)(y + 2U),
                                    szer_wartosci, (uint16_t)(wysokosc - 4U),
                                    wartosc, font_wartosci, wartosc_kolor, tlo);
    }
}

void UI_RysujPoleStatusu(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                        const char *etykieta, const char *wartosc, UI_STYL_t styl)
{
    const LCDColor tlo = UI_Paleta()->pole_tlo;
    const LCDColor ramka = UI_KolorRamki(styl);
    const LCDColor wartosc_kolor = (styl == UI_STYL_NORMALNY) ? UI_TEKST_NORMALNY : ramka;

    if (szerokosc < 20U || wysokosc < 28U)
        return;

    /* Niskie pola zawsze używają wspólnego, odpornego wiersza danych. */
    if (wysokosc <= 44U)
    {
        UI_RysujWierszDanych(x, y, szerokosc, wysokosc, etykieta, wartosc, styl);
        return;
    }

    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), ramka);
    LCD_FillRect(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                 LCD_MakePoint((uint16_t)(x + 4U), (uint16_t)(y + wysokosc - 2U)), ramka);

    if (etykieta != 0)
        FONT_Write(FONT_FRAN, UI_Paleta()->pole_etykieta, tlo, x + 10U, y + 3U, etykieta);
    if (wartosc != 0)
    {
        const uint16_t y_wartosci = (uint16_t)(y + 19U);
        const uint16_t h_wartosci = (uint16_t)(wysokosc > 21U ? wysokosc - 21U : 1U);
        UI_RysujTekstWielowierszowy((uint16_t)(x + 8U), y_wartosci,
                                    (uint16_t)(szerokosc - 14U), h_wartosci,
                                    wartosc, FONT_FRANBIG, wartosc_kolor, tlo);
    }
}


void UI_RysujPoleLiczboweGlowne(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                                const char *etykieta, const char *liczba, const char *jednostka)
{
    LCDColor tlo = UI_Paleta()->pole_tlo;
    uint32_t font_liczby = FONT_BDIGITS;
    const uint32_t font_jednostki = FONT_FRANBIG;
    int szerokosc_liczby;
    int szerokosc_jednostki = 0;
    int szerokosc_calkowita;
    int x_liczby;
    int y_liczby;
    const int margines = 12;

    /*
     * Najważniejszy wynik ma być duży, ale nigdy kosztem obcięcia cyfr.
     * Częstotliwości z sześcioma miejscami po przecinku są znacznie dłuższe
     * od SWR czy napięcia. Najpierw używamy charakterystycznej dużej czcionki
     * cyfr, a gdy cały zapis z jednostką nie mieści się w polu przechodzimy
     * na FONT_FRANBIG. Dzięki temu pojedynczy komponent UI jest bezpieczny
     * zarówno dla SWR, jak i dla 433,500000 MHz.
     */
    if (szerokosc < 60U || wysokosc < 64U || liczba == 0)
    {
        UI_RysujPoleWartosci(x, y, szerokosc, wysokosc, etykieta, liczba);
        return;
    }

    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)),
                  UI_Paleta()->pole_ramka);

    if (etykieta != 0)
        FONT_Write(FONT_FRAN, UI_Paleta()->pole_etykieta, tlo, x + 8U, y + 5U, etykieta);

    if (jednostka != 0 && jednostka[0] != '\0')
        szerokosc_jednostki = FONT_GetStrPixelWidth(font_jednostki, jednostka) + 6;

    szerokosc_liczby = FONT_GetStrPixelWidth(font_liczby, liczba);
    szerokosc_calkowita = szerokosc_liczby + szerokosc_jednostki;
    if (szerokosc_calkowita > (int)szerokosc - margines)
    {
        font_liczby = FONT_FRANBIG;
        szerokosc_liczby = FONT_GetStrPixelWidth(font_liczby, liczba);
        szerokosc_calkowita = szerokosc_liczby + szerokosc_jednostki;
    }

    x_liczby = (int)x + ((int)szerokosc - szerokosc_calkowita) / 2;
    if (x_liczby < (int)x + 6)
        x_liczby = (int)x + 6;

    y_liczby = (int)y + (int)wysokosc - (int)FONT_GetHeight(font_liczby) - 7;
    if (y_liczby < (int)y + 20)
        y_liczby = (int)y + 20;

    FONT_Write(font_liczby, UI_TEKST_NORMALNY, tlo,
               (uint16_t)x_liczby, (uint16_t)y_liczby, liczba);

    if (szerokosc_jednostki != 0)
    {
        int y_jednostki = y_liczby + (int)FONT_GetHeight(font_liczby) - (int)FONT_GetHeight(font_jednostki) - 2;
        FONT_Write(font_jednostki, UI_Paleta()->pole_etykieta, tlo,
                   (uint16_t)(x_liczby + szerokosc_liczby + 6), (uint16_t)y_jednostki, jednostka);
    }
}

void UI_RysujPanel(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                   const char *tytul, UI_STYL_t styl)
{
    const uint16_t pasek_h = (tytul != 0 && tytul[0] != '\0') ? 18U : 0U;
    LCDColor tlo = UI_Paleta()->pole_tlo;
    LCDColor ramka = UI_KolorRamki(styl);
    LCDColor pasek = UI_KolorTlaPrzycisku(styl);

    if (szerokosc < 6U || wysokosc < 6U)
        return;

    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), ramka);

    if (pasek_h != 0U)
    {
        LCD_FillRect(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                     LCD_MakePoint((uint16_t)(x + szerokosc - 2U), (uint16_t)(y + pasek_h)), pasek);
        FONT_Write(FONT_FRAN, UI_KolorTekstu(styl), pasek,
                   (uint16_t)(x + 6U), (uint16_t)(y + 2U), tytul);
    }
}

void UI_RysujEkranPrzejsciowy(const char *tytul, const char *opis)
{
    const LCDColor tlo = UI_KolorTlaEkranu();
    const LCDColor tlo_panelu = UI_KolorTlaPola();
    const LCDColor ramka = UI_KolorRamki(UI_STYL_AKCENT);
    const LCDColor opis_kolor = UI_KolorTekstu(UI_STYL_NIEAKTYWNY);
    const uint16_t x = 30U;
    const uint16_t y = 82U;
    const uint16_t szerokosc = 420U;
    const uint16_t wysokosc = 112U;

    /*
     * Cięższe ekrany potrzebują krótkiego, jednoznacznego potwierdzenia wejścia.
     * Komponent tylko rysuje warstwę informacyjną; czas jej wyświetlania pozostaje
     * po stronie wywołującej funkcji, aby proste menu nie dostawały sztucznej zwłoki.
     */
    UI_WyczyscEkran();
    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                               (uint16_t)(y + wysokosc - 1U)),
                 tlo_panelu);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                                (uint16_t)(y + wysokosc - 1U)),
                  ramka);

    if (tytul != 0 && tytul[0] != '\0')
    {
        uint32_t font = UI_DobierzFont(FONT_FRANBIG, tytul, (uint16_t)(szerokosc - 24U));
        int szerokosc_tekstu = FONT_GetStrPixelWidth(font, tytul);
        int x_tekstu = (480 - szerokosc_tekstu) / 2;
        if (x_tekstu < (int)x + 10)
            x_tekstu = (int)x + 10;
        FONT_Write(font, ramka, tlo_panelu, (uint16_t)x_tekstu, (uint16_t)(y + 24U), tytul);
    }

    if (opis != 0 && opis[0] != '\0')
        UI_RysujTekstWielowierszowy((uint16_t)(x + 18U), (uint16_t)(y + 68U),
                                    (uint16_t)(szerokosc - 36U), 32U,
                                    opis, FONT_FRAN, opis_kolor, tlo_panelu);

    (void)tlo;
}

static bool UI_CzyRetro(void)
{
    return UI_AktualnyZestawIkon() == 0U;
}

static void UI_RysujRogRetroPojedynczy(uint16_t x, uint16_t y,
                                       bool prawa, bool dol,
                                       LCDColor kolor, LCDColor cien)
{
    const uint16_t dlugi = 12U;
    const uint16_t krotki = 7U;
    const int dx = prawa ? -1 : 1;
    const int dy = dol ? -1 : 1;
    const int ax = (int)x;
    const int ay = (int)y;

    /*
     * x,y to właściwy narożnik. Linie biegną do wnętrza kafla, więc po
     * prawej i na dole obliczamy początek odcinka zamiast liczyć na kierunek
     * prymitywu LCD_HLine/LCD_VLine.
     */
    LCD_HLine(LCD_MakePoint((uint16_t)(prawa ? ax - (int)dlugi + 1 : ax),
                            (uint16_t)ay), dlugi, kolor);
    LCD_VLine(LCD_MakePoint((uint16_t)ax,
                            (uint16_t)(dol ? ay - (int)dlugi + 1 : ay)),
              dlugi, kolor);

    LCD_HLine(LCD_MakePoint((uint16_t)(prawa ? ax - (int)krotki + 1 : ax + 2),
                            (uint16_t)(ay + 2 * dy)), krotki, cien);
    LCD_VLine(LCD_MakePoint((uint16_t)(ax + 2 * dx),
                            (uint16_t)(dol ? ay - (int)krotki + 1 : ay + 2)),
              krotki, cien);

    /* Nit, pierścień i mała woluta nawiązują do modelowej grafiki Retro. */
    LCD_FillCircle(LCD_MakePoint((uint16_t)(ax + 4 * dx),
                                 (uint16_t)(ay + 4 * dy)), 2U, kolor);
    LCD_Circle(LCD_MakePoint((uint16_t)(ax + 9 * dx),
                             (uint16_t)(ay + 7 * dy)), 3U, kolor);

    LCD_Line(LCD_MakePoint((uint16_t)(ax + 10 * dx),
                           (uint16_t)(ay + 2 * dy)),
             LCD_MakePoint((uint16_t)(ax + 15 * dx),
                           (uint16_t)(ay + 7 * dy)), kolor);
    LCD_Line(LCD_MakePoint((uint16_t)(ax + 12 * dx),
                           (uint16_t)(ay + 2 * dy)),
             LCD_MakePoint((uint16_t)(ax + 16 * dx),
                           (uint16_t)(ay + 8 * dy)), cien);
    LCD_FillCircle(LCD_MakePoint((uint16_t)(ax + 14 * dx),
                                 (uint16_t)(ay + 10 * dy)), 1U, kolor);
}

static void UI_RysujRogiRetro(uint16_t x, uint16_t y, uint16_t w, uint16_t h, LCDColor kolor)
{
    const LCDColor cien = UI_Paleta()->kafel_ramka_wewn;

    if (!UI_CzyRetro() || w < 34U || h < 28U)
        return;

    /*
     * Cztery symetryczne wsporniki. Nie dodajemy bitmapy ramki, dzięki czemu
     * ozdobnik kosztuje praktycznie tylko kilka wywołań prymitywów LCD i nie
     * zwiększa zajętości Flash przez duży zasób graficzny.
     */
    UI_RysujRogRetroPojedynczy((uint16_t)(x + 3U),
                               (uint16_t)(y + 3U), false, false, kolor, cien);
    UI_RysujRogRetroPojedynczy((uint16_t)(x + w - 4U),
                               (uint16_t)(y + 3U), true, false, kolor, cien);
    UI_RysujRogRetroPojedynczy((uint16_t)(x + 3U),
                               (uint16_t)(y + h - 4U), false, true, kolor, cien);
    UI_RysujRogRetroPojedynczy((uint16_t)(x + w - 4U),
                               (uint16_t)(y + h - 4U), true, true, kolor, cien);
}

void UI_RysujKafelMenu(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                       UI_IKONA_MENU_t ikona, const char *tekst, bool zaznaczony)
{
    const LCDColor tlo = zaznaczony ? UI_Paleta()->kafel_tlo_aktywny : UI_Paleta()->kafel_tlo;
    const LCDColor ramka = zaznaczony ? UI_Paleta()->kafel_ramka_aktywna : UI_Paleta()->kafel_ramka;
    const LCDColor etykieta_tlo = zaznaczony ? UI_Paleta()->kafel_etykieta_tlo_aktywny : UI_Paleta()->kafel_etykieta_tlo;
    const uint16_t et_h = UI_DobierzWysokoscPodpisuKafla(szerokosc, tekst, 20U, 26U);
    const uint16_t ik_y = (uint16_t)(y + 2U);
    const uint16_t ik_h = (uint16_t)(wysokosc - et_h - 4U);

    LCD_FillRect(LCD_MakePoint(x, y), LCD_MakePoint(x + szerokosc - 1U, y + wysokosc - 1U), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y), LCD_MakePoint(x + szerokosc - 1U, y + wysokosc - 1U), ramka);
    LCD_Rectangle(LCD_MakePoint(x + 1U, y + 1U), LCD_MakePoint(x + szerokosc - 2U, y + wysokosc - 2U), UI_Paleta()->kafel_ramka_wewn);
    UI_RysujRogiRetro(x, y, szerokosc, wysokosc, UI_Paleta()->ramka_akcentu);

    /*
     * Od test26 kafel korzysta z jednego zatwierdzonego zestawu Classic 2026.
     * Symbol jest bitmapą 4-bit bez tła; stan kafla nadal rysuje wspólny UI.
     */
    if (!UI_RysujIkoneBitmapZestawu(ikona, UI_AktualnyZestawIkon(),
                                           (int)x + 2, (int)ik_y,
                                           (int)szerokosc - 4, (int)ik_h,
                                           tlo, UI_IKONA_KOLOR))
    {
        UI_RysujIkoneZastepcza((int)x + 2, (int)ik_y,
                                (int)szerokosc - 4, (int)ik_h);
    }

    UI_RysujPodpisKafla2026(x, y, szerokosc, wysokosc, et_h, tekst,
                            etykieta_tlo, ramka, UI_Paleta()->ikona_akcent,
                            UI_TEKST_NORMALNY);
}

void UI_RysujKafelMenuGlownego(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                               UI_IKONA_MENU_t ikona, const char *tekst, bool zaznaczony)
{
    const UI_PALETA_t *paleta = UI_Paleta();
    /*
     * Styl "papierowy" tylko dla pola ikony pięciu kafli menu głównego: jasny
     * beż zamiast czarnego tła. Ikony retro i tak mają ten sam beż wypalony
     * w bitmapie, więc to usuwa szew między obramowaniem kafla a rysunkiem.
     *
     * POPRAWKA PO ZDJĘCIACH: tabliczka z podpisem NIE dostaje tego samego
     * beżu - użytkownik zwrócił uwagę, że na pozostałych ekranach (kafle
     * kompaktowe w podmenu) podpisy siedzą na ciemnym tle, więc menu główne
     * miało wyglądać niespójnie. Tabliczka wraca do paleta->kafel_etykieta_tlo
     * i UI_TEKST_NORMALNY - dokładnie tych samych wartości, których używają
     * kafle kompaktowe - a beż zostaje tylko w polu ikony.
     */
    const LCDColor bez_tlo = LCD_RGB(244, 218, 168);
    /*
     * BŁĄD ZNALEZIONY PRZY PRZEGLĄDZIE: paleta->ikona_akcent i
     * paleta->kafel_ramka_aktywna to (255,211,111) - jasne złoto dobrane pod
     * dawne, ciemne tło kafla. Na beżowym polu ikony (244,218,168) różnica
     * jasności wynosi ok. 7/255 - element praktycznie znika. To samo dotyczyło
     * paleta->ramka_akcentu (247,213,132) użytego do rogów. Poniższe dwa
     * kolory są dobrane pod jasne tło i mają realny kontrast (odpowiednio
     * ~93 i ~152 różnicy jasności wobec beżu, a przy okazji też dobry kontrast
     * wobec ciemnej tabliczki, więc nadają się do obu miejsc, gdzie są użyte):
     * brąz do zwykłych akcentów, ciemna czerwień do stanu zaznaczonego
     * (nawiązuje do koloru przycisku Wstecz).
     */
    const LCDColor bez_akcent = LCD_RGB(150, 105, 45);
    const LCDColor bez_zaznaczenie = LCD_RGB(150, 40, 22);
    const LCDColor tlo = bez_tlo;
    const LCDColor ramka = zaznaczony ? bez_zaznaczenie : paleta->kafel_ramka;
    const LCDColor akcent = zaznaczony ? bez_zaznaczenie : bez_akcent;
    const LCDColor etykieta_tlo = zaznaczony
        ? paleta->kafel_etykieta_tlo_aktywny : paleta->kafel_etykieta_tlo;
    const LCDColor cien = paleta->pasek_stanu_tlo;
    /*
     * Geometria poziomu 0: kafel 150 x 84. Wysokość podpisu podniesiona o 29%
     * (20->26, 26->34) - napisy nowym, wyższym FONT_KAFEL dotykały górnej
     * i dolnej krawędzi tabliczki przy starych wartościach.
     */
    const uint16_t etykieta_h = UI_DobierzWysokoscPodpisuKafla(szerokosc, tekst, 26U, 34U);
    const uint16_t pole_ikony_y = (uint16_t)(y + 2U);
    const uint16_t pole_ikony_h = (uint16_t)(wysokosc - etykieta_h - 4U);

    /*
     * Cień ma tylko odseparować kartę od tła. Nie stosujemy gradientów ani
     * półprzezroczystości, bo na STM32F746 kosztowałyby kod i czas rysowania,
     * a na 480 x 272 nie poprawiłyby czytelności.
     */
    if ((uint32_t)x + szerokosc < 479U && (uint32_t)y + wysokosc < 219U)
    {
        LCD_FillRect(LCD_MakePoint((uint16_t)(x + 2U), (uint16_t)(y + 2U)),
                     LCD_MakePoint((uint16_t)(x + szerokosc),
                                   (uint16_t)(y + wysokosc)), cien);
    }

    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                               (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                                (uint16_t)(y + wysokosc - 1U)), ramka);
    LCD_Rectangle(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 2U),
                                (uint16_t)(y + wysokosc - 2U)), paleta->kafel_ramka_wewn);
    /*
     * Zwykła paleta->ramka_akcentu (247,213,132) ginie na beżu z tego samego
     * powodu co wyżej - używamy tu 'ramka', która już jest dobrana pod jasne
     * tło (brąz na co dzień, ciemna czerwień gdy kafel jest zaznaczony).
     */
    UI_RysujRogiRetro(x, y, szerokosc, wysokosc, ramka);

    /* Cienka listwa jest wizualnym podpisem poziomu 0 i wzmacnia zaznaczenie. */
    LCD_FillRect(LCD_MakePoint((uint16_t)(x + 3U), (uint16_t)(y + 3U)),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 4U),
                               (uint16_t)(y + (zaznaczony ? 5U : 4U))), akcent);

    /*
     * Rysunek jest węższy od kafla o osiem pikseli z każdej strony. Wcześniej
     * sięgał niemal krawędzi i zasłaniał złotą ramkę razem z narożnikami,
     * które są znakiem rozpoznawczym stylu retro.
     */
    if (!UI_RysujIkoneBitmapZestawu(ikona, UI_AktualnyZestawIkon(),
                                    (int)x + 8, (int)pole_ikony_y + 2,
                                    (int)szerokosc - 16, (int)pole_ikony_h - 4,
                                    tlo, UI_IKONA_KOLOR))
    {
        UI_RysujIkoneZastepcza((int)x + 8, (int)pole_ikony_y + 2,
                               (int)szerokosc - 16, (int)pole_ikony_h - 4);
    }

    /*
     * Podpis jest niższy niż w zwykłym kaflu. W menu głównym nazwy są krótkie,
     * więc odzyskane piksele warto oddać symbolowi. Pionowy znacznik po lewej
     * prowadzi wzrok do nazwy bez dodawania kolejnego koloru.
     */
    UI_RysujPodpisKafla2026(x, y, szerokosc, wysokosc, etykieta_h, tekst,
                            etykieta_tlo, ramka, akcent, UI_TEKST_NORMALNY);
}

void UI_RysujPodpowiedzMenuGlownego(const char *tytul, const char *opis, bool aktywna)
{
    const UI_PALETA_t *paleta = UI_Paleta();
    const uint16_t y = UI_PODPOWIEDZ_GLOWNA_Y;
    const uint16_t wysokosc = (uint16_t)(272U - y);
    const LCDColor tlo = paleta->pasek_stanu_tlo;
    const LCDColor akcent = aktywna ? paleta->kafel_ramka_aktywna : paleta->pasek_stanu_linia;
    const LCDColor kolor_opisu = aktywna ? paleta->tekst_normalny : paleta->tekst_nieaktywny;

    LCD_FillRect(LCD_MakePoint(0U, y), LCD_MakePoint(479U, 271U), tlo);
    LCD_HLine(LCD_MakePoint(0U, y), 480U, paleta->pasek_stanu_linia);

    if (aktywna && tytul != 0 && tytul[0] != '\0')
    {
        uint32_t font_tytulu = UI_DobierzFontDoPola(FONT_FRANBIG, tytul, 126U, 20U);
        LCD_FillRect(LCD_MakePoint(8U, (uint16_t)(y + 8U)),
                     LCD_MakePoint(11U, (uint16_t)(y + wysokosc - 9U)), akcent);
        FONT_Write(font_tytulu, akcent, tlo, 19U, (uint16_t)(y + 5U), tytul);

        if (opis != 0 && opis[0] != '\0')
            UI_RysujTekstWielowierszowy(150U, (uint16_t)(y + 6U), 321U, 38U,
                                        opis, FONT_FRAN, kolor_opisu, tlo);
        return;
    }

    if (opis != 0 && opis[0] != '\0')
    {
        char bufor[112];
        uint32_t font = UI_DobierzFontDoPola(FONT_FRAN, opis, 448U, 28U);
        int szerokosc_tekstu;
        int x;

        UI_SkrocTekstDoSzerokosci(opis, font, 448U, bufor, sizeof(bufor));
        szerokosc_tekstu = FONT_GetStrPixelWidth(font, bufor);
        x = (480 - szerokosc_tekstu) / 2;
        if (x < 16)
            x = 16;
        FONT_Write(font, kolor_opisu, tlo, (uint16_t)x, (uint16_t)(y + 18U), bufor);
    }
}


/* ------------------------------------------------------------------------
 * Standardowa siatka kafelków 3 x 2.
 *
 *  x:   8, 165, 322              szerokość 150 px
 *  y:  38, 128                   wysokość 84 px
 *
 * Drugi wiersz kończy się na y=211. Od y=220 zaczyna się stały dolny pasek.
 * Wysokość 84 px pozostawia co najmniej 30 px na historyczną bitmapę ikony;
 * wcześniejsze kafle 90 x 56 miały dla ikony tylko 26 px i dlatego renderer
 * odrzucał bitmapy 70 x 30, pokazując znak „?”.
 * ------------------------------------------------------------------------ */
#define UI_KOMPAKT_X0       8U
#define UI_KOMPAKT_Y0      44U
#define UI_KOMPAKT_W       UI_KAFEL_STANDARD_SZEROKOSC
#define UI_KOMPAKT_H       UI_KAFEL_STANDARD_WYSOKOSC
#define UI_KOMPAKT_DX     157U
#define UI_KOMPAKT_DY      90U

UI_PROSTOKAT_t UI_ObszarKaflaKompaktowego(uint16_t indeks)
{
    UI_PROSTOKAT_t obszar = {0U, 0U, 0U, 0U};
    const uint16_t kolumna = (uint16_t)(indeks % UI_SIATKA_KOMPAKT_KOLUMNY);
    const uint16_t wiersz = (uint16_t)(indeks / UI_SIATKA_KOMPAKT_KOLUMNY);

    if (indeks >= UI_SIATKA_KOMPAKT_NA_STRONE)
        return obszar;

    obszar.x = (uint16_t)(UI_KOMPAKT_X0 + kolumna * UI_KOMPAKT_DX);
    obszar.y = (uint16_t)(UI_KOMPAKT_Y0 + wiersz * UI_KOMPAKT_DY);
    obszar.szerokosc = UI_KOMPAKT_W;
    obszar.wysokosc = UI_KOMPAKT_H;
    return obszar;
}

/*
 * Kontrolka jest maleńka i zawsze w tym samym miejscu — prawy górny róg kafla.
 * Stałe położenie jest ważniejsze od widoczności: oko uczy się jednego punktu
 * i potem skanuje cały ekran jednym spojrzeniem, zamiast czytać kafel po kaflu.
 *
 * Kolor jaskrawozielony, nie czerwony. Czerwień w całym interfejsie znaczy
 * "coś jest nie tak", a tu chodzi o "działa". Zielona dioda mówi to samo, co
 * mówiła na przednich panelach sprzętu audio, i nie odbiera czerwieni jej
 * jedynego zadania — ostrzegania.
 */
#define UI_KONTROLKA_ROZMIAR 9U
#define UI_KONTROLKA_MARGINES 5U
#define UI_KONTROLKA_CRT_W 12U
#define UI_KONTROLKA_CRT_H 5U

static void UI_RysujKontrolkeStanuKaflaDlaZestawu(const UI_PROSTOKAT_t *o,
                                                    bool wlaczona,
                                                    uint8_t zestaw)
{
    if (o == 0 || o->szerokosc < 18U || o->wysokosc < 18U)
        return;


    {
        uint16_t cx = (uint16_t)(o->x + o->szerokosc - UI_KONTROLKA_MARGINES -
                                 UI_KONTROLKA_ROZMIAR / 2U);
        uint16_t cy = (uint16_t)(o->y + UI_KONTROLKA_MARGINES +
                                 UI_KONTROLKA_ROZMIAR / 2U);

        LCD_FillCircle(LCD_MakePoint(cx, cy), 5, LCD_RGB(3, 13, 9));
        LCD_Circle(LCD_MakePoint(cx, cy), 5, LCD_RGB(80, 92, 86));

        if (wlaczona)
        {
            LCD_FillCircle(LCD_MakePoint(cx, cy), 4, LCD_RGB(0, 160, 55));
            LCD_FillCircle(LCD_MakePoint(cx, cy), 2, LCD_RGB(0, 255, 95));
            LCD_SetPixel(LCD_MakePoint((int)cx - 1, (int)cy - 2), LCD_RGB(190, 255, 210));
        }
        else
        {
            LCD_FillCircle(LCD_MakePoint(cx, cy), 3, LCD_RGB(9, 38, 24));
        }
    }
}

void UI_RysujKontrolkeStanuKafla(const UI_PROSTOKAT_t *o, bool wlaczona)
{
    UI_RysujKontrolkeStanuKaflaDlaZestawu(o, wlaczona, UI_AktualnyZestawIkon());
}

void UI_RysujKontrolkeStanuKaflaZestawu(const UI_PROSTOKAT_t *o, bool wlaczona,
                                         uint8_t zestaw_ikon)
{
    if (zestaw_ikon >= UI_LICZBA_ZESTAWOW_IKON)
        zestaw_ikon = UI_AktualnyZestawIkon();
    UI_RysujKontrolkeStanuKaflaDlaZestawu(o, wlaczona, zestaw_ikon);
}

static void UI_RysujKafelKompaktowyWewnetrzny(uint16_t indeks, UI_IKONA_MENU_t ikona,
                                                uint8_t zestaw_ikon, const char *tekst,
                                                bool zaznaczony, bool aktywny)
{
    const UI_PROSTOKAT_t o = UI_ObszarKaflaKompaktowego(indeks);
    const UI_PALETA_t *paleta;
    LCDColor tlo;
    LCDColor ramka;
    LCDColor kolor;
    LCDColor etykieta_tlo;
    LCDColor akcent;
    /*
     * POPRAWKA PO ZDJĘCIACH: menu główne dostało już wyższy pasek etykiety
     * (26/34 zamiast 20/26) w UI_RysujKafelMenuGlownego, ale kafle kompaktowe
     * w podmenu (Pojedynczy/Wykres SWR/Strojenie itd.) nadal używały starych,
     * niższych wartości - stąd te same, wcześniej już naprawione, obcięte
     * podpisy pojawiały się dalej, tylko o piętro niżej w menu.
     */
    const uint16_t et_h = UI_DobierzWysokoscPodpisuKafla(o.szerokosc, tekst, 26U, 34U);
    const uint16_t ik_h = (uint16_t)(o.wysokosc - et_h - 4U);

    if (o.szerokosc == 0U || o.wysokosc == 0U)
        return;

    if (zestaw_ikon >= UI_LICZBA_ZESTAWOW_IKON)
        zestaw_ikon = UI_AktualnyZestawIkon();

    paleta = UI_PaletaDlaZestawu(zestaw_ikon);
    tlo = !aktywny ? paleta->tlo_nieaktywnego
                    : (zaznaczony ? paleta->kafel_tlo_aktywny : paleta->kafel_tlo);
    ramka = !aktywny ? paleta->ramka_nieaktywna
                      : (zaznaczony ? paleta->kafel_ramka_aktywna : paleta->kafel_ramka);
    kolor = !aktywny ? paleta->tekst_nieaktywny : paleta->tekst_normalny;
    etykieta_tlo = !aktywny ? paleta->tlo_nieaktywnego
                             : (zaznaczony ? paleta->kafel_etykieta_tlo_aktywny
                                           : paleta->kafel_etykieta_tlo);
    akcent = !aktywny ? paleta->ramka_nieaktywna
                       : (zaznaczony ? paleta->kafel_ramka_aktywna
                                     : paleta->ikona_akcent);
    LCD_FillRect(LCD_MakePoint(o.x, o.y),
                 LCD_MakePoint((uint16_t)(o.x + o.szerokosc - 1U),
                               (uint16_t)(o.y + o.wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(o.x, o.y),
                  LCD_MakePoint((uint16_t)(o.x + o.szerokosc - 1U),
                                (uint16_t)(o.y + o.wysokosc - 1U)), ramka);
    UI_RysujRogiRetro(o.x, o.y, o.szerokosc, o.wysokosc, paleta->ramka_akcentu);

    if ((uint32_t)ikona < (uint32_t)UI_IKONA_MENU_LICZBA)
    {
        if (!UI_RysujIkoneBitmapZestawu(ikona, zestaw_ikon,
                                        (int)o.x + 2, (int)o.y + 2,
                                        (int)o.szerokosc - 4, (int)ik_h,
                                        tlo, paleta->ikona_kolor))
        {
            UI_RysujIkoneZastepcza((int)o.x + 2, (int)o.y + 2,
                                   (int)o.szerokosc - 4, (int)ik_h);
        }
        UI_RysujPodpisKafla2026(o.x, o.y, o.szerokosc, o.wysokosc, et_h, tekst,
                                etykieta_tlo, ramka, akcent, kolor);
    }
    else
    {
        /* Kafel tekstowy, np. nazwa języka albo pasma. Brak sztucznej ikony
         * zostawia więcej miejsca na czytelny napis. */
        UI_RysujTekstWielowierszowy((uint16_t)(o.x + 3U), (uint16_t)(o.y + 3U),
                                    (uint16_t)(o.szerokosc - 6U),
                                    (uint16_t)(o.wysokosc - 6U),
                                    tekst, FONT_FRANBIG, kolor, tlo);
    }
}

void UI_RysujKafelKompaktowy(uint16_t indeks, UI_IKONA_MENU_t ikona,
                             const char *tekst, bool zaznaczony, bool aktywny)
{
    UI_RysujKafelKompaktowyWewnetrzny(indeks, ikona, UI_AktualnyZestawIkon(),
                                      tekst, zaznaczony, aktywny);
}

void UI_RysujKafelKompaktowyZestaw(uint16_t indeks, UI_IKONA_MENU_t ikona,
                                   uint8_t zestaw_ikon, const char *tekst,
                                   bool zaznaczony, bool aktywny)
{
    /*
     * Podgląd wyboru stylu musi rysować każdy kafel swoim zestawem, a nie
     * zestawem aktualnie aktywnym. Inaczej oba warianty wyglądają identycznie
     * do chwili wyjścia z menu i użytkownik nie widzi, co właściwie wybiera.
     */
    UI_RysujKafelKompaktowyWewnetrzny(indeks, ikona, zestaw_ikon,
                                      tekst, zaznaczony, aktywny);
}

void UI_RysujKafelKompaktowyZeStanem(uint16_t indeks, UI_IKONA_MENU_t ikona,
                                     const char *tekst, bool zaznaczony,
                                     bool aktywny, bool wlaczona)
{
    UI_RysujKafelKompaktowy(indeks, ikona, tekst, zaznaczony, aktywny);

    {
        const UI_PROSTOKAT_t o = UI_ObszarKaflaKompaktowego(indeks);
        if (o.szerokosc != 0U && o.wysokosc != 0U)
            UI_RysujKontrolkeStanuKafla(&o, wlaczona);
    }
}

int16_t UI_KafelKompaktowyPoDotyku(LCDPoint punkt, uint16_t liczba)
{
    uint16_t i;
    if (liczba > UI_SIATKA_KOMPAKT_NA_STRONE)
        liczba = UI_SIATKA_KOMPAKT_NA_STRONE;
    for (i = 0U; i < liczba; ++i)
    {
        const UI_PROSTOKAT_t o = UI_ObszarKaflaKompaktowego(i);
        if (UI_CzyPunktWObszarze(punkt, &o))
            return (int16_t)i;
    }
    return -1;
}

void UI_RysujKafelMenuZWartoscia(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                                 UI_IKONA_MENU_t ikona, const char *etykieta,
                                 const char *wartosc, bool zaznaczony)
{
    const LCDColor tlo = zaznaczony ? UI_Paleta()->kafel_tlo_aktywny : UI_Paleta()->kafel_tlo;
    const LCDColor ramka = zaznaczony ? UI_Paleta()->kafel_ramka_aktywna : UI_Paleta()->kafel_ramka;
    const LCDColor etykieta_tlo = zaznaczony ? UI_Paleta()->kafel_etykieta_tlo_aktywny : UI_Paleta()->kafel_etykieta_tlo;
    const LCDColor kolor = zaznaczony ? UI_KolorTekstu(UI_STYL_AKTYWNY)
                                      : UI_KolorTekstu(UI_STYL_NORMALNY);
    const uint16_t et_h = 19U;
    const uint16_t margines = 5U;
    uint16_t body_h;

    (void)ikona;

    if (szerokosc < 12U || wysokosc <= et_h + 8U)
        return;

    /*
     * Kafel z wartością jest kaflem danych, a nie ikoną z tekstem nałożonym
     * na wierzch. W test18 częstotliwość celu była rysowana na ikonie i
     * schodziła na pasek "Cel". Tutaj obszar roboczy i pasek podpisu są
     * rozłączne, więc żadna liczba nie może zasłonić znaczenia przycisku.
     */
    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                               (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                                (uint16_t)(y + wysokosc - 1U)), ramka);
    LCD_Rectangle(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 2U),
                                (uint16_t)(y + wysokosc - 2U)), UI_Paleta()->kafel_ramka_wewn);

    body_h = (uint16_t)(wysokosc - et_h - 5U);
    if (wartosc != NULL && wartosc[0] != '\0')
    {
        const uint16_t szer_wartosci = (uint16_t)(szerokosc - 2U * margines);
        const uint32_t font_wartosci = UI_DobierzFontDoPola(FONT_FRANBIG, wartosc,
                                                            szer_wartosci, body_h);
        UI_RysujTekstWycentrowany((uint16_t)(x + margines), (uint16_t)(y + 3U),
                                  szer_wartosci, body_h,
                                  wartosc, font_wartosci, kolor, tlo);
    }

    UI_RysujPodpisKafla2026(x, y, szerokosc, wysokosc, et_h, etykieta,
                            etykieta_tlo, ramka, UI_Paleta()->ikona_akcent,
                            UI_TEKST_NORMALNY);
}

/* ========================================================================
 * UX 2026 - pasek gorny, przewijane listy i lekkie kontrolki.
 *
 * Te funkcje nie wprowadzaja osobnego systemu UI. Rozszerzaja ui_wspolny,
 * tak aby kolejne ekrany korzystaly z jednej geometrii i jednego modelu
 * fokusu. Brak alokacji dynamicznej i brak bufora calego ekranu.
 * ======================================================================== */

#define UI_WSTECZ_X 0U
#define UI_WSTECZ_Y UI_DOLNY_PASEK_Y
#define UI_WSTECZ_W UI_DOLNY_PRZYCISK_SZEROKOSC
#define UI_WSTECZ_H UI_DOLNY_PRZYCISK_WYSOKOSC

UI_PROSTOKAT_t UI_ObszarPrzyciskuDolnego(uint8_t pozycja)
{
    UI_PROSTOKAT_t obszar = {0U, 0U, 0U, 0U};

    if (pozycja >= UI_DOLNY_PASEK_MAKS_AKCJI)
        return obszar;

    obszar.x = (uint16_t)(pozycja * (UI_DOLNY_PRZYCISK_SZEROKOSC + UI_DOLNY_PRZYCISK_ODSTEP));
    obszar.y = UI_WSTECZ_Y;
    obszar.szerokosc = UI_DOLNY_PRZYCISK_SZEROKOSC;
    obszar.wysokosc = UI_DOLNY_PRZYCISK_WYSOKOSC;
    return obszar;
}

UI_PROSTOKAT_t UI_ObszarWsteczDolny(void)
{
    return UI_ObszarPrzyciskuDolnego(0U);
}

void UI_RysujWsteczDolny(bool fokus)
{
    /*
     * Wstecz różni się od pozostałych przycisków tylko stylem, nie rysunkiem.
     * Wcześniej miał własną, uboższą ramkę bez narożników i odstawał od reszty
     * paska - teraz korzysta z tej samej funkcji, więc zmiana wyglądu przycisku
     * automatycznie obejmuje również jego.
     */
    UI_RysujPrzycisk(UI_WSTECZ_X, UI_WSTECZ_Y, UI_WSTECZ_W, UI_WSTECZ_H,
                     JEZYK_Tekst(TEKST_WSTECZ),
                     fokus ? UI_STYL_AKCENT : UI_STYL_POWROT, FONT_FRAN);
}

bool UI_CzyDotknietoWstecz(LCDPoint punkt)
{
    const UI_PROSTOKAT_t obszar = UI_ObszarWsteczDolny();
    return UI_CzyPunktWObszarze(punkt, &obszar);
}

void UI_RysujPasekGorny(const char *tytul, bool pokaz_wstecz, bool fokus_wstecz,
                        const UI_STATUS_t *status)
{
    UI_STATUS_t status_lokalny;
    const UI_STATUS_t *status_do_rysowania = status;
    const LCDColor tlo = UI_Paleta()->naglowek_tlo;
    const LCDColor linia = UI_Paleta()->pasek_stanu_linia;
    const uint16_t x_tytulu = 8U;
    const uint16_t x_statusu = 300U;
    const uint16_t szer_tytulu = (uint16_t)(x_statusu - x_tytulu - 6U);
    uint32_t font;
    int y;
    char bufor[16];

    /*
     * Status jest usługą wspólną. Moduły nie muszą znać ADC, RTC ani GPIO.
     * Jeżeli ekran nie przekazał specjalnego statusu (typowy przypadek),
     * pobieramy go z jednego dostawcy zarejestrowanego przez MainWnd.
     */
    if (status_do_rysowania == 0 && UI_PobierzBiezacyStatus(&status_lokalny))
        status_do_rysowania = &status_lokalny;

    /*
     * Tylko ekran startowy używa pełnego paska projektu. Rozpoznajemy go po
     * nazwie projektu przekazanej jednocześnie jako tytuł i status.nazwa.
     * Pozostałe ekrany, również te bez przycisku Wstecz, dostają jeden
     * wspólny pasek tytułu z ikonami statusu po prawej.
     */
    if (!pokaz_wstecz && status_do_rysowania != 0 && tytul != 0 &&
        status_do_rysowania->nazwa != 0 &&
        strcmp(tytul, status_do_rysowania->nazwa) == 0)
    {
        UI_RysujPasekStanu(status_do_rysowania);
        return;
    }

    LCD_FillRect(LCD_MakePoint(0, 0), LCD_MakePoint(479, 31), tlo);
    LCD_HLine(LCD_MakePoint(0, 31), 480, linia);
    UI_RysujOzdobePaskaRetro();

    /*
     * Od wcześniejszej weryfikacji przycisk Wstecz nie zajmuje już lewego górnego narożnika.
     * Sam pasek górny pozostaje wyłącznie nagłówkiem. Ekrany korzystające z
     * pokaz_wstecz rysują wspólny, duży przycisk w lewym dolnym rogu.
     * Rysujemy go tutaj jako domyślną warstwę; ekrany z własnym dolnym paskiem
     * powinny narysować UI_RysujWsteczDolny() ponownie na końcu renderowania.
     */
    if (pokaz_wstecz)
        UI_RysujWsteczDolny(fokus_wstecz);

    {
        char tytul_bezpieczny[96];
        if (tytul == 0)
            tytul = "";
        font = UI_DobierzFontDoPola(FONT_FRANBIG, tytul, szer_tytulu, 30U);
        UI_SkrocTekstDoSzerokosci(tytul, font, szer_tytulu,
                                  tytul_bezpieczny, sizeof(tytul_bezpieczny));
        y = ((int)UI_WYSOKOSC_PASKA_GORNEGO - (int)FONT_GetHeight(font)) / 2 + (int)UI_PASEK_GORNY_TYTUL_OFFSET_Y;
        if (y < 1)
            y = 1;
        FONT_Write(font, UI_TEKST_NAGLOWKA, tlo, x_tytulu, (uint16_t)y, tytul_bezpieczny);
    }

    if (status_do_rysowania == 0)
        return;

    /*
     * Ten sam zestaw ikon występuje na wszystkich podstronach. W ciasnym
     * pasku pokazujemy tylko informacje, które da się odczytać bez tekstowej
     * ściany: zegar, SD, RF i stan akumulatora. Napięcie pozostaje na pełnym
     * pasku startowym, gdzie jest na nie miejsce.
     */
    UI_RysujIkoneZegara(302U, (uint16_t)(7U + UI_PASEK_GORNY_OFFSET_Y), status_do_rysowania->rtc_obecny);
    if (status_do_rysowania->rtc_obecny)
        snprintf(bufor, sizeof(bufor), "%02lu:%02lu",
                 (unsigned long)(status_do_rysowania->czas / 100U),
                 (unsigned long)(status_do_rysowania->czas % 100U));
    else
        strcpy(bufor, "--:--");
    FONT_Write(FONT_KAFEL,
               status_do_rysowania->rtc_obecny ? UI_TEKST_NORMALNY : UI_TEKST_NIEAKTYWNY,
               tlo, 320U, (uint16_t)(7U + UI_PASEK_GORNY_TYTUL_OFFSET_Y), bufor);

    UI_RysujIkoneSD(371U, (uint16_t)(7U + UI_PASEK_GORNY_OFFSET_Y), status_do_rysowania->karta_sd_obecna);
    UI_RysujIkoneRF(396U, (uint16_t)(7U + UI_PASEK_GORNY_OFFSET_Y), status_do_rysowania->rf_aktywne);
    UI_RysujBaterie(431U, (uint16_t)(7U + UI_PASEK_GORNY_OFFSET_Y), status_do_rysowania->procent_baterii,
                    status_do_rysowania->bateria_obecna);
}

void UI_RysujPrzelacznik(uint16_t x, uint16_t y, bool wlaczony, bool fokus, bool aktywny)
{
    const uint16_t w = 44U;
    const uint16_t h = 22U;
    LCDColor tlo;
    LCDColor ramka;
    LCDColor galka;
    uint16_t cx;

    if (!aktywny)
    {
        tlo = UI_TLO_NIEAKTYWNEGO;
        ramka = UI_RAMKA_NIEAKTYWNA;
        galka = UI_TEKST_NIEAKTYWNY;
    }
    else if (wlaczony)
    {
        tlo = UI_TLO_AKTYWNEGO;
        ramka = fokus ? UI_RAMKA_AKCENTU : UI_RAMKA_AKTYWNEGO;
        galka = UI_TEKST_NORMALNY;
    }
    else
    {
        tlo = UI_TLO_PRZYCISKU;
        ramka = fokus ? UI_RAMKA_AKCENTU : UI_RAMKA_NORMALNA;
        galka = UI_TEKST_NIEAKTYWNY;
    }

    LCD_FillRect(LCD_MakePoint(x, y), LCD_MakePoint((uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y), LCD_MakePoint((uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U)), ramka);
    cx = wlaczony ? (uint16_t)(x + w - 11U) : (uint16_t)(x + 10U);
    LCD_FillCircle(LCD_MakePoint(cx, (uint16_t)(y + h / 2U)), 7U, galka);
}

void UI_RysujWierszListy(uint16_t x, uint16_t y, uint16_t szerokosc,
                         const UI_WIERSZ_LISTY_t *wiersz, bool fokus)
{
    LCDColor tlo;
    LCDColor ramka;
    LCDColor tekst;
    uint16_t wysokosc = UI_WYSOKOSC_WIERSZA_LISTY;
    uint16_t prawy;
    uint16_t tekst_x;
    int py;

    if (wiersz == 0 || szerokosc < 80U)
        return;

    if (!wiersz->aktywny)
    {
        tlo = UI_TLO_NIEAKTYWNEGO;
        ramka = UI_RAMKA_NIEAKTYWNA;
        tekst = UI_TEKST_NIEAKTYWNY;
    }
    else
    {
        /*
         * Wiersz listy niesie własny styl. W test21 pole istniało w strukturze,
         * ale renderer go ignorował, przez co ostrzeżenie i akcent wyglądały
         * identycznie jak zwykła pozycja. Tło pozostaje spokojne i wspólne,
         * natomiast ramka oraz tekst przekazują znaczenie stanu.
         */
        tlo = UI_Paleta()->pole_tlo;
        ramka = fokus ? UI_RAMKA_AKCENTU : UI_KolorRamki(wiersz->styl);
        tekst = UI_KolorTekstu(wiersz->styl);
    }

    prawy = (uint16_t)(x + szerokosc - 1U);
    LCD_FillRect(LCD_MakePoint(x, y), LCD_MakePoint(prawy, (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y), LCD_MakePoint(prawy, (uint16_t)(y + wysokosc - 1U)), ramka);
    if (fokus && wiersz->aktywny && szerokosc > 4U)
        LCD_Rectangle(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                      LCD_MakePoint((uint16_t)(prawy - 1U), (uint16_t)(y + wysokosc - 2U)), ramka);

    py = (int)y + ((int)wysokosc - (int)FONT_GetHeight(FONT_FRAN)) / 2;
    if (py < (int)y + 1)
        py = (int)y + 1;
    tekst_x = (uint16_t)(x + 10U);
    if (wiersz->ikona_plus_jeden > 0U)
    {
        const uint32_t indeks_ikony = (uint32_t)wiersz->ikona_plus_jeden - 1U;
        if (indeks_ikony < (uint32_t)UI_IKONA_MENU_LICZBA)
        {
            const uint8_t zestaw = UI_AktualnyZestawIkon();
            const LCDColor kolor_ikony = wiersz->aktywny ? UI_IKONA_KOLOR : UI_TEKST_NIEAKTYWNY;
            if (zestaw == 0U)
            {
                if (UI_RysujIkoneBitmapZestawu((UI_IKONA_MENU_t)indeks_ikony, zestaw,
                                               (int)x + 4, (int)y + ((int)wysokosc - 30) / 2,
                                               72, 30, tlo, kolor_ikony))
                    tekst_x = (uint16_t)(x + 82U);
            }
            else if (UI_RysujIkoneBitmapZestawu((UI_IKONA_MENU_t)indeks_ikony, zestaw,
                                                (int)x + 8, (int)y + ((int)wysokosc - 18) / 2,
                                                18, 18, tlo, kolor_ikony))
            {
                tekst_x = (uint16_t)(x + 34U);
            }
        }
    }
    FONT_Write(FONT_FRAN, tekst, tlo, tekst_x, (uint16_t)py,
               wiersz->tekst ? wiersz->tekst : "");

    if (wiersz->typ == UI_WIERSZ_Z_PRZELACZNIKIEM)
    {
        UI_RysujPrzelacznik((uint16_t)(x + szerokosc - 54U),
                            (uint16_t)(y + (wysokosc - 22U) / 2U),
                            wiersz->przelacznik_wlaczony, fokus, wiersz->aktywny);
    }
    else
    {
        if (wiersz->wartosc != 0 && wiersz->wartosc[0] != '\0')
        {
            FONT_Write_RightAlign(FONT_FRAN, wiersz->aktywny ? UI_KolorTekstu(wiersz->styl) : UI_TEKST_NIEAKTYWNY,
                                  tlo, (uint16_t)(x + szerokosc / 2U), (uint16_t)py,
                                  (uint16_t)(x + szerokosc - 28U), wiersz->wartosc);
        }
        if (wiersz->typ == UI_WIERSZ_Z_WARTOSCIA)
        {
            const uint16_t fh = (uint16_t)FONT_GetHeight(FONT_FRANBIG);
            const uint16_t ay = (uint16_t)(y + (wysokosc > fh ? (wysokosc - fh) / 2U : 0U));
            FONT_Write(FONT_FRANBIG, tekst, tlo, (uint16_t)(x + szerokosc - 20U), ay, ">");
        }
    }
}

void UI_RysujPasekPrzewijania(uint16_t x, uint16_t y, uint16_t wysokosc,
                              const UI_LISTA_STAN_t *stan)
{
    uint32_t uchwyt_h;
    uint32_t zakres;
    uint32_t pozycja;

    if (stan == 0 || wysokosc < UI_MIN_WYSOKOSC_UCHWYTU_PRZEWIJANIA ||
        stan->liczba_pozycji == 0U || stan->liczba_pozycji <= stan->liczba_widocznych)
        return;

    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + UI_SZEROKOSC_PASKA_PRZEWIJANIA - 1U),
                               (uint16_t)(y + wysokosc - 1U)),
                 UI_TLO_PRZYCISKU);

    uchwyt_h = ((uint32_t)wysokosc * stan->liczba_widocznych) / stan->liczba_pozycji;
    if (uchwyt_h < UI_MIN_WYSOKOSC_UCHWYTU_PRZEWIJANIA)
        uchwyt_h = UI_MIN_WYSOKOSC_UCHWYTU_PRZEWIJANIA;
    if (uchwyt_h > wysokosc)
        uchwyt_h = wysokosc;

    zakres = (uint32_t)stan->liczba_pozycji - stan->liczba_widocznych;
    pozycja = (zakres == 0U) ? 0U :
              (((uint32_t)wysokosc - uchwyt_h) * stan->pierwszy_widoczny) / zakres;

    LCD_FillRect(LCD_MakePoint(x, (uint16_t)(y + pozycja)),
                 LCD_MakePoint((uint16_t)(x + UI_SZEROKOSC_PASKA_PRZEWIJANIA - 1U),
                               (uint16_t)(y + pozycja + uchwyt_h - 1U)),
                 UI_TEKST_NIEAKTYWNY);
}

void UI_RysujListe(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                   const UI_WIERSZ_LISTY_t *wiersze, uint16_t liczba,
                   const UI_LISTA_STAN_t *stan)
{
    uint16_t i;
    uint16_t widoczne_geometria;
    uint16_t widoczne;
    uint16_t wiersz_szerokosc;
    bool ma_scroll;

    if (wiersze == 0 || stan == 0 || szerokosc < 100U || wysokosc < UI_WYSOKOSC_WIERSZA_LISTY)
        return;

    widoczne_geometria = (uint16_t)((wysokosc + UI_ODSTEP_WIERSZY_LISTY) /
                                    (UI_WYSOKOSC_WIERSZA_LISTY + UI_ODSTEP_WIERSZY_LISTY));
    widoczne = stan->liczba_widocznych;
    if (widoczne > widoczne_geometria)
        widoczne = widoczne_geometria;

    ma_scroll = stan->liczba_pozycji > widoczne;
    wiersz_szerokosc = ma_scroll && szerokosc > 10U ? (uint16_t)(szerokosc - 10U) : szerokosc;

    for (i = 0U; i < widoczne; ++i)
    {
        const uint16_t indeks = (uint16_t)(stan->pierwszy_widoczny + i);
        const uint16_t yy = (uint16_t)(y + i * (UI_WYSOKOSC_WIERSZA_LISTY + UI_ODSTEP_WIERSZY_LISTY));
        if (indeks >= liczba || indeks >= stan->liczba_pozycji)
            break;
        UI_RysujWierszListy(x, yy, wiersz_szerokosc, &wiersze[indeks],
                            stan->fokus_widoczny && indeks == stan->zaznaczony);
    }

    if (ma_scroll)
        UI_RysujPasekPrzewijania((uint16_t)(x + szerokosc - UI_SZEROKOSC_PASKA_PRZEWIJANIA),
                                  y, wysokosc, stan);
}

int16_t UI_ListaIndeksPoDotyku(LCDPoint punkt, uint16_t x, uint16_t y,
                               uint16_t szerokosc, uint16_t wysokosc,
                               const UI_LISTA_STAN_t *stan)
{
    uint16_t rel_y;
    uint16_t slot;
    uint16_t w_slot = (uint16_t)(UI_WYSOKOSC_WIERSZA_LISTY + UI_ODSTEP_WIERSZY_LISTY);
    uint16_t indeks;

    if (stan == 0 || punkt.x < x || punkt.x >= (uint32_t)x + szerokosc ||
        punkt.y < y || punkt.y >= (uint32_t)y + wysokosc)
        return -1;

    rel_y = (uint16_t)(punkt.y - y);
    slot = (uint16_t)(rel_y / w_slot);
    if ((uint16_t)(rel_y % w_slot) >= UI_WYSOKOSC_WIERSZA_LISTY)
        return -1; /* dotknieto przerwy miedzy wierszami */
    if (slot >= stan->liczba_widocznych)
        return -1;

    indeks = (uint16_t)(stan->pierwszy_widoczny + slot);
    if (indeks >= stan->liczba_pozycji)
        return -1;
    return (int16_t)indeks;
}

void UI_RysujWskaznikPostepu(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                             uint8_t procent, const char *tekst)
{
    uint16_t wypelnienie;
    char auto_tekst[16];
    const char *pokaz = tekst;
    LCDColor tlo = UI_Paleta()->pole_tlo;
    LCDColor ramka = UI_Paleta()->pole_ramka;

    if (szerokosc < 12U || wysokosc < 10U)
        return;
    if (procent > 100U)
        procent = 100U;

    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), ramka);

    wypelnienie = (uint16_t)(((uint32_t)(szerokosc - 2U) * procent) / 100U);
    if (wypelnienie > 0U)
        LCD_FillRect(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                     LCD_MakePoint((uint16_t)(x + wypelnienie), (uint16_t)(y + wysokosc - 2U)),
                     UI_TLO_AKCENTU);

    if (pokaz == 0 || pokaz[0] == '\0')
    {
        snprintf(auto_tekst, sizeof(auto_tekst), "%u%%", (unsigned)procent);
        pokaz = auto_tekst;
    }
    UI_RysujTekstWycentrowany(x, y, szerokosc, wysokosc, pokaz, FONT_FRAN,
                              UI_TEKST_NORMALNY, tlo);
}


/* ==========================================================================
 * Wspólny wodospad
 * ========================================================================== */

extern void BSP_LCD_HLineShift(uint16_t startYY, uint16_t endYY,
                               uint16_t startXX, uint16_t endXX);
extern void BSP_LCD_DrawColorLine(uint16_t posYY, uint16_t startXX,
                                  uint16_t endXX, uint32_t *lineColorInfoBuff);

static uint32_t g_ui_wodospad_linia[UI_WODOSPAD_SZEROKOSC];
static uint16_t g_ui_wodospad_poziomy[UI_WODOSPAD_SZEROKOSC];
#define UI_WODOSPAD_HISTORIA_WIERSZE 272U
/* Historia jest duża, ale trafia do zewnętrznej SDRAM. W Flash kosztuje tylko kod obsługi. */
static uint8_t __attribute__((section(".user_sdram")))
    g_ui_wodospad_historia[UI_WODOSPAD_HISTORIA_WIERSZE][UI_WODOSPAD_SZEROKOSC];
static uint16_t g_ui_wodospad_historia_glowa;
static uint16_t g_ui_wodospad_historia_liczba;

uint32_t UI_WodospadKolor(uint16_t poziom)
{
    uint16_t p = poziom;
    uint16_t q;

    if (p > 1023U)
        p = 1023U;

    if (p < 256U)
        return LCD_RGB(0U, p, 255U);

    if (p < 512U)
    {
        q = (uint16_t)(p - 256U);
        return LCD_RGB(0U, 255U, (uint16_t)(255U - q));
    }

    if (p < 768U)
    {
        q = (uint16_t)(p - 512U);
        return LCD_RGB(q, 255U, 0U);
    }

    q = (uint16_t)(p - 768U);
    return LCD_RGB(255U, (uint16_t)(255U - q), 0U);
}

uint32_t *UI_WodospadBuforLinii(void)
{
    return g_ui_wodospad_linia;
}

uint16_t *UI_WodospadBuforPoziomow(void)
{
    return g_ui_wodospad_poziomy;
}

void UI_WodospadWyczysc(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if (x0 > x1 || y0 > y1 || x1 >= UI_WODOSPAD_SZEROKOSC)
        return;

    LCD_FillRect(LCD_MakePoint(x0, y0), LCD_MakePoint(x1, y1), UI_WodospadKolor(0U));
}

void UI_WodospadDodajLinie(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                           const uint32_t *linia)
{
    if (linia == 0 || x0 > x1 || y0 > y1 || x1 >= UI_WODOSPAD_SZEROKOSC)
        return;

    BSP_LCD_HLineShift(y0, y1, x0, x1);
    BSP_LCD_DrawColorLine(y0, x0, x1, (uint32_t *)linia);
}

void UI_WodospadHistoriaResetuj(void)
{
    g_ui_wodospad_historia_glowa = 0U;
    g_ui_wodospad_historia_liczba = 0U;
}

void UI_WodospadHistoriaDodaj(const uint16_t *poziomy)
{
    uint16_t x;
    uint8_t *wiersz;

    if (poziomy == 0)
        return;

    wiersz = g_ui_wodospad_historia[g_ui_wodospad_historia_glowa];
    for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
    {
        uint16_t p = poziomy[x];
        if (p > 1023U)
            p = 1023U;
        wiersz[x] = (uint8_t)(p >> 2);
    }

    g_ui_wodospad_historia_glowa++;
    if (g_ui_wodospad_historia_glowa >= UI_WODOSPAD_HISTORIA_WIERSZE)
        g_ui_wodospad_historia_glowa = 0U;
    if (g_ui_wodospad_historia_liczba < UI_WODOSPAD_HISTORIA_WIERSZE)
        g_ui_wodospad_historia_liczba++;
}

void UI_WodospadHistoriaRysuj(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint16_t y;
    uint16_t wysokosc;
    uint16_t wiek;
    uint32_t *kolory = g_ui_wodospad_linia;

    if (x0 > x1 || y0 > y1 || x1 >= UI_WODOSPAD_SZEROKOSC)
        return;

    UI_WodospadWyczysc(x0, y0, x1, y1);
    wysokosc = (uint16_t)(y1 - y0 + 1U);
    if (wysokosc > g_ui_wodospad_historia_liczba)
        wysokosc = g_ui_wodospad_historia_liczba;

    for (wiek = 0U; wiek < wysokosc; ++wiek)
    {
        uint16_t indeks;
        uint16_t x;
        const uint8_t *wiersz;

        if (g_ui_wodospad_historia_glowa > wiek)
            indeks = (uint16_t)(g_ui_wodospad_historia_glowa - 1U - wiek);
        else
            indeks = (uint16_t)(UI_WODOSPAD_HISTORIA_WIERSZE + g_ui_wodospad_historia_glowa - 1U - wiek);

        wiersz = g_ui_wodospad_historia[indeks];
        for (x = x0; x <= x1; ++x)
            kolory[x - x0] = UI_WodospadKolor((uint16_t)wiersz[x] << 2);

        y = (uint16_t)(y0 + wiek);
        BSP_LCD_DrawColorLine(y, x0, x1, kolory);
    }
}
