/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <complex.h>

#include "config.h"
#include "LCD.h"
#include "touch.h"
#include "ff.h"
#include "textbox.h"
#include "screenshot.h"
#include "crash.h"
#include "stm32746g_discovery_lcd.h"
#include "keyboard.h"
#include "lodepng.h"
#include "mainwnd.h"
#include "DS3231.h"
#include "komunikaty.h"
#include "jezyk.h"
#include "wejscia_uzytkownika.h"

extern void Sleep(uint32_t);
extern uint32_t GetInternTime(uint8_t *secondsx);
extern void PixPict(unsigned int x0, unsigned int y0, char *bmp);
extern void lodepng_free(void *ptr);

static const TCHAR *SNDIR = "/aa/snapshot";
static uint32_t oldest = 0xFFFFFFFFul;
static uint32_t numfiles = 0;

uint8_t __attribute__((section(".user_sdram"), aligned(32), used)) bmpFileBufferTMP[SCREENSHOT_FILE_SIZE]; //for prevent error, buffer
uint8_t __attribute__((section(".user_sdram"), aligned(32), used)) bmpFileBuffer[SCREENSHOT_FILE_SIZE];

static const uint8_t bmp_hdr[] =
    {
        0x42, 0x4D,             //"BM"
        0x36, 0xFA, 0x05, 0x00, //size in bytes
        0x00, 0x00, 0x00, 0x00, //reserved
        0x36, 0x00, 0x00, 0x00, //offset to image in bytes
        0x28, 0x00, 0x00, 0x00, //info size in bytes
        0xE0, 0x01, 0x00, 0x00, //width
        0x10, 0x01, 0x00, 0x00, //height
        0x01, 0x00,             //planes
        0x18, 0x00,             //bits per pixel
        0x00, 0x00, 0x00, 0x00, //compression
        0x00, 0xfa, 0x05, 0x00, //image size
        0x00, 0x00, 0x00, 0x00, //x resolution
        0x00, 0x00, 0x00, 0x00, //y resolution
        0x00, 0x00, 0x00, 0x00, // colours
        0x00, 0x00, 0x00, 0x00  //important colours
};
static const char *g_logo_fpath1 = "/aa/logo.bmp";

static const char *g_logo_fpath2 = "/aa/logo.png";

DWORD get_fattime(void)
{
    uint8_t second1;
    short AMPM1;
    uint32_t mon, yy, mm, dd, h, m, s;

    if (NoDate == 1)
        return 0;
    if (RTCpresent == 1)
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
    yy = date / 10000 - 1980;
    mm = mon / 100;
    dd = mon % 100;
    h = time / 100;
    m = time % 100;
    s = second1 / 2;
    return ((yy << 25) + (mm << 21) + (dd << 16) + (h << 11) + (m << 5) + s);
}

int32_t SCREENSHOT_RysujPNGZPamieci(const uint8_t *dane, uint32_t rozmiar)
{
    unsigned szerokosc = 0U;
    unsigned wysokosc = 0U;
    unsigned wynik;
    uint8_t *bufor_rgb = 0;

    if (dane == 0 || rozmiar < 8U)
        return -1;

    wynik = lodepng_decode24(&bufor_rgb, &szerokosc, &wysokosc,
                             dane, (size_t)rozmiar);
    if (wynik != 0U || bufor_rgb == 0)
    {
        if (bufor_rgb != 0)
            lodepng_free(bufor_rgb);
        return -1;
    }

    /*
     * Historyczna funkcja PixPict rysuje obraz wycentrowany i wylicza jego
     * rozmiar z marginesow. Akceptujemy wiec tylko obrazy mieszczace sie na
     * LCD i o parzystej roznicy wymiarow, aby nie obciac ostatniego wiersza
     * lub kolumny. Wbudowane logo ma dokladnie 480x272 piksele.
     */
    if (szerokosc == 0U || wysokosc == 0U ||
        szerokosc > 480U || wysokosc > 272U ||
        ((480U - szerokosc) & 1U) != 0U ||
        ((272U - wysokosc) & 1U) != 0U)
    {
        lodepng_free(bufor_rgb);
        return -1;
    }

    PixPict((480U - szerokosc) / 2U,
            (272U - wysokosc) / 2U,
            (char *)bufor_rgb);
    lodepng_free(bufor_rgb);
    return 0;
}

