#include <math.h>
#include <float.h>
#include <string.h>
#include <stdio.h>
#include "oslfile.h"
#include "kalibracja_meta.h"
#include "ff.h"
#include "crash.h"
#include "config.h"
#include "font.h"
#include "LCD.h"
#include "gen.h"
#include "dsp.h"
#include "si5351.h"

#define MAX_OSLFILES 16
#define OSL_BASE_R0 50.0f // Note: all OSL calibration coefficients are calculated using G based on 50 Ohms, not on CFG_PARAM_R0 !!!

#define OSL_SMALL_SCAN_STEP (100000U)
#define OSL_SCAN_STEP (300000U)
#define OSL_FREQUENCY_BORDER (150000000U)

/*
 * V2.1 ma produkcyjny Fmin 100..500 kHz. Siatka OSL jest więc celowo
 * prosta: 100 kHz do 150 MHz, a powyżej 150 MHz 300 kHz. Historyczna
 * gałąź 20..100 kHz co 10 kHz została usunięta razem z laboratoryjnym
 * zakresem, dzięki czemu indeksowanie i interpolacja mają jedną regułę.
 */
#define OSL_MID_COUNT_FROM(fmin_) \
    (((fmin_) <= OSL_FREQUENCY_BORDER) \
         ? (((OSL_FREQUENCY_BORDER - (fmin_)) / OSL_SMALL_SCAN_STEP) + 1U) \
         : 0U)

#define OSL_TABLES_IN_SDRAM (1)

/* Maksymalny rozmiar tablic odpowiada kompilacyjnej dolnej granicy 100 kHz. */
#define OSL_ENTRIES \
    (OSL_MID_COUNT_FROM(BAND_FMIN) + \
     ((MAX_BAND_FREQ - OSL_FREQUENCY_BORDER) / OSL_SCAN_STEP))

/* Liczba rzeczywiście używanych rekordów zależy od zakresu użytkownika. */
#define OSL_NUM_VALID_ENTRIES \
    (OSL_MID_COUNT_FROM(CFG_GetParam(CFG_PARAM_BAND_FMIN)) + \
     ((CFG_GetParam(CFG_PARAM_BAND_FMAX) - OSL_FREQUENCY_BORDER) / OSL_SCAN_STEP))

/* Indeks punktu 150 MHz - ostatniego punktu gęstszej części siatki. */
#define MID_IDX (OSL_MID_COUNT_FROM(CFG_GetParam(CFG_PARAM_BAND_FMIN)) - 1U)

extern void Sleep(uint32_t);
extern void progress_cb(uint32_t new_percent);

static float _nonz(float f) __attribute__((unused));
static float complex _cnonz(float complex f) __attribute__((unused));

typedef float complex COMPLEX;

//Hardware error correction structure

typedef enum
{
    OSL_FILE_EMPTY = 0x00,
    OSL_FILE_VALID = 0x01,
    OSL_FILE_SCANNED_SHORT = 0x02,
    OSL_FILE_SCANNED_LOAD = 0x04,
    OSL_FILE_SCANNED_OPEN = 0x08,
    OSL_FILE_SCANNED_ALL = OSL_FILE_SCANNED_SHORT | OSL_FILE_SCANNED_LOAD | OSL_FILE_SCANNED_OPEN
} OSL_FILE_STATUS;

#if (1 == OSL_TABLES_IN_SDRAM)
#define MEMATTR_OSL __attribute__((section(".user_sdram")))
#else
#define MEMATTR_OSL
#endif

static OSL_FILE_STATUS osl_file_status = OSL_FILE_EMPTY;

static OSL_S21_LINIOWOSC_t s21_ocena_liniowosci;
static OSL_S21_KOREKCJA_LINIOWOSCI_t s21_korekcja_liniowosci;

static OSL_ERRCORR osl_errCorr[OSL_ENTRIES] MEMATTR_OSL;
static S_OSLDATA osl_data[OSL_ENTRIES] MEMATTR_OSL;

static float WORK_BUFF[OSL_ENTRIES * sizeof(OSL_ERRCORR) / sizeof(float)] MEMATTR_OSL;

/*
 * Jakość kalibracji S21 jest przechowywana osobno od historycznego pliku
 * txcorr.osl. Dzięki temu nie zmieniamy jego formatu ani zgodności z wcześniejszym
 * firmware, a jednocześnie możemy zachować informację, które punkty były
 * zmierzone bezpośrednio, słabe albo uzupełnione interpolacją.
 */
static uint8_t s21_jakosc_krok1[OSL_ENTRIES] MEMATTR_OSL;
static uint8_t s21_status_krok1[OSL_ENTRIES] MEMATTR_OSL;
static uint8_t s21_jakosc_koncowa[OSL_ENTRIES] MEMATTR_OSL;
static uint8_t s21_status_koncowy[OSL_ENTRIES] MEMATTR_OSL;
static uint8_t s21_jakosc_dostepna = 0U;

/*
 * Sekcja .user_sdram jest NOLOAD. W przeciwieństwie do zwykłego .bss jej
 * zawartość nie jest zerowana przez kod startowy. Jawne wyzerowanie wykonywane
 * raz po uruchomieniu zachowuje więc dotychczasową semantykę statycznych tablic
 * OSL po przeniesieniu ich do SDRAM.
 */
static uint8_t osl_tablice_zainicjalizowane = 0U;

/*
 * OSLSAFE2: zapis kalibracji jest wykonywany przez mały bufor pośredni.
 *
 * Powód jest praktyczny. Główne tablice OSL mieszkają w zewnętrznym SDRAM,
 * a FatFs potrafi dla dużego f_write() przekazać do sterownika SD kilka
 * sektorów naraz bez dodatkowego kopiowania. Właśnie w tej klasie operacji
 * obserwowaliśmy wcześniej pliki o prawidłowym rozmiarze, ale z powtórzonymi
 * blokami danych. Kalibracja jest zbyt ważna, żeby polegać wyłącznie na kodzie
 * zwrotnym f_write().
 *
 * Każda porcja ma najwyżej jeden sektor FAT (512 B). Najpierw kopiujemy dane
 * ze źródła do wyrównanego bufora w wewnętrznym SRAM, następnie zapisujemy
 * porcję. f_close() wykonuje wewnętrznie f_sync(), więc NIE wywołujemy f_sync()
 * drugi raz. Po zamknięciu plik jest otwierany ponownie i porównywany bajt po
 * bajcie z RAM. Dopiero zweryfikowany plik .tmp może zastąpić poprzednią
 * działającą kalibrację.
 */
#define OSL_ROZMIAR_PORCJI_IO 512U

static uint8_t osl_bufor_zapisu[OSL_ROZMIAR_PORCJI_IO] __attribute__((aligned(32)));
static uint8_t osl_bufor_weryfikacji[OSL_ROZMIAR_PORCJI_IO] __attribute__((aligned(32)));

static int32_t OSL_ZapiszIZweryfikujTmp(const TCHAR *sciezka_tmp,
                                        const void *dane,
                                        UINT rozmiar)
{
    FIL plik = {0};
    FRESULT wynik;
    UINT pozycja = 0U;
    const uint8_t *zrodlo = (const uint8_t *)dane;

    if (sciezka_tmp == NULL || dane == NULL || rozmiar == 0U)
        return -1;

    wynik = f_open(&plik, sciezka_tmp, FA_WRITE | FA_CREATE_ALWAYS);
    if (wynik != FR_OK)
        return -2;

    while (pozycja < rozmiar)
    {
        const UINT pozostalo = rozmiar - pozycja;
        const UINT porcja = pozostalo > OSL_ROZMIAR_PORCJI_IO
                                ? OSL_ROZMIAR_PORCJI_IO
                                : pozostalo;
        UINT zapisano = 0U;

        memcpy(osl_bufor_zapisu, zrodlo + pozycja, porcja);
        wynik = f_write(&plik, osl_bufor_zapisu, porcja, &zapisano);
        if (wynik != FR_OK || zapisano != porcja)
        {
            (void)f_close(&plik);
            (void)f_unlink(sciezka_tmp);
            return -3;
        }

        pozycja += porcja;
    }

    /*
     * W tej wersji FatFs f_close() samo wywołuje f_sync(). MASKSAFE1 robił
     * najpierw jawne f_sync(), a potem f_close(), czyli synchronizował plik
     * dwa razy. Na części kart SD taki nadmiarowy cykl kończył się błędem mimo
     * poprawnego wcześniejszego zapisu.
     */
    wynik = f_close(&plik);
    if (wynik != FR_OK)
    {
        (void)f_unlink(sciezka_tmp);
        return -4;
    }

    wynik = f_open(&plik, sciezka_tmp, FA_READ | FA_OPEN_EXISTING);
    if (wynik != FR_OK)
    {
        (void)f_unlink(sciezka_tmp);
        return -5;
    }

    pozycja = 0U;
    while (pozycja < rozmiar)
    {
        const UINT pozostalo = rozmiar - pozycja;
        const UINT porcja = pozostalo > OSL_ROZMIAR_PORCJI_IO
                                ? OSL_ROZMIAR_PORCJI_IO
                                : pozostalo;
        UINT odczytano = 0U;

        wynik = f_read(&plik, osl_bufor_weryfikacji, porcja, &odczytano);
        if (wynik != FR_OK || odczytano != porcja ||
            memcmp(osl_bufor_weryfikacji, zrodlo + pozycja, porcja) != 0)
        {
            (void)f_close(&plik);
            (void)f_unlink(sciezka_tmp);
            return -6;
        }

        pozycja += porcja;
    }

    wynik = f_close(&plik);
    if (wynik != FR_OK)
    {
        (void)f_unlink(sciezka_tmp);
        return -7;
    }

    return 0;
}

static void OSL_InicjalizujTablicePamieci(void)
{
    if (osl_tablice_zainicjalizowane)
        return;

    memset(osl_errCorr, 0, sizeof(osl_errCorr));
    memset(osl_data, 0, sizeof(osl_data));
    memset(WORK_BUFF, 0, sizeof(WORK_BUFF));
    memset(s21_jakosc_krok1, 0, sizeof(s21_jakosc_krok1));
    memset(s21_status_krok1, 0, sizeof(s21_status_krok1));
    memset(s21_jakosc_koncowa, 0, sizeof(s21_jakosc_koncowa));
    memset(s21_status_koncowy, 0, sizeof(s21_status_koncowy));
    osl_tablice_zainicjalizowane = 1U;
}

float *WORK_Ptr = &WORK_BUFF[0];

int WorkBuffMode = 0; //0: bufor wolny, 1: OSL, 2: S21/VNA.
OSL_ERRCORR *osl_txCorr = (OSL_ERRCORR *)&WORK_BUFF[0];
#ifdef _DEBUG_UART
void PrintOSLSize(void)
{
    DBG_Printf("1)err: OSL_ENTRIES:%u, OSL_ERRCORR:%u, osl_errCorr:%u", OSL_ENTRIES, sizeof(OSL_ERRCORR), sizeof(osl_errCorr));
    DBG_Printf("2)osl: OSL_ENTRIES:%u, S_OSLDATA:%u, osl_data:%u", OSL_ENTRIES, sizeof(S_OSLDATA), sizeof(osl_data));
    DBG_Printf("3)vna: OSL_ENTRIES:%u, OSL_ERRCORR:%u, osl_txCorr:%u", OSL_ENTRIES, sizeof(OSL_ERRCORR), sizeof(osl_txCorr));
}
#endif

static int32_t osl_file_loaded = -1;
static int32_t osl_last_load_result = -99;
static int32_t osl_err_loaded = 0;
static int32_t osl_tx_loaded = 0; //TX Calibration (Network Analyzer)

static const COMPLEX cmplus1 = 1.0f + 0.0fi;
static const COMPLEX cmminus1 = -1.0f + 0.0fi;

static int32_t OSL_LoadFromFile(void);

uint32_t OSL_GetCalFreqByIdx(int32_t idx)
{
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t liczba_srednich = OSL_MID_COUNT_FROM(fmin);

    if ((idx < 0) || (idx >= OSL_NUM_VALID_ENTRIES))
        return 0U;

    if ((uint32_t)idx < liczba_srednich)
        return fmin + (uint32_t)idx * OSL_SMALL_SCAN_STEP;

    return OSL_FREQUENCY_BORDER +
           (((uint32_t)idx - liczba_srednich) + 1U) * OSL_SCAN_STEP;
}

uint8_t OSL_CzyTabliceWSdram(void)
{
#if (1 == OSL_TABLES_IN_SDRAM)
    return 1U;
#else
    return 0U;
#endif
}

int Get_OSL_Entries(void)
{
    return OSL_ENTRIES;
}

//Fix by OM0IM: now returns floor instead of round in order to linearly interpolate
//HW calibration in OSL_CorrectErr(). This improves precision on low frequencies.
int GetIndexForFreq(uint32_t fhz)
{
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t liczba_srednich = OSL_MID_COUNT_FROM(fmin);

    if (fhz < fmin || fhz > CFG_GetParam(CFG_PARAM_BAND_FMAX))
        return -1;

    if (fhz <= OSL_FREQUENCY_BORDER)
        return (int)((fhz - fmin) / OSL_SMALL_SCAN_STEP);

    /*
     * Pierwszy punkt wysokiej części siatki to 150,3 MHz. Dla wartości
     * pomiędzy 150,0 a 150,3 MHz zwracamy indeks 150,0 MHz, aby dalsza
     * interpolacja pracowała pomiędzy rzeczywistymi sąsiadami.
     */
    return (int)(liczba_srednich - 1U +
                 ((fhz - OSL_FREQUENCY_BORDER) / OSL_SCAN_STEP));
}

typedef enum
{
    OSL_WZORZEC_ZWARCIE = 0,
    OSL_WZORZEC_OBCIAZENIE,
    OSL_WZORZEC_ROZWARCIE
} OSL_WZORZEC_t;

