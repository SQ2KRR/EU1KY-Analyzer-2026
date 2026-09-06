#include "tdr_interpretacja.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define TDRI_MIN_PROBEK 8U
#define TDRI_MARGINES_PROBEK 2U
#define TDRI_POL_SZEROKOSCI_SZCZYTU 2U
#define TDRI_MIN_STOSUNEK_SZCZYT_TLO 4.0f
#define TDRI_PROG_ULAMEK_MAKSIMUM 0.25f
#define TDRI_PROG_TLO_RAZY 3.0f
#define TDRI_DRUGIE_ODBICIE_ULAMEK 0.55f
#define TDRI_EPSILON 1.0e-7f

static float TDRI_ModulProbki(float wartosc)
{
    if (!isfinite(wartosc))
        return 0.0f;
    return fabsf(wartosc);
}

static bool TDRI_CzyLokalnySzczyt(const float *odpowiedz, uint16_t indeks)
{
    const float biezacy = TDRI_ModulProbki(odpowiedz[indeks]);
    const float lewy = TDRI_ModulProbki(odpowiedz[indeks - 1U]);
    const float prawy = TDRI_ModulProbki(odpowiedz[indeks + 1U]);

    return biezacy >= lewy && biezacy >= prawy &&
           (biezacy > lewy || biezacy > prawy);
}

/*
 * Trzy sąsiednie próbki traktujemy lokalnie jak fragment paraboli. Pozwala to
 * oszacować położenie maksimum pomiędzy punktami IFFT bez filtrowania przebiegu
 * i bez udawania większej fizycznej rozdzielczości. Ograniczenie do +/-0,5
 * próbki chroni wynik przy płaskim lub niesymetrycznym wierzchołku.
 */
static float TDRI_InterpolujSzczyt(const float *odpowiedz, uint16_t liczba,
                                   uint16_t indeks)
{
    float lewy;
    float srodek;
    float prawy;
    float mianownik;
    float przesuniecie;

    if (odpowiedz == NULL || indeks == 0U || indeks + 1U >= liczba)
        return (float)indeks;

    lewy = TDRI_ModulProbki(odpowiedz[indeks - 1U]);
    srodek = TDRI_ModulProbki(odpowiedz[indeks]);
    prawy = TDRI_ModulProbki(odpowiedz[indeks + 1U]);
    mianownik = lewy - 2.0f * srodek + prawy;

    if (!isfinite(mianownik) || fabsf(mianownik) <= TDRI_EPSILON)
        return (float)indeks;

    przesuniecie = 0.5f * (lewy - prawy) / mianownik;
    if (!isfinite(przesuniecie))
        return (float)indeks;
    if (przesuniecie < -0.5f)
        przesuniecie = -0.5f;
    if (przesuniecie > 0.5f)
        przesuniecie = 0.5f;

    return (float)indeks + przesuniecie;
}

static float TDRI_ObliczTloRms(const float *odpowiedz, uint16_t liczba,
                               uint16_t indeks_najsilniejszego)
{
    float suma_kwadratow = 0.0f;
    uint32_t licznik = 0U;
    uint16_t i;

    for (i = TDRI_MARGINES_PROBEK; i + TDRI_MARGINES_PROBEK < liczba; ++i)
    {
        const int32_t odleglosc = (int32_t)i - (int32_t)indeks_najsilniejszego;
        const float wartosc = odpowiedz[i];

        if (odleglosc >= -(int32_t)TDRI_POL_SZEROKOSCI_SZCZYTU &&
            odleglosc <= (int32_t)TDRI_POL_SZEROKOSCI_SZCZYTU)
            continue;
        if (!isfinite(wartosc))
            continue;

        suma_kwadratow += wartosc * wartosc;
        ++licznik;
    }

    if (licznik == 0U)
        return 0.0f;

    return sqrtf(suma_kwadratow / (float)licznik);
}

