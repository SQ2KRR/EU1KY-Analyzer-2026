/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include <string.h>
#include <math.h>
#include <limits.h>
#include <complex.h>
#include "stm32f7xx_hal.h"
#include "stm32746g_discovery.h"
#include "stm32746g_discovery_audio.h"
#include "arm_math.h"

#include "dsp.h"
#include "port_extension.h"
#include "gen.h"
#include "oslfile.h"
#include "config.h"
#include "crash.h"
#include "si5351.h"
#include "si5351_hs.h"

//Measuring bridge parameters
//#define Rmeas 5.1f : moved to configuration variable
//#define RmeasAdd 200.0f : moved to configuration variable
//#define Rload 51.0f : moved to configuration variable

#define Rtotal (RmeasAdd + Rmeas + Rload)
#define DSP_Z0 50.0f

//Maximum number of measurements to average
#define MAXNMEAS 20

extern void Sleep(uint32_t);
void GEN_SetTXFreq(uint32_t fhz);

static float Rmeas = 5.1f;
static float RmeasAdd = 200.0f;
static float Rload = 51.0f;

static float mag_v_buf[MAXNMEAS];
static float mag_i_buf[MAXNMEAS];
static float phdif_buf[MAXNMEAS];

float windowfunc[NSAMPLES];
float rfft_input[NSAMPLES];
float rfft_output[NSAMPLES];
const float complex *prfft = (float complex *)rfft_output;
int16_t audioBuf[(NSAMPLES + NDUMMY) * 2];

//Measurement results
static float complex magphase_v = 0.1f + 0.fi; //Measured magnitude and phase for V channel
static float complex magphase_i = 0.1f + 0.fi; //Measured magnitude and phase for I channel
static float magmv_v = 1.;                     //Measured magnitude in millivolts for V channel
static float magmv_i = 1.;                     //Measured magnitude in millivolts for I channel
static float magdif = 1.f;                     //Measured magnitude ratio
static float magdifdb = 0.f;                   //Measured magnitude ratio in dB
static float phdif = 0.f;                      //Measured phase difference in radians
static float phdifdeg = 0.f;                   //Measured phase difference in degrees
static DSP_RX mZ = DSP_Z0 + 0.0fi;
static int ostatni_pomiar_poprawny = 1;
static int ostatni_pomiar_track_poprawny = 0;
static float ostatni_track_poziom = 0.0f;
static float ostatnia_stabilnosc_fazy = 1.0f;
static float ostatni_rozrzut_v_proc = 0.0f;
static float ostatni_rozrzut_i_proc = 0.0f;
static DSP_RX ostatnia_impedancja_przed_osl = DSP_Z0 + 0.0fi;
static DSP_RX ostatnia_impedancja_po_osl_przed_portext = DSP_Z0 + 0.0fi;
static uint8_t ostatni_port_extension_zastosowany = 0U;
static uint32_t ostatni_port_extension_ps = 0U;
static uint32_t ostatnia_czestotliwosc_hz = 0U;
static uint8_t ostatnia_liczba_pomiarow = 0U;
static uint8_t ostatnia_korekcja_hw_zadana = 0U;
static uint8_t ostatnia_korekcja_osl_zadana = 0U;

static float DSP_CalcR(void);
float DSP_CalcX(void);

//Ensure f is nonzero (to be safely used as denominator)
static float _nonz(float f)
{
    if (0.f == f || -0.f == f)
        return 1e-30; //Small, but much larger than __FLT_MIN__ to avoid INF result
    return f;
}

/*
 * Względny rozrzut serii pomiarów, wyrażony w procentach.
 * Używamy odchylenia standardowego z próby (n-1), ponieważ ta wartość ma
 * opisywać powtarzalność kolejnych odczytów, a nie pełną populację.
 * Nie jest to niepewność bezwzględna przyrządu i nie wolno jej tak interpretować.
 */
static float DSP_RozrzutWzglednyProc(const float *dane, int liczba)
{
    int i;
    float srednia = 0.0f;
    float suma_kwadratow = 0.0f;
    float odchylenie;

    if (dane == 0 || liczba < 2)
        return 0.0f;
    if (liczba > MAXNMEAS)
        liczba = MAXNMEAS;

    for (i = 0; i < liczba; i++)
        srednia += dane[i];
    srednia /= (float)liczba;

    if (!isfinite(srednia) || fabsf(srednia) < 1e-20f)
        return NAN;

    for (i = 0; i < liczba; i++)
    {
        const float roznica = dane[i] - srednia;
        suma_kwadratow += roznica * roznica;
    }

    odchylenie = sqrtf(suma_kwadratow / (float)(liczba - 1));
    return 100.0f * odchylenie / fabsf(srednia);
}