/*
 * W OSL nie wolno oceniac wzorcow tym samym warunkiem co zwyklego pomiaru Z.
 * Dla poprawnego OPEN prad moze byc tak maly, ze obliczona impedancja dazy do
 * nieskonczonosci. To nie jest blad pomiaru - wspolczynnik odbicia wzorca OPEN
 * dazy wtedy do +1. Historyczny OSL_GFromZ() wlasnie taki przypadek obslugiwal.
 *
 * Do decyzji o przerwaniu skanu wystarcza wiec sprawdzenie danych, ktore sa
 * rzeczywistym wynikiem toru odbiorczego: obu amplitud i fazy. Nie ma tu
 * arbitralnego progu napiecia. Zero, NaN lub Inf oznaczaja techniczny brak
 * pomiaru; mala, ale dodatnia amplituda jest dopuszczalna.
 */
static int OSL_SurowyPomiarPoprawny(void)
{
    const float napiecie_v_mv = DSP_MeasuredMagVmv();
    const float napiecie_i_mv = DSP_MeasuredMagImv();
    const float faza = DSP_MeasuredPhase();

    return isfinite(napiecie_v_mv) && napiecie_v_mv > 0.0f &&
           isfinite(napiecie_i_mv) && napiecie_i_mv > 0.0f &&
           isfinite(faza);
}

/*
 * Kalibracja sprzetowa HW nie uzywa skrajnych wzorcow OPEN/SHORT, dlatego
 * moze zachowac ostrzejsza kontrole zwyklego wyniku Z. Rozdzielenie tych dwoch
 * warunkow jest celowe: nie przenosimy wyjatku OPEN do kalibracji HW.
 */
static int OSL_PomiarHWWiarygodny(void)
{
    const float stosunek = DSP_MeasuredDiff();
    const COMPLEX z = DSP_MeasuredZ();

    return OSL_SurowyPomiarPoprawny() &&
           DSP_CzyOstatniPomiarPoprawny() &&
           isfinite(stosunek) && stosunek > 0.0f &&
           isfinite(crealf(z)) && isfinite(cimagf(z));
}

static int OSL_PobierzGammaPunktu(OSL_WZORZEC_t wzorzec, COMPLEX *gamma)
{
    const COMPLEX z = DSP_MeasuredZ();
    COMPLEX g;

    if (gamma == NULL || !OSL_SurowyPomiarPoprawny())
        return 0;

    if (isfinite(crealf(z)) && isfinite(cimagf(z)))
    {
        g = OSL_GFromZ(z, OSL_BASE_R0);
        if (isfinite(crealf(g)) && isfinite(cimagf(g)))
        {
            *gamma = g;
            return 1;
        }
    }

    /*
     * Nieskonczona impedancja jest poprawnym stanem granicznym tylko dla OPEN.
     * Uzywamy tej samej wartosci granicznej, ktora od lat stosuje OSL_GFromZ().
     * SHORT i LOAD nadal musza dac skonczona impedancje i skonczone Gamma.
     */
    if (wzorzec == OSL_WZORZEC_ROZWARCIE)
    {
        *gamma = 0.99999999f + 0.0fi;
        return 1;
    }

    return 0;
}

uint32_t OSL_PobierzLiczbeUsrednienKalibracji(void)
{
    uint32_t liczba = CFG_GetParam(CFG_PARAM_OSL_NSCANS);

    /*
     * Pojedyncza próbka była zbyt podatna na chwilowy szum fazy i amplitudy,
     * szczególnie w części pasma pracującej na 3. harmonicznej. Taki błąd
     * zostaje potem zapisany w każdym współczynniku OSL i daje poszarpany
     * wykres nawet dla dobrego wzorca rezystancyjnego.
     *
     * Trzy próbki są rozsądnym minimum: pozwalają filtrowi odrzucić pojedynczy
     * odstający wynik, a nie wydłużają kalibracji tak mocno jak stałe 5..10.
     */
    if (liczba < 3U)
        liczba = 3U;
    if (liczba > 20U)
        liczba = 20U;
    return liczba;
}

static int OSL_ZmierzGammaPunktu(uint32_t freq_hz, OSL_WZORZEC_t wzorzec,
                                 COMPLEX *gamma)
{
    int proba;

    /*
     * DSP ma wlasne ponowienia dla blednych probek FFT. Tu dodajemy trzy proby
     * calego punktu OSL, aby pojedynczy chwilowy odczyt nie kasowal skanu,
     * ktory na szerokim pasmie trwa wiele minut.
     */
    for (proba = 0; proba < 3; proba++)
    {
        DSP_Measure(freq_hz, 1, 0, (int)OSL_PobierzLiczbeUsrednienKalibracji());
        if (OSL_PobierzGammaPunktu(wzorzec, gamma))
            return 1;
    }

    return 0;
}

int32_t OSL_IsErrCorrLoaded(void)
{
    return osl_err_loaded;
}

void OSL_LoadErrCorr(void)
{
    OSL_InicjalizujTablicePamieci();
    FRESULT res;
    FIL fp;
    TCHAR path[64];

    osl_err_loaded = 0;

    sprintf(path, "%s/errcorr.osl", g_cfg_osldir);
    res = f_open(&fp, path, FA_READ | FA_OPEN_EXISTING);
    if (FR_OK != res)
        return;
    UINT br = 0;
    res = f_read(&fp, osl_errCorr, sizeof(*osl_errCorr) * OSL_NUM_VALID_ENTRIES, &br);
    f_close(&fp);
    if (FR_OK != res || (sizeof(*osl_errCorr) * OSL_NUM_VALID_ENTRIES != br))
        return;

    /*
     * RF160 nie uzywa po cichu korekcji HW z innego planu generatora.
     * Zmiana 200 -> 160 MHz oraz wersji planu harmonicznego wymaga nowego HW.
     */
    {
        KAL_META_DANE_t meta_hw;
        if (!KAL_META_Pobierz(KAL_META_HW, -1, &meta_hw) ||
            !KAL_META_CzyZgodnaZKonfiguracja(&meta_hw))
        {
            osl_err_loaded = 0;
            return;
        }
    }

    osl_err_loaded = 1;
}

int32_t OSL_ScanErrCorr(void (*progresscb)(uint32_t))
{
    uint32_t i;
    const UINT rozmiar = (UINT)(sizeof(OSL_ERRCORR) * OSL_NUM_VALID_ENTRIES);
    OSL_ERRCORR *nowa_korekcja = (OSL_ERRCORR *)&WORK_BUFF[0];
    TCHAR path[64];
    TCHAR path_tmp[64];
    TCHAR path_bak[64];

    /*
     * Skan trafia najpierw do bufora roboczego. Dopiero kompletny i poprawny
     * zestaw moze zastapic dzialajaca korekcje. Przerwana kalibracja nie
     * niszczy poprzednich wspolczynnikow w RAM ani na karcie SD.
     */
    for (i = 0; i < OSL_NUM_VALID_ENTRIES; i++)
    {
        const uint32_t freq = OSL_GetCalFreqByIdx(i);
        GEN_SetMeasurementFreq(freq);
        DSP_Measure(freq, 0, 0, (int)OSL_PobierzLiczbeUsrednienKalibracji());

        if (!OSL_PomiarHWWiarygodny())
        {
            GEN_SetMeasurementFreq(0);
            return -4;
        }

        nowa_korekcja[i].mag0 = 1.0f / DSP_MeasuredDiff();
        nowa_korekcja[i].phase0 = DSP_MeasuredPhase();
        if (!isfinite(nowa_korekcja[i].mag0) || !isfinite(nowa_korekcja[i].phase0))
        {
            GEN_SetMeasurementFreq(0);
            return -4;
        }

        if (progresscb)
            progresscb((i * 100) / OSL_NUM_VALID_ENTRIES);
    }
    GEN_SetMeasurementFreq(0);

    if (!CFG_CzyKartaSDDostepna())
        return -1;

    snprintf(path, sizeof(path), "%s/errcorr.osl", g_cfg_osldir);
    snprintf(path_tmp, sizeof(path_tmp), "%s/errcorr.tmp", g_cfg_osldir);
    snprintf(path_bak, sizeof(path_bak), "%s/errcorr.bak", g_cfg_osldir);
    f_mkdir(g_cfg_osldir);
    f_unlink(path_tmp);

    if (OSL_ZapiszIZweryfikujTmp(path_tmp, nowa_korekcja, rozmiar) != 0)
        return -3;

    /* Bezpieczna podmiana pliku: stary profil pozostaje kopia awaryjna. */
    f_unlink(path_bak);
    if (f_rename(path, path_bak) != FR_OK)
        f_unlink(path_bak); /* Brak starego pliku jest poprawnym przypadkiem. */

    if (f_rename(path_tmp, path) != FR_OK)
    {
        f_unlink(path_tmp);
        (void)f_rename(path_bak, path);
        return -3;
    }
    f_unlink(path_bak);

    memcpy(osl_errCorr, nowa_korekcja, rozmiar);
    osl_err_loaded = 1;
    (void)KAL_META_Zapisz(KAL_META_HW, -1);
    return 0;
}

/*
 * Wspólna korekcja sprzętowa HW.
 *
 * Historyczny wariant miał błąd nawiasów poniżej 150 MHz:
 *     fhz - BAND_FMIN / krok
 * zamiast:
 *     (fhz - BAND_FMIN) / krok
 * W praktyce współczynnik interpolacji wychodził tam poza zakres i korekcja
 * HW nie była nakładana. To utrudniało odtworzenie OSL i mogło maskować błąd
 * zależny od częstotliwości.
 *
 * Od wcześniejszej weryfikacji obie publiczne ścieżki korzystają z jednego, poprawnego algorytmu.
 * Faza jest interpolowana najkrótszym łukiem, aby przejście +179/-179 stopni
 * nie było błędnie prowadzone przez 0 stopni.
 */
static void OSL_CorrectErrWspolny(uint32_t fhz, float *magdif, float *phdif)
{
    int idx;
    uint32_t f0;
    uint32_t f1;
    float udzial = 0.0f;
    float mag0;
    float faza0;

    if (!osl_err_loaded || magdif == NULL || phdif == NULL)
        return;

    idx = GetIndexForFreq(fhz);
    if (idx < 0 || idx >= OSL_NUM_VALID_ENTRIES)
        return;

    f0 = OSL_GetCalFreqByIdx(idx);
    mag0 = osl_errCorr[idx].mag0;
    faza0 = osl_errCorr[idx].phase0;

    if (idx + 1 < OSL_NUM_VALID_ENTRIES &&
        fhz < CFG_GetParam(CFG_PARAM_BAND_FMAX))
    {
        const float mag1 = osl_errCorr[idx + 1].mag0;
        const float faza1 = osl_errCorr[idx + 1].phase0;
        const uint32_t rezim_cel = GEN_RezimPomiarowy(fhz);
        const uint32_t rezim_0 = GEN_RezimPomiarowy(f0);
        float roznica_fazy;

        f1 = OSL_GetCalFreqByIdx(idx + 1);

        /*
         * Nie mieszamy korekcji HW pomiedzy H1/H3 ani pomiedzy sposobami
         * programowania MultiSynth Si5351 (fractional/div6/div4). Na samej
         * granicy bierzemy najblizszy wspolczynnik z tego samego rezimu.
         */
        if (rezim_cel != 0U && rezim_0 != rezim_cel &&
            GEN_RezimPomiarowy(f1) == rezim_cel)
        {
            mag0 = mag1;
            faza0 = faza1;
        }
        else if (rezim_cel != 0U && rezim_0 == rezim_cel &&
                 GEN_RezimPomiarowy(f1) == rezim_cel)
        {
            if (f1 > f0)
                udzial = (float)(fhz - f0) / (float)(f1 - f0);

            if (udzial < 0.0f)
                udzial = 0.0f;
            else if (udzial > 1.0f)
                udzial = 1.0f;

            mag0 += (mag1 - mag0) * udzial;
            roznica_fazy = remainderf(faza1 - faza0, 2.0f * (float)M_PI);
            faza0 += roznica_fazy * udzial;
        }
        /* Gdy zaden sasiad nie nalezy do rezimu celu, zostaje punkt dolny. */
    }

    if (!isfinite(mag0) || !isfinite(faza0))
        return;

    *magdif *= mag0;
    *phdif = remainderf(*phdif - faza0, 2.0f * (float)M_PI);
}

void OSL_CorrectErr(uint32_t fhz, float *magdif, float *phdif)
{
    OSL_CorrectErrWspolny(fhz, magdif, phdif);
}

//---------------------------------------------------------------------------------

// Function to calculate determinant of 3x3 matrix
// Input: 3x3 matrix [[a, b, c],
//                    [m, n, k],
//                    [u, v, w]]
static COMPLEX Determinant3(const COMPLEX a, const COMPLEX b, const COMPLEX c,
                            const COMPLEX m, const COMPLEX n, const COMPLEX k,
                            const COMPLEX u, const COMPLEX v, const COMPLEX w)
{
    return a * n * w + b * k * u + m * v * c - c * n * u - b * m * w - a * k * v;
}

//Cramer's rule implementation (see Wikipedia article)
//Solves three equations with three unknowns
static void CramersRule(const COMPLEX a11, const COMPLEX a12, const COMPLEX a13, const COMPLEX b1,
                        const COMPLEX a21, const COMPLEX a22, const COMPLEX a23, const COMPLEX b2,
                        const COMPLEX a31, const COMPLEX a32, const COMPLEX a33, const COMPLEX b3,
                        COMPLEX *pResult)
{
    COMPLEX div = Determinant3(a11, a12, a13, a21, a22, a23, a31, a32, a33);
    div = _cnonz(div);
    pResult[0] = Determinant3(b1, a12, a13, b2, a22, a23, b3, a32, a33) / div;
    pResult[1] = Determinant3(a11, b1, a13, a21, b2, a23, a31, b3, a33) / div;
    pResult[2] = Determinant3(a11, a12, b1, a21, a22, b2, a31, a32, b3) / div;
}

//Ensure f is nonzero (to be safely used as denominator)
static float _nonz(float f)
{
    if (0.f == f || -0.f == f)
        return 1e-30; //Small, but much larger than __FLT_MIN__ to avoid INF result
    return f;
}

