/*
 *   KD8CEC
 *   kd8cec@gmail.com
 *
 *   for Audio Callback
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include <string.h>
#include <math.h>
#include <limits.h>
#include <complex.h>
#include "stm32f7xx_hal.h"
#include "stm32746g_discovery.h"
#include "stm32746g_discovery_audio.h"

#include "config.h"
#include "crash.h"

#include "audioirq.h"

volatile uint8_t Audio_Status;
volatile uint8_t Audio_Play_Status;

/*
 * Jednoproducentowa/jednokonsumencka kolejka blokow audio.
 * Glowica jest modyfikowana tylko w przerwaniu, ogon tylko w petli glownej.
 * Jeden slot pozostaje pusty, aby stan pusty i pelny byl jednoznaczny.
 */
static uint16_t *audioirq_bufor_dma;
static uint16_t *audioirq_bufor_kolejki;
static uint16_t audioirq_probki_polowy;
static uint16_t audioirq_liczba_slotow;
static volatile uint16_t audioirq_glowica;
static volatile uint16_t audioirq_ogon;
static volatile uint32_t audioirq_przepelnienia;
static volatile uint32_t audioirq_bledy;
static volatile uint8_t audioirq_kolejka_aktywna;

/* Liczniki diagnostyczne callbackow SAI/DMA. Nie zmieniaja logiki kolejki. */
static volatile uint32_t audioirq_licznik_polowek;
static volatile uint32_t audioirq_licznik_calosci;

static uint16_t AUDIOIRQ_NastepnySlot(uint16_t slot)
{
    slot++;
    if (slot >= audioirq_liczba_slotow)
        slot = 0U;
    return slot;
}

static void AUDIOIRQ_SkopiujPolowe(uint16_t przesuniecie)
{
    uint16_t glowica;
    uint16_t nastepny;
    uint16_t *cel;

    if (!audioirq_kolejka_aktywna || audioirq_bufor_dma == 0 ||
        audioirq_bufor_kolejki == 0 || audioirq_probki_polowy == 0U ||
        audioirq_liczba_slotow < 2U)
        return;

    glowica = audioirq_glowica;
    nastepny = AUDIOIRQ_NastepnySlot(glowica);
    if (nastepny == audioirq_ogon)
    {
        audioirq_przepelnienia++;
        return;
    }

    cel = audioirq_bufor_kolejki + (uint32_t)glowica * audioirq_probki_polowy;
    memcpy(cel,
           audioirq_bufor_dma + przesuniecie,
           (size_t)audioirq_probki_polowy * sizeof(uint16_t));

    /* Dane musza byc widoczne przed opublikowaniem nowej pozycji glowicy. */
    __DMB();
    audioirq_glowica = nastepny;
}

int AUDIOIRQ_UstawKolejkeWejscia(uint16_t *bufor_dma,
                                 uint16_t liczba_probek_dma,
                                 uint16_t *bufor_kolejki,
                                 uint16_t liczba_slotow)
{
    if (bufor_dma == 0 || bufor_kolejki == 0 || liczba_slotow < 2U ||
        liczba_probek_dma < 2U || (liczba_probek_dma & 1U) != 0U)
        return 0;

    /* Najpierw blokujemy callback, dopiero potem podmieniamy caly stan. */
    audioirq_kolejka_aktywna = 0U;
    __DMB();

    audioirq_bufor_dma = bufor_dma;
    audioirq_bufor_kolejki = bufor_kolejki;
    audioirq_probki_polowy = liczba_probek_dma / 2U;
    audioirq_liczba_slotow = liczba_slotow;
    audioirq_glowica = 0U;
    audioirq_ogon = 0U;
    audioirq_przepelnienia = 0U;
    audioirq_bledy = 0U;

    __DMB();
    audioirq_kolejka_aktywna = 1U;
    return 1;
}

void AUDIOIRQ_WylaczKolejkeWejscia(void)
{
    audioirq_kolejka_aktywna = 0U;
    __DMB();
    audioirq_bufor_dma = 0;
    audioirq_bufor_kolejki = 0;
    audioirq_probki_polowy = 0U;
    audioirq_liczba_slotow = 0U;
    audioirq_glowica = 0U;
    audioirq_ogon = 0U;
}

const uint16_t *AUDIOIRQ_PobierzBlokWejscia(void)
{
    uint16_t ogon;

    if (!audioirq_kolejka_aktywna)
        return 0;

    ogon = audioirq_ogon;
    if (ogon == audioirq_glowica)
        return 0;

    __DMB();
    return audioirq_bufor_kolejki + (uint32_t)ogon * audioirq_probki_polowy;
}

void AUDIOIRQ_ZwolnijBlokWejscia(void)
{
    uint16_t ogon;

    if (!audioirq_kolejka_aktywna)
        return;

    ogon = audioirq_ogon;
    if (ogon == audioirq_glowica)
        return;

    __DMB();
    audioirq_ogon = AUDIOIRQ_NastepnySlot(ogon);
}

uint16_t AUDIOIRQ_PobierzZaleglosc(void)
{
    const uint16_t glowica = audioirq_glowica;
    const uint16_t ogon = audioirq_ogon;

    if (!audioirq_kolejka_aktywna || audioirq_liczba_slotow < 2U)
        return 0U;
    if (glowica >= ogon)
        return glowica - ogon;
    return (uint16_t)(audioirq_liczba_slotow - ogon + glowica);
}

uint32_t AUDIOIRQ_PobierzLiczbePrzepelnien(void)
{
    return audioirq_przepelnienia;
}

uint32_t AUDIOIRQ_PobierzLiczbeBledow(void)
{
    return audioirq_bledy;
}

void BSP_AUDIO_IN_TransferComplete_CallBack(void)
{
    audioirq_licznik_calosci++;
    AUDIOIRQ_SkopiujPolowe(audioirq_probki_polowy);
    Audio_Status = AUDIO_TRANSFER_COMPLETE;
}

void BSP_AUDIO_IN_HalfTransfer_CallBack(void)
{
    audioirq_licznik_polowek++;
    AUDIOIRQ_SkopiujPolowe(0U);
    Audio_Status = AUDIO_TRANSFER_HALF;
}

void BSP_AUDIO_IN_Error_CallBack(void)
{
    audioirq_bledy++;
}

uint32_t AUDIOIRQ_PobierzLicznikPolowek(void)
{
    return audioirq_licznik_polowek;
}

uint32_t AUDIOIRQ_PobierzLicznikCalosci(void)
{
    return audioirq_licznik_calosci;
}

void AUDIOIRQ_ZerujLicznikiDiagnostyczne(void)
{
    audioirq_licznik_polowek = 0U;
    audioirq_licznik_calosci = 0U;
    audioirq_przepelnienia = 0U;
    audioirq_bledy = 0U;
}

void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
{
    Audio_Play_Status = AUDIO_TRANSFER_COMPLETE;
}

void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
{
    Audio_Play_Status = AUDIO_TRANSFER_HALF;
}

void BSP_AUDIO_OUT_Error_CallBack(void)
{
}