/*
//Goertzel_2  +++++++++++++ TEST ++++++++++++++++++

float sine, cosine, selstartGoertzel=0;

void startGoertzel(void){
    int m=107;//10031*(NSAMPLES)/48000+.5;
    sine = sin((2.0 * M_PI * (float) m) / (float) (NSAMPLES));
    cosine = cos((2.0 * M_PI * (float) m) / (float) (NSAMPLES));
}

float goertzel(int NumSamples, float* data) //, float* realresult, float* imagresult)
{
    int     k,i;
    float   q0,q1,q2,magnitude,real,imag;

    float   scalingFactor = NumSamples / 2.0;
    if(selstartGoertzel==0){
        startGoertzel();
    }

    q0=0;
    q1=0;
    q2=0;

    for(i=0; i<NumSamples; i++)
    {
        q0 = 2.0 * cosine * q1 - q2 + data[i];
        q2 = q1;
        q1 = q0;
    }

    // calculate the real and imaginary results
    // scaling appropriately
    real = (q1 * cosine - q2) / scalingFactor;
    imag = (q1 * sine) / scalingFactor;

    magnitude = sqrtf(real*real + imag*imag);
    //realresult=real;
    // *imagresult=imag;
    //phase = atan(imag/real)
    return magnitude;
}

*/

static float complex DSP_FFT(int channel)
{
    float magnitude, phase;
    uint32_t i;
    arm_rfft_fast_instance_f32 S;

    int16_t *pBuf = &audioBuf[NDUMMY + (channel != 0)];
    for (i = 0; i < NSAMPLES; i++)
    {
        rfft_input[i] = (float)*pBuf * windowfunc[i];
        pBuf += 2;
    }

    arm_rfft_fast_init_f32(&S, NSAMPLES);
    arm_rfft_fast_f32(&S, rfft_input, rfft_output, 0);

    //Calculate magnitude value considering +/-2 bins from maximum
    float power = 0.f;
    for (i = FFTBIN - 2; i <= FFTBIN + 2; i++)
    {
        float complex binf = prfft[i];
        float bin_magnitude = cabsf(binf) / (NSAMPLES / 2);
        //power += powf(bin_magnitude, 2);
        power += bin_magnitude * bin_magnitude; // avoid powf()
    }
    magnitude = sqrtf(power);

    //Calculate results
    float re = crealf(prfft[FFTBIN]);
    float im = cimagf(prfft[FFTBIN]);
    phase = atan2f(im, re);
    return magnitude - phase * I; // was magnitude + phase * I;
}

