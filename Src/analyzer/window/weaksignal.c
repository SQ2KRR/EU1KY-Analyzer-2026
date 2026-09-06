/*
  Weaksignal TX for STM32F746 with si5351_HS Library
  kd8cec@gmail.com

  by KD8CEC
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
#include <math.h>
#include <complex.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "config.h"
#include "LCD.h"
#include "font.h"
#include "touch.h"
#include "hit.h"
#include "dsp.h"
#include "gen.h"
#include "stm32f746xx.h"
#include "oslfile.h"
#include "stm32746g_discovery_lcd.h"
#include "match.h"
#include "num_keypad.h"
#include "screenshot.h"
//#include "smith.h"
//#include "measurement.h"
#include "panfreq.h"

#include "DS3231.h"

#include "si5351.h"
#include "si5351_hs.h"
#include "JTEncode.h"
#include "keyboard.h"
#include "gpio_control.h"
#include "jezyk.h"
#include "ui_wspolny.h"
#include "wejscia_uzytkownika.h"
#include "komunikaty.h"

#define BACK_COLOR (UI_KolorTlaEkranu())
#define SELECT_FREQ_COLOR LCD_RGB(255, 127, 39)
#define SELECT_PROTOCOL_COLOR LCD_RGB(255, 30, 30)

#include "audioirq.h"

/*
 * Tor audio WSPR pracuje z 8 kHz i dwiema identycznymi probkami L/R.
 * Bufor DMA ma dwie polowy po 1024 ramek stereo. Polowa trwa 128 ms, dzieki
 * czemu procesor ma duzy zapas na uzupelnienie czesci, ktora DMA juz zuzylo.
 */
#define WSPR_AUDIO_HZ 8000U
#define WSPR_AUDIO_RAMKI_POL_BUFORA 1024U
#define WSPR_AUDIO_RAMKI_BUFORA (2U * WSPR_AUDIO_RAMKI_POL_BUFORA)
#define WSPR_AUDIO_KANALY 2U
#define AUDIO_OUT_SIZE (WSPR_AUDIO_RAMKI_BUFORA * WSPR_AUDIO_KANALY)
static int16_t __attribute__((section(".user_sdram"))) AUDIO_BUFFER_OUT[AUDIO_OUT_SIZE];

/* WSPR: 12000/8192 Hz = 375/256 Hz. */
#define WSPR_TONE_SPACING (375.0 / 256.0)
#define WSPR_AUDIO_RAMKI_SYMBOL_X3 16384ULL
#define WSPR_AUDIO_LICZBA_SYMBOLI 162U
#define WSPR_AUDIO_RAMKI_CALKOWITE \
    ((uint32_t)(((uint64_t)WSPR_AUDIO_LICZBA_SYMBOLI * WSPR_AUDIO_RAMKI_SYMBOL_X3) / 3ULL))

extern void Sleep(uint32_t ms);
extern void TRACK_Beep(int duration);

extern void JTEncode(void);                       //for JTEncode converted C++ -> C
extern uint32_t GetInternTime(uint8_t *secondsx); //at mainwind.c
//=============================================================================
//FREQUENCYS for Digital Mode
//By KD8CEC
//From WSJ-X Version 2.0
//-----------------------------------------------------------------------------
#define BAND_LENGTH 15
#define BAND_MAX_INDEX (BAND_LENGTH - 1)
const char *BAND_NAME[] = {"2190", "630m", "160m", "80m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m", "4m", "2m", "XCVR"};
const uint32_t FREQS_WSPR[] = {136000, 474200, 1836600, 3568600, 7038600, 10138700, 14095600, 18104600, 21094600, 24924600, 28124600, 50293000, 70091000, 144489000};
const uint32_t FREQS_FT8[] = {136130, 474200, 1840000, 3573000, 7074000, 10136000, 14074000, 18100000, 21074000, 24915000, 28074000, 50313000, 70100000, 144174000};
const uint32_t FREQS_JT65[] = {136130, 474200, 1838000, 3570000, 7076000, 10138000, 14076000, 18102000, 21076000, 24917000, 28076000, 50276000, 70102000, 144120000};
const uint32_t FREQS_JT9[] = {136130, 474200, 1839000, 3572000, 7078000, 10140000, 14078000, 18104000, 21078000, 24919000, 28078000, 50312000, 70104000, 144120000};
const uint32_t FREQS_FT4[] = {136130, 474200, 1839000, 3568000, 7047000, 10140000, 14080000, 18104000, 21140000, 24919000, 28180000, 50318000, 70104000, 144170000};

uint32_t WSFreq = 814074000;

//Draw ImageButton
//BOTTOM
#define MENU_EXIT 0
#define MENU_SAVECONFIG 1
#define MENU_SETCONTINUE 2
#define MENU_MSEND 3   //Probe Select Down
#define MENU_RTCSEND 4 //Probe Select Up

//RIGHT
#define MENU_BANDDOWN 5   //Prior Band
#define MENU_BANDUP 6     //Next Band
#define MENU_AFFREQDOWN 7 //-10Hz
#define MENU_AFFREQUP 8   //+10Hz

//FREQ SELECT MENU
#define MENU_TOP1 9
#define MENU_TOP2 10
#define MENU_TOP3 11
#define MENU_TOP4 12

#define wsMenus_Length 13

#define WSPR_AUDIO_VOLUME 70

#define FREQ_MENU_TOP 95
#define TOP_MENU_TOP 0
#define FREQ_INFO_TOP 80


//uint8_t isOnAir = 1;

#define PR_WSPR 0
#define PR_FT8 1
#define PR_FT4 2
#define PR_JT65 3
#define PR_JT9 4

//uint8_t nowPR = PR_WSPR;  //
//uint8_t nowBandIndex = 4;

static char g_ws_data[59] = {0};
char *wsCallSign = &g_ws_data[0];  //11
char *wsLocation = &g_ws_data[12]; //9

uint8_t wsDBM = 0;
char *wsDBMStr = &g_ws_data[23];  //7
char *wsMessage = &g_ws_data[31]; //21 = 52
char *nowPR = &g_ws_data[53];
uint8_t *nowBandIndex = (uint8_t *)&g_ws_data[54];

char *wsTXPower = &g_ws_data[55];

/*
 * Ostatnie dwa bajty historycznego pliku /aa/wsignal.bin przechowuja
 * korekcje czestotliwosci audio jako int16_t. Wczesniej kod rzutowal adres
 * g_ws_data[57] bezposrednio na int16_t*. Offset 57 jest nieparzysty, wiec
 * taki wskaznik moze byc niewyrownany i jest niepotrzebnie zależny od
 * zachowania procesora oraz kompilatora. Stan roboczy trzymamy teraz w
 * normalnie wyrownanej zmiennej, a dwa bajty kopiujemy tylko przy
 * odczycie i zapisie pliku. Format pliku pozostaje bez zmian.
 */
static int16_t g_ws_audio_freq = 0;
int16_t *wsAUDIOFreq = &g_ws_audio_freq;

char wsKeyboardTmp[21];

//void char *GetBandName()
//{

//}

#define WS_ST_NONE 0        //Not Status
#define WS_ST_READY 1       //RTC Send Ready
#define WS_ST_SENDMANUAL1 2 //Send By Manual
#define WS_ST_SENDMANUAL2 3 //Send By Manual and Continue (not support)
#define WS_ST_SENDRTC1 4    //Send by RTC
#define WS_ST_SENDRTC2 5    //Send By Manual and Continue

uint8_t NowStatus = WS_ST_NONE;

#include "ff.h"
static const char *g_ws_fpath = "/aa/wsignal.bin";

#define WS_AUDIO_OFFSET_W_PLIKU 57U

static void WS_WczytajAudioZBufora(void)
{
    memcpy(&g_ws_audio_freq, &g_ws_data[WS_AUDIO_OFFSET_W_PLIKU],
           sizeof(g_ws_audio_freq));
}

static void WS_ZapiszAudioDoBufora(void)
{
    memcpy(&g_ws_data[WS_AUDIO_OFFSET_W_PLIKU], &g_ws_audio_freq,
           sizeof(g_ws_audio_freq));
}

void WS_StoredInformatoin(void)
{
    FRESULT res;
    FIL fo = {0};

    /* Zachowujemy zgodnosc binarna z dotychczasowym plikiem ustawien. */
    WS_ZapiszAudioDoBufora();

    res = f_open(&fo, g_ws_fpath, FA_OPEN_ALWAYS | FA_WRITE);
    if (FR_OK == res)
    {
        UINT bw;
        res = f_write(&fo, g_ws_data, sizeof(g_ws_data), &bw);
        res = f_close(&fo);
    }
}

void SetDefaultFrequency()
{
    if (*nowBandIndex == BAND_MAX_INDEX)
    {
        WSFreq = 0;
    }
    else
    {
        if (*nowPR == PR_WSPR)
            WSFreq = FREQS_WSPR[*nowBandIndex];
        else if (*nowPR == PR_FT8)
            WSFreq = FREQS_FT8[*nowBandIndex];
        else if (*nowPR == PR_JT65)
            WSFreq = FREQS_JT65[*nowBandIndex];
        else if (*nowPR == PR_JT9)
            WSFreq = FREQS_JT9[*nowBandIndex];
        else if (*nowPR == PR_FT4)
            WSFreq = FREQS_FT4[*nowBandIndex];
    }
}