int32_t ShowLogo(void)
{

    uint32_t br = 0;
    int type = 0;
    FRESULT res;
    FIL fo = {0};
    //FILINFO finfo;
    //DIR dir = { 0 };
    //FILINFO fno = { 0 };

    res = f_open(&fo, g_logo_fpath1, FA_READ);
    if (FR_OK == res)
    {
        type = 1; //bmp
    }

    else
    {
        res = f_open(&fo, g_logo_fpath2, FA_READ);
        if (res == FR_OK)
        {
            type = 2; // png
        }
        else
        {

            Sleep(1000);
        }
    }
    if (type == 0)
    {
        return -1;
    }
    if (type == 1)
    {
        res = f_read(&fo, bmpFileBuffer, SCREENSHOT_FILE_SIZE, &br);
        f_close(&fo);
        if (FR_OK != res)
            return -1; // no logo file found
        if (br != SCREENSHOT_FILE_SIZE || FR_OK != res)
            return -1;
        LCD_DrawBitmap(LCD_MakePoint(0, 0), bmpFileBuffer, SCREENSHOT_FILE_SIZE);
        return 0;
    }
    else
    {
        res = f_read(&fo, bmpFileBuffer, fo.fsize, &br);
        if (res != 0)
        {
            f_close(&fo);
            return -1;
        }
        else
        {
            f_close(&fo);
            if (SCREENSHOT_RysujPNGZPamieci(bmpFileBuffer, br) != 0)
                return -1;
        }
    }
    return 0;
}

static TCHAR fileNames[13][13];
static DWORD FileLength[12];

void AnalyzeDate(WORD datex, char *str)
{
    int yy, mm, dd;
    if (NoDate == 1)
        return;
    yy = datex >> 9;
    dd = datex & 0x1f;
    mm = (datex >> 5) & 0xf;
    if (DatumDDMMYYYY)
    {
        sprintf(&str[0], "%02d %02d %04d", dd, mm, yy + 1980);
    }
    else
    {
        sprintf(&str[0], "%04d %02d %02d", yy + 1980, mm, dd);
    }
}
void AnalyzeTime(WORD datex, char *str)
{
    int hh, mm;
    if (NoDate == 1)
        return;
    hh = datex >> 11;
    mm = (datex >> 5) & 0x3f;
    sprintf(&str[0], "%02d:%02d", hh, mm);
}

static uint8_t SCREENSHOT_CzyPlikZrzutu(const FILINFO *fno)
{
    const char *kropka;

    if (fno == 0 || fno->fname[0] == '\0')
        return 0U;
    if ((_FS_RPATH != 0) && fno->fname[0] == '.')
        return 0U;
    if ((fno->fattrib & AM_DIR) != 0U)
        return 0U;

    kropka = strrchr(fno->fname, '.');
    if (kropka == 0)
        return 0U;

    return (uint8_t)(strcasecmp(kropka, ".bmp") == 0 ||
                     strcasecmp(kropka, ".png") == 0);
}

static void SCREENSHOT_FormatujDate(WORD data_fat, char *bufor, size_t rozmiar)
{
    int rok;
    int miesiac;
    int dzien;

    if (bufor == 0 || rozmiar == 0U)
        return;

    rok = (int)(data_fat >> 9) + 1980;
    miesiac = (int)((data_fat >> 5) & 0x0FU);
    dzien = (int)(data_fat & 0x1FU);

    if (data_fat == 0U || NoDate == 1)
    {
        snprintf(bufor, rozmiar, "--");
        return;
    }

    if (DatumDDMMYYYY)
        snprintf(bufor, rozmiar, "%02d.%02d.%04d", dzien, miesiac, rok);
    else
        snprintf(bufor, rozmiar, "%04d-%02d-%02d", rok, miesiac, dzien);
}

static void SCREENSHOT_FormatujCzas(WORD czas_fat, char *bufor, size_t rozmiar)
{
    int godzina;
    int minuta;

    if (bufor == 0 || rozmiar == 0U)
        return;

    if (czas_fat == 0U || NoDate == 1)
    {
        snprintf(bufor, rozmiar, "--:--");
        return;
    }

    godzina = (int)(czas_fat >> 11);
    minuta = (int)((czas_fat >> 5) & 0x3FU);
    snprintf(bufor, rozmiar, "%02d:%02d", godzina, minuta);
}

