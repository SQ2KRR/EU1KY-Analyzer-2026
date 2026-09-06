#ifndef PORT_EXTENSION_H_INCLUDED
#define PORT_EXTENSION_H_INCLUDED

#include <complex.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Kompensacja długości przewodu pomiarowego (port extension).
 *
 * Parametr opoznienie_ps oznacza czas przejścia fali w jedną stronę od
 * płaszczyzny kalibracji do badanego obiektu. Odbicie pokonuje przewód dwa
 * razy, dlatego korekcja fazy współczynnika odbicia ma kąt 4*pi*f*tau.
 *
 * Funkcja nie próbuje kompensować strat kabla ani jego niedopasowania. Jest
 * celowo czystą funkcją matematyczną, dzięki czemu można ją testować na PC i
 * nie mieszać logiki pomiarowej ze sprzętem lub interfejsem.
 */
float complex PORTEXT_KorygujImpedancje(float complex impedancja_ohm,
                                        float z0_ohm,
                                        uint32_t czestotliwosc_hz,
                                        uint32_t opoznienie_ps);

/* Zwraca aktualne opóźnienie zapisane w konfiguracji. 0 oznacza wyłączenie. */
uint32_t PORTEXT_PobierzOpoznieniePs(void);

/* Nakłada konfigurację użytkownika na impedancję po OSL. */
float complex PORTEXT_Zastosuj(uint32_t czestotliwosc_hz,
                               float complex impedancja_ohm,
                               float z0_ohm);

#ifdef __cplusplus
}
#endif

#endif