#include "crash.h"

static void WS_TekstNaWielkie(char *tekst, size_t maks_dlugosc)
{
    size_t i;

    if (tekst == 0)
        return;

    for (i = 0; i < maks_dlugosc && tekst[i] != '\0'; ++i)
        tekst[i] = (char)toupper((unsigned char)tekst[i]);
}

static uint8_t WS_CzySameCyfry(const char *tekst)
{
    size_t i;

    if (tekst == 0 || tekst[0] == '\0')
        return 0U;

    for (i = 0U; tekst[i] != '\0'; ++i)
    {
        if (!isdigit((unsigned char)tekst[i]))
            return 0U;
    }
    return 1U;
}

static void WS_NormalizujDane(void)
{
    /*
     * wsignal.bin pochodzi jeszcze ze starej wersji programu i nie ma pola
     * wersji formatu. Dlatego po odczycie zawsze stawiamy terminatory w
     * granicach, ktore sa bezpieczne dla rzeczywistych koderow protokolow.
     */
    wsCallSign[6] = '\0';
    wsLocation[4] = '\0';
    wsDBMStr[7] = '\0';
    wsMessage[18] = '\0';

    WS_TekstNaWielkie(wsCallSign, 6U);
    WS_TekstNaWielkie(wsLocation, 4U);

    if ((uint8_t)*nowPR > PR_JT9)
        *nowPR = PR_WSPR;
    if (*nowBandIndex > BAND_MAX_INDEX)
        *nowBandIndex = 4U;

    if (((*wsTXPower & 0x0FU) != 0x02U) &&
        ((*wsTXPower & 0x0FU) != 0x04U) &&
        ((*wsTXPower & 0x0FU) != 0x06U) &&
        ((*wsTXPower & 0x0FU) != 0x08U))
        *wsTXPower = 0x08U;

    /*
     * Starszy format zapisywal w bicie 0x10 wybor S1/CLK0. W EU1KY-PL 2026
     * bezposrednie tryby cyfrowe maja jedno stale wyjscie GEN OUT = S2/CLK2.
     * Zachowujemy wybrany prad 2/4/6/8 mA, ale migrujemy stare ustawienie
     * portu bez zmiany formatu pliku wsignal.bin.
     */
    *wsTXPower &= 0x0FU;

    if (*wsAUDIOFreq < -1400)
        *wsAUDIOFreq = -1400;
    else if (*wsAUDIOFreq > 1400)
        *wsAUDIOFreq = 1400;

    if (WS_CzySameCyfry(wsDBMStr))
        wsDBM = (uint8_t)atoi(wsDBMStr);
    else
    {
        snprintf(wsDBMStr, 8U, "10");
        wsDBM = 10U;
    }
}

void WS_LoadInformation(void)
{
    FRESULT res;
    FIL fo = {0};
    FILINFO finfo;

    res = f_stat(g_ws_fpath, &finfo);
    if (FR_OK == res)
    {
        UINT br = 0U;
        res = f_open(&fo, g_ws_fpath, FA_READ);
        if (FR_OK == res)
        {
            (void)f_read(&fo, g_ws_data, sizeof(g_ws_data), &br);
            (void)f_close(&fo);

            if (br == sizeof(g_ws_data))
                WS_WczytajAudioZBufora();
            else
                g_ws_audio_freq = 0;
        }
    }
    else
    {
        memset(g_ws_data, 0, sizeof(g_ws_data));
        snprintf(wsDBMStr, 8U, "10");
        wsDBM = 10U;
        *wsTXPower = 0x08U;
        *nowPR = PR_WSPR;
        *nowBandIndex = 4U; /* 40 m */
        g_ws_audio_freq = 0;
    }

    WS_NormalizujDane();
    SetDefaultFrequency();
}

//==============================================================================
//DISPLAY Protocol Information
//------------------------------------------------------------------------------
#define INFO_TOP 130
/* Drugi rząd informacji musi kończyć się przed wspólnym paskiem dolnym
 * zaczynającym się na y=220. Przy wysokości 44 px wartość 174 daje
 * obszar 175..218 i zostawia jeden piksel oddechu przed przyciskami. */
#define INFO_LINE2 174

//#define TITLE_COLOR LCD_RGB(0, 63, 119)
#define TITLE_COLOR (UI_KolorTlaPrzycisku(UI_STYL_NORMALNY))

extern uint32_t RTCpresent;

static uint32_t g_ws_pozostalo_ms = 0U;
static uint8_t g_ws_w_oknie_startu = 0U;
/* Cache napisu czasu należy zadeklarować przed funkcjami, które go używają. */
static char g_ws_czas_poprzedni[32] = "";

/*
 * DS3231 nie udostepnia ulamkow sekundy. Do FT4 potrzebujemy jednak rastra
 * 7,5 s, wiec faze polowy sekundy odtwarzamy z HAL_GetTick(). Punkt poczatku
 * nowej sekundy jest korygowany przy kazdej zmianie wskazania RTC. Typowy blad
 * jest wtedy ograniczony do okresu sprawdzania czasu w petli interfejsu.
 */
static uint8_t g_ws_ostatnia_sekunda = 0xFFU;
static uint32_t g_ws_tick_poczatku_sekundy = 0U;
static uint8_t g_ws_faza_sekundy_gotowa = 0U;

static uint16_t WS_MilisekundyWSekundzie(uint8_t sekunda)
{
    const uint32_t teraz = HAL_GetTick();

    if (g_ws_ostatnia_sekunda == 0xFFU)
    {
        g_ws_ostatnia_sekunda = sekunda;
        g_ws_tick_poczatku_sekundy = teraz;
        return 0U;
    }

    if (sekunda != g_ws_ostatnia_sekunda)
    {
        g_ws_ostatnia_sekunda = sekunda;
        g_ws_tick_poczatku_sekundy = teraz;
        g_ws_faza_sekundy_gotowa = 1U;
        return 0U;
    }

    if (!g_ws_faza_sekundy_gotowa)
        return 0U;

    {
        uint32_t uplynelo = teraz - g_ws_tick_poczatku_sekundy;
        if (uplynelo > 999U)
            uplynelo = 999U;
        return (uint16_t)uplynelo;
    }
}

static uint32_t WS_OkresSlotuMs(uint8_t protokol)
{
    switch (protokol)
    {
    case PR_WSPR:
        return 120000U;
    case PR_FT8:
        return 15000U;
    case PR_FT4:
        return 7500U;
    case PR_JT65:
    case PR_JT9:
        return 60000U;
    default:
        return 0U;
    }
}

static uint32_t WS_PozycjaWSlocieMs(uint8_t protokol, uint8_t minuta,
                                    uint8_t sekunda, uint16_t ms)
{
    uint32_t pozycja_ms;
    const uint32_t okres_ms = WS_OkresSlotuMs(protokol);

    if (okres_ms == 0U)
        return 0U;

    if (protokol == PR_WSPR)
    {
        pozycja_ms = (uint32_t)(minuta & 1U) * 60000U;
        pozycja_ms += (uint32_t)sekunda * 1000U + (uint32_t)ms;
    }
    else
    {
        pozycja_ms = (uint32_t)sekunda * 1000U + (uint32_t)ms;
    }

    return pozycja_ms % okres_ms;
}

static uint32_t WS_CzasDoNastepnegoSlotuMs(uint8_t protokol, uint8_t minuta,
                                           uint8_t sekunda, uint16_t ms)
{
    const uint32_t okres_ms = WS_OkresSlotuMs(protokol);
    const uint32_t pozycja_ms = WS_PozycjaWSlocieMs(protokol, minuta, sekunda, ms);

    if (okres_ms == 0U || pozycja_ms == 0U)
        return 0U;
    return okres_ms - pozycja_ms;
}

static uint8_t WS_CzyOknoStartu(uint8_t protokol, uint8_t minuta,
                                uint8_t sekunda, uint16_t ms)
{
    const uint32_t okres_ms = WS_OkresSlotuMs(protokol);
    uint32_t pozycja_ms;

    if (okres_ms == 0U)
        return 0U;

    /*
     * Pętla sprawdza czas co 50 ms. Okno 350 ms daje zapas na odczyt RTC
     * i obsługę LCD, ale pozostaje małe względem całego slotu. Nie rozszerzamy
     * go bardziej, aby po chwilowym obciążeniu interfejsu nie zaczynać emisji
     * wyraźnie po początku właściwego rastra czasowego.
     */
    pozycja_ms = WS_PozycjaWSlocieMs(protokol, minuta, sekunda, ms);
    return (uint8_t)(pozycja_ms <= 350U);
}

static void WS_RysujPoleCzasStale(void)
{
    LCD_FillRect(LCD_MakePoint(258, INFO_LINE2 + 1),
                 LCD_MakePoint(479, INFO_LINE2 + 44), BACK_COLOR);
    LCD_FillRect(LCD_MakePoint(258, INFO_LINE2 + 1),
                 LCD_MakePoint(479, INFO_LINE2 + 15), TITLE_COLOR);
    LCD_Rectangle(LCD_MakePoint(258, INFO_LINE2 + 1),
                  LCD_MakePoint(478, INFO_LINE2 + 44), TITLE_COLOR);
    FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 264, INFO_LINE2,
               JEZYK_Tekst(TEKST_WSPR_CZAS));
    g_ws_czas_poprzedni[0] = '\0';
}

