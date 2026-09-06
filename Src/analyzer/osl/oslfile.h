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
int32_t OSL_ScanTXAttenuator(void (*progresscb)(uint32_t));
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