bool TDRI_Analizuj(const float *odpowiedz, uint16_t liczba, TDRI_WYNIK_t *wynik)
{
    uint16_t i;
    uint16_t indeks_maksimum = 0U;
    uint16_t indeks_pierwszego = 0U;
    float maksimum = 0.0f;
    float prog;
    float prog_drugiego;
    float tlo_rms;
    float stosunek;
    bool znaleziono_pierwsze = false;
    bool znaleziono_drugie = false;

    if (wynik == NULL)
        return false;

    memset(wynik, 0, sizeof(*wynik));
    wynik->charakter = TDRI_CHARAKTER_BRAK;

    if (odpowiedz == NULL || liczba < TDRI_MIN_PROBEK)
        return false;

    /*
     * Pomijamy po dwie próbki na krańcach. Początek jest szczególnie podatny
     * na artefakt płaszczyzny pomiarowej, a ostatnia próbka na zawinięcie IFFT.
     */
    for (i = TDRI_MARGINES_PROBEK; i + TDRI_MARGINES_PROBEK < liczba; ++i)
    {
        const float modul = TDRI_ModulProbki(odpowiedz[i]);
        if (modul > maksimum)
        {
            maksimum = modul;
            indeks_maksimum = i;
        }
    }

    wynik->indeks_najsilniejszego = indeks_maksimum;
    wynik->indeks_najsilniejszego_dokladny =
        TDRI_InterpolujSzczyt(odpowiedz, liczba, indeks_maksimum);
    wynik->amplituda_najsilniejszego =
        indeks_maksimum < liczba && isfinite(odpowiedz[indeks_maksimum])
            ? odpowiedz[indeks_maksimum]
            : 0.0f;

    if (!(maksimum > TDRI_EPSILON))
    {
        wynik->flagi |= TDRI_FLAGA_SLABY_SYGNAL;
        return true;
    }

    tlo_rms = TDRI_ObliczTloRms(odpowiedz, liczba, indeks_maksimum);
    stosunek = maksimum / fmaxf(tlo_rms, TDRI_EPSILON);
    prog = fmaxf(TDRI_PROG_ULAMEK_MAKSIMUM * maksimum,
                 TDRI_PROG_TLO_RAZY * tlo_rms);

    wynik->poziom_tla_rms = tlo_rms;
    wynik->stosunek_szczyt_tlo = stosunek;

    if (stosunek < TDRI_MIN_STOSUNEK_SZCZYT_TLO)
        wynik->flagi |= TDRI_FLAGA_SLABY_SYGNAL;

    for (i = TDRI_MARGINES_PROBEK; i + TDRI_MARGINES_PROBEK < liczba; ++i)
    {
        if (TDRI_ModulProbki(odpowiedz[i]) < prog)
            continue;
        if (!TDRI_CzyLokalnySzczyt(odpowiedz, i))
            continue;

        indeks_pierwszego = i;
        znaleziono_pierwsze = true;
        break;
    }

    if (!znaleziono_pierwsze)
        indeks_pierwszego = indeks_maksimum;

    wynik->indeks_pierwszego = indeks_pierwszego;
    wynik->indeks_pierwszego_dokladny =
        TDRI_InterpolujSzczyt(odpowiedz, liczba, indeks_pierwszego);
    wynik->amplituda_pierwszego = isfinite(odpowiedz[indeks_pierwszego])
                                      ? odpowiedz[indeks_pierwszego]
                                      : 0.0f;
    wynik->poprawny = true;

    if (wynik->flagi & TDRI_FLAGA_SLABY_SYGNAL)
    {
        wynik->charakter = TDRI_CHARAKTER_NIEJEDNOZNACZNY;
    }
    else if (wynik->amplituda_pierwszego > 0.0f)
    {
        wynik->charakter = TDRI_CHARAKTER_WZROST_IMPEDANCJI;
    }
    else if (wynik->amplituda_pierwszego < 0.0f)
    {
        wynik->charakter = TDRI_CHARAKTER_SPADEK_IMPEDANCJI;
    }
    else
    {
        wynik->charakter = TDRI_CHARAKTER_NIEJEDNOZNACZNY;
    }

    if (indeks_pierwszego <= TDRI_MARGINES_PROBEK + 1U)
        wynik->flagi |= TDRI_FLAGA_BLISKO_POCZATKU;
    if (indeks_pierwszego + TDRI_MARGINES_PROBEK + 2U >= liczba)
        wynik->flagi |= TDRI_FLAGA_BLISKO_KONCA;

    /*
     * Drugi porównywalny szczyt oznacza, że pojedyncza diagnoza kabla byłaby
     * zbyt uproszczona. Nie próbujemy wtedy wybierać jednej "usterki".
     */
    prog_drugiego = fmaxf(TDRI_DRUGIE_ODBICIE_ULAMEK * maksimum,
                          TDRI_PROG_TLO_RAZY * tlo_rms);
    for (i = TDRI_MARGINES_PROBEK; i + TDRI_MARGINES_PROBEK < liczba; ++i)
    {
        const int32_t odleglosc = (int32_t)i - (int32_t)indeks_maksimum;

        if (odleglosc >= -3 && odleglosc <= 3)
            continue;
        if (TDRI_ModulProbki(odpowiedz[i]) < prog_drugiego)
            continue;
        if (!TDRI_CzyLokalnySzczyt(odpowiedz, i))
            continue;

        znaleziono_drugie = true;
        break;
    }

    if (znaleziono_drugie)
        wynik->flagi |= TDRI_FLAGA_WIELE_ODBIC;

    return true;
}