static void DSP_UstawOknoHann(void)
{
    int32_t i;
    const float n_minus_1 = (float)(NSAMPLES - 1U);

    /*
     * V2.1: Hann jest jedynym oknem produkcyjnego toru pomiarowego.
     * Porównanie na tym samym rezystorze i tych samych ustawieniach wykazało
     * mniejszą wrażliwość na przesunięcie częstotliwości pośredniej oraz
     * mniejszy błąd R niż dotychczasowy Exact Blackman, przy tylko nieznacznie
     * większym rozrzucie pojedynczych wyników. Usunięcie wyboru okna zapobiega
     * też przypadkowej zmianie metody pomiarowej między ekranami.
     */
    for (i = 0; i < NSAMPLES; ++i)
    {
        const float faza = 2.0f * (float)M_PI * (float)i / n_minus_1;
        windowfunc[i] = 0.5f - 0.5f * cosf(faza);
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
//Prepare ADC for sampling two channels
void DSP_Init(void)
{
    uint8_t ret;
    uint32_t tmp;

    tmp = CFG_GetParam(CFG_PARAM_BRIDGE_RM);
    Rmeas = *(float *)&tmp;
    tmp = CFG_GetParam(CFG_PARAM_BRIDGE_RADD);
    RmeasAdd = *(float *)&tmp;
    tmp = CFG_GetParam(CFG_PARAM_BRIDGE_RLOAD);
    Rload = *(float *)&tmp;

    OSL_Select(CFG_GetParam(CFG_PARAM_OSL_SELECTED));
    OSL_LoadErrCorr();

    ret = BSP_AUDIO_IN_Init(INPUT_DEVICE_INPUT_LINE_1, 100 - CFG_GetParam(CFG_PARAM_LIN_ATTENUATION), FSAMPLE);
    if (ret != AUDIO_OK)
    {
        CRASH("BSP_AUDIO_IN_Init failed");
    }

    /* Jedna zweryfikowana metoda DSP dla całego firmware. */
    DSP_UstawOknoHann();
}
#pragma GCC diagnostic pop

/*
 * Filtr amplitudy odporny na pojedyncze próbki odstające.
 *
 * Historyczny DSP_FilterArray wybierał zakres ±0,75 sigma wokół średniej
 * i uznawał serię za błędną, gdy w tym zakresie zostało mniej niż połowa
 * próbek. Weryfikacja F180ALL wykazała, że takie kryterium odrzucało poprawne,
 * stabilne serie V/I. Nowy filtr korzysta z mediany i MAD, a następnie liczy
 * średnią z próbek mieszczących się w szerokim progu 3,5 sigma_MAD.
 *
 * Dla N <= 20 prosty insertion sort jest mniejszy i bardziej przewidywalny
 * niż zależność od qsort(). Zwracamy NAN tylko dla rzeczywiście niepoprawnych
 * danych, nigdy 0 jako ukryty sentinel błędu.
 */
static float DSP_Mediana(const float *dane, int liczba)
{
    float kopia[MAXNMEAS];
    int i;
    int j;

    if (dane == NULL || liczba <= 0)
        return NAN;
    if (liczba > MAXNMEAS)
        liczba = MAXNMEAS;

    for (i = 0; i < liczba; ++i)
    {
        float wartosc;
        if (!isfinite(dane[i]))
            return NAN;
        wartosc = dane[i];
        j = i;
        while (j > 0 && kopia[j - 1] > wartosc)
        {
            kopia[j] = kopia[j - 1];
            --j;
        }
        kopia[j] = wartosc;
    }

    if ((liczba & 1) != 0)
        return kopia[liczba / 2];
    return 0.5f * (kopia[(liczba / 2) - 1] + kopia[liczba / 2]);
}

static float DSP_FilterAmplitudeRobust(const float *arr, int nm)
{
    float odchylenia[MAXNMEAS];
    float mediana;
    float mad;
    float prog;
    float suma = 0.0f;
    int liczba = 0;
    int i;

    if (arr == NULL || nm <= 0)
        return NAN;
    if (nm > MAXNMEAS)
        nm = MAXNMEAS;

    if (nm < 5)
    {
        for (i = 0; i < nm; ++i)
        {
            if (!isfinite(arr[i]))
                return NAN;
            suma += arr[i];
        }
        return suma / (float)nm;
    }

    mediana = DSP_Mediana(arr, nm);
    if (!isfinite(mediana))
        return NAN;

    for (i = 0; i < nm; ++i)
        odchylenia[i] = fabsf(arr[i] - mediana);
    mad = DSP_Mediana(odchylenia, nm);
    if (!isfinite(mad))
        return NAN;

    /* 1,4826 * MAD przybliża sigma dla rozkładu normalnego. */
    prog = 3.5f * 1.4826f * mad;
    if (prog < 1.0e-6f)
        prog = fabsf(mediana) * 0.001f + 1.0e-6f;

    for (i = 0; i < nm; ++i)
    {
        if (fabsf(arr[i] - mediana) <= prog)
        {
            suma += arr[i];
            ++liczba;
        }
    }

    if (liczba <= 0)
        return mediana;
    return suma / (float)liczba;
}

/*
 * Średnia kołowa fazy jest jedyną metodą produkcyjną.
 * Zwykła średnia arytmetyczna daje błędny wynik na granicy -pi/+pi:
 * +179 stopni i -179 stopni powinny dać około 180 stopni, a nie 0.
 *
 * Długość średniego wektora 0..1 jest jednocześnie prostą miarą spójności
 * fazy. Wartość bliska 1 oznacza zgodne próbki, wartość bliska 0 - fazę
 * rozproszoną i mało wiarygodną.
 */
static float DSP_FilterPhaseCircular(const float *arr, int nm, int wymagaj_spojnosci)
{
    int i;
    float suma_sin = 0.0f;
    float suma_cos = 0.0f;
    float dlugosc;

    if (nm <= 0)
        return NAN;
    if (nm > MAXNMEAS)
        nm = MAXNMEAS;

    for (i = 0; i < nm; i++)
    {
        if (!isfinite(arr[i]))
            return NAN;
        suma_sin += sinf(arr[i]);
        suma_cos += cosf(arr[i]);
    }

    dlugosc = hypotf(suma_sin, suma_cos) / (float)nm;
    ostatnia_stabilnosc_fazy = dlugosc;

    /*
     * Nie odrzucamy lekkiego szumu fazy. Ponawiamy tylko pomiar, w którym
     * próbki praktycznie nie wskazują wspólnego kierunku fazowego.
     */
    if (wymagaj_spojnosci && nm >= 5 && dlugosc < 0.25f)
        return NAN;

    return atan2f(suma_sin, suma_cos);
}



void DSP_Sample(void)
{
    extern SAI_HandleTypeDef haudio_in_sai;
    HAL_StatusTypeDef res = HAL_SAI_Receive(&haudio_in_sai, (uint8_t *)audioBuf, (NSAMPLES + NDUMMY) * 2, HAL_MAX_DELAY);
    if (HAL_OK != res)
    {
        CRASHF("HAL_SAI_Receive failed, err %d", res);
    }
}

void DSP_Sample64(void)
{
    extern SAI_HandleTypeDef haudio_in_sai;
    HAL_StatusTypeDef res = HAL_SAI_Receive(&haudio_in_sai, (uint8_t *)audioBuf, (2 + 62) * 2, HAL_MAX_DELAY);
    if (HAL_OK != res)
    {
        CRASHF("HAL_SAI_Receive failed, err %d", res);
    }
}

void DSP_Measure2(void)
{
    float mag_v = 0.0f;
    float mag_i = 0.0f;
    float pdif = 0.0f;
    float complex res_v, res_i;
    int i;
    int retries = 3;
    int nMeasurements = CFG_GetParam(CFG_PARAM_MEAS_NSCANS);
REMEASURE:
    for (i = 0; i < nMeasurements; i++)
    {
        DSP_Sample();

        //NB:
        //  If DMA is in use, HAL_SAI_Receive_DMA is to be called instead of HAL_SAI_Receive.
        //  In this case, to provide cache coherence, a call SCB_InvalidateDCache() should be added
        //  to the custom HAL_SAI_RxCpltCallback() implementation !!! (EU1KY)

        res_i = DSP_FFT(0);
        res_v = DSP_FFT(1);

        mag_v_buf[i] = crealf(res_v);
        mag_i_buf[i] = crealf(res_i);
        pdif = cimagf(res_i) - cimagf(res_v);
        //Correct phase difference quadrant
        pdif = fmodf(pdif + M_PI, 2 * M_PI) - M_PI;

        if (pdif < -M_PI)
            pdif += 2 * M_PI;
        else if (pdif > M_PI)
            pdif -= 2 * M_PI;

        phdif_buf[i] = pdif;
    }

    //Now perform filtering to remove outliers with sigma > 1.0
    mag_v = DSP_FilterAmplitudeRobust(mag_v_buf, nMeasurements);
    mag_i = DSP_FilterAmplitudeRobust(mag_i_buf, nMeasurements);
    phdif = DSP_FilterPhaseCircular(phdif_buf, nMeasurements, retries > 0);

    /*
     * Faza rowna 0 rad jest prawidlowym wynikiem pomiaru i nie moze byc
     * traktowana jak blad. Stary kod ponawial taki pomiar bez sprawdzenia,
     * czy licznik prob doszedl juz do zera, co moglo utworzyc petle bez konca.
     */
    if ((!isfinite(mag_v) || !isfinite(mag_i) || !isfinite(phdif) ||
         mag_v <= 0.0f || mag_i <= 0.0f) && retries > 0)
    {
        retries--;
        goto REMEASURE;
    }

    magdif = mag_v / _nonz(mag_i);
    //Calculate derived results
    magmv_v = mag_v * MCF;
    magmv_i = mag_i * MCF;
}

//Set frequency, run measurement sampling and calculate phase, magnitude ratio
//and Z from sampled data, applying hardware error correction and OSL correction
//if requested. Note that clock source remains turned on after the measurement!
static void DSP_MeasureWspolny(uint32_t freqHz, int applyErrCorr, int applyOSL,
                               int nMeasurements)
{
    float mag_v = 0.0f;
    float mag_i = 0.0f;
    float pdif = 0.0f;
    float complex res_v, res_i;
    int i;
    int retries = 3;

    ostatni_pomiar_poprawny = 0;
    ostatnia_stabilnosc_fazy = 1.0f;
    ostatni_rozrzut_v_proc = 0.0f;
    ostatni_rozrzut_i_proc = 0.0f;
    ostatnia_impedancja_przed_osl = NAN + NAN * I;
    ostatnia_impedancja_po_osl_przed_portext = NAN + NAN * I;
    ostatnia_czestotliwosc_hz = freqHz;
    ostatnia_korekcja_hw_zadana = applyErrCorr ? 1U : 0U;
    ostatnia_korekcja_osl_zadana = applyOSL ? 1U : 0U;
    ostatni_port_extension_zastosowany = 0U;
    ostatni_port_extension_ps = 0U;
    assert_param(nMeasurements > 0);
    if (nMeasurements > MAXNMEAS)
        nMeasurements = MAXNMEAS;
    ostatnia_liczba_pomiarow = (uint8_t)nMeasurements;

    {
        const int ustaw_generator = freqHz != 0U;
        if (!ustaw_generator)
            freqHz = GEN_GetLastFreq();

        ostatnia_czestotliwosc_hz = freqHz;
        if (freqHz < CFG_GetParam(CFG_PARAM_BAND_FMIN) ||
            freqHz > CFG_GetParam(CFG_PARAM_BAND_FMAX) ||
            !GEN_CzyCzestotliwoscObslugiwana(freqHz))
        {
            /*
             * Poza rzeczywista mozliwoscia generatora nie zwracamy juz
             * wiarygodnie wygladajacego 50+j0. To byl stan techniczny, ktory
             * mogl zostac pomylony z prawidlowym pomiarem wzorca 50 Ohm.
             */
            magmv_v = 0.0f;
            magmv_i = 0.0f;
            magdif = NAN;
            magdifdb = NAN;
            phdif = NAN;
            phdifdeg = NAN;
            mZ = NAN + NAN * I;
            ostatnia_impedancja_przed_osl = NAN + NAN * I;
            ostatnia_impedancja_po_osl_przed_portext = NAN + NAN * I;
            ostatni_pomiar_poprawny = 0;
            return;
        }

        if (ustaw_generator)
            GEN_SetMeasurementFreq(freqHz);
    }

REMEASURE:
    for (i = 0; i < nMeasurements; i++)
    {
        DSP_Sample();

        res_i = DSP_FFT(0);
        res_v = DSP_FFT(1);

        mag_v_buf[i] = crealf(res_v);
        mag_i_buf[i] = crealf(res_i);
        pdif = cimagf(res_i) - cimagf(res_v);
        pdif = remainderf(pdif, 2.0f * (float)M_PI);
        phdif_buf[i] = pdif;
    }

    mag_v = DSP_FilterAmplitudeRobust(mag_v_buf, nMeasurements);
    mag_i = DSP_FilterAmplitudeRobust(mag_i_buf, nMeasurements);

    phdif = DSP_FilterPhaseCircular(phdif_buf, nMeasurements, retries > 0);

    ostatni_rozrzut_v_proc = DSP_RozrzutWzglednyProc(mag_v_buf, nMeasurements);
    ostatni_rozrzut_i_proc = DSP_RozrzutWzglednyProc(mag_i_buf, nMeasurements);

    if ((!isfinite(mag_v) || !isfinite(mag_i) || !isfinite(phdif) ||
         mag_v <= 0.0f || mag_i <= 0.0f) && retries > 0)
    {
        retries--;
        goto REMEASURE;
    }

    if (!isfinite(mag_v) || !isfinite(mag_i) || !isfinite(phdif) ||
        mag_v <= 0.0f || mag_i <= 0.0f)
    {
        magmv_v = 0.0f;
        magmv_i = 0.0f;
        magdif = 0.0f;
        magdifdb = -120.0f;
        phdif = 0.0f;
        phdifdeg = 0.0f;
        mZ = NAN + NAN * I;
        return;
    }

    magdif = mag_v / _nonz(mag_i);
    if (applyErrCorr)
        OSL_CorrectErr(freqHz, &magdif, &phdif);

    magmv_v = mag_v * MCF;
    magmv_i = mag_i * MCF;

    phdifdeg = (phdif * 180.0f) / (float)M_PI;
    magdifdb = 20.0f * log10f(magdif);
    mZ = DSP_CalcR() + DSP_CalcX() * I;
    ostatnia_impedancja_przed_osl = mZ;

    if (applyOSL)
        mZ = OSL_CorrectZ(freqHz, mZ);

    /*
     * Tu kończy się wspólny, historyczny tor pomiarowy. Kompensację przewodu
     * nakładają jawnie tylko ekrany S11, które informują o niej użytkownika.
     * TDR, L/C, kwarc oraz protokoły PC pozostają przez to bez zmian.
     */
    ostatnia_impedancja_po_osl_przed_portext = mZ;

    ostatni_pomiar_poprawny = isfinite(crealf(mZ)) && isfinite(cimagf(mZ)) &&
                              isfinite(magdif) && isfinite(phdif);
}

void DSP_Measure(uint32_t freqHz, int applyErrCorr, int applyOSL, int nMeasurements)
{
    DSP_MeasureWspolny(freqHz, applyErrCorr, applyOSL, nMeasurements);
}

void DSP_ZastosujKompensacjePortu(uint32_t freqHz)
{
    const uint32_t opoznienie_ps = PORTEXT_PobierzOpoznieniePs();

    ostatni_port_extension_zastosowany = 0U;
    ostatni_port_extension_ps = 0U;

    if (!ostatni_pomiar_poprawny || opoznienie_ps == 0U ||
        !ostatnia_korekcja_osl_zadana)
        return;

    /* Nie wykonujemy ponownego pomiaru. Korygujemy wyłącznie wynik po OSL. */
    mZ = PORTEXT_KorygujImpedancje(ostatnia_impedancja_po_osl_przed_portext,
                                   (float)CFG_GetParam(CFG_PARAM_R0),
                                   freqHz, opoznienie_ps);
    ostatni_pomiar_poprawny = isfinite(crealf(mZ)) && isfinite(cimagf(mZ));
    if (ostatni_pomiar_poprawny)
    {
        ostatni_port_extension_zastosowany = 1U;
        ostatni_port_extension_ps = opoznienie_ps;
    }
}

//Set frequency, run measurement sampling and calculate phase, magnitude ratio
//and Z from sampled data, applying hardware error correction and OSL correction
//if requested. Note that clock source remains turned on after the measurement!
void DSP_Measure_DET(uint32_t freqHz, int applyErrCorr, int applyOSL, int nMeasurements)
{
    float mag_v = 0.0f;
    float mag_i = 0.0f;
    float pdif = 0.0f;
    float complex res_v, res_i;
    int i;
    int retries = 3;

    assert_param(nMeasurements > 0);
    if (nMeasurements > MAXNMEAS)
        nMeasurements = MAXNMEAS;

    {
        const int ustaw_generator = freqHz != 0U;
        if (!ustaw_generator)
            freqHz = GEN_GetLastFreq();

        if (freqHz < CFG_GetParam(CFG_PARAM_BAND_FMIN) ||
            freqHz > CFG_GetParam(CFG_PARAM_BAND_FMAX) ||
            !GEN_CzyCzestotliwoscObslugiwana(freqHz))
        {
            magmv_v = 0.0f;
            magmv_i = 0.0f;
            magdif = NAN;
            magdifdb = NAN;
            phdif = NAN;
            phdifdeg = NAN;
            mZ = NAN + NAN * I;
            return;
        }

        if (ustaw_generator)
            GEN_SetTXFreq(freqHz);
    }

REMEASURE:
    for (i = 0; i < nMeasurements; i++)
    {
        DSP_Sample();

        //NB:
        //  If DMA is in use, HAL_SAI_Receive_DMA is to be called instead of HAL_SAI_Receive.
        //  In this case, to provide cache coherence, a call SCB_InvalidateDCache() should be added
        //  to the custom HAL_SAI_RxCpltCallback() implementation !!! (EU1KY)

        res_i = DSP_FFT(0);
        res_v = DSP_FFT(1);

        mag_v_buf[i] = crealf(res_v);
        mag_i_buf[i] = crealf(res_i);
        pdif = cimagf(res_i) - cimagf(res_v);
        //Correct phase difference quadrant
        pdif = fmodf(pdif + M_PI, 2 * M_PI) - M_PI;

        if (pdif < -M_PI)
            pdif += 2 * M_PI;
        else if (pdif > M_PI)
            pdif -= 2 * M_PI;

        phdif_buf[i] = pdif;
    }

    //Now perform filtering to remove outliers with sigma > 1.0
    mag_v = DSP_FilterAmplitudeRobust(mag_v_buf, nMeasurements);
    mag_i = DSP_FilterAmplitudeRobust(mag_i_buf, nMeasurements);
    phdif = DSP_FilterPhaseCircular(phdif_buf, nMeasurements, retries > 0);

    /*
     * Faza rowna 0 rad jest prawidlowym wynikiem pomiaru i nie moze byc
     * traktowana jak blad. Stary kod ponawial taki pomiar bez sprawdzenia,
     * czy licznik prob doszedl juz do zera, co moglo utworzyc petle bez konca.
     */
    if ((!isfinite(mag_v) || !isfinite(mag_i) || !isfinite(phdif) ||
         mag_v <= 0.0f || mag_i <= 0.0f) && retries > 0)
    {
        retries--;
        goto REMEASURE;
    }

    magdif = mag_v / _nonz(mag_i);
    if (applyErrCorr)
        OSL_CorrectErr(freqHz, &magdif, &phdif);

    //Calculate derived results
    magmv_v = mag_v * MCF;
    magmv_i = mag_i * MCF;

    phdifdeg = (phdif * 180.) / M_PI;
    magdifdb = 20 * log10f(magdif);
    mZ = DSP_CalcR() + DSP_CalcX() * I;

    //Apply OSL correction if needed
    if (applyOSL)
    {
        mZ = OSL_CorrectZ(freqHz, mZ);
    }
}

//KD8CEC  : CLK2 TX
//Set frequency, run measurement sampling and calculate phase, magnitude ratio
//and Z from sampled data, applying hardware error correction and OSL correction
//if requested. Note that clock source remains turned on after the measurement!
float DSP_MeasureTrack(uint32_t freqHz, int applyErrCorr, int applyOSL, int nMeasurements)
{
    float mag_i = 0.0f;
    float complex res_i;
    int i;

    (void)applyErrCorr;
    (void)applyOSL;
    ostatni_pomiar_track_poprawny = 0;
    ostatni_track_poziom = 0.0f;

    /*
     * Tor S21 jest pomiarem skalarnym: pobieramy amplitudę jednego kanału
     * odbiorczego. Nie wyznaczamy fazy S21, więc wynik nie może być
     * przedstawiany jako pełny zespolony parametr rozproszenia.
     */
    if (freqHz < CFG_GetParam(CFG_PARAM_BAND_FMIN) ||
        freqHz > CFG_GetParam(CFG_PARAM_BAND_FMAX) ||
        !GEN_CzyCzestotliwoscObslugiwana(freqHz))
        return NAN;

    if (nMeasurements < 1)
        nMeasurements = 1;
    if (nMeasurements > MAXNMEAS)
        nMeasurements = MAXNMEAS;

    GEN_SetTXFreq(freqHz);
    HS_SetPower(2, CLK2_drive, 1); // CLK2: 2 mA .. 8 mA
    memset(audioBuf, 0, sizeof(audioBuf));

    /*
     * Stary kod wykonywał nMeasurements+1 próbek (<=), a do filtra
     * przekazywał tylko nMeasurements. Dla MAXNMEAS=20 zapisywał też
     * element 20 poza końcem tablicy [0..19]. Mierzymy dokładnie tyle
     * próbek, ile później filtrujemy.
     */
    for (i = 0; i < nMeasurements; i++)
    {
        DSP_Sample();
        res_i = DSP_FFT(0);
        mag_i_buf[i] = crealf(res_i);
    }

    mag_i = DSP_FilterAmplitudeRobust(mag_i_buf, nMeasurements);
    if (!isfinite(mag_i) || mag_i <= 0.0f)
        return NAN;

    ostatni_track_poziom = mag_i;
    ostatni_pomiar_track_poprawny = 1;
    return mag_i;
}

int DSP_CzyOstatniPomiarTrackPoprawny(void)
{
    return ostatni_pomiar_track_poprawny;
}

float DSP_OstatniPoziomTrack(void)
{
    return ostatni_track_poziom;
}

// END OF KD8CEC's LOGIC
//------------------------------------------------------------------

int DSP_CzyOstatniPomiarPoprawny(void)
{
    return ostatni_pomiar_poprawny;
}

float DSP_StabilnoscFazy(void)
{
    return ostatnia_stabilnosc_fazy;
}

void DSP_PobierzDaneMetrologiczne(DSP_DANE_METROLOGICZNE_t *dane)
{
    if (dane == 0)
        return;

    dane->czestotliwosc_hz = ostatnia_czestotliwosc_hz;
    dane->liczba_pomiarow = ostatnia_liczba_pomiarow;
    dane->korekcja_hw_zadana = ostatnia_korekcja_hw_zadana;
    dane->korekcja_osl_zadana = ostatnia_korekcja_osl_zadana;
    dane->harmoniczna_generatora = GEN_WyznaczHarmoniczna(ostatnia_czestotliwosc_hz);
    dane->generator_obslugiwany = dane->harmoniczna_generatora != 0U;
    dane->czestotliwosc_bazowa_hz = GEN_CzestotliwoscBazowa(ostatnia_czestotliwosc_hz);
    dane->poprawny = ostatni_pomiar_poprawny;
    dane->napiecie_v_mv = DSP_MeasuredMagVmv();
    dane->napiecie_i_mv = DSP_MeasuredMagImv();
    dane->stosunek_amplitud = magdif;
    dane->stosunek_db = magdifdb;
    dane->faza_stopnie = phdifdeg;
    dane->spojnosc_fazy = ostatnia_stabilnosc_fazy;
    dane->rozrzut_v_proc = ostatni_rozrzut_v_proc;
    dane->rozrzut_i_proc = ostatni_rozrzut_i_proc;
    dane->impedancja_przed_osl = ostatnia_impedancja_przed_osl;
    dane->impedancja_po_osl_przed_portext = ostatnia_impedancja_po_osl_przed_portext;
    dane->impedancja_po_osl = mZ;
    dane->port_extension_ps = ostatni_port_extension_ps;
    dane->port_extension_aktywna = ostatni_port_extension_zastosowany;
}

//Return last measured Z
DSP_RX DSP_MeasuredZ(void)
{
    return mZ;
}

//Return last measured phase shift
float DSP_MeasuredPhase(void)
{
    return phdif;
}

float DSP_MeasuredPhaseDeg(void)
{
    return phdifdeg;
}

float DSP_MeasuredDiffdB(void)
{
    return magdifdb;
}

float DSP_MeasuredDiff(void)
{
    return magdif;
}

float complex DSP_MeasuredMagPhaseV(void)
{
    return magphase_v;
}

float complex DSP_MeasuredMagPhaseI(void)
{
    return magphase_i;
}

float DSP_MeasuredMagVmv(void)
{
    //from WM8994 spec & from the driver
    uint8_t attvalue = VOLUME_IN_CONVERT(100 - CFG_GetParam(CFG_PARAM_LIN_ATTENUATION));
    float dbatt = (239 - attvalue) * 0.375f;
    float fatt = powf(10.f, dbatt / 20);
    return magmv_v * fatt;
}

//KD8CEC
float DSP_MeasuredMagV(void)
{
    return magmv_v;
}

float DSP_MeasuredMagImv(void)
{
    //from WM8994 spec & from the driver
    uint8_t attvalue = VOLUME_IN_CONVERT(100 - CFG_GetParam(CFG_PARAM_LIN_ATTENUATION));
    float dbatt = (239 - attvalue) * 0.375f;
    float fatt = powf(10.f, dbatt / 20);
    return magmv_i * fatt;
}

static float DSP_CalcR(void)
{
    float RR = (cosf(phdif) * Rtotal * magdif) - (Rmeas + RmeasAdd);
    //NB: It can be negative here! OSL calibration gets rid of the sign.
    return RR;
}

float DSP_CalcX(void)
{
    return sinf(phdif) * Rtotal * magdif;
}

//Calculate VSWR from Z, based on Z0 from configuration
float DSP_CalcVSWR(DSP_RX Z)
{
    float X2 = powf(cimagf(Z), 2);
    float R = crealf(Z);
    if (R < 0.0)
    {
        R = 0.0;
    }
    float ro = sqrtf((powf((R - CFG_GetParam(CFG_PARAM_R0)), 2) + X2) / _nonz(powf((R + CFG_GetParam(CFG_PARAM_R0)), 2) + X2));
    if (ro > .999f)
    {
        ro = 0.999f;
    }
    X2 = (1.0f + ro) / (1.0f - ro);
    return X2;
}

uint32_t DSP_GetIF(void)
{
    static const float binwidth = ((float)(FSAMPLE)) / (NSAMPLES);
    return (uint32_t)(binwidth * FFTBIN); //10031,25 Hz -> 10031 Hz
}