static void CheckTime(void)
{
    short am_pm;
    char tekst[32];
    uint32_t czas_hhmm;
    uint8_t sekunda;
    uint8_t minuta;
    uint16_t ms;

    if (RTCpresent)
        getTime(&czas_hhmm, &sekunda, &am_pm, 0);
    else
        czas_hhmm = GetInternTime(&sekunda);

    minuta = (uint8_t)(czas_hhmm % 100U);
    ms = WS_MilisekundyWSekundzie(sekunda);
    g_ws_pozostalo_ms = WS_CzasDoNastepnegoSlotuMs((uint8_t)*nowPR, minuta, sekunda, ms);
    g_ws_w_oknie_startu = WS_CzyOknoStartu((uint8_t)*nowPR, minuta, sekunda, ms);

    if (*nowPR == PR_WSPR)
    {
        const uint32_t pozostalo_s = (g_ws_pozostalo_ms + 999U) / 1000U;
        snprintf(tekst, sizeof(tekst), "%02lu:%02lu:%02u   %lu.%02lus",
                 (unsigned long)(czas_hhmm / 100U),
                 (unsigned long)(czas_hhmm % 100U),
                 (unsigned int)sekunda,
                 (unsigned long)(pozostalo_s / 60U),
                 (unsigned long)(pozostalo_s % 60U));
    }
    else if (*nowPR == PR_FT4)
    {
        /* Polsekundowy odczyt wystarcza do czytelnego pokazania rastra 7,5 s. */
        const uint32_t polsekundy = (g_ws_pozostalo_ms + 499U) / 500U;
        snprintf(tekst, sizeof(tekst), "%02lu:%02lu:%02u   %lu.%lus",
                 (unsigned long)(czas_hhmm / 100U),
                 (unsigned long)(czas_hhmm % 100U),
                 (unsigned int)sekunda,
                 (unsigned long)(polsekundy / 2U),
                 (unsigned long)((polsekundy & 1U) ? 5U : 0U));
    }
    else
    {
        const uint32_t pozostalo_s = (g_ws_pozostalo_ms + 999U) / 1000U;
        snprintf(tekst, sizeof(tekst), "%02lu:%02lu:%02u   %lus",
                 (unsigned long)(czas_hhmm / 100U),
                 (unsigned long)(czas_hhmm % 100U),
                 (unsigned int)sekunda,
                 (unsigned long)pozostalo_s);
    }

    /*
     * Pole czasu odswiezamy tylko po zmianie napisu. Nawet FT4 zmienia tekst
     * najwyzej dwa razy na sekunde, wiec poprawa synchronizacji nie przywraca
     * migotania ekranu.
     */
    if (strcmp(g_ws_czas_poprzedni, tekst) != 0)
    {
        LCD_FillRect(LCD_MakePoint(261, INFO_LINE2 + 17),
                     LCD_MakePoint(477, INFO_LINE2 + 42), BACK_COLOR);
        FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 264, INFO_LINE2 + 23, tekst);
        snprintf(g_ws_czas_poprzedni, sizeof(g_ws_czas_poprzedni), "%s", tekst);
    }
}

