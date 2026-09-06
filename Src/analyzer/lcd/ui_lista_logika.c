#include "ui_lista_logika.h"

static void UI_LISTA_Ogranicz(UI_LISTA_STAN_t *stan)
{
    uint16_t maks_pierwszy;

    if (stan == 0)
        return;

    if (stan->liczba_widocznych == 0U)
        stan->liczba_widocznych = 1U;

    if (stan->liczba_pozycji == 0U)
    {
        stan->pierwszy_widoczny = 0U;
        stan->zaznaczony = 0U;
        stan->fokus_widoczny = false;
        return;
    }

    if (stan->zaznaczony >= stan->liczba_pozycji)
        stan->zaznaczony = (uint16_t)(stan->liczba_pozycji - 1U);

    maks_pierwszy = (stan->liczba_pozycji > stan->liczba_widocznych)
                        ? (uint16_t)(stan->liczba_pozycji - stan->liczba_widocznych)
                        : 0U;
    if (stan->pierwszy_widoczny > maks_pierwszy)
        stan->pierwszy_widoczny = maks_pierwszy;

    if (stan->zaznaczony < stan->pierwszy_widoczny)
        stan->pierwszy_widoczny = stan->zaznaczony;
    else if (stan->zaznaczony >= (uint16_t)(stan->pierwszy_widoczny + stan->liczba_widocznych))
        stan->pierwszy_widoczny = (uint16_t)(stan->zaznaczony - stan->liczba_widocznych + 1U);
}

void UI_LISTA_Init(UI_LISTA_STAN_t *stan, uint16_t liczba_pozycji,
                   uint16_t liczba_widocznych)
{
    if (stan == 0)
        return;

    stan->pierwszy_widoczny = 0U;
    stan->zaznaczony = 0U;
    stan->liczba_pozycji = liczba_pozycji;
    stan->liczba_widocznych = (liczba_widocznych == 0U) ? 1U : liczba_widocznych;
    stan->fokus_widoczny = false;
    UI_LISTA_Ogranicz(stan);
}

void UI_LISTA_UstawLiczbe(UI_LISTA_STAN_t *stan, uint16_t liczba_pozycji)
{
    if (stan == 0)
        return;
    stan->liczba_pozycji = liczba_pozycji;
    UI_LISTA_Ogranicz(stan);
}

void UI_LISTA_UstawZaznaczony(UI_LISTA_STAN_t *stan, uint16_t indeks,
                              bool pokaz_fokus)
{
    if (stan == 0 || stan->liczba_pozycji == 0U)
        return;

    if (indeks >= stan->liczba_pozycji)
        indeks = (uint16_t)(stan->liczba_pozycji - 1U);
    stan->zaznaczony = indeks;
    stan->fokus_widoczny = pokaz_fokus;
    UI_LISTA_Ogranicz(stan);
}

static bool UI_LISTA_CzyAktywna(const uint8_t *aktywne, uint16_t indeks)
{
    return aktywne == 0 || aktywne[indeks] != 0U;
}

bool UI_LISTA_PrzesunFokus(UI_LISTA_STAN_t *stan, int8_t kierunek,
                           const uint8_t *aktywne)
{
    uint16_t proba;
    int32_t indeks;

    if (stan == 0 || stan->liczba_pozycji == 0U || kierunek == 0)
        return false;

    indeks = (int32_t)stan->zaznaczony;
    for (proba = 0U; proba < stan->liczba_pozycji; ++proba)
    {
        indeks += (kierunek > 0) ? 1 : -1;
        if (indeks < 0)
            indeks = (int32_t)stan->liczba_pozycji - 1;
        else if (indeks >= (int32_t)stan->liczba_pozycji)
            indeks = 0;

        if (UI_LISTA_CzyAktywna(aktywne, (uint16_t)indeks))
        {
            stan->zaznaczony = (uint16_t)indeks;
            stan->fokus_widoczny = true;
            UI_LISTA_Ogranicz(stan);
            return true;
        }
    }

    return false;
}

void UI_LISTA_Przewin(UI_LISTA_STAN_t *stan, int16_t wiersze)
{
    int32_t nowy;
    uint16_t maks_pierwszy;

    if (stan == 0 || stan->liczba_pozycji <= stan->liczba_widocznych)
    {
        if (stan != 0)
            stan->pierwszy_widoczny = 0U;
        return;
    }

    maks_pierwszy = (uint16_t)(stan->liczba_pozycji - stan->liczba_widocznych);
    nowy = (int32_t)stan->pierwszy_widoczny + (int32_t)wiersze;
    if (nowy < 0)
        nowy = 0;
    if (nowy > (int32_t)maks_pierwszy)
        nowy = maks_pierwszy;
    stan->pierwszy_widoczny = (uint16_t)nowy;

    /* Po ręcznym przewinięciu bez zmiany fokusu nie wymuszamy skoku listy
     * z powrotem do zaznaczonej pozycji. Fokus zostanie ponownie zsynchronizowany
     * przy następnym obrocie enkodera lub jawnej zmianie zaznaczenia. */
}

bool UI_LISTA_CzyWidoczny(const UI_LISTA_STAN_t *stan, uint16_t indeks)
{
    uint32_t koniec;

    if (stan == 0 || indeks >= stan->liczba_pozycji)
        return false;
    koniec = (uint32_t)stan->pierwszy_widoczny + (uint32_t)stan->liczba_widocznych;
    return indeks >= stan->pierwszy_widoczny && (uint32_t)indeks < koniec;
}

uint16_t UI_LISTA_OstatniWidoczny(const UI_LISTA_STAN_t *stan)
{
    uint32_t koniec;

    if (stan == 0 || stan->liczba_pozycji == 0U)
        return 0U;

    koniec = (uint32_t)stan->pierwszy_widoczny + (uint32_t)stan->liczba_widocznych;
    if (koniec > stan->liczba_pozycji)
        koniec = stan->liczba_pozycji;
    return (uint16_t)(koniec - 1U);
}