int16_t SCREENSHOT_WczytajStrone(uint16_t pierwszy,
                                 SCREENSHOT_PLIK_t *pliki,
                                 uint16_t pojemnosc,
                                 uint16_t *liczba_wszystkich)
{
    DIR dir = {0};
    FILINFO fno = {0};
    FRESULT wynik;
    uint16_t indeks = 0U;
    uint16_t zapisanych = 0U;

    if (liczba_wszystkich != 0)
        *liczba_wszystkich = 0U;
    if (pliki == 0 || pojemnosc == 0U)
        return 0;

    memset(pliki, 0, (size_t)pojemnosc * sizeof(pliki[0]));
    (void)f_mkdir(SNDIR);

    wynik = f_opendir(&dir, SNDIR);
    if (wynik != FR_OK)
        return -1;

    for (;;)
    {
        wynik = f_readdir(&dir, &fno);
        if (wynik != FR_OK || fno.fname[0] == '\0')
            break;
        if (!SCREENSHOT_CzyPlikZrzutu(&fno))
            continue;

        if (indeks >= pierwszy && zapisanych < pojemnosc)
        {
            SCREENSHOT_PLIK_t *plik = &pliki[zapisanych];
            snprintf(plik->nazwa, sizeof(plik->nazwa), "%s", fno.fname);
            SCREENSHOT_FormatujDate(fno.fdate, plik->data, sizeof(plik->data));
            SCREENSHOT_FormatujCzas(fno.ftime, plik->czas, sizeof(plik->czas));
            plik->rozmiar_b = (uint32_t)fno.fsize;
            zapisanych++;
        }
        indeks++;
    }

    (void)f_closedir(&dir);
    if (wynik != FR_OK)
        return -1;

    if (liczba_wszystkich != 0)
        *liczba_wszystkich = indeks;
    return (int16_t)zapisanych;
}

/*
 * Zachowujemy historyczna funkcje jako cienki adapter dla starszych wywolan.
 * Nie rysuje juz nazw plikow na LCD; tylko wypelnia bufor zgodnosci.
 */
int16_t SCREENSHOT_SelectFileNames(int fileNoMin)
{
    SCREENSHOT_PLIK_t pliki[12];
    uint16_t razem = 0U;
    int16_t liczba;
    int i;

    if (fileNoMin < 0)
        fileNoMin = 0;

    liczba = SCREENSHOT_WczytajStrone((uint16_t)fileNoMin, pliki, 12U, &razem);
    if (liczba < 0)
        return liczba;

    memset(fileNames, 0, sizeof(fileNames));
    memset(FileLength, 0, sizeof(FileLength));
    for (i = 0; i < liczba; ++i)
    {
        /*
         * Historyczny bufor zgodności ma format krótkiej nazwy 8.3 (12 znaków
         * plus NUL). Nowy ekran używa pełnego SCREENSHOT_PLIK_t, więc tutaj
         * jawnie kopiujemy tylko tyle, ile stary interfejs potrafi przenieść.
         */
        memcpy(fileNames[i], pliki[i].nazwa, sizeof(fileNames[i]) - 1U);
        fileNames[i][sizeof(fileNames[i]) - 1U] = '\0';
        FileLength[i] = pliki[i].rozmiar_b;
    }
    FileNo = (uint16_t)fileNoMin;
    (void)razem;
    return liczba;
}

char *SCREENSHOT_SelectFileName(void)
{
    static char fname[64];

    if (!CFG_CzyKartaSDDostepna())
    {
        fname[0] = '\0';
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_BRAK_KARTY_SD);
        return fname;
    }
    //char path[128];
    uint32_t dfnum = 0;
    //uint32_t retVal;

    f_mkdir(SNDIR);

    //Scan dir for snapshot files
    uint32_t fmax = 0;
    uint32_t fmin = 0xFFFFFFFFul;
    DIR dir = {0};
    FILINFO fno = {0};
    FRESULT fr = f_opendir(&dir, SNDIR);
    numfiles = 0;
    oldest = 0xFFFFFFFFul;
    fname[0] = '\0';
    int i;
    if (fr == FR_OK)
    {
        for (;;)
        {
            fr = f_readdir(&dir, &fno); //Iterate through the directory
            if (fr != FR_OK || !fno.fname[0])
                break; //Nothing to do
            if (_FS_RPATH && fno.fname[0] == '.')
                continue; //bypass hidden files
            if (fno.fattrib & AM_DIR)
                continue; //bypass subdirs
            int len = strlen(fno.fname);
            if (len != 12) //Bypass filenames with unexpected name length
                continue;
            const char *pdot = strchr(fno.fname, (int)'.');
            if (0 == pdot)
                continue;
            if (0 != strcasecmp(pdot, ".bmp") && 0 != strcasecmp(pdot, ".png"))
                continue; //Bypass files that are not bmp
            for (i = 0; i < 8; i++)
                if (!isdigit((int)fno.fname[i]))
                    break;
            if (i != 8)
                continue; //Bypass file names that are not 8-digit numbers
            numfiles++;
            //Now convert file name to number

            char *endptr;
            dfnum = strtoul(fno.fname, &endptr, 10);
            if (dfnum < fmin)
                fmin = dfnum;
            if (dfnum > fmax)
                fmax = dfnum;
        }
        f_closedir(&dir);
    }
    else
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ODCZYTU_PLIKU);
        return fname;
    }

    oldest = fmin;
    dfnum = fmax + 1;
    sprintf(fname, "%08lu", dfnum);

    if (KeyboardWindow(fname, 8, JEZYK_Wybierz("Wpisz nazwę pliku", "Enter the file name", "Dateinamen eingeben", "Введите имя файла")) == 0)
        fname[0] = '\0';
    return fname;
}