static uint8_t WS_CzyPoprawnyZnakWSPR(const char *znak)
{
    const size_t dlugosc = znak == 0 ? 0U : strlen(znak);
    size_t pozycja_cyfry;
    size_t i;

    if (dlugosc < 3U || dlugosc > 6U)
        return 0U;

    /*
     * Ten koder obsługuje klasyczny znak WSPR typu 1. Po przygotowaniu
     * koder wymaga cyfry na trzeciej pozycji, a po niej dopuszcza tylko
     * litery. Gdy cyfra jest druga, wspr_message_prep() dopisuje z przodu
     * spację; taki znak może mieć najwyżej 5 znaków, bo w przeciwnym razie
     * ostatni znak zostałby bez ostrzeżenia obcięty.
     */
    if (isdigit((unsigned char)znak[1]) &&
        !isdigit((unsigned char)znak[2]))
    {
        if (dlugosc > 5U)
            return 0U;
        pozycja_cyfry = 1U;
    }
    else if (isdigit((unsigned char)znak[2]))
    {
        pozycja_cyfry = 2U;
    }
    else
    {
        return 0U;
    }

    for (i = 0U; i < dlugosc; ++i)
    {
        const unsigned char znak_biezacy = (unsigned char)znak[i];

        if (i > pozycja_cyfry)
        {
            /* Sufiks typu 1 jest kodowany w podstawie 27: spacja lub A..Z. */
            if (znak_biezacy < (unsigned char)'A' ||
                znak_biezacy > (unsigned char)'Z')
                return 0U;
        }
        else if (!isalnum(znak_biezacy))
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t WS_CzyPoprawnyLokatorWSPR(const char *lokator)
{
    if (lokator == 0 || strlen(lokator) != 4U)
        return 0U;

    return (uint8_t)(lokator[0] >= 'A' && lokator[0] <= 'R' &&
                     lokator[1] >= 'A' && lokator[1] <= 'R' &&
                     isdigit((unsigned char)lokator[2]) &&
                     isdigit((unsigned char)lokator[3]));
}

static uint8_t WS_CzyPoprawnaMocWSPR(const char *tekst)
{
    static const uint8_t dozwolone_moce_dbm[] =
        {0U, 3U, 7U, 10U, 13U, 17U, 20U, 23U, 27U, 30U,
         33U, 37U, 40U, 43U, 47U, 50U, 53U, 57U, 60U};
    unsigned int moc;
    size_t i;

    if (!WS_CzySameCyfry(tekst))
        return 0U;

    moc = (unsigned int)atoi(tekst);
    for (i = 0U; i < sizeof(dozwolone_moce_dbm) / sizeof(dozwolone_moce_dbm[0]); ++i)
    {
        if (moc == dozwolone_moce_dbm[i])
            return 1U;
    }

    /*
     * Koder historyczny zaokrąglał niedozwoloną wartość w dół bez informacji
     * dla operatora. Lepiej odrzucić wpis przed nadawaniem niż wysłać inną
     * moc w komunikacie WSPR niż ta, którą użytkownik wpisał na ekranie.
     */
    return 0U;
}

static uint8_t WS_CzyHex(const char *tekst)
{
    size_t i;

    if (tekst == 0 || tekst[0] == '\0')
        return 0U;

    for (i = 0U; tekst[i] != '\0'; ++i)
    {
        if (!isxdigit((unsigned char)tekst[i]))
            return 0U;
    }
    return 1U;
}

static TEKST_ID_t WS_WalidujDane(void)
{
    size_t dlugosc;

    if ((uint8_t)*nowPR > PR_JT9 || *nowBandIndex > BAND_MAX_INDEX)
        return TEKST_WSPR_BLAD_TRYBU;

    if (*nowBandIndex == BAND_MAX_INDEX && *nowPR != PR_WSPR)
        return TEKST_WSPR_AUDIO_TYLKO_WSPR;

    if (*nowPR == PR_WSPR)
    {
        if (!WS_CzyPoprawnyZnakWSPR(wsCallSign))
            return TEKST_WSPR_BLAD_ZNAKU;
        if (!WS_CzyPoprawnyLokatorWSPR(wsLocation))
            return TEKST_WSPR_BLAD_LOKATORA;
        if (!WS_CzyPoprawnaMocWSPR(wsDBMStr))
            return TEKST_WSPR_BLAD_MOCY;
        return TEKST_LICZBA_TEKSTOW;
    }

    dlugosc = strlen(wsMessage);
    if (dlugosc == 0U)
        return TEKST_WSPR_WPISZ_WIADOMOSC;

    if (*nowPR == PR_FT8 || *nowPR == PR_FT4)
    {
        if (dlugosc <= 13U)
            return TEKST_LICZBA_TEKSTOW;
        if (dlugosc <= 18U && WS_CzyHex(wsMessage))
            return TEKST_LICZBA_TEKSTOW;
        return TEKST_WSPR_LIMIT_FT;
    }

    if ((*nowPR == PR_JT65 || *nowPR == PR_JT9) && dlugosc <= 13U)
        return TEKST_LICZBA_TEKSTOW;

    return TEKST_WSPR_LIMIT_JT;
}

static void WS_PokazBlad(TEKST_ID_t blad)
{
    if (blad != TEKST_LICZBA_TEKSTOW)
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, blad);
}

void SetWSStatus()
{
    //TOP MENU
    //On-Air Marked
    int32_t OnAirBackColor;
    int32_t OnAirForeColor;
    char str[20] = "";

    if (NowStatus == WS_ST_NONE)
    {
        OnAirBackColor = LCD_BLUE;
        OnAirForeColor = LCD_WHITE;
        snprintf(str, sizeof(str), "%s", JEZYK_Tekst(TEKST_WSPR_STATUS_GOTOWE));
    }
    else if (NowStatus == WS_ST_READY)
    {
        OnAirBackColor = LCD_RGB(255, 102, 102);
        OnAirForeColor = LCD_WHITE;
        snprintf(str, sizeof(str), "%s", JEZYK_Tekst(TEKST_WSPR_STATUS_CZEKAJ));
    }
    else
    {
        OnAirBackColor = LCD_RED;
        OnAirForeColor = LCD_WHITE;
        snprintf(str, sizeof(str), "%s", JEZYK_Tekst(TEKST_WSPR_STATUS_NADAWANIE));
    }

    LCD_FillRect(LCD_MakePoint(0, TOP_MENU_TOP), LCD_MakePoint(77, TOP_MENU_TOP + 32), OnAirBackColor); //LCD_BLACK);
    LCD_Rectangle(LCD_MakePoint(0, TOP_MENU_TOP), LCD_MakePoint(77, TOP_MENU_TOP + 32), OnAirForeColor);
    {
        int szerokosc = FONT_GetStrPixelWidth(FONT_FRAN, str);
        int x = (77 - szerokosc) / 2;
        if (x < 2)
            x = 2;
        FONT_Write(FONT_FRAN, OnAirForeColor, OnAirBackColor, x, TOP_MENU_TOP + 8, str);
    }
}

//Show AF Frequency
void UpdateAFFreq(void)
{
    char str[10] = "";
    //Clear
    LCD_FillRect(LCD_MakePoint(128, INFO_LINE2 + 1), LCD_MakePoint(255, INFO_LINE2 + 43), BACK_COLOR);

    //AUDIO Frequency
    LCD_FillRect(LCD_MakePoint(128, INFO_LINE2 + 1), LCD_MakePoint(255, INFO_LINE2 + 15), TITLE_COLOR);
    LCD_Rectangle(LCD_MakePoint(128, INFO_LINE2 + 1), LCD_MakePoint(254, INFO_LINE2 + 44), TITLE_COLOR);
    FONT_Write(FONT_FRAN, TextColor, 0, 138, INFO_LINE2, JEZYK_Tekst(TEKST_WSPR_CZEST_AUDIO));
    sprintf(str, "%d", 1500 + *wsAUDIOFreq);
    FONT_Write_RightAlign(FONT_FRANBIG, SELECT_FREQ_COLOR, 0, 128, INFO_LINE2 + 12, 250, str);
}

static void DrawWSInformation(void)
{
    char str[80] = "";

    uint32_t tempFreq = WSFreq;
    uint32_t freqMhs = tempFreq / 1000000;
    tempFreq = tempFreq % 1000000;
    uint32_t freqKhz = tempFreq / 1000;
    uint32_t freqHz = tempFreq % 1000;

    //Clear Frequency Window
    LCD_FillRect(LCD_MakePoint(4, 42), LCD_MakePoint(479, 90), BACK_COLOR);

    if (*nowBandIndex == BAND_MAX_INDEX)
    {
        uint32_t fore_freq_color = LCD_RED;
        //Display Inforamtion for Via Transceiver Mode
        if (*nowPR == PR_WSPR)
            snprintf(str, sizeof(str), "%s", JEZYK_Tekst(TEKST_WSPR_PRZEZ_TRX));
        else
        {
            snprintf(str, sizeof(str), "%s", JEZYK_Tekst(TEKST_WSPR_TYLKO_WSPR));
            fore_freq_color = LCD_GRAY;
        }

        FONT_Write(FONT_FRANBIG, fore_freq_color, BACK_COLOR, 4, 45, str);
    }
    else
    {
        //Frequency
        snprintf(str, sizeof(str), "%lu.%03lu.%03lu ",
                 (unsigned long)freqMhs,
                 (unsigned long)freqKhz,
                 (unsigned long)freqHz);

        //DISPLAY Result of Measure
        FONT_Write_RightAlign(FONT_BDIGITS, LCD_WHITE, BACK_COLOR, 4, 42, 475, str);
    }

    LCD_HLine(LCD_MakePoint(1, 36), 479, LCD_BLACK);
    LCD_VLine(LCD_MakePoint(1, 36), 57, LCD_BLACK);
    LCD_HLine(LCD_MakePoint(1, 92), 479, LCD_WHITE);
    LCD_VLine(LCD_MakePoint(479, 36), 57, LCD_WHITE);

    LCD_FillRect(LCD_MakePoint(0, FREQ_MENU_TOP), LCD_MakePoint(77, FREQ_MENU_TOP + 32), LCD_BLUE); //LCD_BLACK);
    FONT_Write(FONT_FRANBIG, LCD_WHITE, 0, 4, FREQ_MENU_TOP, BAND_NAME[*nowBandIndex]);

    SetWSStatus();

    /* Czyścimy wyłącznie obszar informacji. Wspólny pasek dolnych
     * przycisków zaczyna się niżej; nie wolno go nadpisywać, bo górna
     * część przycisków znika pod czarnym pasem. */
    LCD_FillRect(LCD_MakePoint(1, INFO_TOP), LCD_MakePoint(479, INFO_LINE2 + 44), BACK_COLOR); //LCD_BLACK);

    if (*nowPR == PR_WSPR)
    {
        //WSPR : Callsign, Location, dBm
        //wsCallSign
        LCD_FillRect(LCD_MakePoint(1, INFO_TOP + 1), LCD_MakePoint(255, INFO_TOP + 15), TITLE_COLOR); //LCD_BLACK);
        LCD_Rectangle(LCD_MakePoint(1, INFO_TOP + 1), LCD_MakePoint(254, INFO_TOP + 44), TITLE_COLOR);
        FONT_Write(FONT_FRAN, TextColor, 0, 14, INFO_TOP, JEZYK_Tekst(TEKST_WSPR_ZNAK));
        FONT_Write(FONT_FRANBIG, TextColor, 0, 7, INFO_TOP + 12, wsCallSign);

        LCD_FillRect(LCD_MakePoint(258, INFO_TOP + 1), LCD_MakePoint(397, INFO_TOP + 15), TITLE_COLOR); //LCD_BLACK);
        LCD_Rectangle(LCD_MakePoint(258, INFO_TOP + 1), LCD_MakePoint(396, INFO_TOP + 44), TITLE_COLOR);
        FONT_Write(FONT_FRAN, TextColor, 0, 268, INFO_TOP, JEZYK_Tekst(TEKST_WSPR_LOKATOR));
        FONT_Write(FONT_FRANBIG, TextColor, 0, 261, INFO_TOP + 12, wsLocation);

        LCD_FillRect(LCD_MakePoint(400, INFO_TOP + 1), LCD_MakePoint(479, INFO_TOP + 15), TITLE_COLOR); //LCD_BLACK);
        LCD_Rectangle(LCD_MakePoint(400, INFO_TOP + 1), LCD_MakePoint(478, INFO_TOP + 44), TITLE_COLOR);
        FONT_Write(FONT_FRAN, TextColor, 0, 410, INFO_TOP, "dBm");
        sprintf(str, "%d", wsDBM);
        FONT_Write(FONT_FRANBIG, TextColor, 0, 403, INFO_TOP + 12, str);
    }
    else
    {
        LCD_FillRect(LCD_MakePoint(1, INFO_TOP + 1), LCD_MakePoint(479, INFO_TOP + 15), TITLE_COLOR); //LCD_BLACK);
        LCD_Rectangle(LCD_MakePoint(1, INFO_TOP + 1), LCD_MakePoint(478, INFO_TOP + 44), TITLE_COLOR);
        FONT_Write(FONT_FRAN, TextColor, 0, 14, INFO_TOP, JEZYK_Tekst(TEKST_WSPR_WIADOMOSC));
        if (wsMessage[0] != '\0')
            FONT_Write(FONT_FRANBIG, TextColor, 0, 7, INFO_TOP + 12, wsMessage);
        else
            FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BACK_COLOR,
                       12, INFO_TOP + 20, JEZYK_Tekst(TEKST_WSPR_DOTKNIJ_ABY_WPISAC));
    }

    //TX POWER
    LCD_FillRect(LCD_MakePoint(1, INFO_LINE2 + 1), LCD_MakePoint(125, INFO_LINE2 + 15), TITLE_COLOR); //LCD_BLACK);
    LCD_Rectangle(LCD_MakePoint(1, INFO_LINE2 + 1), LCD_MakePoint(124, INFO_LINE2 + 44), TITLE_COLOR);
    FONT_Write(FONT_FRAN, TextColor, 0, 14, INFO_LINE2, JEZYK_Tekst(TEKST_WSPR_MOC_TX));
    snprintf(str, sizeof(str), "%d mA", (*wsTXPower & 0x0F));
    FONT_Write(FONT_FRANBIG, TextColor, 0, 7, INFO_LINE2 + 12, str);

    UpdateAFFreq();
    WS_RysujPoleCzasStale();
    CheckTime();
}

uint8_t isContinuousTX = 0;

