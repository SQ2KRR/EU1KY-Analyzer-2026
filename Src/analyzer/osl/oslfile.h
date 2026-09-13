#ifndef _OSLFILE_H_
#define _OSLFILE_H_

#include <complex.h>
#include <stdint.h>
#include "config.h"

typedef union
{
    struct
    {
        float mag0;   //Magnitude ratio correction coefficient
        float phase0; //Phase correction value
    };
    struct
    {
        float val0;
        float valAtt;
    };
} OSL_ERRCORR;

//OSL calibration data structure
typedef union
{
    struct
    {
        float complex e00; //e00 correction coefficient
        float complex e11; //e11 correction coefficient
        float complex de;  //delta-e correction coefficient
    };
    struct
    {
        float complex gshort; //measured gamma for short load
        float complex gload;  //measured gamma for 50 Ohm load
        float complex gopen;  //measured gamma for open load
    };
} S_OSLDATA;

typedef enum
{
    OSL_S21_BLAD_BRAK = 0,
    OSL_S21_BLAD_CZESTOTLIWOSC,
    OSL_S21_BLAD_BRAK_WYJSCIA_TX,
    OSL_S21_BLAD_BRAK_SYGNALU,
    OSL_S21_BLAD_BRAK_KROKU_1,
    OSL_S21_BLAD_TLUMIK_NIE_TLUMI,
    OSL_S21_BLAD_ZBYT_DUZA_LUKA
} OSL_S21_BLAD_t;

typedef enum
{
    OSL_S21_JAKOSC_BRAK = 0,
    OSL_S21_JAKOSC_DOBRY = 1,
    OSL_S21_JAKOSC_SLABY = 2,
    OSL_S21_JAKOSC_INTERPOLOWANY = 3,
    /*
     * Punkt oszacowany oznacza dłuższą lukę, dla której nie było
     * wiarygodnego pomiaru. Program zachowuje ciągłość kalibracji, ale
     * wyraźnie obniża pewność zamiast udawać pełnowartościowy pomiar.
     */
    OSL_S21_JAKOSC_SZACOWANY = 4
} OSL_S21_STATUS_JAKOSCI_t;

typedef struct
{
    OSL_S21_BLAD_t kod;
    uint32_t czestotliwosc_hz;
    float poziom_bez_tlumika;
    float poziom_z_tlumikiem;
    float napiecie_v_mv;
    float napiecie_i_mv;
    float rozrzut_i_proc;
    float tlo_i_mv;
    uint8_t pewnosc_proc;
    OSL_S21_STATUS_JAKOSCI_t status_jakosci;
} OSL_S21_DIAGNOSTYKA_t;

typedef struct
{
    uint32_t liczba_punktow;
    uint32_t dobre;
    uint32_t slabe;
    uint32_t interpolowane;
    uint32_t szacowane;
    uint32_t nierozwiazane;
    uint32_t pierwsza_nierozwiazana_hz;
    uint32_t pierwsza_szacowana_hz;
    uint32_t ostatnia_szacowana_hz;
    uint8_t pewnosc_ogolna_proc;

    /*
     * Kontrola liniowości drugiego punktu kalibracji. Wartości są liczone
     * z rzeczywistych par THRU/ATT po zakończeniu skanu, a więc nie opierają
     * się na pojedynczym punkcie diagnostycznym.
     */
    float tlumienie_zmierzone_srednie_db;
    float tlumienie_zmierzone_min_db;
    float tlumienie_zmierzone_max_db;
    float odchylka_tlumika_srednia_db;
    float odchylka_tlumika_rms_db;
    float odchylka_tlumika_max_abs_db;
    uint32_t punkty_tlumika;
} OSL_S21_RAPORT_t;


typedef struct
{
    uint32_t wzorzec_db_x100;
    uint32_t liczba_punktow;
    uint32_t punkty_poprawne;
    uint32_t czestotliwosc_najgorsza_hz;
    uint8_t jakosc_proc;
    float tlumienie_srednie_db;
    float tlumienie_min_db;
    float tlumienie_max_db;
    float odchylka_srednia_db;
    float odchylka_rms_db;
    float odchylka_max_abs_db;
} OSL_S21_WERYFIKACJA_t;

typedef struct
{
    uint8_t wazna;
    uint8_t liczba_wzorcow;
    uint8_t jakosc_min_proc;
    uint8_t blad_rosnie_z_tlumieniem;
    uint32_t wzorzec_najgorszy_db_x100;
    float odchylka_rms_max_db;
    float odchylka_max_abs_db;
} OSL_S21_LINIOWOSC_t;