void SCREENSHOT_DeleteOldest(void)
{
    char path[128];
    if (0xFFFFFFFFul != oldest && numfiles >= 100)
    {
        sprintf(path, "%s/%08lu.s1p", SNDIR, oldest);
        f_unlink(path);
        sprintf(path, "%s/%08lu.bmp", SNDIR, oldest);
        f_unlink(path);
        sprintf(path, "%s/%08lu.png", SNDIR, oldest);
        f_unlink(path);
        numfiles = 0;
        oldest = 0xFFFFFFFFul;
    }
}

void Date_Time_Stamp(void)
{
    char text[24];
    uint8_t second1;
    uint32_t mon;
    short AMPM1;

    //LCD_FillRect((LCDPoint){70,248}, (LCDPoint){479,271}, BackGrColor);
    LCD_FillRect((LCDPoint){50, 248}, (LCDPoint){479, 271}, BackGrColor); //changed by wk
    if (NoDate == 1)
        return;
    if (RTCpresent == 1)
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
    if (DatumDDMMYYYY)
    {
        sprintf(text, "%02lu %02lu %04lu ", mon % 100, mon / 100, date / 10000);
    }
    else
    {
        sprintf(text, "%04lu %02lu %02lu ", date / 10000, mon / 100, mon % 100);
    }
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 100, 252, text);
    sprintf(text, "%02lu:%02lu:%02lu ", time / 100, time % 100, (uint32_t)second1);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 180, 252, text);
}

/*
 * Zapis BMP wraca do sprawdzonego mechanizmu z rc6-test3: ekran jest
 * odczytywany wiersz po wierszu, a FatFs dostaje po 1440 bajtow na wiersz.
 *
 * Poprzednia optymalizacja test4-test8 skladala cale 391734 B w jednym
 * buforze i przekazywala je jednym f_write(). To moglo wygenerowac bardzo
 * duzy transfer wielosektorowy do SDMMC/DMA. Sterownik BSP dokumentuje
 * liczbe blokow w pojedynczym wywolaniu jako 1..128, dlatego nie warto
 * uzalezniac zrzutow od tak duzego transferu. Wierszowy zapis jest wolniejszy
 * tylko nieznacznie, a byl juz sprawdzony sprzetowo w tej galezi projektu.
 */
static uint32_t __attribute__((section(".user_sdram"), aligned(32))) screenshot_linia_argb[480];

static SCREENSHOT_DIAGNOSTYKA_ZAPISU_t screenshot_diagnostyka_zapisu =
{
    SCREENSHOT_ETAP_OK, 0U, 0U, SCREENSHOT_FILE_SIZE
};

static void SCREENSHOT_UstawDiagnostyke(SCREENSHOT_ETAP_ZAPISU_t etap,
                                         FRESULT kod_fatfs,
                                         uint32_t zapisano_b)
{
    screenshot_diagnostyka_zapisu.etap = etap;
    screenshot_diagnostyka_zapisu.kod_fatfs = (uint8_t)kod_fatfs;
    screenshot_diagnostyka_zapisu.zapisano_b = zapisano_b;
    screenshot_diagnostyka_zapisu.oczekiwano_b = SCREENSHOT_FILE_SIZE;
}

void SCREENSHOT_PobierzDiagnostykeZapisu(SCREENSHOT_DIAGNOSTYKA_ZAPISU_t *diagnostyka)
{
    if (diagnostyka != 0)
        *diagnostyka = screenshot_diagnostyka_zapisu;
}