static void WS_WypelnijKontrolki(UI_KONTROLKA_t kontrolki[wsMenus_Length])
{
    {
        UI_PROSTOKAT_t o;

        /* Dolny pasek jest wspólny z resztą programu: 70x45, y=220. Długie
         * opisy pozostają w treści ekranu, a na przyciskach używamy krótkich
         * etykiet, żeby dotyk nie wymuszał mikroskopijnej czcionki. */
        o = UI_ObszarPrzyciskuDolnego(0U);
        kontrolki[MENU_EXIT] = UI_UtworzKontrolke(MENU_EXIT, o.x, o.y, o.szerokosc, o.wysokosc,
            JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, UI_ROLA_TEKSTU_PRZYCISK, true, false);
        o = UI_ObszarPrzyciskuDolnego(1U);
        kontrolki[MENU_SAVECONFIG] = UI_UtworzKontrolke(MENU_SAVECONFIG, o.x, o.y, o.szerokosc, o.wysokosc,
            JEZYK_Wybierz("Zapisz", "Save", "Speich.", "Сохр."), UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);
        o = UI_ObszarPrzyciskuDolnego(2U);
        kontrolki[MENU_SETCONTINUE] = UI_UtworzKontrolke(MENU_SETCONTINUE, o.x, o.y, o.szerokosc, o.wysokosc,
            JEZYK_Wybierz("Ciągłe", "Cont.", "Dauer", "Пост."), UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, isContinuousTX != 0U);
        o = UI_ObszarPrzyciskuDolnego(3U);
        kontrolki[MENU_MSEND] = UI_UtworzKontrolke(MENU_MSEND, o.x, o.y, o.szerokosc, o.wysokosc,
            JEZYK_Wybierz("Nadaj", "Send", "Senden", "TX"), UI_STYL_AKCENT, UI_ROLA_TEKSTU_PRZYCISK, true, false);
        o = UI_ObszarPrzyciskuDolnego(4U);
        kontrolki[MENU_RTCSEND] = UI_UtworzKontrolke(MENU_RTCSEND, o.x, o.y, o.szerokosc, o.wysokosc,
            "RTC", UI_STYL_AKCENT, UI_ROLA_TEKSTU_PRZYCISK, true, false);
    }

    kontrolki[MENU_BANDDOWN] = UI_UtworzKontrolke(MENU_BANDDOWN, 80, FREQ_MENU_TOP, 100, 37,
        JEZYK_Tekst(TEKST_WSPR_POPRZEDNIE_PASMO), UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);
    kontrolki[MENU_BANDUP] = UI_UtworzKontrolke(MENU_BANDUP, 180, FREQ_MENU_TOP, 100, 37,
        JEZYK_Tekst(TEKST_WSPR_NASTEPNE_PASMO), UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);
    kontrolki[MENU_AFFREQDOWN] = UI_UtworzKontrolke(MENU_AFFREQDOWN, 280, FREQ_MENU_TOP, 100, 37,
        "AF -10 Hz", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);
    kontrolki[MENU_AFFREQUP] = UI_UtworzKontrolke(MENU_AFFREQUP, 380, FREQ_MENU_TOP, 100, 37,
        "AF +10 Hz", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);

    kontrolki[MENU_TOP1] = UI_UtworzKontrolke(MENU_TOP1, 80, TOP_MENU_TOP, 100, 37,
        "WSPR", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, *nowPR == PR_WSPR);
    kontrolki[MENU_TOP2] = UI_UtworzKontrolke(MENU_TOP2, 180, TOP_MENU_TOP, 100, 37,
        "FT8", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, *nowPR == PR_FT8);
    kontrolki[MENU_TOP3] = UI_UtworzKontrolke(MENU_TOP3, 280, TOP_MENU_TOP, 100, 37,
        "FT4", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, *nowPR == PR_FT4);
    kontrolki[MENU_TOP4] = UI_UtworzKontrolke(MENU_TOP4, 380, TOP_MENU_TOP, 100, 37,
        *nowPR == PR_JT9 ? "JT9" : "JT65", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true,
        (*nowPR == PR_JT65 || *nowPR == PR_JT9));
}

void wsMenuDraw(void)
{
    UI_KONTROLKA_t kontrolki[wsMenus_Length];
    WS_WypelnijKontrolki(kontrolki);
    UI_RysujKontrolki(kontrolki, wsMenus_Length);
}

LCDPoint pt;

void ClearTmpBuffer()
{
    for (int i = 0; i < 20; i++)
        wsKeyboardTmp[i] = ' ';
    wsKeyboardTmp[20] = 0;
}

void TmpBuffToBuff(char *targetBuff, int length)
{
    for (int i = 0; i < length; i++)
        targetBuff[i] = wsKeyboardTmp[i];

    targetBuff[length] = 0;
    for (int i = length - 1; i >= 0; i--)
    {
        if (targetBuff[i] == ' ')
            targetBuff[i] = 0;
        else
            break;
    }
}


static uint8_t WS_EdytujZnak(void)
{
    ClearTmpBuffer();
    if (!KeyboardWindow(wsKeyboardTmp, 6, JEZYK_Tekst(TEKST_WSPR_PROMPT_ZNAK)))
        return 0U;
    TmpBuffToBuff(wsCallSign, 6);
    WS_TekstNaWielkie(wsCallSign, 6U);
    return 1U;
}

static uint8_t WS_EdytujLokator(void)
{
    ClearTmpBuffer();
    if (!KeyboardWindow(wsKeyboardTmp, 4, JEZYK_Tekst(TEKST_WSPR_PROMPT_LOKATOR)))
        return 0U;
    TmpBuffToBuff(wsLocation, 4);
    WS_TekstNaWielkie(wsLocation, 4U);
    return 1U;
}

static uint8_t WS_EdytujMocWSPR(void)
{
    ClearTmpBuffer();
    if (!KeyboardWindow(wsKeyboardTmp, 2, JEZYK_Tekst(TEKST_WSPR_PROMPT_MOC)))
        return 0U;
    TmpBuffToBuff(wsDBMStr, 2);
    wsDBM = WS_CzySameCyfry(wsDBMStr) ? (uint8_t)atoi(wsDBMStr) : 0U;
    return 1U;
}

static uint8_t WS_EdytujWiadomosc(void)
{
    ClearTmpBuffer();
    if (!KeyboardWindow(wsKeyboardTmp, 18, JEZYK_Tekst(TEKST_WSPR_PROMPT_WIADOMOSC)))
        return 0U;
    TmpBuffToBuff(wsMessage, 18);
    return 1U;
}

static uint8_t WS_UzupelnijBrakujaceDanePrzedTX(void)
{
    /*
     * Naciśnięcie "Nadaj" przy pustym polu nie powinno prowadzić do ślepego
     * ostrzeżenia, po którym użytkownik nadal nie wie, gdzie wpisać dane.
     * Otwieramy dokładnie ten edytor, którego brakuje. Błędne, ale niepuste
     * dane nadal przechodzą przez zwykłą walidację i czytelny komunikat.
     */
    if (*nowPR == PR_WSPR)
    {
        if (!WS_CzyPoprawnyZnakWSPR(wsCallSign))
            return WS_EdytujZnak();
        if (!WS_CzyPoprawnyLokatorWSPR(wsLocation))
            return WS_EdytujLokator();
        if (!WS_CzyPoprawnaMocWSPR(wsDBMStr))
            return WS_EdytujMocWSPR();
        return 1U;
    }

    if (wsMessage[0] == '\0')
        return WS_EdytujWiadomosc();

    return 1U;
}

uint8_t wsUserStop = 0;

#define JT9_DELAY 576     // Delay value for JT9-1
#define JT65_DELAY 371    // Delay in ms for JT65A
#define JT4_DELAY 229     // Delay value for JT4A
#define WSPR_DELAY 683    // Delay value for WSPR
#define FSQ_2_DELAY 500   // Delay value for 2 baud FSQ
#define FSQ_3_DELAY 333   // Delay value for 3 baud FSQ
#define FSQ_4_5_DELAY 222 // Delay value for 4.5 baud FSQ
#define FSQ_6_DELAY 167   // Delay value for 6 baud FSQ
#define FT8_DELAY 160     // FT8: 6,25 symbolu/s; 79 symboli daje 12,64 s emisji.
#define FT4_DELAY 48      // FT4: 20,8333 symbolu/s; okres symbolu wynosi około 48 ms.

#define JT65_SYMBOL_COUNT 126
#define JT9_SYMBOL_COUNT 85
#define JT4_SYMBOL_COUNT 207
#define WSPR_SYMBOL_COUNT 162
#define FT8_SYMBOL_COUNT 79
#define FT4_SYMBOL_COUNT 103


typedef struct
{
    uint8_t liczba_symboli;
    uint16_t czas_symbolu_ms;
    uint16_t odniesienie_tonu_hz;
    uint16_t dzielnik_odniesienia;
    uint8_t maks_symbol;
} WS_PARAMETRY_TX_t;

static uint8_t WS_CzySymbolePoprawne(const uint8_t *symbole, uint8_t liczba, uint8_t maks_symbol)
{
    uint8_t i;

    if (symbole == 0 || liczba == 0U)
        return 0U;

    for (i = 0U; i < liczba; ++i)
    {
        if (symbole[i] > maks_symbol)
            return 0U;
    }
    return 1U;
}

