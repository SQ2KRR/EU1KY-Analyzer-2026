#ifndef _UI_EDYTOR_LICZBY_H_
#define _UI_EDYTOR_LICZBY_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Wspolny edytor liczby sterowany dotykiem i enkoderem.
 *
 * Wartość jest dzielona na pola cyfr podobnie jak w edycji zegara. Dzięki
 * temu częstotliwość, długość albo inny parametr nie wymaga osobnej klawiatury
 * na każdym ekranie.
 *
 * Wariant Ex rozróżnia świadome zapisanie od Anuluj/Wstecz. Jest używany
 * przez zgodnościową funkcję NumKeypad(), aby zachować historyczne API, w
 * którym anulowanie zwraca 0.
 */
bool UI_EdytujLiczbePolamiEx(uint32_t poczatkowa,
                             uint32_t minimum,
                             uint32_t maksimum,
                             const char *tytul,
                             const char *jednostka,
                             uint32_t *wynik);

uint32_t UI_EdytujLiczbePolami(uint32_t poczatkowa,
                               uint32_t minimum,
                               uint32_t maksimum,
                               const char *tytul,
                               const char *jednostka);

/*
 * Wspólny edytor częstotliwości RF. Wartość jest przechowywana w Hz, ale
 * na ekranie zawsze ma ten sam zapis MHz z separatorem dziesiętnym, np.
 * 14,200 MHz albo 25,000000 MHz. Parametr rozdzielczosc_hz określa
 * najmniejszy krok (1, 10, 100, 1000, ... 1000000 Hz).
 *
 * Dzięki temu ustawianie częstotliwości w pomiarze, generatorze,
 * metrologii, profilu sprzętu i diagnostyce wygląda oraz działa tak samo
 * jak edycja daty/czasu: dotyk wybiera pole, enkoder lub +/- zmienia
 * cyfrę, OK przechodzi do kolejnego pola, ostatnie OK zapisuje.
 */
bool UI_EdytujCzestotliwoscHzEx(uint32_t poczatkowa_hz,
                                 uint32_t minimum_hz,
                                 uint32_t maksimum_hz,
                                 uint32_t rozdzielczosc_hz,
                                 const char *tytul,
                                 uint32_t *wynik_hz);

uint32_t UI_EdytujCzestotliwoscHz(uint32_t poczatkowa_hz,
                                  uint32_t minimum_hz,
                                  uint32_t maksimum_hz,
                                  uint32_t rozdzielczosc_hz,
                                  const char *tytul);

/*
 * Edytor dokładnej rezystancji OSL w miliohmach.
 *
 * Przykład: 49873 oznacza 49,873 Ohm. Ekran jest celowo zbudowany tak samo
 * jak współczesna edycja daty/czasu: osobne duże pola, aktywne pole wskazane
 * ramką, enkoder lub +/- zmienia wartość, OK przechodzi dalej, a ostatnie OK
 * zapisuje. Pola to: część całkowita [Ohm], 0,1 Ohm, 0,01 Ohm i 0,001 Ohm.
 */
bool UI_EdytujRezystancjeMiliohmEx(uint32_t poczatkowa_mohm,
                                   uint32_t minimum_mohm,
                                   uint32_t maksimum_mohm,
                                   const char *tytul,
                                   uint32_t *wynik_mohm);

uint32_t UI_EdytujRezystancjeMiliohm(uint32_t poczatkowa_mohm,
                                     uint32_t minimum_mohm,
                                     uint32_t maksimum_mohm,
                                     const char *tytul);

#endif /* _UI_EDYTOR_LICZBY_H_ */