const char *SCREENSHOT_NazwaEtapuZapisu(SCREENSHOT_ETAP_ZAPISU_t etap)
{
    switch (etap)
    {
    case SCREENSHOT_ETAP_PARAMETRY: return "PARAMETRY";
    case SCREENSHOT_ETAP_BUDOWA_OBRAZU: return "OBRAZ";
    case SCREENSHOT_ETAP_OTWARCIE: return "OPEN";
    case SCREENSHOT_ETAP_ZAPIS: return "WRITE";
    case SCREENSHOT_ETAP_SYNCHRONIZACJA: return "SYNC";
    case SCREENSHOT_ETAP_ZAMKNIECIE: return "CLOSE";
    case SCREENSHOT_ETAP_WERYFIKACJA: return "VERIFY";
    case SCREENSHOT_ETAP_OK:
    default: return "OK";
    }
}

uint8_t SCREENSHOT_ZapiszBMPDoPliku(const char *sciezka)
{
    FRESULT fr = FR_OK;
    FIL fo = {0};
    FILINFO informacje = {0};
    uint8_t plik_otwarty = 0U;
    uint8_t ekran_wylaczony = 0U;
    uint32_t zapisano_lacznie = 0U;
    uint8_t *dane_obrazu = &bmpFileBufferTMP[sizeof(bmp_hdr)];
    const uint32_t bajtow_wiersza = 480U * 3U;

    SCREENSHOT_UstawDiagnostyke(SCREENSHOT_ETAP_OK, FR_OK, 0U);

    if (sciezka == NULL || sciezka[0] == '\0')
    {
        SCREENSHOT_UstawDiagnostyke(SCREENSHOT_ETAP_PARAMETRY, FR_INVALID_PARAMETER, 0U);
        return 0U;
    }
    if (!CFG_CzyKartaSDDostepna())
    {
        SCREENSHOT_UstawDiagnostyke(SCREENSHOT_ETAP_PARAMETRY, FR_NOT_READY, 0U);
        return 0U;
    }

    /*
     * v2.03-test12: najpierw budujemy cały BMP w istniejącym buforze SDRAM,
     * a dopiero potem wykonujemy jeden duży f_write(). Poprzednia wersja
     * wykonywała 272 osobne zapisy po 1440 B. Na kartach o dużej latencji
     * administracyjnej dawało to 15-20 s na zrzut mimo małego pliku 382 KiB.
     *
     * Format obrazu pozostaje identyczny: 24-bit BGR, 480x272, bez kompresji.
     */
    memcpy(bmpFileBufferTMP, bmp_hdr, sizeof(bmp_hdr));

    SCB_CleanDCache_by_Addr((uint32_t *)LCD_FB_START_ADDRESS,
                            BSP_LCD_GetXSize() * BSP_LCD_GetYSize() * 4);
    Sleep(2);

    if (LCD_Get_Orientation() == 1)
    {
        int y;
        for (y = 0; y < 272; ++y)
        {
            int x;
            uint8_t *wiersz = dane_obrazu + ((uint32_t)y * bajtow_wiersza);
            BSP_LCD_ReadLine((uint16_t)y, screenshot_linia_argb);
            for (x = 0; x < 480; ++x)
            {
                const uint32_t px = screenshot_linia_argb[479 - x];
                wiersz[3 * x + 0] = (uint8_t)(px & 0xFFU);
                wiersz[3 * x + 1] = (uint8_t)((px >> 8) & 0xFFU);
                wiersz[3 * x + 2] = (uint8_t)((px >> 16) & 0xFFU);
            }
        }
    }
    else
    {
        int wiersz_docelowy;
        for (wiersz_docelowy = 0; wiersz_docelowy < 272; ++wiersz_docelowy)
        {
            int x;
            const int y = 271 - wiersz_docelowy;
            uint8_t *wiersz = dane_obrazu + ((uint32_t)wiersz_docelowy * bajtow_wiersza);
            BSP_LCD_ReadLine((uint16_t)y, screenshot_linia_argb);
            for (x = 0; x < 480; ++x)
            {
                const uint32_t px = screenshot_linia_argb[x];
                wiersz[3 * x + 0] = (uint8_t)(px & 0xFFU);
                wiersz[3 * x + 1] = (uint8_t)((px >> 8) & 0xFFU);
                wiersz[3 * x + 2] = (uint8_t)((px >> 16) & 0xFFU);
            }
        }
    }

    BSP_LCD_DisplayOff();
    ekran_wylaczony = 1U;

    fr = f_open(&fo, sciezka, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        SCREENSHOT_UstawDiagnostyke(SCREENSHOT_ETAP_OTWARCIE, fr, 0U);
        goto blad_zapisu;
    }
    plik_otwarty = 1U;

    /*
     * POWROT DO MECHANIZMU Z rc6-test9/test13 (galaz test17C).
     *
     * Wersja test12 skladala caly obraz w SDRAM i zapisywala go jednym
     * f_write(391734 B). Bylo to szybsze, ale na rzeczywistej karcie zawodzilo
     * w dlugiej serii — pojedyncze zadanie tej wielkosci trafia w wewnetrzne
     * przepisywanie blokow i nie przechodzi. Na test17C ten sam sprzet zebral
     * 702 zrzuty w szesciu jezykach, zapisujac naglowek i 272 wiersze po 1440 B.
     *
     * Obraz jest juz zlozony w buforze; zmienia sie wylacznie wielkosc
     * pojedynczej operacji zapisu. Format pliku pozostaje identyczny.
     */
    {
        uint32_t pozycja;

        for (pozycja = 0U; pozycja < SCREENSHOT_FILE_SIZE; )
        {
            UINT bw = 0U;
            const uint32_t pozostalo = SCREENSHOT_FILE_SIZE - pozycja;
            const UINT porcja = (pozostalo > bajtow_wiersza)
                                    ? (UINT)bajtow_wiersza
                                    : (UINT)pozostalo;

            fr = f_write(&fo, &bmpFileBufferTMP[pozycja], porcja, &bw);
            if (fr != FR_OK || bw != porcja)
            {
                zapisano_lacznie = pozycja + (uint32_t)bw;

                /*
                 * FatFs może zwrócić FR_OK i jednocześnie zapisać mniej bajtów
                 * (np. po wyczerpaniu miejsca). Taki wynik nie jest sukcesem.
                 * Ujednolicamy go z zapisem konfiguracji i raportujemy FR_DENIED,
                 * aby ekran nie pokazywał mylącego „FatFS: 0”.
                 */
                if (fr == FR_OK)
                    fr = FR_DENIED;
                SCREENSHOT_UstawDiagnostyke(SCREENSHOT_ETAP_ZAPIS, fr, zapisano_lacznie);
                goto blad_zapisu;
            }
            pozycja += bw;
        }
        zapisano_lacznie = pozycja;
    }

    /*
     * f_sync() przed f_close() byl czescia sprawdzonej sekwencji. Kosztuje,
     * ale to wlasnie ta wersja przetrwala dluga serie na sprzecie.
     */
    fr = f_sync(&fo);
    if (fr != FR_OK)
    {
        /*
         * Synchronizacja ma osobny etap diagnostyczny. Wczesniej blad
         * f_sync() byl raportowany jako WRITE, co utrudnialo rozroznienie
         * problemu zapisu danych od problemu opróżniania buforow FatFs/SD.
         */
        SCREENSHOT_UstawDiagnostyke(SCREENSHOT_ETAP_SYNCHRONIZACJA, fr, zapisano_lacznie);
        goto blad_zapisu;
    }

    fr = f_close(&fo);
    plik_otwarty = 0U;
    if (fr != FR_OK)
    {
        SCREENSHOT_UstawDiagnostyke(SCREENSHOT_ETAP_ZAMKNIECIE, fr, zapisano_lacznie);
        goto blad_zapisu;
    }

    fr = f_stat(sciezka, &informacje);
    if (fr != FR_OK || informacje.fsize != SCREENSHOT_FILE_SIZE)
    {
        SCREENSHOT_UstawDiagnostyke(SCREENSHOT_ETAP_WERYFIKACJA, fr, informacje.fsize);
        goto blad_zapisu;
    }

    SCREENSHOT_UstawDiagnostyke(SCREENSHOT_ETAP_OK, FR_OK, SCREENSHOT_FILE_SIZE);
    if (ekran_wylaczony)
        BSP_LCD_DisplayOn();
    return 1U;

blad_zapisu:
    if (plik_otwarty)
        (void)f_close(&fo);
    (void)f_unlink(sciezka);
    if (ekran_wylaczony)
        BSP_LCD_DisplayOn();
    return 0U;
}