static uint8_t WS_PrzygotujSymbole(uint8_t *symbole, WS_PARAMETRY_TX_t *parametry)
{
    if (symbole == 0 || parametry == 0)
        return 0U;

    memset(symbole, 0, 255U);
    memset(parametry, 0, sizeof(*parametry));

    switch (*nowPR)
    {
    case PR_WSPR:
        parametry->liczba_symboli = WSPR_SYMBOL_COUNT;
        parametry->czas_symbolu_ms = WSPR_DELAY;
        /* 375 / 256 = 1.46484375 Hz, zgodne z rastrem WSPR. */
        parametry->odniesienie_tonu_hz = 375U;
        parametry->dzielnik_odniesienia = 256U;
        parametry->maks_symbol = 3U;
        wsDBM = (uint8_t)atoi(wsDBMStr);
        wspr_encode(wsCallSign, wsLocation, wsDBM, symbole);
        break;

    case PR_FT8:
        parametry->liczba_symboli = FT8_SYMBOL_COUNT;
        parametry->czas_symbolu_ms = FT8_DELAY;
        parametry->odniesienie_tonu_hz = 50U;
        parametry->dzielnik_odniesienia = 8U;
        parametry->maks_symbol = 7U;
        ft8_encode_msg(wsMessage, symbole);
        break;

    case PR_FT4:
        parametry->liczba_symboli = FT4_SYMBOL_COUNT;
        parametry->czas_symbolu_ms = FT4_DELAY;
        /* Finalny FT4: 125 / 6 = 20.833333 Hz miedzy tonami. */
        parametry->odniesienie_tonu_hz = 125U;
        parametry->dzielnik_odniesienia = 6U;
        parametry->maks_symbol = 3U;
        ft4_encode_msg(wsMessage, symbole);
        break;

    case PR_JT65:
        parametry->liczba_symboli = JT65_SYMBOL_COUNT;
        parametry->czas_symbolu_ms = JT65_DELAY;
        parametry->odniesienie_tonu_hz = 189U;
        parametry->dzielnik_odniesienia = 70U;
        parametry->maks_symbol = 65U;
        jt65_encode(wsMessage, symbole);
        break;

    case PR_JT9:
        parametry->liczba_symboli = JT9_SYMBOL_COUNT;
        parametry->czas_symbolu_ms = JT9_DELAY;
        parametry->odniesienie_tonu_hz = 14U;
        parametry->dzielnik_odniesienia = 8U;
        parametry->maks_symbol = 8U;
        jt9_encode(wsMessage, symbole);
        break;

    default:
        return 0U;
    }

    return WS_CzySymbolePoprawne(symbole, parametry->liczba_symboli,
                                 parametry->maks_symbol);
}

static uint8_t WS_PunktWPrzyciskuStop(LCDPoint punkt, uint8_t menu)
{
    UI_KONTROLKA_t kontrolki[wsMenus_Length];
    WS_WypelnijKontrolki(kontrolki);
    if (menu >= wsMenus_Length)
        return 0U;
    return UI_CzyPunktWObszarze(punkt, &kontrolki[menu].obszar) ? 1U : 0U;
}

static uint8_t WS_CzyZadanoStop(void)
{
    WEJSCIE_ZDARZENIE_t zdarzenie;

    if (TOUCH_Poll(&pt))
    {
        if (WS_PunktWPrzyciskuStop(pt, MENU_MSEND) ||
            WS_PunktWPrzyciskuStop(pt, MENU_RTCSEND))
        {
            TRACK_Beep(1);
            wsUserStop = 1U;
            return 1U;
        }
    }

    zdarzenie = WEJSCIA_PobierzZdarzenie();
    if (zdarzenie == WEJSCIE_ZDARZENIE_START_STOP ||
        zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
    {
        wsUserStop = 1U;
        return 1U;
    }

    return wsUserStop;
}

static uint32_t WS_CzasDocelowySymboluMs(uint8_t protokol,
                                          uint16_t numer_symbolu,
                                          uint16_t czas_nominalny_ms)
{
    /*
     * WSPR ma dokładnie 12000 / 8192 = 1,46484375 symbolu/s, więc jeden
     * symbol trwa 8192 / 12000 s = 2048/3 ms. Nie da się tego zapisać jako
     * stałej całkowitoliczbowej bez narastającego błędu. Liczymy zatem czas
     * końca od początku całej transmisji. Zaokrąglenie do najbliższego ms
     * daje po 162 symbolach dokładnie 110592 ms zamiast 162 * 683 ms.
     */
    if (protokol == PR_WSPR)
        return ((uint32_t)numer_symbolu * 2048U + 1U) / 3U;

    return (uint32_t)numer_symbolu * (uint32_t)czas_nominalny_ms;
}

static uint8_t WS_CzekajDoCzasu(uint32_t poczatek_transmisji,
                                uint32_t czas_docelowy_ms)
{
    while ((HAL_GetTick() - poczatek_transmisji) < czas_docelowy_ms)
    {
        if (WS_CzyZadanoStop())
            return 0U;
        HAL_Delay(1U);
    }
    return 1U;
}

static int g_ws_postep_poprzedni = -1;
static uint32_t g_ws_postep_czas = 0U;

static void WS_ResetujPostep(void)
{
    g_ws_postep_poprzedni = -1;
    g_ws_postep_czas = 0U;
}

static void WS_RysujPostep(int procent, uint8_t wymus)
{
    char tekst[8];
    uint32_t teraz = HAL_GetTick();

    if (procent < 0)
        procent = 0;
    if (procent > 100)
        procent = 100;

    /* FT4 ma bardzo krotkie symbole. LCD nie musi byc odswiezany 20+ razy/s. */
    if (!wymus && procent == g_ws_postep_poprzedni)
        return;
    if (!wymus && g_ws_postep_czas != 0U && (teraz - g_ws_postep_czas) < 100U)
        return;

    LCD_FillRect(LCD_MakePoint(261, INFO_LINE2 + 20),
                 LCD_MakePoint(477, INFO_LINE2 + 40), BACK_COLOR);
    if (procent > 0)
    {
        LCD_FillRect(LCD_MakePoint(268, INFO_LINE2 + 20),
                     LCD_MakePoint((uint16_t)(268 + procent * 2), INFO_LINE2 + 40),
                     UI_KolorRamki(UI_STYL_AKCENT));
    }

    snprintf(tekst, sizeof(tekst), "%d%%", procent);
    FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 430, INFO_LINE2 + 23, tekst);

    g_ws_postep_poprzedni = procent;
    g_ws_postep_czas = teraz;
}

static void WS_RysujStanNadawania(const char *opis)
{
    LCD_FillRect(LCD_MakePoint(258, INFO_LINE2 + 1),
                 LCD_MakePoint(479, INFO_LINE2 + 44), BACK_COLOR);
    LCD_FillRect(LCD_MakePoint(258, INFO_LINE2 + 1),
                 LCD_MakePoint(479, INFO_LINE2 + 15),
                 UI_KolorTlaPrzycisku(UI_STYL_OSTRZEZENIE));
    LCD_Rectangle(LCD_MakePoint(258, INFO_LINE2 + 1),
                  LCD_MakePoint(478, INFO_LINE2 + 44),
                  UI_KolorRamki(UI_STYL_OSTRZEZENIE));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), BACK_COLOR,
               264, INFO_LINE2, opis);
    WS_ResetujPostep();
    WS_RysujPostep(0, 1U);
}

/* Historyczny DrawSendingRateAndCheckStop usunieto w v2.03-test9: zastapil go wspolny obiekt kontrolki STOP. */

static void WS_AudioWypelnijFragment(int16_t *bufor, uint32_t liczba_ramek,
                                   uint32_t pierwsza_ramka, const uint8_t *symbole,
                                   double *faza)
{
    uint32_t ramka;
    const double tau = 2.0 * M_PI;
    const double amplituda = 12000.0;

    if (bufor == NULL || symbole == NULL || faza == NULL)
        return;

    for (ramka = 0U; ramka < liczba_ramek; ++ramka)
    {
        const uint32_t numer_ramki = pierwsza_ramka + ramka;
        uint32_t symbol = (uint32_t)(((uint64_t)numer_ramki * 3ULL) / WSPR_AUDIO_RAMKI_SYMBOL_X3);
        double czestotliwosc;
        int16_t probka;

        if (symbol >= WSPR_AUDIO_LICZBA_SYMBOLI)
        {
            bufor[2U * ramka] = 0;
            bufor[2U * ramka + 1U] = 0;
            continue;
        }

        czestotliwosc = ((double)1500 + (double)*wsAUDIOFreq) +
                        WSPR_TONE_SPACING * (double)symbole[symbol];
        probka = (int16_t)(amplituda * sin(*faza));
        bufor[2U * ramka] = probka;
        bufor[2U * ramka + 1U] = probka;

        *faza += tau * czestotliwosc / (double)WSPR_AUDIO_HZ;
        if (*faza >= tau)
            *faza = fmod(*faza, tau);
    }
}

