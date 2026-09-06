/*
 * EU1KY-PL 2026 - zgodnościowa warstwa starego API NumKeypad.
 *
 * Historyczna klawiatura 0..9 została celowo usunięta z toru wykonania.
 * Wszystkie miejsca, które nadal wywołują NumKeypad(), korzystają teraz z
 * jednego współczesnego edytora pól cyfr używanego również przy ustawianiu
 * częstotliwości. Dzięki temu nie może już pojawić się stary ekran z siatką
 * klawiszy numerycznych.
 */

#include <stdint.h>

#include "num_keypad.h"
#include "ui_edytor_liczby.h"

uint32_t NumKeypad(uint32_t initial, uint32_t min_value, uint32_t max_value,
                   const char *header_text)
{
    uint32_t wynik = initial;

    /* Zachowujemy historyczną semantykę: Anuluj/Wstecz zwraca 0. */
    if (!UI_EdytujLiczbePolamiEx(initial, min_value, max_value,
                                 header_text, "", &wynik))
        return 0U;
    return wynik;
}

uint32_t NumKeypadMiliohm(uint32_t initial_mohm, uint32_t min_mohm,
                          uint32_t max_mohm, const char *header_text)
{
    uint32_t wynik = initial_mohm;

    /* Także tutaj Anuluj/Wstecz musi pozostać zgodne ze starym API. */
    if (!UI_EdytujRezystancjeMiliohmEx(initial_mohm, min_mohm, max_mohm,
                                       header_text, &wynik))
        return 0U;
    return wynik;
}