void SCREENSHOT_Save(const char *fname)
{
    char path[64];

    if (!CFG_CzyKartaSDDostepna())
    {
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_BRAK_KARTY_SD);
        return;
    }

    sprintf(path, "%s.bmp", fname);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 300, 252, path);

    f_mkdir(SNDIR);
    sprintf(path, "%s/%s.bmp", SNDIR, fname);
    if (!SCREENSHOT_ZapiszBMPDoPliku(path))
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ZAPISU_PLIKU);
}

//Custom allocators for LodePNG using SDRAM heap
#include "sdram_heap.h"
void *lodepng_malloc(size_t size)
{
    return SDRH_try_malloc(size);
}

void *lodepng_realloc(void *ptr, size_t new_size)
{
    return SDRH_try_realloc(ptr, new_size);
}

void lodepng_free(void *ptr)
{
    SDRH_free(ptr);
}

//Exhange Red and Blue colors for proper PNG encoding
static void _Change_B_R(uint32_t *image)
{
    uint32_t i, px, c, r, b;
    uint32_t endi = (uint32_t)LCD_GetWidth() * (uint32_t)LCD_GetHeight();
    if (LCD_Get_Orientation() == 1)
    {
        for (i = 0; i < endi / 2; i++)
        {

            c = image[i];
            r = (c >> 16) & 0xFF;
            b = c & 0xFF;
            px = (c & 0xFF00FF00) | r | (b << 16);
            c = image[endi - i - 1]; // [(271-i)*480-k]
            r = (c >> 16) & 0xFF;
            b = c & 0xFF;
            image[i] = (c & 0xFF00FF00) | r | (b << 16);
            image[endi - i - 1] = px; // (271-i)*480-k
        }
    }
    else
    {
        for (i = 0; i < endi; i++)
        {
            uint32_t c = image[i];
            uint32_t r = (c >> 16) & 0xFF;
            uint32_t b = c & 0xFF;
            image[i] = (c & 0xFF00FF00) | r | (b << 16);
        }
    }
}

