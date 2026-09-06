/*
 *   KD8CEC
 *   kd8cec@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#ifndef AUDIODSP_H_INCLUDED
#define AUDIODSP_H_INCLUDED

#include <stdint.h>
#include <complex.h>
#include "stm32746g_discovery_audio.h"

#define AUDIO_TRANSFER_NONE 0
#define AUDIO_TRANSFER_HALF 1
#define AUDIO_TRANSFER_COMPLETE 2

extern volatile uint8_t Audio_Status;
extern volatile uint8_t Audio_Play_Status;

/*
 * Opcjonalna kolejka kopii blokow wejscia DMA.
 *
 * Starsze ekrany nadal moga korzystac z Audio_Status. Kolejka jest wlaczana
 * tylko przez funkcje, ktore musza przetrwac chwilowe opoznienia glownej petli,
 * na przyklad zapis dekodera CW na karte SD. Przerwanie kopiuje zakonczona
 * polowe DMA do SDRAM, a kod uzytkowy odbiera gotowe bloki bez wyscigu na
 * pojedynczej fladze HALF/COMPLETE.
 */
int AUDIOIRQ_UstawKolejkeWejscia(uint16_t *bufor_dma,
                                 uint16_t liczba_probek_dma,
                                 uint16_t *bufor_kolejki,
                                 uint16_t liczba_slotow);
void AUDIOIRQ_WylaczKolejkeWejscia(void);
const uint16_t *AUDIOIRQ_PobierzBlokWejscia(void);
void AUDIOIRQ_ZwolnijBlokWejscia(void);
uint16_t AUDIOIRQ_PobierzZaleglosc(void);
uint32_t AUDIOIRQ_PobierzLiczbePrzepelnien(void);
uint32_t AUDIOIRQ_PobierzLiczbeBledow(void);

/* Liczniki diagnostyczne przerwan polowy i konca transferu DMA/SAI. */
uint32_t AUDIOIRQ_PobierzLicznikPolowek(void);
uint32_t AUDIOIRQ_PobierzLicznikCalosci(void);
void AUDIOIRQ_ZerujLicznikiDiagnostyczne(void);

/* Callback functions using HAL Driver. */
void BSP_AUDIO_IN_TransferComplete_CallBack(void);
void BSP_AUDIO_IN_HalfTransfer_CallBack(void);
void BSP_AUDIO_IN_Error_CallBack(void);

#endif /* AUDIODSP_H_INCLUDED */