void SendingStart(void)
{
    uint8_t tx_buffer[255];
    WS_PARAMETRY_TX_t parametry;
    const uint8_t tx_output_port = HS_CLK2; /* GEN OUT / S2 */
    uint8_t zegar_rf_aktywny = 0U;
    uint8_t audio_aktywne = 0U;
    TEKST_ID_t blad;
    unsigned long tx_p1 = 0U, tx_p2 = 0U, tx_p3 = 0U;
    unsigned long koniec_p1 = 0U, koniec_p2 = 0U, koniec_p3 = 0U;
    unsigned int krok_tonu = 0U;
    unsigned long czestotliwosc_start;
    uint32_t poczatek_transmisji_ms = 0U;
    int i;

    blad = WS_WalidujDane();
    if (blad != TEKST_LICZBA_TEKSTOW)
    {
        WS_PokazBlad(blad);
        return;
    }

    if (!WS_PrzygotujSymbole(tx_buffer, &parametry))
    {
        WS_PokazBlad(TEKST_WSPR_BLAD_KODERA);
        return;
    }

    /*
     * Ostatnia pozycja pasma oznacza wyjście audio do zewnętrznego TRX i nie
     * wymaga Si5351. Każdy pozostały tryb nadaje bezpośrednio przez GEN OUT,
     * więc brak syntezy zgłaszamy przed PTT i przed pierwszym zapisem I2C.
     */
    if (*nowBandIndex != BAND_MAX_INDEX && !GEN_CzyWyjscieDodatkoweObslugiwane())
    {
        /*
         * Przy 2x ADF Si5351 nadal jest fizycznie obecny, ale jego CLK2 zasila
         * oba PLL jako wzorzec 27 MHz. Samo sprawdzenie obecnosci Si5351 nie wystarcza:
         * bezposredni WSPR/FT8 RF nie moze wtedy przeprogramowac CLK2.
         * Tryb audio do zewnetrznego TRX pozostaje dozwolony.
         */
        WS_PokazBlad(TEKST_BRAK_SYGNALU_SPRAWDZ_I2C);
        return;
    }

    wsUserStop = 0U;
    WEJSCIA_WyczyscZdarzenia();

    /* Podczas calego TX oba prawe przyciski dolnego paska sa przyciskami STOP. */
    {
        UI_KONTROLKA_t kontrolki[wsMenus_Length];
        WS_WypelnijKontrolki(kontrolki);
        kontrolki[MENU_MSEND].tekst = "STOP TX";
        kontrolki[MENU_MSEND].styl = UI_STYL_OSTRZEZENIE;
        kontrolki[MENU_RTCSEND].tekst = "STOP TX";
        kontrolki[MENU_RTCSEND].styl = UI_STYL_OSTRZEZENIE;
        UI_RysujKontrolke(&kontrolki[MENU_MSEND]);
        UI_RysujKontrolke(&kontrolki[MENU_RTCSEND]);
    }

    if (*nowBandIndex == BAND_MAX_INDEX)
    {
        uint32_t nastepna_ramka = WSPR_AUDIO_RAMKI_BUFORA;
        uint32_t polowki_odtworzone = 0U;
        const uint32_t liczba_polowek = WSPR_AUDIO_RAMKI_CALKOWITE / WSPR_AUDIO_RAMKI_POL_BUFORA;
        double faza_audio = 0.0;

        /* Tryb audio jest swiadomie ograniczony do WSPR przez walidacje. */
        if (parametry.liczba_symboli != WSPR_AUDIO_LICZBA_SYMBOLI ||
            (WSPR_AUDIO_RAMKI_CALKOWITE % WSPR_AUDIO_RAMKI_POL_BUFORA) != 0U)
            goto sprzatanie;

        if (BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_HEADPHONE, WSPR_AUDIO_VOLUME,
                               I2S_AUDIOFREQ_8K) != 0)
            goto sprzatanie;

        audio_aktywne = 1U;
        memset(AUDIO_BUFFER_OUT, 0, sizeof(AUDIO_BUFFER_OUT));
        WS_AudioWypelnijFragment(AUDIO_BUFFER_OUT, WSPR_AUDIO_RAMKI_BUFORA,
                                 0U, tx_buffer, &faza_audio);
        Audio_Play_Status = AUDIO_TRANSFER_NONE;
        BSP_AUDIO_OUT_SetAudioFrameSlot(CODEC_AUDIOFRAME_SLOT_02);

        WS_RysujStanNadawania(JEZYK_Tekst(TEKST_WSPR_PRZEZ_TRX));
        SET_PTT(1U);
        SET_LED_STANU_RF(1U);
        if (BSP_AUDIO_OUT_Play((uint16_t *)AUDIO_BUFFER_OUT,
                               (uint32_t)sizeof(AUDIO_BUFFER_OUT)) != 0)
            goto sprzatanie;

        while (polowki_odtworzone < liczba_polowek)
        {
            const uint8_t stan = Audio_Play_Status;
            int16_t *fragment;

            if (WS_CzyZadanoStop())
                goto sprzatanie;
            if (stan != AUDIO_TRANSFER_HALF && stan != AUDIO_TRANSFER_COMPLETE)
            {
                HAL_Delay(1U);
                continue;
            }

            Audio_Play_Status = AUDIO_TRANSFER_NONE;
            ++polowki_odtworzone;

            if (polowki_odtworzone < liczba_polowek)
            {
                fragment = (stan == AUDIO_TRANSFER_HALF)
                               ? &AUDIO_BUFFER_OUT[0]
                               : &AUDIO_BUFFER_OUT[WSPR_AUDIO_RAMKI_POL_BUFORA * WSPR_AUDIO_KANALY];
                if (nastepna_ramka < WSPR_AUDIO_RAMKI_CALKOWITE)
                {
                    WS_AudioWypelnijFragment(fragment, WSPR_AUDIO_RAMKI_POL_BUFORA,
                                             nastepna_ramka, tx_buffer, &faza_audio);
                    nastepna_ramka += WSPR_AUDIO_RAMKI_POL_BUFORA;
                }
                else
                {
                    memset(fragment, 0,
                           WSPR_AUDIO_RAMKI_POL_BUFORA * WSPR_AUDIO_KANALY * sizeof(fragment[0]));
                }
            }

            if ((polowki_odtworzone & 7U) == 0U || polowki_odtworzone == liczba_polowek)
                WS_RysujPostep((int)((100UL * polowki_odtworzone) / liczba_polowek), 0U);
        }

        WS_RysujPostep(100, 1U);
        goto sprzatanie;
    }

    /* Bezposrednie RF z Si5351. */
    czestotliwosc_start = WSFreq + 1500UL + (long)*wsAUDIOFreq;
    HS_CalcPLLParam(HS_MAX_C_VAL, czestotliwosc_start, &tx_p1, &tx_p2, &tx_p3);
    HS_CalcPLLParam(HS_MAX_C_VAL, czestotliwosc_start + parametry.odniesienie_tonu_hz,
                    &koniec_p1, &koniec_p2, &koniec_p3);

    if (parametry.dzielnik_odniesienia == 0U || tx_p3 == 0U)
        goto sprzatanie;

    /*
     * Dla malego odstepu tonow parametry P1/P3 powinny pozostac wspolne.
     * Jezeli przekroczylismy granice dzielnika, nie ryzykujemy zawiniecia
     * odejmowania unsigned - przerywamy zamiast nadac bledne widmo.
     */
    if (koniec_p1 != tx_p1 || koniec_p3 != tx_p3 || koniec_p2 < tx_p2)
        goto sprzatanie;

    krok_tonu = (unsigned int)((koniec_p2 - tx_p2) / parametry.dzielnik_odniesienia);

    HS_AllClockDown();
    HS_ApplyPLLParam(HS_PLLA, tx_p1, tx_p2, tx_p3);
    HS_BindCLKToPLL(tx_output_port, HS_PLLA, czestotliwosc_start);
    HS_SetClockEnabled(tx_output_port, HS_FALSE, HS_TRUE);
    HS_SetClockUp(tx_output_port, HS_TRUE, HS_TRUE);
    HS_SetPower(tx_output_port, HS_MAToParam(*wsTXPower & 0x0FU), HS_TRUE);

    /*
     * Rejestry Si5351 są już przygotowane, ale wyjście nadal jest wyłączone.
     * Dopiero teraz podnosimy PTT i diodę RF, a następnie uruchamiamy zegar.
     * Błąd obliczeń lub konfiguracji nie powoduje więc krótkiego impulsu PTT.
     */
    /*
     * Pierwszy symbol ustawiamy jeszcze przy wyłączonym wyjściu. Dzięki temu
     * po podniesieniu CLK2 transmisja nie zaczyna się krótkim fragmentem tonu 0,
     * jeżeli pierwszy zakodowany symbol ma inną wartość.
     */
    {
        const unsigned long pierwszy_p2 = tx_p2 + (unsigned long)tx_buffer[0] * krok_tonu;
        HS_SetPLLP1P2(HS_PLLA,
                      tx_p1 + pierwszy_p2 / tx_p3,
                      pierwszy_p2 % tx_p3,
                      tx_p3);
    }

    SET_PTT(1U);
    SET_LED_STANU_RF(1U);
    HS_SetClockEnabled(tx_output_port, HS_TRUE, HS_TRUE);
    zegar_rf_aktywny = 1U;
    poczatek_transmisji_ms = HAL_GetTick();

    WS_RysujStanNadawania(JEZYK_Tekst(TEKST_WSPR_STATUS_TX));

    for (i = 0; i < (int)parametry.liczba_symboli; ++i)
    {
        const uint32_t czas_docelowy_ms =
            WS_CzasDocelowySymboluMs((uint8_t)*nowPR,
                                     (uint16_t)(i + 1),
                                     parametry.czas_symbolu_ms);

        if (i > 0)
        {
            const unsigned long txa_p2 = tx_p2 + (unsigned long)tx_buffer[i] * krok_tonu;
            HS_SetPLLP1P2(HS_PLLA,
                          tx_p1 + txa_p2 / tx_p3,
                          txa_p2 % tx_p3,
                          tx_p3);
        }

        WS_RysujPostep(((i + 1) * 100) / parametry.liczba_symboli, 0U);

        /*
         * Czekamy do czasu liczonego od początku całej ramki. Koszt rysowania
         * postępu i zapisu PLL nie dodaje się więc po każdym symbolu do długości
         * transmisji, co ogranicza narastający dryft programowy.
         */
        if (!WS_CzekajDoCzasu(poczatek_transmisji_ms, czas_docelowy_ms))
            break;
    }

    if (!wsUserStop)
        WS_RysujPostep(100, 1U);