static float complex _cnonz(float complex f)
{
    if (0.f == cabsf(f))
        return 1e-30 + 0.fi; //Small, but much larger than __FLT_MIN__ to avoid INF result
    return f;
}

//Parabolic interpolation
// Let (x1,y1), (x2,y2), and (x3,y3) be the three "nearest" points and (x,y)
// be the "concerned" point, with x2 < x < x3. If (x,y) is to lie on the
// parabola through the three points, you can express y as a quadratic function
// of x in the form:
//    y = a*(x-x2)^2 + b*(x-x2) + y2
// where a and b are:
//    a = ((y3-y2)/(x3-x2)-(y2-y1)/(x2-x1))/(x3-x1)
//    b = ((y3-y2)/(x3-x2)*(x2-x1)+(y2-y1)/(x2-x1)*(x3-x2))/(x3-x1)
float complex OSL_ParabolicInterpolation(float complex y1, float complex y2, float complex y3, //values for frequencies x1, x2, x3
                                         float x1, float x2, float x3,                         //frequencies of respective y values
                                         float x)                                              //Frequency between x2 and x3 where we want to interpolate result
{
    float complex z1 = (y3 - y2) / (x3 - x2);
    float complex z2 = (y2 - y1) / (x2 - x1);
    //float complex a = ((y3-y2)/(x3-x2)-(y2-y1)/(x2-x1))/(x3-x1);// a may be zero (if all points are collinear)
    float complex a = (z1 - z2) / (x3 - x1); // a may be zero (if all points are collinear)
    float complex b = (z1 * (x2 - x1) + z2 * (x3 - x2)) / (x3 - x1);
    // float complex res = a * (x - x2) * (x - x2) + b * (x - x2) + y2;// avoid powf function
    float complex res = (x - x2) * (a * (x - x2) + b) + y2; // avoid the powf function
    return res;
}

int32_t OSL_GetSelected(void)
{
    return (int32_t)CFG_GetParam(CFG_PARAM_OSL_SELECTED);
}

int32_t OSL_IsSelectedValid(void)
{
    if (-1 == OSL_GetSelected() || osl_file_status != OSL_FILE_VALID)
        return 0;
    return 1;
}

const char *OSL_GetSelectedName(void)
{
    static char fn[2];
    if (OSL_GetSelected() < 0 || OSL_GetSelected() >= MAX_OSLFILES)
        return "None";
    fn[0] = (char)(OSL_GetSelected() + (int32_t)'A');
    fn[1] = '\0';
    return fn;
}

bool OSL_WybierzProfilBezpiecznie(int32_t index)
{
    const int32_t poprzedni = OSL_GetSelected();
    const uint32_t nowy = (index >= 0 && index < MAX_OSLFILES) ? (uint32_t)index : ~0U;

    OSL_InicjalizujTablicePamieci();
    CFG_SetParam(CFG_PARAM_OSL_SELECTED, nowy);

    if (!CFG_FlushSprawdzony())
    {
        CFG_SetParam(CFG_PARAM_OSL_SELECTED, poprzedni >= 0 ? (uint32_t)poprzedni : ~0U);
        (void)OSL_LoadFromFile();
        return false;
    }

    /* Brak pliku jest poprawnym stanem dla nowego profilu przeznaczonego do kalibracji. */
    (void)OSL_LoadFromFile();
    return true;
}

bool OSL_UsunProfil(uint32_t index)
{
    char path[64];
    char path_tmp[64];
    char path_bak[64];
    FRESULT wynik;

    if (index >= MAX_OSLFILES || !CFG_CzyKartaSDDostepna())
        return false;

    /* Jeżeli kasowany profil jest aktywny, najpierw bezpiecznie wyłączamy OSL
     * i zapisujemy ten stan w konfiguracji. Dzięki temu błąd zapisu config.bin
     * nie może pozostawić po restarcie wskazania na skasowany plik. */
    if (OSL_GetSelected() == (int32_t)index &&
        !OSL_WybierzProfilBezpiecznie(-1))
        return false;

    snprintf(path, sizeof(path), "%s/%c.osl", g_cfg_osldir, (char)('A' + index));
    snprintf(path_tmp, sizeof(path_tmp), "%s/%c.tmp", g_cfg_osldir, (char)('A' + index));
    snprintf(path_bak, sizeof(path_bak), "%s/%c.bak", g_cfg_osldir, (char)('A' + index));

    (void)f_unlink(path_tmp);
    (void)f_unlink(path_bak);
    wynik = f_unlink(path);
    if (wynik != FR_OK && wynik != FR_NO_FILE)
        return false;

    /* Metadane nie mogą pozostać po pliku profilu, bo sugerowałyby istnienie
     * kalibracji, której współczynniki zostały świadomie usunięte. */
    (void)KAL_META_Usun(KAL_META_OSL, (int32_t)index);
    return true;
}

void OSL_Select(int32_t index)
{
    (void)OSL_WybierzProfilBezpiecznie(index);
}

int32_t OSL_ReloadSelected(void)
{
    return OSL_LoadFromFile();
}

void OSL_PobierzDiagnostyke(OSL_DIAGNOSTYKA_t *diagnostyka)
{
    FILINFO info;
    FRESULT wynik_stat;

    if (diagnostyka == 0)
        return;

    memset(diagnostyka, 0, sizeof(*diagnostyka));
    diagnostyka->wybrany_profil = OSL_GetSelected();
    diagnostyka->zaladowany_profil = osl_file_loaded;
    diagnostyka->wynik_ostatniego_ladowania = osl_last_load_result;
    diagnostyka->rozmiar_oczekiwany = (uint32_t)(sizeof(*osl_data) * OSL_NUM_VALID_ENTRIES);
    diagnostyka->kalibracja_aktywna = OSL_IsSelectedValid() ? 1U : 0U;
    diagnostyka->korekcja_sprzetowa_aktywna = OSL_IsErrCorrLoaded() ? 1U : 0U;
    diagnostyka->korekcja_s21_aktywna = OSL_IsTXCorrLoaded() ? 1U : 0U;

    if (diagnostyka->wybrany_profil >= 0 && diagnostyka->wybrany_profil < MAX_OSLFILES)
    {
        snprintf(diagnostyka->sciezka, sizeof(diagnostyka->sciezka),
                 "%s/%s.osl", g_cfg_osldir, OSL_GetSelectedName());
        memset(&info, 0, sizeof(info));
        wynik_stat = f_stat(diagnostyka->sciezka, &info);
        if (wynik_stat == FR_OK)
        {
            diagnostyka->plik_istnieje = 1U;
            diagnostyka->rozmiar_pliku = (uint32_t)info.fsize;
        }
    }
    else
    {
        snprintf(diagnostyka->sciezka, sizeof(diagnostyka->sciezka), "%s/-.osl", g_cfg_osldir);
    }
}

int32_t OSL_ScanShort(void (*progresscb)(uint32_t))
{
    int i;
    int indeks;
    COMPLEX *wyniki = (COMPLEX *)&WORK_BUFF[0];

    if (OSL_GetSelected() < 0)
        return -1;

    for (i = 0; i < OSL_NUM_VALID_ENTRIES; i++)
    {
        const uint32_t oslCalFreqHz = OSL_GetCalFreqByIdx(i);
        if (oslCalFreqHz == 0)
            break;
        if (i == 0)
            DSP_Measure(oslCalFreqHz, 1, 0, (int)OSL_PobierzLiczbeUsrednienKalibracji());

        if (!OSL_ZmierzGammaPunktu(oslCalFreqHz, OSL_WZORZEC_ZWARCIE, &wyniki[i]))
        {
            GEN_SetMeasurementFreq(0);
            return -4;
        }

        if (progresscb)
            progresscb((i * 100) / OSL_NUM_VALID_ENTRIES);
    }
    GEN_SetMeasurementFreq(0);

    /* Dopiero kompletny skan zastępuje odpowiednia czesc danych profilu. */
    for (indeks = 0; indeks < i; indeks++)
        osl_data[indeks].gshort = wyniki[indeks];

    osl_file_status &= ~OSL_FILE_VALID;
    osl_file_status |= OSL_FILE_SCANNED_SHORT;
    return 0;
}

int32_t OSL_ScanLoad(void (*progresscb)(uint32_t))
{
    int i;
    int indeks;
    COMPLEX *wyniki = (COMPLEX *)&WORK_BUFF[0];

    if (OSL_GetSelected() < 0)
        return -1;

    for (i = 0; i < OSL_NUM_VALID_ENTRIES; i++)
    {
        const uint32_t oslCalFreqHz = OSL_GetCalFreqByIdx(i);
        if (oslCalFreqHz == 0)
            break;
        if (i == 0)
            DSP_Measure(oslCalFreqHz, 1, 0, (int)OSL_PobierzLiczbeUsrednienKalibracji());

        if (!OSL_ZmierzGammaPunktu(oslCalFreqHz, OSL_WZORZEC_OBCIAZENIE, &wyniki[i]))
        {
            GEN_SetMeasurementFreq(0);
            return -4;
        }

        if (progresscb)
            progresscb((i * 100) / OSL_NUM_VALID_ENTRIES);
    }
    GEN_SetMeasurementFreq(0);

    /* Dopiero kompletny skan zastępuje odpowiednia czesc danych profilu. */
    for (indeks = 0; indeks < i; indeks++)
        osl_data[indeks].gload = wyniki[indeks];

    osl_file_status &= ~OSL_FILE_VALID;
    osl_file_status |= OSL_FILE_SCANNED_LOAD;
    return 0;
}

int32_t OSL_ScanOpen(void (*progresscb)(uint32_t))
{
    int i;
    int indeks;
    COMPLEX *wyniki = (COMPLEX *)&WORK_BUFF[0];

    if (OSL_GetSelected() < 0)
        return -1;

    for (i = 0; i < OSL_NUM_VALID_ENTRIES; i++)
    {
        const uint32_t oslCalFreqHz = OSL_GetCalFreqByIdx(i);
        if (oslCalFreqHz == 0)
            break;
        if (i == 0)
            DSP_Measure(oslCalFreqHz, 1, 0, (int)OSL_PobierzLiczbeUsrednienKalibracji());

        if (!OSL_ZmierzGammaPunktu(oslCalFreqHz, OSL_WZORZEC_ROZWARCIE, &wyniki[i]))
        {
            GEN_SetMeasurementFreq(0);
            return -4;
        }

        if (progresscb)
            progresscb((i * 100) / OSL_NUM_VALID_ENTRIES);
    }
    GEN_SetMeasurementFreq(0);

    /* Dopiero kompletny skan zastępuje odpowiednia czesc danych profilu. */
    for (indeks = 0; indeks < i; indeks++)
        osl_data[indeks].gopen = wyniki[indeks];

    osl_file_status &= ~OSL_FILE_VALID;
    osl_file_status |= OSL_FILE_SCANNED_OPEN;
    return 0;
}

static int32_t OSL_StoreFile(void)
{
    TCHAR path[64];
    TCHAR path_tmp[64];
    TCHAR path_bak[64];
    const UINT rozmiar = (UINT)sizeof(osl_data);

    /*
     * .user_sdram jest sekcja NOLOAD, wiec nieuzywany ogon tablicy nie jest
     * zerowany przez kod startowy. Historyczny format zapisuje pelny rozmiar
     * OSL_ENTRIES, dlatego zerujemy tylko rekordy poza aktualnie uzywanym
     * zakresem. Nie dotykamy wspolczynnikow aktywnego profilu.
     */
    if (OSL_NUM_VALID_ENTRIES < OSL_ENTRIES)
        memset(&osl_data[OSL_NUM_VALID_ENTRIES], 0,
               (OSL_ENTRIES - OSL_NUM_VALID_ENTRIES) * sizeof(osl_data[0]));

    if ((-1 == OSL_GetSelected()) || (osl_file_status != OSL_FILE_VALID))
        return -1;
    if (!CFG_CzyKartaSDDostepna())
        return -2;

    snprintf(path, sizeof(path), "%s/%s.osl", g_cfg_osldir, OSL_GetSelectedName());
    snprintf(path_tmp, sizeof(path_tmp), "%s/%s.tmp", g_cfg_osldir, OSL_GetSelectedName());
    snprintf(path_bak, sizeof(path_bak), "%s/%s.bak", g_cfg_osldir, OSL_GetSelectedName());
    f_mkdir(g_cfg_osldir);
    f_unlink(path_tmp);

    if (OSL_ZapiszIZweryfikujTmp(path_tmp, osl_data, rozmiar) != 0)
        return -4;

    /*
     * Stary profil zostaje zachowany do chwili, gdy nowy plik jest w pelni
     * zapisany. Utrata zasilania lub blad SD nie powinny kasowac ostatniej
     * dzialajacej kalibracji.
     */
    f_unlink(path_bak);
    if (f_rename(path, path_bak) != FR_OK)
        f_unlink(path_bak);

    if (f_rename(path_tmp, path) != FR_OK)
    {
        f_unlink(path_tmp);
        (void)f_rename(path_bak, path);
        return -4;
    }

    f_unlink(path_bak);
    return 0;
}