typedef struct
{
    uint8_t dostepna;
    uint8_t aktywna;
    uint8_t liczba_wzorcow;
    uint8_t zarezerwowane;
    float zmierzone_db[3];
    float wzorce_db[3];
} OSL_S21_KOREKCJA_LINIOWOSCI_t;

typedef struct
{
    int32_t wybrany_profil;
    int32_t zaladowany_profil;
    int32_t wynik_ostatniego_ladowania;
    uint32_t rozmiar_oczekiwany;
    uint32_t rozmiar_pliku;
    uint8_t plik_istnieje;
    uint8_t kalibracja_aktywna;
    uint8_t korekcja_sprzetowa_aktywna;
    uint8_t korekcja_s21_aktywna;
    char sciezka[64];
} OSL_DIAGNOSTYKA_t;

extern OSL_ERRCORR *osl_txCorr;
extern float *WORK_Ptr;

float complex OSL_GFromZ(float complex Z, float Rbase);
float complex OSL_ZFromG(float complex Z, float Rbase);
float complex OSL_CorrectZ(uint32_t fhz, float complex zMeasured);
float complex OSL_GtoMA(float complex G);
float complex OSL_ParabolicInterpolation(float complex y1, float complex y2, float complex y3, //values for frequencies x1, x2, x3
                                         float x1, float x2, float x3,                         //frequencies of respective y values
                                         float x);                                             //Frequency between x2 and x3 where we want to interpolate result

int32_t OSL_GetSelected(void);
const char *OSL_GetSelectedName(void);
void OSL_Select(int32_t index);
bool OSL_WybierzProfilBezpiecznie(int32_t index);
bool OSL_UsunProfil(uint32_t index);
int32_t OSL_IsSelectedValid(void);
int32_t OSL_ReloadSelected(void);
uint32_t OSL_PobierzLiczbeUsrednienKalibracji(void);
void OSL_PobierzDiagnostyke(OSL_DIAGNOSTYKA_t *diagnostyka);

int32_t OSL_ScanOpen(void (*progresscb)(uint32_t));
int32_t OSL_ScanShort(void (*progresscb)(uint32_t));
int32_t OSL_ScanLoad(void (*progresscb)(uint32_t));
int32_t OSL_Calculate(void);
void OSL_LoadErrCorr(void);
int32_t OSL_ScanErrCorr(void (*progresscb)(uint32_t));
void OSL_CorrectErr(uint32_t fhz, float *magdif, float *phdif);
int32_t OSL_IsErrCorrLoaded(void);
int Get_OSL_Entries(void);
uint8_t OSL_CzyTabliceWSdram(void);
//KD8CEC
void OSL_LoadTXCorr(void);
int32_t OSL_ScanTXCorr(void (*progresscb)(uint32_t));
int32_t OSL_ScanTXAttenuator(void (*progresscb)(uint32_t), uint32_t tlumik_db_x100);
void OSL_S21_PobierzDiagnostyke(OSL_S21_DIAGNOSTYKA_t *diagnostyka);
void OSL_S21_PobierzRaport(OSL_S21_RAPORT_t *raport);
int32_t OSL_S21_WeryfikujTlumik(uint32_t tlumik_db_x100,
                                  void (*progresscb)(uint32_t),
                                  OSL_S21_WERYFIKACJA_t *wynik);
void OSL_S21_UstawOceneLiniowosci(const OSL_S21_WERYFIKACJA_t *wyniki, uint32_t liczba);
void OSL_S21_PobierzOceneLiniowosci(OSL_S21_LINIOWOSC_t *wynik);
int32_t OSL_S21_PrzygotujKorekcjeLiniowosci(const OSL_S21_WERYFIKACJA_t *wyniki, uint32_t liczba);
void OSL_S21_UstawKorekcjeLiniowosciAktywna(uint8_t aktywna);
void OSL_S21_WyczyscKorekcjeLiniowosci(void);
void OSL_S21_PobierzKorekcjeLiniowosci(OSL_S21_KOREKCJA_LINIOWOSCI_t *wynik);
float OSL_S21_KorygujLiniowoscDb(float tlumienie_db);
int OSL_S21_PobierzPewnosc(uint32_t czestotliwosc_hz, uint8_t *pewnosc_proc,
                           OSL_S21_STATUS_JAKOSCI_t *status);
float OSL_CorrectTX(float inp, float Corr0dB, float CorrAtt);
int32_t OSL_IsTXCorrLoaded(void);
int OSL_Calc_dBPix(float inp);
extern void progress_cb(uint32_t new_percent);
extern void S21progress_cb(uint32_t new_percent);
int32_t SaveS21CorrToFile(void);
uint32_t OSL_GetCalFreqByIdx(int32_t idx);
int GetIndexForFreq(uint32_t fhz);
void SmoothOSL(void);


#endif //_OSLFILE_H_
