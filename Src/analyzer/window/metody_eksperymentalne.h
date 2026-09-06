#ifndef _METODY_EKSPERYMENTALNE_H_
#define _METODY_EKSPERYMENTALNE_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    MET_MODEL_RLC_SZEREGOWY = 0,
    MET_MODEL_RLC_ROWNOLEGLY = 1
} MET_MODEL_RLC_t;

typedef enum
{
    MET_OKNO_PROSTOKATNE = 0,
    MET_OKNO_HANN,
    MET_OKNO_HAMMING,
    MET_OKNO_BLACKMAN_HARRIS,
    MET_OKNO_KAISER
} MET_OKNO_t;

typedef struct
{
    float f0_hz;
    float f1_hz;
    float f2_hz;
    float q;
    float poziom_szczytu_db;
    bool poprawny;
} MET_Q_3DB_WYNIK_t;

typedef struct
{
    float f0_hz;
    float szerokosc_3db_hz;
    float q;
    float amplituda;
    float tlo;
    float blad_rms;
    bool poprawny;
} MET_Q_LORENTZ_WYNIK_t;

typedef struct
{
    float r_ohm;
    float l_h;
    float c_f;
    float f0_hz;
    float q;
    float blad_rms_ohm;
    MET_MODEL_RLC_t model;
    bool poprawny;
} MET_RLC_WYNIK_t;

typedef struct
{
    float stan;
    float wariancja;
    float szum_procesu;
    float szum_pomiaru;
    bool zainicjalizowany;
} MET_KALMAN_1D_t;

/* Q z szerokości pasma -3 dB. Wejście jest przebiegiem poziomu w dB. */
bool MET_QMetoda3dB(const float *f_hz, const float *poziom_db, uint32_t liczba,
                    bool rezonans_jako_maksimum, MET_Q_3DB_WYNIK_t *wynik);

/* Dopasowanie krzywej Lorentza do dodatniej amplitudy liniowej. */
bool MET_QDopasujLorentza(const float *f_hz, const float *amplituda, uint32_t liczba,
                           MET_Q_LORENTZ_WYNIK_t *wynik);

/*
 * Dopasowanie zespolonej impedancji do idealnego modelu RLC.
 * Dla modelu równoległego dopasowanie wykonywane jest w admitancji.
 */
bool MET_DopasujRLC(const float *f_hz, const float *r_ohm, const float *x_ohm,
                    const uint8_t *poprawny, uint32_t liczba,
                    MET_MODEL_RLC_t model, MET_RLC_WYNIK_t *wynik);

/* 5-punktowy Savitzky-Golay, wielomian stopnia 2. */
void MET_SavitzkyGolay5(const float *wejscie, float *wyjscie, uint32_t liczba);

/* Współczynnik funkcji okna; beta jest używana tylko przez okno Kaisera. */
float MET_WspolczynnikOkna(MET_OKNO_t okno, uint32_t indeks, uint32_t liczba, float beta);

void MET_Kalman1DInit(MET_KALMAN_1D_t *filtr, float szum_procesu, float szum_pomiaru);
float MET_Kalman1DAktualizuj(MET_KALMAN_1D_t *filtr, float pomiar);

#endif