void SCREENSHOT_SavePNG(const char *fname)
{
    char path[64];
    FRESULT fr = FR_OK;
    FIL fo = {0};
    uint8_t plik_otwarty = 0;
    uint8_t plik_utworzony = 0;
    uint8_t ekran_wylaczony = 0;
    uint8_t kolory_zamienione = 0;
    uint8_t *png = 0;
    uint8_t *image = 0;

    if (!CFG_CzyKartaSDDostepna())
    {
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_BRAK_KARTY_SD);
        return;
    }

    sprintf(path, "%s.png", fname);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 300, 252, path);

    image = LCD_Push();
    if (0 == image)
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ZAPISU_PLIKU);
        return;
    }

    _Change_B_R((uint32_t *)image);
    kolory_zamienione = 1;

    size_t pngsize = 0;
    BSP_LCD_DisplayOff();
    ekran_wylaczony = 1;

    if (lodepng_encode32(&png, &pngsize, image, LCD_GetWidth(), LCD_GetHeight()) != 0 || png == 0)
        goto BLAD_ZAPISU;

    f_mkdir(SNDIR);
    sprintf(path, "%s/%s.png", SNDIR, fname);
    fr = f_open(&fo, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (FR_OK != fr)
        goto BLAD_ZAPISU;
    plik_otwarty = 1;
    plik_utworzony = 1;

    UINT bw = 0;
    fr = f_write(&fo, png, pngsize, &bw);
    if (FR_OK != fr || bw != pngsize)
        goto BLAD_ZAPISU;

    f_close(&fo);
    plik_otwarty = 0;
    BSP_LCD_DisplayOn();
    ekran_wylaczony = 0;
    lodepng_free(png);
    png = 0;
    _Change_B_R((uint32_t *)image);
    kolory_zamienione = 0;
    LCD_Pop();
    return;

BLAD_ZAPISU:
    if (plik_otwarty)
        f_close(&fo);
    if (plik_utworzony)
        f_unlink(path);
    if (ekran_wylaczony)
        BSP_LCD_DisplayOn();
    if (png != 0)
        lodepng_free(png);
    if (kolory_zamienione)
        _Change_B_R((uint32_t *)image);
    LCD_Pop();
    KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ZAPISU_PLIKU);
}

extern unsigned lodepng_decode32(unsigned char **out, unsigned *w, unsigned *h,
                                 const unsigned char *in, size_t insize);