static int32_t OSL_LoadFromFile(void)
{
    FRESULT res;
    FIL fp;
    TCHAR path[64];
    UINT br = 0;
    const UINT oczekiwane = (UINT)(sizeof(*osl_data) * OSL_NUM_VALID_ENTRIES);

    if (-1 == OSL_GetSelected())
    {
        osl_file_status = OSL_FILE_EMPTY;
        osl_file_loaded = -1;
        osl_last_load_result = -1;
        return osl_last_load_result;
    }

    osl_file_loaded = OSL_GetSelected(); // Zapamietaj profil, ktory probowalismy wczytac.

    sprintf(path, "%s/%s.osl", g_cfg_osldir, OSL_GetSelectedName());
    res = f_open(&fp, path, FA_READ | FA_OPEN_EXISTING);
    if (FR_OK != res)
    {
        osl_file_status = OSL_FILE_EMPTY;
        osl_last_load_result = 1; // pliku nie mozna otworzyc
        return osl_last_load_result;
    }

    res = f_read(&fp, osl_data, oczekiwane, &br);
    f_close(&fp);
    if (FR_OK != res)
    {
        osl_file_status = OSL_FILE_EMPTY;
        osl_last_load_result = 2; // blad odczytu
        return osl_last_load_result;
    }
    if (br != oczekiwane)
    {
        osl_file_status = OSL_FILE_EMPTY;
        osl_last_load_result = 3; // plik jest za krotki dla obecnego zakresu
        return osl_last_load_result;
    }

    /*
     * Profil OSL musi nalezec do biezacego planu generatora. Wczesniej
     * metadane byly tylko diagnostyczne i stary profil mogl zostac uzyty po
     * zmianie SI5351_MAX_FREQ. Dla kontroli regresji jest to niedopuszczalne.
     */
    {
        KAL_META_DANE_t meta_osl;
        if (!KAL_META_Pobierz(KAL_META_OSL, OSL_GetSelected(), &meta_osl) ||
            !KAL_META_CzyZgodnaZKonfiguracja(&meta_osl))
        {
            osl_file_status = OSL_FILE_EMPTY;
            osl_last_load_result = 4; /* profil z innego planu/konfiguracji RF */
            return osl_last_load_result;
        }
    }

    osl_file_status = OSL_FILE_VALID;
    osl_last_load_result = 0;
    return 0;
}

//for using other source file, without flush
int32_t OSL_ChangeCurrentOSL(int oslIndex)
{
    if (oslIndex >= 0 && oslIndex < MAX_OSLFILES)
        CFG_SetParam(CFG_PARAM_OSL_SELECTED, (uint32_t)oslIndex);
    else
        CFG_SetParam(CFG_PARAM_OSL_SELECTED, ~0U);
    return OSL_LoadFromFile();
}

//Calculate OSL correction coefficients and write to flash in place of original values
int32_t OSL_Calculate(void)
{
    if (OSL_GetSelected() < 0)
        return -1;

    if ((osl_file_status & OSL_FILE_SCANNED_ALL) != OSL_FILE_SCANNED_ALL)
        return -2;

    float r = CFG_GetOslRloadOhm();
    COMPLEX gammaLoad = (r - OSL_BASE_R0) / (r + OSL_BASE_R0) + 0.0fi;
    r = CFG_GetOslRshortOhm();
    COMPLEX gammaShort = (r - OSL_BASE_R0) / (r + OSL_BASE_R0) + 0.0fi;
    r = CFG_GetOslRopenOhm();
    COMPLEX gammaOpen = (r - OSL_BASE_R0) / (r + OSL_BASE_R0) + 0.0fi;

    //Calculate calibration coefficients from measured reflection coefficients
    int i;
    for (i = 0; i < OSL_NUM_VALID_ENTRIES; i++)
    {
        S_OSLDATA *pd = &osl_data[i];
        COMPLEX result[3]; //[e00, e11, de]
        COMPLEX a12 = gammaShort * pd->gshort;
        COMPLEX a22 = gammaLoad * pd->gload;
        COMPLEX a32 = gammaOpen * pd->gopen;
        COMPLEX a13 = cmminus1 * gammaShort;
        COMPLEX a23 = cmminus1 * gammaLoad;
        COMPLEX a33 = cmminus1 * gammaOpen;
        CramersRule(cmplus1, a12, a13, pd->gshort,
                    cmplus1, a22, a23, pd->gload,
                    cmplus1, a32, a33, pd->gopen,
                    result);

        if (!isfinite(crealf(result[0])) || !isfinite(cimagf(result[0])) ||
            !isfinite(crealf(result[1])) || !isfinite(cimagf(result[1])) ||
            !isfinite(crealf(result[2])) || !isfinite(cimagf(result[2])))
        {
            osl_file_status &= ~OSL_FILE_VALID;
            return -5;
        }

        pd->e00 = result[0];
        pd->e11 = result[1];
        pd->de = result[2];
    }

    //Now store calculated data to file
    osl_file_status = OSL_FILE_VALID;
    osl_file_loaded = OSL_GetSelected();
    int32_t wynik_zapisu = OSL_StoreFile();
    if (wynik_zapisu != 0)
    {
        /*
         * Nie oznaczamy profilu jako aktywnego, jezeli nowy plik nie zostal
         * bezpiecznie zapisany. Dane trzech wzorcow pozostaja w RAM, wiec
         * uzytkownik moze ponowic sam zapis bez ponownego skanowania.
         */
        osl_file_status = OSL_FILE_SCANNED_ALL;
        return wynik_zapisu;
    }
    (void)KAL_META_Zapisz(KAL_META_OSL, OSL_GetSelected());
    return 0;
}

float complex OSL_GFromZ(float complex Z, float Rbase)
{
    float complex Z0 = Rbase + 0.0 * I;
    float complex G = (Z - Z0) / (Z + Z0);
    if (isnan(crealf(G)) || isnan(cimagf(G)))
    {
        return 0.99999999f + 0.0fi;
    }
    return G;
}

float complex OSL_ZFromG(float complex G, float Rbase)
{
    float gr2 = powf(crealf(G), 2);
    float gi2 = powf(cimagf(G), 2);
    float dg = _nonz(powf((1.0f - crealf(G)), 2) + gi2);

    float r = Rbase * (1.0f - gr2 - gi2) / dg;
    if (r < 0.0f) //Sometimes it overshoots a little due to limited calculation accuracy
        r = 0.0f;
    float x = Rbase * 2.0f * cimagf(G) / dg;
    return r + x * I;
}

//Correct measured G (vs OSL_BASE_R0) using selected OSL calibration file
static float complex OSL_CorrectG(uint32_t fhz, float complex gMeasured)
{
    uint32_t fr1;
    float prop;
    if (fhz < CFG_GetParam(CFG_PARAM_BAND_FMIN) || fhz > CFG_GetParam(CFG_PARAM_BAND_FMAX))
    {
        return gMeasured;
    }

    if (OSL_GetSelected() >= 0 && osl_file_loaded != OSL_GetSelected()) //Reload OSL file if needed
        OSL_LoadFromFile();

    if (OSL_GetSelected() < 0 || !OSL_IsSelectedValid())
    {
        return gMeasured;
    }

    int i, k = 0;
    S_OSLDATA oslData;
    COMPLEX gResult;
    i = GetIndexForFreq(fhz);
    if (i < 0 || i >= OSL_NUM_VALID_ENTRIES)
        return gMeasured;
    fr1 = OSL_GetCalFreqByIdx(i);

    /*
     * Nie wolno interpolować współczynników OSL przez zmianę harmonicznej
     * generatora. Dla Si5351 punkt 270,0 MHz może jeszcze należeć do H1,
     * a 270,3 MHz już do H3. Współczynniki toru po obu stronach tej granicy
     * opisują fizycznie inny sposób generacji sygnału i ich mieszanie tworzy
     * sztuczny garb dokładnie tam, gdzie użytkownik oczekuje ciągłości.
     *
     * W pierwszym niepełnym przedziale nowej harmonicznej używamy najbliższego
     * współczynnika należącego już do tej samej harmonicznej. Dalej wracamy do
     * zwykłej interpolacji.
     */
    if (fr1 != fhz && i + 1 < OSL_NUM_VALID_ENTRIES)
    {
        const uint32_t rezim_cel = GEN_RezimPomiarowy(fhz);
        const uint32_t rezim_dol = GEN_RezimPomiarowy(fr1);
        const uint32_t fr2 = OSL_GetCalFreqByIdx(i + 1);
        const uint32_t rezim_gora = GEN_RezimPomiarowy(fr2);

        if (rezim_cel != 0U && (rezim_dol != rezim_cel || rezim_gora != rezim_cel))
        {
            if (rezim_gora == rezim_cel)
                oslData = osl_data[i + 1];
            else
                oslData = osl_data[i];
            k = 1;
        }
    }

    if (k == 0 && fr1 == fhz)
    {
        k = 1; // No interpolation necessary.
        oslData = osl_data[i];
    }
    else if (k == 0)
    {
        if (i == 0)
        { // first interval
            //Corner cases. Linearly interpolate two OSL factors for two nearby records
            //(there is no third point for this interval)
            const uint32_t fr2 = OSL_GetCalFreqByIdx(i + 1);
            if (fr2 > fr1)
            {
                prop = (float)(fhz - fr1) / (float)(fr2 - fr1);
                k = 2;
            }
        }
        else if (i == MID_IDX - 1 || i == MID_IDX || i == MID_IDX + 1)
        {
            /*
             * Granica siatki 100 kHz -> 300 kHz przy 150 MHz. Poprzedni kod
             * przesuwal indeks o 1/2 punkty i stosowal ujemne proporcje, czyli
             * wykonywal ekstrapolacje zamiast interpolacji. To moglo tworzyc
             * sztuczne skoki dokladnie przy 150 MHz. Wystarcza zwykla liniowa
             * interpolacja pomiedzy rzeczywistymi sasiednimi czestotliwosciami.
             */
            const uint32_t fr2 = OSL_GetCalFreqByIdx(i + 1);
            if (fr2 > fr1)
            {
                prop = (float)(fhz - fr1) / (float)(fr2 - fr1);
                k = 2;
            }
        }
        else if (fr1 > CFG_GetParam(CFG_PARAM_BAND_FMAX) - OSL_SCAN_STEP)
        { //Last interval
            prop = (float)(fhz - fr1) / OSL_SCAN_STEP;
            k = 2;
        }
        if (k == 2)
        { // linear interpolation
            oslData.e00 = (osl_data[i + 1].e00 - osl_data[i].e00) * prop + osl_data[i].e00;
            oslData.e11 = (osl_data[i + 1].e11 - osl_data[i].e11) * prop + osl_data[i].e11;
            oslData.de = (osl_data[i + 1].de - osl_data[i].de) * prop + osl_data[i].de;
        }
    }
    if (k == 0)
    { //We have three OSL points near fhz, thusly using parabolic interpolation
        float f1, f2, f3;
        f1 = (float)OSL_GetCalFreqByIdx(i - 1);
        f2 = (float)fr1; // =f[i]                  // f2 <= fhz < f3
        f3 = (float)OSL_GetCalFreqByIdx(i + 1);

        oslData.e00 = OSL_ParabolicInterpolation(osl_data[i - 1].e00, osl_data[i].e00, osl_data[i + 1].e00,
                                                 f1, f2, f3, (float)(fhz));
        oslData.e11 = OSL_ParabolicInterpolation(osl_data[i - 1].e11, osl_data[i].e11, osl_data[i + 1].e11,
                                                 f1, f2, f3, (float)(fhz));
        oslData.de = OSL_ParabolicInterpolation(osl_data[i - 1].de, osl_data[i].de, osl_data[i + 1].de,
                                                f1, f2, f3, (float)(fhz));
    }
    //At this point oslData contains correction structure for given frequency fhz

    gResult = gMeasured * oslData.e11 - oslData.de; //Denominator
    gResult = (gMeasured - oslData.e00) / _cnonz(gResult);
    return gResult;
}

float complex OSL_CorrectZ(uint32_t fhz, float complex zMeasured)
{
    COMPLEX g = OSL_GFromZ(zMeasured, OSL_BASE_R0);
    g = OSL_CorrectG(fhz, g);
    if (crealf(g) > 1.0f)
        g = 1.0f + cimagf(g) * I;
    else if (crealf(g) < -1.0f)
        g = -1.0f + cimagf(g) * I;
    if (cimagf(g) > 1.0f)
        g = crealf(g) + 1.0f * I;
    else if (cimagf(g) < -1.0f)
        g = crealf(g) - 1.0f * I;
    g = OSL_ZFromG(g, OSL_BASE_R0);
    return g;
}

//Convert G to magnitude (-1 .. 1) and angle in degrees (-180 .. 180)
//returning complex just for convenience, with magnitude in its real part, and angle in imaginary
float complex OSL_GtoMA(float complex G)
{
    float mag = cabsf(G);
    if (0.f == fabs(mag)) //Handle special case where atan2 is undefined
        return 0.f + 0.fi;
    float phaseDegrees = cargf(G) * 180. / M_PI;
    return mag + phaseDegrees * I;
}

//===========================================================
//KD8CEC'S CALIBRATION
//for S21 Gain, LC Meter
//-----------------------------------------------------------

//===========================================================
//Network Analyzer (S21 Gain)Calibration
//KD8CEC
//-----------------------------------------------------------
#define S21_LICZBA_POMIAROW_SERII 5
#define S21_LICZBA_PROB_SERII 3
#define S21_MAKS_LUKA_INTERPOLACJI 5U
#define S21_MAKS_LUKA_BRZEGOWEJ 2U
#define S21_JAKOSC_MAGIC 0x51313253UL /* "S21Q" w zapisie little-endian */
#define S21_JAKOSC_WERSJA 2U

typedef struct
{
    float poziom;
    float napiecie_v_mv;
    float napiecie_i_mv;
    float rozrzut_i_proc;
    uint8_t pewnosc_proc;
} S21_POMIAR_PUNKTU_t;

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t wersja;
    uint16_t rozmiar_rekordu;
    uint32_t liczba_punktow;
    uint32_t fmin_hz;
    uint32_t fmax_hz;
    uint32_t tlumik_db_x100;
} S21_NAGLOWEK_JAKOSCI_t;

typedef struct __attribute__((packed))
{
    uint8_t pewnosc_proc;
    uint8_t status;
} S21_REKORD_JAKOSCI_t;

typedef struct __attribute__((packed))
{
    S21_NAGLOWEK_JAKOSCI_t naglowek;
    S21_REKORD_JAKOSCI_t rekordy[OSL_ENTRIES];
} S21_PLIK_JAKOSCI_t;

static S21_PLIK_JAKOSCI_t s21_plik_jakosci MEMATTR_OSL;
static OSL_S21_RAPORT_t s21_raport;

static uint8_t S21_MinU8(uint8_t a, uint8_t b)
{
    return a < b ? a : b;
}

/*
 * Długa luka nie może mieć takiej samej wagi jak rzeczywisty pomiar.
 * Zachowujemy więc ciągłość charakterystyki, lecz pewność ograniczamy do
 * kilkunastu procent i dodatkowo obniżamy ją w środku długiego odcinka.
 */