sprzatanie:
    /*
     * Każde wyjście z funkcji przechodzi przez ten blok. Nie może pozostać
     * aktywny zegar Si5351, PTT ani dioda RF, także po STOP lub błędzie.
     */
    if (audio_aktywne)
        BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);

    if (zegar_rf_aktywny)
    {
        HS_SetClockEnabled(tx_output_port, HS_FALSE, HS_TRUE);
        HS_SetClockUp(tx_output_port, HS_FALSE, HS_TRUE);
    }

    SET_PTT(0U);
    SET_LED_STANU_RF(0U);

    WS_RysujPoleCzasStale();
    CheckTime();
}

void WeakSignal_Proc(void)
{
    uint32_t ostatnie_sprawdzenie_czasu_ms = 0U;

    SetColours();
    BSP_LCD_SelectLayer(0);
    LCD_FillAll(BACK_COLOR);
    BSP_LCD_SelectLayer(1);
    LCD_FillAll(BACK_COLOR);
    LCD_ShowActiveLayerOnly();

    WS_LoadInformation();

    while (TOUCH_IsPressed())
        ;

    uint32_t si5351_XTAL_FREQ = (uint32_t)((int)CFG_GetParam(CFG_PARAM_SI5351_XTAL_FREQ) + (int)CFG_GetParam(CFG_PARAM_SI5351_CORR));
    if (si5351_IsPresent())
        HS_SI5351_Init(si5351_XTAL_FREQ, (CFG_GetParam(CFG_PARAM_SI5351_CAPS) & 3));

    JTEncode();
    wsMenuDraw();        //Draw Menu
    DrawWSInformation(); //Draw Information

    //Init Start Frequency
    //NowMeasureFreq = MeasureStartFreq;

    for (;;)
    {
        if (TOUCH_Poll(&pt))
        {
            //Check Text (Callsign, TX dBm, Message, Loc)
            if ((pt.y > INFO_TOP && pt.y < INFO_LINE2) || (pt.y > INFO_LINE2 && pt.y < INFO_LINE2 + 35 && pt.x < 125))
            {
                TRACK_Beep(1);
                while (TOUCH_IsPressed())
                    ;

                //Check Line 1
                if (pt.y < INFO_LINE2)
                {
                    if (*nowPR == PR_WSPR)
                    {
                        if (pt.x < 255)
                            (void)WS_EdytujZnak();
                        else if (pt.x < 397) //LOCATION
                            (void)WS_EdytujLokator();
                        else //DB
                            (void)WS_EdytujMocWSPR();
                    } //end of WSPR
                    else
                    { //FT8, JT65, JT9

                        (void)WS_EdytujWiadomosc();
                    }
                }
                else
                {
                    //TX POWER SELECT
                    if ((*wsTXPower & 0x0FU) == 0x02U)
                        *wsTXPower = 0x08U;
                    else
                        *wsTXPower = (char)((*wsTXPower & 0x0FU) - 2U);
                }

                DrawWSInformation();
                continue;
            }

            UI_KONTROLKA_t kontrolki[wsMenus_Length];
            WS_WypelnijKontrolki(kontrolki);
            int touchIndex = UI_ZnajdzKontrolke(pt, kontrolki, wsMenus_Length);

            //Play Beep
            if (touchIndex != -1)
            {
                TRACK_Beep(1);

                //Chbeck AF Frequency Up And Down / for Continues change
                if (touchIndex == MENU_AFFREQDOWN || touchIndex == MENU_AFFREQUP) // +-10 Hz
                {
                    int32_t nowa_korekcja_hz = (int32_t)*wsAUDIOFreq +
                                                (touchIndex == MENU_AFFREQDOWN ? -10 : 10);

                    /*
                     * Ten sam zakres jest używany podczas wczytywania ustawień.
                     * Pilnujemy go również w obsłudze przycisków, aby wielokrotne
                     * naciskanie nie wyprowadziło generatora poza przewidziany zakres.
                     */
                    if (nowa_korekcja_hz < -1400)
                        nowa_korekcja_hz = -1400;
                    if (nowa_korekcja_hz > 1400)
                        nowa_korekcja_hz = 1400;

                    *wsAUDIOFreq = (int16_t)nowa_korekcja_hz;
                    UpdateAFFreq();
                    Sleep(100);
                    continue;
                }
                else
                {
                    while (TOUCH_IsPressed())
                        ;
                }
            }

            if (touchIndex == MENU_EXIT) //EXIT
            {
                break;
            }
            else if (touchIndex == MENU_SAVECONFIG)
            {
                WS_StoredInformatoin();
            }
            else if (touchIndex == MENU_SETCONTINUE)
            {
                isContinuousTX = !isContinuousTX;
            }
            else if (touchIndex == MENU_MSEND)
            {
                TEKST_ID_t blad;

                if (!WS_UzupelnijBrakujaceDanePrzedTX())
                {
                    DrawWSInformation();
                    wsMenuDraw();
                    continue;
                }

                blad = WS_WalidujDane();
                if (blad != TEKST_LICZBA_TEKSTOW)
                {
                    WS_PokazBlad(blad);
                    DrawWSInformation();
                    wsMenuDraw();
                    continue;
                }

                NowStatus = WS_ST_SENDMANUAL1;
                SetWSStatus();
                SendingStart();
                NowStatus = WS_ST_NONE;
                SetWSStatus();
                wsMenuDraw();

                while (TOUCH_IsPressed())
                    ;
            }
            else if (touchIndex == MENU_RTCSEND)
            {
                if (WS_ST_READY == NowStatus)
                {
                    NowStatus = WS_ST_NONE;
                }
                else if (!RTCpresent)
                {
                    WS_PokazBlad(TEKST_WSPR_BRAK_RTC);
                    DrawWSInformation();
                    wsMenuDraw();
                    continue;
                }
                else
                {
                    TEKST_ID_t blad;

                    if (!WS_UzupelnijBrakujaceDanePrzedTX())
                    {
                        DrawWSInformation();
                        wsMenuDraw();
                        continue;
                    }

                    blad = WS_WalidujDane();
                    if (blad != TEKST_LICZBA_TEKSTOW)
                    {
                        WS_PokazBlad(blad);
                        DrawWSInformation();
                        wsMenuDraw();
                        continue;
                    }
                    NowStatus = WS_ST_READY;
                }
            }
            else if (touchIndex == MENU_BANDDOWN || touchIndex == MENU_BANDUP) //Prior Band, Next Band
            {
                if (touchIndex == MENU_BANDDOWN)
                {
                    if (*nowBandIndex == 0)
                        *nowBandIndex = BAND_MAX_INDEX;
                    else
                        (*nowBandIndex)--;
                }
                else
                {
                    if (*nowBandIndex == BAND_MAX_INDEX)
                        *nowBandIndex = 0;
                    else
                        (*nowBandIndex)++;
                }

                SetDefaultFrequency();
            }
            else if (touchIndex >= MENU_TOP1 && touchIndex <= MENU_TOP4) //WSPR ~ JT9, Protocol Change
            {
                if (touchIndex == MENU_TOP4)
                {
                    if (*nowPR == PR_JT65)
                        *nowPR = PR_JT9;
                    else
                        *nowPR = PR_JT65;
                }
                else
                {
                    *nowPR = touchIndex - MENU_TOP1;
                }

                SetDefaultFrequency();
            } //end of else if

            DrawWSInformation();
            wsMenuDraw();
        }

        Sleep(2);

        // Sprawdzanie czasu jest oparte o HAL_GetTick(), nie o liczbe obiegow petli.
        if ((HAL_GetTick() - ostatnie_sprawdzenie_czasu_ms) >= 50U)
        {
            ostatnie_sprawdzenie_czasu_ms = HAL_GetTick();
            CheckTime();

            if (NowStatus == WS_ST_READY && g_ws_w_oknie_startu)
            {
                TEKST_ID_t blad = WS_WalidujDane();

                if (!RTCpresent)
                    blad = TEKST_WSPR_BRAK_RTC;

                if (blad != TEKST_LICZBA_TEKSTOW)
                {
                    NowStatus = WS_ST_NONE;
                    WS_PokazBlad(blad);
                    DrawWSInformation();
                    wsMenuDraw();
                    continue;
                }

                NowStatus = WS_ST_SENDRTC1;
                SetWSStatus();
                SendingStart();

                NowStatus = isContinuousTX && (!wsUserStop) ? WS_ST_READY : WS_ST_NONE;
                SetWSStatus();
                wsMenuDraw();

                while (TOUCH_IsPressed())
                    ;
            }
        }
    } //end of for

    //Release Memory
    //free(MeasureIM);
    //free(MeasureFreq);
    SET_PTT(0U);
    HS_SetClockEnabled(HS_CLK0, HS_FALSE, HS_TRUE);
    HS_SetClockUp(HS_CLK0, HS_FALSE, HS_TRUE);
    HS_SetClockEnabled(HS_CLK2, HS_FALSE, HS_TRUE);
    HS_SetClockUp(HS_CLK2, HS_FALSE, HS_TRUE);
    SET_LED_STANU_RF(0U);
    GEN_SetMeasurementFreq(0);
    DSP_Init();
    return;
}