static uint8_t SCREENSHOT_ZbudujSciezke(const char *nazwa, char *sciezka, size_t rozmiar)
{
    size_t i;

    if (nazwa == 0 || nazwa[0] == '\0' || sciezka == 0 || rozmiar == 0U)
        return 0U;

    /* Nazwa pochodzi z katalogu zrzutow. Mimo to nie dopuszczamy separatorow. */
    for (i = 0U; nazwa[i] != '\0'; ++i)
    {
        if (nazwa[i] == '/' || nazwa[i] == '\\')
            return 0U;
    }

    if (snprintf(sciezka, rozmiar, "%s/%s", SNDIR, nazwa) >= (int)rozmiar)
        return 0U;
    return 1U;
}

uint8_t SCREENSHOT_PokazSciezke(const char *sciezka)
{
    unsigned szerokosc = 0U;
    unsigned wysokosc = 0U;
    unsigned wynik_png;
    uint8_t *bufor_png = 0;
    FIL plik = {0};
    FRESULT wynik;
    UINT odczytano = 0U;
    const char *kropka;

    if (sciezka == NULL || sciezka[0] == '\0' || !CFG_CzyKartaSDDostepna())
        return 0U;

    while (TOUCH_IsPressed())
        ;
    WEJSCIA_WyczyscZdarzenia();

    wynik = f_open(&plik, sciezka, FA_READ);
    if (wynik != FR_OK)
        return 0U;

    kropka = strrchr(sciezka, '.');
    if (kropka != 0 && strcasecmp(kropka, ".bmp") == 0)
    {
        wynik = f_read(&plik, bmpFileBuffer, SCREENSHOT_FILE_SIZE, &odczytano);
        (void)f_close(&plik);
        if (wynik != FR_OK || odczytano != SCREENSHOT_FILE_SIZE)
            return 0U;
        LCD_DrawBitmap(LCD_MakePoint(0, 0), bmpFileBuffer, SCREENSHOT_FILE_SIZE);
    }
    else if (kropka != 0 && strcasecmp(kropka, ".png") == 0)
    {
        const DWORD rozmiar_pliku = f_size(&plik);
        if (rozmiar_pliku == 0U || rozmiar_pliku > sizeof(bmpFileBuffer))
        {
            (void)f_close(&plik);
            return 0U;
        }

        wynik = f_read(&plik, bmpFileBuffer, (UINT)rozmiar_pliku, &odczytano);
        (void)f_close(&plik);
        if (wynik != FR_OK || odczytano != (UINT)rozmiar_pliku)
            return 0U;

        wynik_png = lodepng_decode24(&bufor_png, &szerokosc, &wysokosc,
                                     bmpFileBuffer, (size_t)rozmiar_pliku);
        if (wynik_png != 0U || bufor_png == 0)
            return 0U;

        if (szerokosc > 480U || wysokosc > 272U)
        {
            lodepng_free(bufor_png);
            return 0U;
        }

        PixPict((unsigned int)((480U - szerokosc) / 2U),
                (unsigned int)((272U - wysokosc) / 2U),
                (char *)bufor_png);
        lodepng_free(bufor_png);
    }
    else
    {
        (void)f_close(&plik);
        return 0U;
    }

    /* Podgląd zamykamy dotykiem, OK albo Wstecz. */
    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK ||
            zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ ||
            zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
            break;
        if (TOUCH_Poll(&punkt))
        {
            while (TOUCH_IsPressed())
                ;
            break;
        }
        Sleep(20);
    }

    return 1U;
}

uint8_t SCREENSHOT_PokazPlik(const char *nazwa)
{
    char sciezka[96];

    if (!SCREENSHOT_ZbudujSciezke(nazwa, sciezka, sizeof(sciezka)))
        return 0U;
    return SCREENSHOT_PokazSciezke(sciezka);
}

uint8_t SCREENSHOT_UsunPlik(const char *nazwa)
{
    char sciezka[96];

    if (!SCREENSHOT_ZbudujSciezke(nazwa, sciezka, sizeof(sciezka)))
        return 0U;
    return (uint8_t)(f_unlink(sciezka) == FR_OK);
}

void SCREENSHOT_ShowPicture(uint16_t Pointer1)
{
    if (Pointer1 >= 12U || fileNames[Pointer1][0] == '\0')
        return;
    (void)SCREENSHOT_PokazPlik((const char *)fileNames[Pointer1]);
}

void SCREENSHOT_DeleteFile(uint16_t Pointer1)
{
    if (Pointer1 >= 12U || fileNames[Pointer1][0] == '\0')
        return;
    (void)SCREENSHOT_UsunPlik((const char *)fileNames[Pointer1]);
}