static uint8_t S21_JakoscSzacowania(uint8_t q_sasiadow, uint32_t dlugosc,
                                    uint32_t odleglosc_od_krawedzi)
{
    uint32_t q = (uint32_t)q_sasiadow / 4U;
    uint32_t kara = 0U;

    if (q > 15U)
        q = 15U;
    if (q < 4U)
        q = 4U;

    if (dlugosc > S21_MAKS_LUKA_INTERPOLACJI)
    {
        const uint32_t polowa = dlugosc / 2U + 1U;
        kara = (odleglosc_od_krawedzi * 8U) / polowa;
        if (kara > 10U)
            kara = 10U;
    }

    if (q > kara + 2U)
        q -= kara;
    else
        q = 2U;

    return (uint8_t)q;
}

static uint8_t S21_JakoscRozrzutu(float rozrzut_proc)
{
    if (!isfinite(rozrzut_proc) || rozrzut_proc < 0.0f)
        return 0U;
    if (rozrzut_proc <= 2.0f)
        return 100U;
    if (rozrzut_proc <= 5.0f)
        return 92U;
    if (rozrzut_proc <= 10.0f)
        return 80U;
    if (rozrzut_proc <= 20.0f)
        return 62U;
    if (rozrzut_proc <= 35.0f)
        return 40U;
    if (rozrzut_proc <= 60.0f)
        return 20U;
    return 8U;
}

static uint8_t S21_JakoscSeparacji(float separacja_log_db)
{
    if (!isfinite(separacja_log_db) || separacja_log_db <= 0.05f)
        return 0U;
    if (separacja_log_db >= 8.0f)
        return 100U;
    if (separacja_log_db >= 4.0f)
        return 92U;
    if (separacja_log_db >= 2.0f)
        return 78U;
    if (separacja_log_db >= 1.0f)
        return 62U;
    if (separacja_log_db >= 0.5f)
        return 45U;
    if (separacja_log_db >= 0.2f)
        return 28U;
    return 15U;
}

static uint8_t S21_JakoscOdstepuOdTla(float odstep_log_db)
{
    if (!isfinite(odstep_log_db) || odstep_log_db <= 0.5f)
        return 0U;
    if (odstep_log_db >= 8.0f)
        return 100U;
    if (odstep_log_db >= 5.0f)
        return 88U;
    if (odstep_log_db >= 3.0f)
        return 68U;
    if (odstep_log_db >= 1.5f)
        return 42U;
    return 22U;
}

static void S21_WyczyscJakoscRobocza(void)
{
    memset(s21_jakosc_krok1, 0, sizeof(s21_jakosc_krok1));
    memset(s21_status_krok1, 0, sizeof(s21_status_krok1));
    memset(s21_jakosc_koncowa, 0, sizeof(s21_jakosc_koncowa));
    memset(s21_status_koncowy, 0, sizeof(s21_status_koncowy));
    memset(&s21_raport, 0, sizeof(s21_raport));
    s21_jakosc_dostepna = 0U;
}

static int S21_WczytajJakoscZPliku(void)
{
    FIL plik = {0};
    TCHAR path[64];
    UINT odczytano = 0U;
    const uint32_t liczba = OSL_NUM_VALID_ENTRIES;
    const UINT rozmiar = (UINT)(sizeof(S21_NAGLOWEK_JAKOSCI_t) +
                                liczba * sizeof(S21_REKORD_JAKOSCI_t));
    uint32_t i;

    memset(s21_jakosc_koncowa, 0, sizeof(s21_jakosc_koncowa));
    memset(s21_status_koncowy, 0, sizeof(s21_status_koncowy));
    s21_jakosc_dostepna = 0U;

    snprintf(path, sizeof(path), "%s/txcorr.qly", g_cfg_osldir);
    if (f_open(&plik, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return 0;

    if (f_read(&plik, &s21_plik_jakosci, rozmiar, &odczytano) != FR_OK ||
        odczytano != rozmiar)
    {
        (void)f_close(&plik);
        return 0;
    }
    (void)f_close(&plik);

    if (s21_plik_jakosci.naglowek.magic != S21_JAKOSC_MAGIC ||
        s21_plik_jakosci.naglowek.wersja != S21_JAKOSC_WERSJA ||
        s21_plik_jakosci.naglowek.rozmiar_rekordu != sizeof(S21_REKORD_JAKOSCI_t) ||
        s21_plik_jakosci.naglowek.liczba_punktow != liczba ||
        s21_plik_jakosci.naglowek.fmin_hz != CFG_GetParam(CFG_PARAM_BAND_FMIN) ||
        s21_plik_jakosci.naglowek.fmax_hz != CFG_GetParam(CFG_PARAM_BAND_FMAX) ||
        s21_plik_jakosci.naglowek.tlumik_db_x100 != CFG_GetS21TlumikDbX100())
        return 0;

    for (i = 0U; i < liczba; ++i)
    {
        const uint8_t status = s21_plik_jakosci.rekordy[i].status;
        const uint8_t pewnosc = s21_plik_jakosci.rekordy[i].pewnosc_proc;
        if (status < OSL_S21_JAKOSC_DOBRY || status > OSL_S21_JAKOSC_SZACOWANY ||
            pewnosc == 0U || pewnosc > 100U)
            return 0;
        s21_jakosc_koncowa[i] = pewnosc;
        s21_status_koncowy[i] = status;
    }

    s21_jakosc_dostepna = 1U;
    return 1;
}

static int S21_ZapiszJakoscDoPliku(void)
{
    TCHAR path[64];
    TCHAR path_tmp[64];
    const uint32_t liczba = OSL_NUM_VALID_ENTRIES;
    const UINT rozmiar = (UINT)(sizeof(S21_NAGLOWEK_JAKOSCI_t) +
                                liczba * sizeof(S21_REKORD_JAKOSCI_t));
    uint32_t i;

    if (!CFG_CzyKartaSDDostepna() || !s21_jakosc_dostepna)
        return 0;

    memset(&s21_plik_jakosci.naglowek, 0, sizeof(s21_plik_jakosci.naglowek));
    s21_plik_jakosci.naglowek.magic = S21_JAKOSC_MAGIC;
    s21_plik_jakosci.naglowek.wersja = S21_JAKOSC_WERSJA;
    s21_plik_jakosci.naglowek.rozmiar_rekordu = sizeof(S21_REKORD_JAKOSCI_t);
    s21_plik_jakosci.naglowek.liczba_punktow = liczba;
    s21_plik_jakosci.naglowek.fmin_hz = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    s21_plik_jakosci.naglowek.fmax_hz = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    s21_plik_jakosci.naglowek.tlumik_db_x100 = CFG_GetS21TlumikDbX100();

    for (i = 0U; i < liczba; ++i)
    {
        s21_plik_jakosci.rekordy[i].pewnosc_proc = s21_jakosc_koncowa[i];
        s21_plik_jakosci.rekordy[i].status = s21_status_koncowy[i];
    }

    snprintf(path, sizeof(path), "%s/txcorr.qly", g_cfg_osldir);
    snprintf(path_tmp, sizeof(path_tmp), "%s/txcorr.qlt", g_cfg_osldir);
    (void)f_unlink(path_tmp);

    if (OSL_ZapiszIZweryfikujTmp(path_tmp, &s21_plik_jakosci, rozmiar) != 0)
    {
        (void)f_unlink(path_tmp);
        (void)f_unlink(path); /* stara jakość nie może opisywać nowej kalibracji */
        return 0;
    }

    (void)f_unlink(path);
    if (f_rename(path_tmp, path) != FR_OK)
    {
        (void)f_unlink(path_tmp);
        return 0;
    }
    return 1;
}

int32_t OSL_IsTXCorrLoaded(void)
{
    return (osl_tx_loaded == 1) && (WorkBuffMode == 2);
}

void OSL_LoadTXCorr(void)
{
    FRESULT res;
    FIL fp;
    TCHAR path[64];

    OSL_InicjalizujTablicePamieci();
    osl_tx_loaded = 0;
    WorkBuffMode = 0;
    memset(s21_jakosc_koncowa, 0, sizeof(s21_jakosc_koncowa));
    memset(s21_status_koncowy, 0, sizeof(s21_status_koncowy));
    s21_jakosc_dostepna = 0U;

    osl_txCorr = (OSL_ERRCORR *)&WORK_BUFF[0]; // DH1AKF
    WORK_Ptr = &WORK_BUFF[0];

    sprintf(path, "%s/txcorr.osl", g_cfg_osldir);
    res = f_open(&fp, path, FA_READ | FA_OPEN_EXISTING);
    if (FR_OK != res)
        return;
    UINT br = 0;
    res = f_read(&fp, osl_txCorr, sizeof(OSL_ERRCORR) * OSL_NUM_VALID_ENTRIES, &br);
    f_close(&fp);
    if (FR_OK != res || (sizeof(OSL_ERRCORR) * OSL_NUM_VALID_ENTRIES != br))
        return;

    /* Sam prawidłowy rozmiar pliku nie oznacza jeszcze poprawnej kalibracji. */
    {
        uint32_t i;
        for (i = 0; i < OSL_NUM_VALID_ENTRIES; i++)
        {
            if (!isfinite(osl_txCorr[i].val0) || !isfinite(osl_txCorr[i].valAtt) ||
                osl_txCorr[i].val0 <= 0.0000001f || osl_txCorr[i].valAtt <= 0.0000001f ||
                osl_txCorr[i].valAtt >= osl_txCorr[i].val0)
            {
                osl_tx_loaded = 0;
                WorkBuffMode = 0;
                return;
            }
        }
    }
    osl_tx_loaded = 1;
    WorkBuffMode = 2;
    (void)S21_WczytajJakoscZPliku();
}

static OSL_S21_DIAGNOSTYKA_t s21_diagnostyka = {
    OSL_S21_BLAD_BRAK, 0U, 0.0f, 0.0f, NAN, NAN, NAN, NAN, 0U, OSL_S21_JAKOSC_BRAK
};

static void S21_WyczyscDiagnostyke(void)
{
    s21_diagnostyka.kod = OSL_S21_BLAD_BRAK;
    s21_diagnostyka.czestotliwosc_hz = 0U;
    s21_diagnostyka.poziom_bez_tlumika = 0.0f;
    s21_diagnostyka.poziom_z_tlumikiem = 0.0f;
    s21_diagnostyka.napiecie_v_mv = NAN;
    s21_diagnostyka.napiecie_i_mv = NAN;
    s21_diagnostyka.rozrzut_i_proc = NAN;
    s21_diagnostyka.tlo_i_mv = NAN;
    s21_diagnostyka.pewnosc_proc = 0U;
    s21_diagnostyka.status_jakosci = OSL_S21_JAKOSC_BRAK;
}

static void S21_UstawDiagnostykePelna(OSL_S21_BLAD_t kod, uint32_t czestotliwosc_hz,
                                      float poziom_bez_tlumika, float poziom_z_tlumikiem,
                                      const S21_POMIAR_PUNKTU_t *pomiar, float tlo_i_mv,
                                      uint8_t pewnosc_proc,
                                      OSL_S21_STATUS_JAKOSCI_t status_jakosci)
{
    s21_diagnostyka.kod = kod;
    s21_diagnostyka.czestotliwosc_hz = czestotliwosc_hz;
    s21_diagnostyka.poziom_bez_tlumika = poziom_bez_tlumika;
    s21_diagnostyka.poziom_z_tlumikiem = poziom_z_tlumikiem;
    s21_diagnostyka.tlo_i_mv = tlo_i_mv;
    s21_diagnostyka.pewnosc_proc = pewnosc_proc;
    s21_diagnostyka.status_jakosci = status_jakosci;

    if (pomiar != NULL)
    {
        s21_diagnostyka.napiecie_v_mv = pomiar->napiecie_v_mv;
        s21_diagnostyka.napiecie_i_mv = pomiar->napiecie_i_mv;
        s21_diagnostyka.rozrzut_i_proc = pomiar->rozrzut_i_proc;
    }
    else
    {
        s21_diagnostyka.napiecie_v_mv = NAN;
        s21_diagnostyka.napiecie_i_mv = NAN;
        s21_diagnostyka.rozrzut_i_proc = NAN;
    }
}

static void S21_UstawDiagnostyke(OSL_S21_BLAD_t kod, uint32_t czestotliwosc_hz,
                                 float poziom_bez_tlumika, float poziom_z_tlumikiem)
{
    S21_UstawDiagnostykePelna(kod, czestotliwosc_hz, poziom_bez_tlumika,
                              poziom_z_tlumikiem, NULL, NAN, 0U,
                              OSL_S21_JAKOSC_BRAK);
}

void OSL_S21_PobierzDiagnostyke(OSL_S21_DIAGNOSTYKA_t *diagnostyka)
{
    if (diagnostyka != NULL)
        *diagnostyka = s21_diagnostyka;
}

void OSL_S21_PobierzRaport(OSL_S21_RAPORT_t *raport)
{
    if (raport != NULL)
        *raport = s21_raport;
}

int OSL_S21_PobierzPewnosc(uint32_t czestotliwosc_hz, uint8_t *pewnosc_proc,
                           OSL_S21_STATUS_JAKOSCI_t *status)
{
    int idx;
    uint8_t q;
    uint8_t st;
    uint32_t fr1;
    uint32_t fr2;

    if (!s21_jakosc_dostepna || pewnosc_proc == NULL || status == NULL)
        return 0;

    idx = GetIndexForFreq(czestotliwosc_hz);
    if (idx < 0 || idx >= (int)OSL_NUM_VALID_ENTRIES)
        return 0;

    q = s21_jakosc_koncowa[idx];
    st = s21_status_koncowy[idx];
    fr1 = OSL_GetCalFreqByIdx(idx);
    fr2 = OSL_GetCalFreqByIdx(idx + 1);

    /* Między dwoma punktami przyjmujemy ostrożnie gorszą z ocen. */
    if (fr2 != 0U && czestotliwosc_hz > fr1 && czestotliwosc_hz < fr2)
    {
        q = S21_MinU8(q, s21_jakosc_koncowa[idx + 1]);
        if (st == OSL_S21_JAKOSC_SZACOWANY ||
            s21_status_koncowy[idx + 1] == OSL_S21_JAKOSC_SZACOWANY)
            st = OSL_S21_JAKOSC_SZACOWANY;
        else if (s21_status_koncowy[idx + 1] == OSL_S21_JAKOSC_INTERPOLOWANY)
            st = OSL_S21_JAKOSC_INTERPOLOWANY;
        else if (st != OSL_S21_JAKOSC_INTERPOLOWANY &&
                 s21_status_koncowy[idx + 1] == OSL_S21_JAKOSC_SLABY)
            st = OSL_S21_JAKOSC_SLABY;
    }

    if (q > 100U || st < OSL_S21_JAKOSC_DOBRY || st > OSL_S21_JAKOSC_SZACOWANY)
        return 0;

    *pewnosc_proc = q;
    *status = (OSL_S21_STATUS_JAKOSCI_t)st;
    return 1;
}

static S21_POMIAR_PUNKTU_t S21_ZmierzPoziom(uint32_t czestotliwosc_hz)
{
    S21_POMIAR_PUNKTU_t najlepszy = {NAN, NAN, NAN, NAN, 0U};
    uint8_t proba;

    /*
     * Pierwszy blok po zmianie częstotliwości służy tylko ustaleniu toru.
     * Nie wchodzi do wyniku. Potem wykonujemy serie po pięć próbek, filtrowane
     * medianą/MAD w DSP. Przy niestabilnym poziomie wybieramy najbardziej
     * powtarzalną z maksymalnie trzech serii zamiast przerywać kalibrację.
     */
    (void)DSP_MeasureTrack(czestotliwosc_hz, 0, 0, 1);
    Sleep(2U);

    for (proba = 0U; proba < S21_LICZBA_PROB_SERII; ++proba)
    {
        const float poziom = DSP_MeasureTrack(czestotliwosc_hz, 0, 0,
                                               S21_LICZBA_POMIAROW_SERII);
        const float rozrzut = DSP_OstatniTrackRozrzutIProc();
        const uint8_t jakosc = S21_JakoscRozrzutu(rozrzut);

        if (DSP_CzyOstatniPomiarTrackPoprawny() && isfinite(poziom) &&
            poziom > 0.0000001f && jakosc > 0U)
        {
            if (!isfinite(najlepszy.poziom) || jakosc > najlepszy.pewnosc_proc)
            {
                najlepszy.poziom = poziom;
                najlepszy.napiecie_v_mv = DSP_OstatniTrackVmv();
                najlepszy.napiecie_i_mv = DSP_OstatniTrackImv();
                najlepszy.rozrzut_i_proc = rozrzut;
                najlepszy.pewnosc_proc = jakosc;
            }

            if (rozrzut <= 10.0f)
                break;
        }
        Sleep(3U);
    }

    return najlepszy;
}

static void S21_PrzywrocPoprzedniaKalibracje(void)
{
    /*
     * Oba kroki kalibracji pracują w WORK_BUFF. Jeżeli wynik całego skanu
     * okaże się niewiarygodny, ponownie ładujemy ostatni poprawnie zapisany
     * plik zamiast pozostawiać częściowo nadpisany bufor.
     */
    OSL_LoadTXCorr();
}

static int S21_PoziomPoprawny(float poziom)
{
    return isfinite(poziom) && poziom > 0.0000001f;
}

/*
 * Nieniszcząca weryfikacja liniowości S21 z niezależnym tłumikiem.
 *
 * Funkcja nie nadpisuje txcorr.osl i nie zmienia współczynników bieżącej
 * kalibracji. Korzysta z zapisanych poziomów THRU (val0) i mierzy dziewięć
 * równomiernie rozłożonych punktów. Dzięki temu drugi lub trzeci tłumik może
 * służyć do sprawdzenia, czy dwupunktowa kalibracja zachowuje liniowość poza
 * wartością wzorca, na którym została wykonana.
 */
int32_t OSL_S21_WeryfikujTlumik(uint32_t tlumik_db_x100,
                                  void (*progresscb)(uint32_t),
                                  OSL_S21_WERYFIKACJA_t *wynik)
{
    enum { S21_WERYFIKACJA_PUNKTY = 9 };
    const float wzorzec_db = (float)tlumik_db_x100 / 100.0f;
    uint32_t suma_jakosci = 0U;
    float suma_db = 0.0f;
    float suma_odchylki = 0.0f;
    float suma_kwadratow = 0.0f;
    float min_db = INFINITY;
    float max_db = -INFINITY;
    float max_abs = 0.0f;
    uint32_t najgorsza_f = 0U;
    uint32_t p;

    if (wynik == NULL)
        return -1;
    memset(wynik, 0, sizeof(*wynik));
    wynik->wzorzec_db_x100 = tlumik_db_x100;
    wynik->liczba_punktow = S21_WERYFIKACJA_PUNKTY;

    if (tlumik_db_x100 < 50U || tlumik_db_x100 > 8000U)
        return -2;
    if (!OSL_IsTXCorrLoaded() || osl_txCorr == NULL)
        return -3;
    if (!GEN_CzyWyjscieDodatkoweObslugiwane())
        return -4;

    if (progresscb != NULL)
        progresscb(0U);

    CLK2_drive = 0;
    for (p = 0U; p < S21_WERYFIKACJA_PUNKTY; ++p)
    {
        const uint32_t idx = (p * (OSL_NUM_VALID_ENTRIES - 1U)) /
                             (S21_WERYFIKACJA_PUNKTY - 1U);
        const uint32_t f_hz = OSL_GetCalFreqByIdx((int32_t)idx);
        const float val0 = osl_txCorr[idx].val0;
        S21_POMIAR_PUNKTU_t pomiar;
        uint8_t q_kal = 100U;
        uint8_t q = 0U;
        OSL_S21_STATUS_JAKOSCI_t st_kal = OSL_S21_JAKOSC_BRAK;

        if (!S21_PoziomPoprawny(val0) || !GEN_CzyCzestotliwoscObslugiwana(f_hz))
        {
            if (progresscb != NULL)
                progresscb(((p + 1U) * 100U) / S21_WERYFIKACJA_PUNKTY);
            continue;
        }

        pomiar = S21_ZmierzPoziom(f_hz);
        if (S21_PoziomPoprawny(pomiar.poziom) && pomiar.poziom < val0 &&
            S21_PoziomPoprawny(osl_txCorr[idx].valAtt) && osl_txCorr[idx].valAtt < val0)
        {
            /*
             * Weryfikujemy dokładnie to, co pokazałby ekran S21 po bieżącej
             * dwupunktowej kalibracji, a nie sam surowy stosunek THRU/DUT.
             * Dzięki temu seria 29/40/60 naprawdę ujawnia nieliniowość
             * pozostającą PO kalibracji.
             */
            const float raw_dut_db = 10.0f * log10f(val0 / pomiar.poziom);
            const float raw_kal_db = 10.0f * log10f(val0 / osl_txCorr[idx].valAtt);
            const float kal_wzorzec_db = CFG_GetS21TlumikDb();
            const float db = (isfinite(raw_kal_db) && raw_kal_db > 0.05f)
                                 ? kal_wzorzec_db * raw_dut_db / raw_kal_db
                                 : NAN;
            const float odchylka = db - wzorzec_db;
            const float abs_odchylka = fabsf(odchylka);

            if (OSL_S21_PobierzPewnosc(f_hz, &q_kal, &st_kal) == 0)
                q_kal = 100U;
            q = S21_MinU8(q_kal, pomiar.pewnosc_proc);

            if (isfinite(db) && isfinite(odchylka) && q > 0U)
            {
                suma_db += db;
                suma_odchylki += odchylka;
                suma_kwadratow += odchylka * odchylka;
                suma_jakosci += q;
                if (db < min_db) min_db = db;
                if (db > max_db) max_db = db;
                if (abs_odchylka > max_abs)
                {
                    max_abs = abs_odchylka;
                    najgorsza_f = f_hz;
                }
                ++wynik->punkty_poprawne;
            }
        }

        if (progresscb != NULL)
            progresscb(((p + 1U) * 100U) / S21_WERYFIKACJA_PUNKTY);
    }

    GEN_SetTXFreq(0U);

    if (wynik->punkty_poprawne < 5U)
        return -5;

    wynik->tlumienie_srednie_db = suma_db / (float)wynik->punkty_poprawne;
    wynik->tlumienie_min_db = min_db;
    wynik->tlumienie_max_db = max_db;
    wynik->odchylka_srednia_db = suma_odchylki / (float)wynik->punkty_poprawne;
    wynik->odchylka_rms_db = sqrtf(suma_kwadratow / (float)wynik->punkty_poprawne);
    wynik->odchylka_max_abs_db = max_abs;
    wynik->czestotliwosc_najgorsza_hz = najgorsza_f;
    wynik->jakosc_proc = (uint8_t)(suma_jakosci / wynik->punkty_poprawne);
    return 0;
}

void OSL_S21_UstawOceneLiniowosci(const OSL_S21_WERYFIKACJA_t *wyniki, uint32_t liczba)
{
    uint32_t i;
    float pierwszy_abs = NAN;
    float ostatni_abs = NAN;

    memset(&s21_ocena_liniowosci, 0, sizeof(s21_ocena_liniowosci));
    if (wyniki == NULL || liczba == 0U)
        return;

    s21_ocena_liniowosci.jakosc_min_proc = 100U;
    for (i = 0U; i < liczba; ++i)
    {
        const float abs_srednia = fabsf(wyniki[i].odchylka_srednia_db);
        if (wyniki[i].punkty_poprawne == 0U || !isfinite(wyniki[i].odchylka_max_abs_db))
            continue;

        if (!isfinite(pierwszy_abs))
            pierwszy_abs = abs_srednia;
        ostatni_abs = abs_srednia;
        ++s21_ocena_liniowosci.liczba_wzorcow;

        if (wyniki[i].jakosc_proc < s21_ocena_liniowosci.jakosc_min_proc)
            s21_ocena_liniowosci.jakosc_min_proc = wyniki[i].jakosc_proc;
        if (wyniki[i].odchylka_rms_db > s21_ocena_liniowosci.odchylka_rms_max_db)
            s21_ocena_liniowosci.odchylka_rms_max_db = wyniki[i].odchylka_rms_db;
        if (wyniki[i].odchylka_max_abs_db > s21_ocena_liniowosci.odchylka_max_abs_db)
        {
            s21_ocena_liniowosci.odchylka_max_abs_db = wyniki[i].odchylka_max_abs_db;
            s21_ocena_liniowosci.wzorzec_najgorszy_db_x100 = wyniki[i].wzorzec_db_x100;
        }
    }

    if (s21_ocena_liniowosci.liczba_wzorcow == 0U)
    {
        memset(&s21_ocena_liniowosci, 0, sizeof(s21_ocena_liniowosci));
        return;
    }

    s21_ocena_liniowosci.wazna = 1U;
    if (s21_ocena_liniowosci.liczba_wzorcow >= 2U &&
        isfinite(pierwszy_abs) && isfinite(ostatni_abs) &&
        ostatni_abs > pierwszy_abs + 1.0f)
        s21_ocena_liniowosci.blad_rosnie_z_tlumieniem = 1U;
}

void OSL_S21_PobierzOceneLiniowosci(OSL_S21_LINIOWOSC_t *wynik)
{
    if (wynik == NULL)
        return;
    *wynik = s21_ocena_liniowosci;
}

/*
 * Opcjonalna korekcja nieliniowości amplitudowej. Profil jest sesyjny i
 * powstaje dopiero po niezależnym teście 29/40/60 dB. Nie zmienia pliku
 * txcorr.osl. Bazowa, częstotliwościowa kalibracja THRU+ATT nadal działa
 * pierwsza; tutaj korygujemy jedynie jej pozostałą nieliniowość poziomu.
 */
int32_t OSL_S21_PrzygotujKorekcjeLiniowosci(const OSL_S21_WERYFIKACJA_t *wyniki, uint32_t liczba)
{
    uint32_t i;
    OSL_S21_KOREKCJA_LINIOWOSCI_t nowa;

    memset(&nowa, 0, sizeof(nowa));
    if (wyniki == NULL || liczba < 2U || liczba > 3U)
        return -1;

    for (i = 0U; i < liczba; ++i)
    {
        const float zm = wyniki[i].tlumienie_srednie_db;
        const float wz = (float)wyniki[i].wzorzec_db_x100 / 100.0f;
        float dx, dy, nachylenie;

        if (wyniki[i].punkty_poprawne < 5U || wyniki[i].jakosc_proc < 45U ||
            !isfinite(zm) || !isfinite(wz) || zm <= 1.0f || wz <= 1.0f)
            return -2;
        if (i > 0U && (zm <= nowa.zmierzone_db[i - 1U] + 1.0f ||
                       wz <= nowa.wzorce_db[i - 1U] + 1.0f))
            return -3;

        dx = i == 0U ? zm : zm - nowa.zmierzone_db[i - 1U];
        dy = i == 0U ? wz : wz - nowa.wzorce_db[i - 1U];
        nachylenie = dy / dx;
        /* Odrzucamy profil, który próbowałby naprawić ewidentnie błędny tor. */
        if (!isfinite(nachylenie) || nachylenie < 0.65f || nachylenie > 1.45f)
            return -4;

        nowa.zmierzone_db[i] = zm;
        nowa.wzorce_db[i] = wz;
    }

    nowa.liczba_wzorcow = (uint8_t)liczba;
    nowa.dostepna = 1U;
    nowa.aktywna = 0U;
    s21_korekcja_liniowosci = nowa;
    return 0;
}

void OSL_S21_UstawKorekcjeLiniowosciAktywna(uint8_t aktywna)
{
    s21_korekcja_liniowosci.aktywna =
        (aktywna != 0U && s21_korekcja_liniowosci.dostepna) ? 1U : 0U;
}

void OSL_S21_WyczyscKorekcjeLiniowosci(void)
{
    memset(&s21_korekcja_liniowosci, 0, sizeof(s21_korekcja_liniowosci));
}

void OSL_S21_PobierzKorekcjeLiniowosci(OSL_S21_KOREKCJA_LINIOWOSCI_t *wynik)
{
    if (wynik != NULL)
        *wynik = s21_korekcja_liniowosci;
}

float OSL_S21_KorygujLiniowoscDb(float tlumienie_db)
{
    uint32_t i;
    float x0 = 0.0f, y0 = 0.0f;

    if (!s21_korekcja_liniowosci.aktywna ||
        !s21_korekcja_liniowosci.dostepna ||
        !isfinite(tlumienie_db) || tlumienie_db <= 0.0f)
        return tlumienie_db;

    for (i = 0U; i < s21_korekcja_liniowosci.liczba_wzorcow; ++i)
    {
        const float x1 = s21_korekcja_liniowosci.zmierzone_db[i];
        const float y1 = s21_korekcja_liniowosci.wzorce_db[i];
        if (tlumienie_db <= x1)
            return y0 + (tlumienie_db - x0) * (y1 - y0) / (x1 - x0);
        x0 = x1;
        y0 = y1;
    }

    /* Powyżej ostatniego wzorca ekstrapolujemy tylko nachyleniem ostatniego
     * odcinka. Ekran jakości nadal ostrzega, że jest to obszar poza wzorcem. */
    if (s21_korekcja_liniowosci.liczba_wzorcow >= 2U)
    {
        const uint32_t n = s21_korekcja_liniowosci.liczba_wzorcow;
        const float xa = s21_korekcja_liniowosci.zmierzone_db[n - 2U];
        const float ya = s21_korekcja_liniowosci.wzorce_db[n - 2U];
        const float xb = s21_korekcja_liniowosci.zmierzone_db[n - 1U];
        const float yb = s21_korekcja_liniowosci.wzorce_db[n - 1U];
        return yb + (tlumienie_db - xb) * (yb - ya) / (xb - xa);
    }

    return tlumienie_db;
}

static uint32_t S21_UzupelnijBrakiVal0(uint32_t *pierwsza_nierozwiazana_hz)
{
    const uint32_t liczba = OSL_NUM_VALID_ENTRIES;
    uint32_t i = 0U;
    uint32_t nierozwiazane = 0U;

    if (pierwsza_nierozwiazana_hz != NULL)
        *pierwsza_nierozwiazana_hz = 0U;

    while (i < liczba)
    {
        uint32_t poczatek;
        uint32_t koniec;
        uint32_t dlugosc;
        int ma_lewy;
        int ma_prawy;

        if (S21_PoziomPoprawny(osl_txCorr[i].val0))
        {
            ++i;
            continue;
        }

        poczatek = i;
        while (i < liczba && !S21_PoziomPoprawny(osl_txCorr[i].val0))
            ++i;
        koniec = i - 1U;
        dlugosc = koniec - poczatek + 1U;
        ma_lewy = poczatek > 0U && S21_PoziomPoprawny(osl_txCorr[poczatek - 1U].val0);
        ma_prawy = i < liczba && S21_PoziomPoprawny(osl_txCorr[i].val0);

        if (ma_lewy && ma_prawy)
        {
            const uint32_t lewy = poczatek - 1U;
            const uint32_t prawy = i;
            const float log_lewy = logf(osl_txCorr[lewy].val0);
            const float log_prawy = logf(osl_txCorr[prawy].val0);
            const uint8_t q_sasiadow = S21_MinU8(s21_jakosc_krok1[lewy],
                                                 s21_jakosc_krok1[prawy]);
            uint32_t j;

            for (j = poczatek; j <= koniec; ++j)
            {
                const float udzial = (float)(j - lewy) / (float)(prawy - lewy);
                uint8_t q;
                OSL_S21_STATUS_JAKOSCI_t status;

                if (dlugosc <= S21_MAKS_LUKA_INTERPOLACJI)
                {
                    q = (uint8_t)((uint16_t)q_sasiadow * 2U / 3U);
                    if (q > 45U)
                        q = 45U;
                    if (q < 15U)
                        q = 15U;
                    status = OSL_S21_JAKOSC_INTERPOLOWANY;
                }
                else
                {
                    const uint32_t od_lewej = j - poczatek + 1U;
                    const uint32_t od_prawej = koniec - j + 1U;
                    const uint32_t od_krawedzi = od_lewej < od_prawej ? od_lewej : od_prawej;
                    q = S21_JakoscSzacowania(q_sasiadow, dlugosc, od_krawedzi);
                    status = OSL_S21_JAKOSC_SZACOWANY;
                }

                osl_txCorr[j].val0 = expf(log_lewy + (log_prawy - log_lewy) * udzial);
                s21_jakosc_krok1[j] = q;
                s21_status_krok1[j] = status;
            }
        }
        else if (ma_lewy || ma_prawy)
        {
            const uint32_t wzorzec = ma_lewy ? (poczatek - 1U) : i;
            const uint8_t q_wzorca = s21_jakosc_krok1[wzorzec];
            uint32_t j;

            for (j = poczatek; j <= koniec; ++j)
            {
                if (dlugosc <= S21_MAKS_LUKA_BRZEGOWEJ)
                {
                    osl_txCorr[j].val0 = osl_txCorr[wzorzec].val0;
                    s21_jakosc_krok1[j] = 18U;
                    s21_status_krok1[j] = OSL_S21_JAKOSC_INTERPOLOWANY;
                }
                else
                {
                    const uint32_t od_krawedzi = ma_lewy ? (j - poczatek + 1U)
                                                         : (koniec - j + 1U);
                    osl_txCorr[j].val0 = osl_txCorr[wzorzec].val0;
                    s21_jakosc_krok1[j] = S21_JakoscSzacowania(q_wzorca, dlugosc,
                                                               od_krawedzi);
                    s21_status_krok1[j] = OSL_S21_JAKOSC_SZACOWANY;
                }
            }
        }
        else
        {
            uint32_t j;
            nierozwiazane += dlugosc;
            if (pierwsza_nierozwiazana_hz != NULL && *pierwsza_nierozwiazana_hz == 0U)
                *pierwsza_nierozwiazana_hz = OSL_GetCalFreqByIdx((int32_t)poczatek);
            for (j = poczatek; j <= koniec; ++j)
            {
                s21_jakosc_krok1[j] = 0U;
                s21_status_krok1[j] = OSL_S21_JAKOSC_BRAK;
            }
        }
    }

    return nierozwiazane;
}

static uint32_t S21_UzupelnijBrakiValAtt(uint32_t *pierwsza_nierozwiazana_hz)
{
    const uint32_t liczba = OSL_NUM_VALID_ENTRIES;
    uint32_t i = 0U;
    uint32_t nierozwiazane = 0U;

    if (pierwsza_nierozwiazana_hz != NULL)
        *pierwsza_nierozwiazana_hz = 0U;

    while (i < liczba)
    {
        uint32_t poczatek;
        uint32_t koniec;
        uint32_t dlugosc;
        int ma_lewy;
        int ma_prawy;

        if (S21_PoziomPoprawny(osl_txCorr[i].valAtt) &&
            osl_txCorr[i].valAtt < osl_txCorr[i].val0)
        {
            ++i;
            continue;
        }

        poczatek = i;
        while (i < liczba &&
               (!S21_PoziomPoprawny(osl_txCorr[i].valAtt) ||
                osl_txCorr[i].valAtt >= osl_txCorr[i].val0))
            ++i;
        koniec = i - 1U;
        dlugosc = koniec - poczatek + 1U;
        ma_lewy = poczatek > 0U && S21_PoziomPoprawny(osl_txCorr[poczatek - 1U].valAtt) &&
                  osl_txCorr[poczatek - 1U].valAtt < osl_txCorr[poczatek - 1U].val0;
        ma_prawy = i < liczba && S21_PoziomPoprawny(osl_txCorr[i].valAtt) &&
                   osl_txCorr[i].valAtt < osl_txCorr[i].val0;

        if (ma_lewy && ma_prawy)
        {
            const uint32_t lewy = poczatek - 1U;
            const uint32_t prawy = i;
            const float log_r_lewy = logf(osl_txCorr[lewy].valAtt / osl_txCorr[lewy].val0);
            const float log_r_prawy = logf(osl_txCorr[prawy].valAtt / osl_txCorr[prawy].val0);
            const uint8_t q_sasiadow = S21_MinU8(s21_jakosc_koncowa[lewy],
                                                 s21_jakosc_koncowa[prawy]);
            uint32_t j;

            for (j = poczatek; j <= koniec; ++j)
            {
                const float udzial = (float)(j - lewy) / (float)(prawy - lewy);
                const float stosunek = expf(log_r_lewy + (log_r_prawy - log_r_lewy) * udzial);
                uint8_t q;
                OSL_S21_STATUS_JAKOSCI_t status;

                if (dlugosc <= S21_MAKS_LUKA_INTERPOLACJI)
                {
                    q = (uint8_t)((uint16_t)q_sasiadow * 2U / 3U);
                    if (q > 42U)
                        q = 42U;
                    if (q < 12U)
                        q = 12U;
                    status = OSL_S21_JAKOSC_INTERPOLOWANY;
                }
                else
                {
                    const uint32_t od_lewej = j - poczatek + 1U;
                    const uint32_t od_prawej = koniec - j + 1U;
                    const uint32_t od_krawedzi = od_lewej < od_prawej ? od_lewej : od_prawej;
                    q = S21_JakoscSzacowania(q_sasiadow, dlugosc, od_krawedzi);
                    status = OSL_S21_JAKOSC_SZACOWANY;
                }

                osl_txCorr[j].valAtt = osl_txCorr[j].val0 * stosunek;
                s21_jakosc_koncowa[j] = S21_MinU8(q, s21_jakosc_krok1[j]);
                if (s21_status_krok1[j] == OSL_S21_JAKOSC_SZACOWANY)
                    status = OSL_S21_JAKOSC_SZACOWANY;
                s21_status_koncowy[j] = status;
            }
        }
        else if (ma_lewy || ma_prawy)
        {
            const uint32_t wzorzec = ma_lewy ? (poczatek - 1U) : i;
            const float stosunek = osl_txCorr[wzorzec].valAtt / osl_txCorr[wzorzec].val0;
            const uint8_t q_wzorca = s21_jakosc_koncowa[wzorzec];
            uint32_t j;

            for (j = poczatek; j <= koniec; ++j)
            {
                if (dlugosc <= S21_MAKS_LUKA_BRZEGOWEJ)
                {
                    osl_txCorr[j].valAtt = osl_txCorr[j].val0 * stosunek;
                    s21_jakosc_koncowa[j] = S21_MinU8(15U, s21_jakosc_krok1[j]);
                    s21_status_koncowy[j] = OSL_S21_JAKOSC_INTERPOLOWANY;
                }
                else
                {
                    const uint32_t od_krawedzi = ma_lewy ? (j - poczatek + 1U)
                                                         : (koniec - j + 1U);
                    uint8_t q = S21_JakoscSzacowania(q_wzorca, dlugosc, od_krawedzi);
                    osl_txCorr[j].valAtt = osl_txCorr[j].val0 * stosunek;
                    q = S21_MinU8(q, s21_jakosc_krok1[j]);
                    if (q == 0U)
                        q = 2U;
                    s21_jakosc_koncowa[j] = q;
                    s21_status_koncowy[j] = OSL_S21_JAKOSC_SZACOWANY;
                }
            }
        }
        else
        {
            uint32_t j;
            nierozwiazane += dlugosc;
            if (pierwsza_nierozwiazana_hz != NULL && *pierwsza_nierozwiazana_hz == 0U)
                *pierwsza_nierozwiazana_hz = OSL_GetCalFreqByIdx((int32_t)poczatek);
            for (j = poczatek; j <= koniec; ++j)
            {
                s21_jakosc_koncowa[j] = 0U;
                s21_status_koncowy[j] = OSL_S21_JAKOSC_BRAK;
            }
        }
    }

    return nierozwiazane;
}

static void S21_AktualizujRaport(float tlumik_wzorcowy_db)
{
    const uint32_t liczba = OSL_NUM_VALID_ENTRIES;
    uint32_t suma = 0U;
    uint32_t punkty_tlumika = 0U;
    float suma_tlumienia_db = 0.0f;
    float suma_odchylki_db = 0.0f;
    float suma_kwadratow_odchylki_db = 0.0f;
    float min_tlumienia_db = INFINITY;
    float max_tlumienia_db = -INFINITY;
    float max_abs_odchylki_db = 0.0f;
    uint32_t i;

    memset(&s21_raport, 0, sizeof(s21_raport));
    s21_raport.liczba_punktow = liczba;

    for (i = 0U; i < liczba; ++i)
    {
        const uint8_t q = s21_jakosc_koncowa[i];
        const uint8_t status = s21_status_koncowy[i];
        if (!S21_PoziomPoprawny(osl_txCorr[i].val0) ||
            !S21_PoziomPoprawny(osl_txCorr[i].valAtt) ||
            osl_txCorr[i].valAtt >= osl_txCorr[i].val0 || q == 0U)
        {
            ++s21_raport.nierozwiazane;
            if (s21_raport.pierwsza_nierozwiazana_hz == 0U)
                s21_raport.pierwsza_nierozwiazana_hz = OSL_GetCalFreqByIdx((int32_t)i);
            continue;
        }

        {
            const float tlumienie_db = 10.0f * log10f(osl_txCorr[i].val0 / osl_txCorr[i].valAtt);
            const float odchylka_db = tlumienie_db - tlumik_wzorcowy_db;
            const float abs_odchylka_db = fabsf(odchylka_db);
            if (isfinite(tlumienie_db) && isfinite(odchylka_db))
            {
                suma_tlumienia_db += tlumienie_db;
                suma_odchylki_db += odchylka_db;
                suma_kwadratow_odchylki_db += odchylka_db * odchylka_db;
                if (tlumienie_db < min_tlumienia_db)
                    min_tlumienia_db = tlumienie_db;
                if (tlumienie_db > max_tlumienia_db)
                    max_tlumienia_db = tlumienie_db;
                if (abs_odchylka_db > max_abs_odchylki_db)
                    max_abs_odchylki_db = abs_odchylka_db;
                ++punkty_tlumika;
            }
        }

        suma += q;
        if (status == OSL_S21_JAKOSC_SZACOWANY)
        {
            ++s21_raport.szacowane;
            if (s21_raport.pierwsza_szacowana_hz == 0U)
                s21_raport.pierwsza_szacowana_hz = OSL_GetCalFreqByIdx((int32_t)i);
            s21_raport.ostatnia_szacowana_hz = OSL_GetCalFreqByIdx((int32_t)i);
        }
        else if (status == OSL_S21_JAKOSC_INTERPOLOWANY)
            ++s21_raport.interpolowane;
        else if (q >= 70U)
            ++s21_raport.dobre;
        else
            ++s21_raport.slabe;
    }

    if (liczba > 0U)
        s21_raport.pewnosc_ogolna_proc = (uint8_t)(suma / liczba);

    s21_raport.punkty_tlumika = punkty_tlumika;
    if (punkty_tlumika > 0U)
    {
        s21_raport.tlumienie_zmierzone_srednie_db =
            suma_tlumienia_db / (float)punkty_tlumika;
        s21_raport.tlumienie_zmierzone_min_db = min_tlumienia_db;
        s21_raport.tlumienie_zmierzone_max_db = max_tlumienia_db;
        s21_raport.odchylka_tlumika_srednia_db =
            suma_odchylki_db / (float)punkty_tlumika;
        s21_raport.odchylka_tlumika_rms_db =
            sqrtf(suma_kwadratow_odchylki_db / (float)punkty_tlumika);
        s21_raport.odchylka_tlumika_max_abs_db = max_abs_odchylki_db;
    }
}

int32_t OSL_ScanTXCorr(void (*progresscb)(uint32_t))
{
    uint32_t i;
    uint32_t pierwsza_nierozwiazana = 0U;

    S21_WyczyscDiagnostyke();
    S21_WyczyscJakoscRobocza();
    OSL_S21_WyczyscKorekcjeLiniowosci();

    if (!GEN_CzyWyjscieDodatkoweObslugiwane())
    {
        S21_UstawDiagnostyke(OSL_S21_BLAD_BRAK_WYJSCIA_TX, 0U, 0.0f, 0.0f);
        GEN_SetTXFreq(0);
        S21_PrzywrocPoprzedniaKalibracje();
        return -5;
    }

    WorkBuffMode = 2;
    osl_tx_loaded = 0;
    osl_txCorr = (OSL_ERRCORR *)&WORK_BUFF[0];
    if (progresscb)
        progresscb(0);

    CLK2_drive = 0; // CLK2: 2 mA

    for (i = 0; i < OSL_NUM_VALID_ENTRIES; i++)
    {
        const uint32_t freq = OSL_GetCalFreqByIdx(i);
        S21_POMIAR_PUNKTU_t pomiar;

        if (freq < CFG_GetParam(CFG_PARAM_BAND_FMIN) ||
            freq > CFG_GetParam(CFG_PARAM_BAND_FMAX) ||
            !GEN_CzyCzestotliwoscObslugiwana(freq))
        {
            S21_UstawDiagnostyke(OSL_S21_BLAD_CZESTOTLIWOSC, freq, 0.0f, 0.0f);
            GEN_SetTXFreq(0);
            S21_PrzywrocPoprzedniaKalibracje();
            return -2;
        }

        pomiar = S21_ZmierzPoziom(freq);
        if (S21_PoziomPoprawny(pomiar.poziom))
        {
            osl_txCorr[i].val0 = pomiar.poziom;
            s21_jakosc_krok1[i] = pomiar.pewnosc_proc;
            s21_status_krok1[i] = pomiar.pewnosc_proc >= 70U
                                      ? OSL_S21_JAKOSC_DOBRY
                                      : OSL_S21_JAKOSC_SLABY;
            S21_UstawDiagnostykePelna(OSL_S21_BLAD_BRAK, freq, pomiar.poziom, NAN,
                                      &pomiar, NAN, pomiar.pewnosc_proc,
                                      (OSL_S21_STATUS_JAKOSCI_t)s21_status_krok1[i]);
        }
        else
        {
            /* Pojedynczy brak sygnału zostawiamy jako lukę i skanujemy dalej. */
            osl_txCorr[i].val0 = NAN;
            s21_jakosc_krok1[i] = 0U;
            s21_status_krok1[i] = OSL_S21_JAKOSC_BRAK;
            S21_UstawDiagnostykePelna(OSL_S21_BLAD_BRAK_SYGNALU, freq, NAN, NAN,
                                      &pomiar, NAN, 0U, OSL_S21_JAKOSC_BRAK);
        }
        osl_txCorr[i].valAtt = NAN;

        if (progresscb)
            progresscb(((i + 1U) * 100U) / OSL_NUM_VALID_ENTRIES);
    }
    GEN_SetTXFreq(0);

    if (S21_UzupelnijBrakiVal0(&pierwsza_nierozwiazana) != 0U)
    {
        S21_UstawDiagnostyke(OSL_S21_BLAD_ZBYT_DUZA_LUKA,
                             pierwsza_nierozwiazana, NAN, NAN);
        S21_PrzywrocPoprzedniaKalibracje();
        return -6;
    }

    return 0;
}

int32_t OSL_ScanTXAttenuator(void (*progresscb)(uint32_t), uint32_t tlumik_db_x100)
{
    uint32_t i;
    uint32_t pierwsza_nierozwiazana = 0U;
    const float tlumik_wzorcowy_db = (float)tlumik_db_x100 / 100.0f;

    S21_WyczyscDiagnostyke();
    memset(s21_jakosc_koncowa, 0, sizeof(s21_jakosc_koncowa));
    memset(s21_status_koncowy, 0, sizeof(s21_status_koncowy));

    if (!GEN_CzyWyjscieDodatkoweObslugiwane())
    {
        S21_UstawDiagnostyke(OSL_S21_BLAD_BRAK_WYJSCIA_TX, 0U, 0.0f, 0.0f);
        GEN_SetTXFreq(0);
        S21_PrzywrocPoprzedniaKalibracje();
        return -5;
    }

    if (progresscb)
        progresscb(0);

    CLK2_drive = 0; // CLK2: 2 mA

    for (i = 0; i < OSL_NUM_VALID_ENTRIES; i++)
    {
        const uint32_t freq = OSL_GetCalFreqByIdx(i);
        const float val0 = osl_txCorr[i].val0;
        S21_POMIAR_PUNKTU_t pomiar;
        float val_att;
        float separacja = NAN;
        float poziom_tla = NAN;
        uint8_t q_sep = 0U;
        uint8_t q = 0U;
        OSL_S21_STATUS_JAKOSCI_t status = OSL_S21_JAKOSC_BRAK;

        if (!S21_PoziomPoprawny(val0))
        {
            S21_UstawDiagnostyke(OSL_S21_BLAD_BRAK_KROKU_1, freq, val0, 0.0f);
            GEN_SetTXFreq(0);
            S21_PrzywrocPoprzedniaKalibracje();
            return -3;
        }

        if (freq < CFG_GetParam(CFG_PARAM_BAND_FMIN) ||
            freq > CFG_GetParam(CFG_PARAM_BAND_FMAX) ||
            !GEN_CzyCzestotliwoscObslugiwana(freq))
        {
            S21_UstawDiagnostyke(OSL_S21_BLAD_CZESTOTLIWOSC, freq, val0, 0.0f);
            GEN_SetTXFreq(0);
            S21_PrzywrocPoprzedniaKalibracje();
            return -2;
        }

        pomiar = S21_ZmierzPoziom(freq);
        val_att = pomiar.poziom;

        if (S21_PoziomPoprawny(val_att) && val_att < val0)
        {
            separacja = 10.0f * log10f(val0 / val_att);
            q_sep = S21_JakoscSeparacji(separacja);
            q = S21_MinU8(s21_jakosc_krok1[i], pomiar.pewnosc_proc);
            q = S21_MinU8(q, q_sep);

        }

        /*
         * Tło mierzymy tylko dla punktów podejrzanych. Obejmuje to także
         * przypadek ze zdjęcia użytkownika, gdy po wstawieniu tłumika poziom
         * nie maleje. Taki punkt nadal nie jest używany bezpośrednio, ale
         * poziom tła trafia do diagnostyki i pomaga odróżnić przesłuch od
         * rzeczywistego sygnału.
         */
        if (S21_PoziomPoprawny(val_att) && (val_att >= val0 || q < 65U))
        {
            float rozrzut_tla = NAN;
            poziom_tla = DSP_MeasureTrackTlo(freq, S21_LICZBA_POMIAROW_SERII,
                                             &rozrzut_tla);
            if (q > 0U && S21_PoziomPoprawny(poziom_tla) && val_att > poziom_tla)
            {
                const float odstep_tla = 10.0f * log10f(val_att / poziom_tla);
                const uint8_t q_tlo = S21_JakoscOdstepuOdTla(odstep_tla);
                q = S21_MinU8(q, q_tlo);
            }
            else if (q > 0U && S21_PoziomPoprawny(poziom_tla))
            {
                q = 0U;
            }
        }

        if (q > 0U && S21_PoziomPoprawny(val_att) && val_att < val0)
        {
            osl_txCorr[i].valAtt = val_att;
            if (s21_status_krok1[i] == OSL_S21_JAKOSC_SZACOWANY)
                status = OSL_S21_JAKOSC_SZACOWANY;
            else if (s21_status_krok1[i] == OSL_S21_JAKOSC_INTERPOLOWANY)
                status = OSL_S21_JAKOSC_INTERPOLOWANY;
            else
                status = q >= 70U ? OSL_S21_JAKOSC_DOBRY : OSL_S21_JAKOSC_SLABY;
            s21_jakosc_koncowa[i] = q;
            s21_status_koncowy[i] = status;
            S21_UstawDiagnostykePelna(OSL_S21_BLAD_BRAK, freq, val0, val_att,
                                      &pomiar, DSP_TrackPoziomNaMv(poziom_tla), q, status);
        }
        else
        {
            /*
             * Punkt jest niewiarygodny, lecz nie zatrzymuje skanu. Po przejściu
             * całego pasma spróbujemy uzupełnić krótką lukę z sąsiednich danych.
             */
            osl_txCorr[i].valAtt = NAN;
            s21_jakosc_koncowa[i] = 0U;
            s21_status_koncowy[i] = OSL_S21_JAKOSC_BRAK;
            S21_UstawDiagnostykePelna(
                S21_PoziomPoprawny(val_att) ? OSL_S21_BLAD_TLUMIK_NIE_TLUMI
                                            : OSL_S21_BLAD_BRAK_SYGNALU,
                freq, val0, val_att, &pomiar, DSP_TrackPoziomNaMv(poziom_tla), 0U, OSL_S21_JAKOSC_BRAK);
        }

        if (progresscb)
            progresscb(((i + 1U) * 100U) / OSL_NUM_VALID_ENTRIES);
    }

    GEN_SetTXFreq(0);
    (void)S21_UzupelnijBrakiValAtt(&pierwsza_nierozwiazana);
    S21_AktualizujRaport(tlumik_wzorcowy_db);

    if (s21_raport.nierozwiazane != 0U)
    {
        S21_UstawDiagnostyke(OSL_S21_BLAD_ZBYT_DUZA_LUKA,
                             s21_raport.pierwsza_nierozwiazana_hz != 0U
                                 ? s21_raport.pierwsza_nierozwiazana_hz
                                 : pierwsza_nierozwiazana,
                             NAN, NAN);
        S21_PrzywrocPoprzedniaKalibracje();
        return -6;
    }

    s21_jakosc_dostepna = 1U;
    return 0;
}

int32_t SaveS21CorrToFile(void)
{
    TCHAR path[64];
    TCHAR path_tmp[64];
    TCHAR path_bak[64];
    const UINT rozmiar = sizeof(OSL_ERRCORR) * OSL_NUM_VALID_ENTRIES;
    uint32_t i;

    GEN_SetTXFreq(0);

    if (!CFG_CzyKartaSDDostepna())
        return -1;

    /* Przed zapisem sprawdzamy kompletność obu kroków. */
    for (i = 0; i < OSL_NUM_VALID_ENTRIES; i++)
    {
        if (!isfinite(osl_txCorr[i].val0) || !isfinite(osl_txCorr[i].valAtt) ||
            osl_txCorr[i].val0 <= 0.0000001f || osl_txCorr[i].valAtt <= 0.0000001f ||
            osl_txCorr[i].valAtt >= osl_txCorr[i].val0)
            return -4;
    }

    snprintf(path, sizeof(path), "%s/txcorr.osl", g_cfg_osldir);
    snprintf(path_tmp, sizeof(path_tmp), "%s/txcorr.tmp", g_cfg_osldir);
    snprintf(path_bak, sizeof(path_bak), "%s/txcorr.bak", g_cfg_osldir);
    f_mkdir(g_cfg_osldir);
    f_unlink(path_tmp);

    if (OSL_ZapiszIZweryfikujTmp(path_tmp, osl_txCorr, rozmiar) != 0)
        return -3;

    /* Atomowa podmiana z możliwością przywrócenia poprzedniego pliku. */
    f_unlink(path_bak);
    if (f_rename(path, path_bak) != FR_OK)
        f_unlink(path_bak);

    if (f_rename(path_tmp, path) != FR_OK)
    {
        f_unlink(path_tmp);
        (void)f_rename(path_bak, path);
        return -3;
    }
    f_unlink(path_bak);

    /*
     * Plik jakości jest metadanymi pomocniczymi. Brak jego zapisu nie może
     * unieważnić poprawnej kalibracji amplitudowej, ale stary plik jakości
     * jest usuwany w S21_ZapiszJakoscDoPliku(), żeby nigdy nie opisywał
     * nowych danych txcorr.osl.
     */
    (void)S21_ZapiszJakoscDoPliku();

    osl_tx_loaded = 1;
    WorkBuffMode = 2;
    (void)KAL_META_Zapisz(KAL_META_S21, -1);
    return 0;
}

//-----------------------------------------------------------------------------
