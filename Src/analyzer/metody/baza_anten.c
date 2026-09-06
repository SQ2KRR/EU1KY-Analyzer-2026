#include "baza_anten.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define ANTENA_PREDKOSC_SWIATLA_M_S 299792458.0
#define ANTENA_DIPOL_STALA_M_MHZ    143.0
#define ANTENA_QUAD_ZASILANY_M_MHZ  301.0
#define ANTENA_QUAD_REFLEKTOR_M_MHZ 309.0
#define ANTENA_QUAD_DIREKTOR_M_MHZ  293.0

/* Receptura W3DZZ z materiału źródłowego użytego przy projektowaniu modułu. */
#define ANTENA_W3DZZ_WEWNETRZNY_MM 9700U
#define ANTENA_W3DZZ_ZEWNETRZNY_MM 6700U
#define ANTENA_W3DZZ_TRAP_HZ       7200000U
#define ANTENA_W3DZZ_TRAP_L_NH     8200U
#define ANTENA_W3DZZ_TRAP_C_PF_X10 600U

/* TwinYagi 145/435 MHz wg SP2XDQ - geometria oryginalnego projektu. */
#define ANTENA_TWIN_R_MM             1013U
#define ANTENA_TWIN_W_MM              988U
#define ANTENA_TWIN_D1_MM             955U
#define ANTENA_TWIN_D2_MM             938U
#define ANTENA_TWIN_D3_MM             917U

/* 3-elementowa Yagi DK7ZB 2 m, wersja 50 Ohm, boom 850 mm, elementy 6 mm.
 * To jest konkretny zweryfikowany projekt - celowo NIE skalujemy go automatycznie
 * na inne pasma ani średnice elementów. */
#define ANTENA_YAGI3_R_MM             1024U
#define ANTENA_YAGI3_W_MM              973U
#define ANTENA_YAGI3_D_MM              868U
#define ANTENA_YAGI3_RW_MM             480U
#define ANTENA_YAGI3_WD_MM             370U

/* ARRL/HF Kits: EFHW 40/20/15/10 m. 66 ft to ok. 20,1 m drutu.
 * Wartość jest długością startową - montaż i izolacja wymagają końcowego strojenia. */
#define ANTENA_EFHW_DRUT_MM          20100U

static uint32_t ANTENA_ZaokraglijMm(double wartosc_mm)
{
    if (!isfinite(wartosc_mm) || wartosc_mm <= 0.0)
        return 0U;
    if (wartosc_mm >= 4294967295.0)
        return UINT32_MAX;
    return (uint32_t)(wartosc_mm + 0.5);
}

static uint32_t ANTENA_DlugoscZeStalej(double stala_m_mhz, uint32_t f_hz)
{
    const double f_mhz = (double)f_hz / 1000000.0;
    if (f_mhz <= 0.0)
        return 0U;
    return ANTENA_ZaokraglijMm((stala_m_mhz / f_mhz) * 1000.0);
}

static uint32_t ANTENA_DlugoscFaliMm(uint32_t f_hz)
{
    if (f_hz == 0U)
        return 0U;
    return ANTENA_ZaokraglijMm((ANTENA_PREDKOSC_SWIATLA_M_S / (double)f_hz) * 1000.0);
}

uint8_t ANTENA_BAZA_LiczbaWymiarow(const ANTENA_PROFIL_t *profil)
{
    if (profil == NULL)
        return 0U;

    switch (profil->typ)
    {
    case ANTENA_TYP_DIPOL_POLFALOWY: return 1U;
    case ANTENA_TYP_W3DZZ:           return 2U;
    case ANTENA_TYP_QUAD_1EL:        return 1U;
    case ANTENA_TYP_QUAD_2EL:        return 3U;
    case ANTENA_TYP_QUAD_3EL:        return 5U;
    case ANTENA_TYP_TWINYAGI_SP2XDQ: return 5U;
    case ANTENA_TYP_YAGI3_DK7ZB_2M:  return 5U;
    case ANTENA_TYP_EFHW_40_10:       return 1U;
    default:                          return 0U;
    }
}

ANTENA_POLE_t ANTENA_BAZA_PobierzPole(const ANTENA_PROFIL_t *profil, uint8_t indeks)
{
    if (profil == NULL || indeks >= ANTENA_BAZA_MAX_WYMIAROW)
        return ANTENA_POLE_BRAK;

    switch (profil->typ)
    {
    case ANTENA_TYP_DIPOL_POLFALOWY:
        return indeks == 0U ? ANTENA_POLE_DIPOL_DLUGOSC_CALKOWITA : ANTENA_POLE_BRAK;

    case ANTENA_TYP_W3DZZ:
        if (indeks == 0U) return ANTENA_POLE_W3DZZ_ODCINEK_WEWNETRZNY;
        if (indeks == 1U) return ANTENA_POLE_W3DZZ_ODCINEK_ZEWNETRZNY;
        return ANTENA_POLE_BRAK;

    case ANTENA_TYP_QUAD_1EL:
    case ANTENA_TYP_QUAD_2EL:
    case ANTENA_TYP_QUAD_3EL:
        if (indeks == 0U) return ANTENA_POLE_QUAD_OBWOD_ZASILANY;
        if (profil->typ >= ANTENA_TYP_QUAD_2EL && indeks == 1U)
            return ANTENA_POLE_QUAD_OBWOD_REFLEKTORA;
        if (profil->typ == ANTENA_TYP_QUAD_2EL && indeks == 2U)
            return ANTENA_POLE_QUAD_ODSTEP_R_Z;
        if (profil->typ == ANTENA_TYP_QUAD_3EL)
        {
            if (indeks == 2U) return ANTENA_POLE_QUAD_OBWOD_DIREKTORA;
            if (indeks == 3U) return ANTENA_POLE_QUAD_ODSTEP_R_Z;
            if (indeks == 4U) return ANTENA_POLE_QUAD_ODSTEP_Z_D;
        }
        return ANTENA_POLE_BRAK;

    case ANTENA_TYP_TWINYAGI_SP2XDQ:
        if (indeks == 0U) return ANTENA_POLE_TWIN_REFLEKTOR;
        if (indeks == 1U) return ANTENA_POLE_TWIN_WIBRATOR;
        if (indeks == 2U) return ANTENA_POLE_TWIN_D1;
        if (indeks == 3U) return ANTENA_POLE_TWIN_D2;
        if (indeks == 4U) return ANTENA_POLE_TWIN_D3;
        return ANTENA_POLE_BRAK;

    case ANTENA_TYP_YAGI3_DK7ZB_2M:
        if (indeks == 0U) return ANTENA_POLE_YAGI_REFLEKTOR;
        if (indeks == 1U) return ANTENA_POLE_YAGI_WIBRATOR;
        if (indeks == 2U) return ANTENA_POLE_YAGI_DIREKTOR;
        if (indeks == 3U) return ANTENA_POLE_YAGI_ODSTEP_R_W;
        if (indeks == 4U) return ANTENA_POLE_YAGI_ODSTEP_W_D;
        return ANTENA_POLE_BRAK;

    case ANTENA_TYP_EFHW_40_10:
        return indeks == 0U ? ANTENA_POLE_EFHW_DRUT : ANTENA_POLE_BRAK;

    default:
        return ANTENA_POLE_BRAK;
    }
}

void ANTENA_BAZA_ObliczZalecenia(const ANTENA_PROFIL_t *profil,
                                 ANTENA_ZALECENIA_t *zalecenia)
{
    uint8_t i;
    uint32_t lambda_mm;

    if (zalecenia == NULL)
        return;
    memset(zalecenia, 0, sizeof(*zalecenia));
    if (profil == NULL || profil->czestotliwosc_docelowa_hz == 0U)
        return;

    zalecenia->liczba = ANTENA_BAZA_LiczbaWymiarow(profil);
    for (i = 0U; i < zalecenia->liczba; ++i)
        zalecenia->wymiary[i].pole = ANTENA_BAZA_PobierzPole(profil, i);

    switch (profil->typ)
    {
    case ANTENA_TYP_DIPOL_POLFALOWY:
        zalecenia->wymiary[0].zalecane_mm =
            ANTENA_DlugoscZeStalej(ANTENA_DIPOL_STALA_M_MHZ,
                                   profil->czestotliwosc_docelowa_hz);
        zalecenia->wymiary[0].regulowane_z_s11 = 1U;
        break;

    case ANTENA_TYP_W3DZZ:
        zalecenia->wymiary[0].zalecane_mm = ANTENA_W3DZZ_WEWNETRZNY_MM;
        zalecenia->wymiary[1].zalecane_mm = ANTENA_W3DZZ_ZEWNETRZNY_MM;
        /* Regulacja zależy od badanego pasma, dlatego decyzję podejmuje
           ANTENA_BAZA_WyznaczKorekte() na podstawie f_docelowej. */
        break;

    case ANTENA_TYP_QUAD_1EL:
    case ANTENA_TYP_QUAD_2EL:
    case ANTENA_TYP_QUAD_3EL:
        zalecenia->wymiary[0].zalecane_mm =
            ANTENA_DlugoscZeStalej(ANTENA_QUAD_ZASILANY_M_MHZ,
                                   profil->czestotliwosc_docelowa_hz);
        zalecenia->wymiary[0].regulowane_z_s11 = 1U;

        if (profil->typ >= ANTENA_TYP_QUAD_2EL)
        {
            zalecenia->wymiary[1].zalecane_mm =
                ANTENA_DlugoscZeStalej(ANTENA_QUAD_REFLEKTOR_M_MHZ,
                                       profil->czestotliwosc_docelowa_hz);
        }
        if (profil->typ == ANTENA_TYP_QUAD_3EL)
        {
            zalecenia->wymiary[2].zalecane_mm =
                ANTENA_DlugoscZeStalej(ANTENA_QUAD_DIREKTOR_M_MHZ,
                                       profil->czestotliwosc_docelowa_hz);
        }

        lambda_mm = ANTENA_DlugoscFaliMm(profil->czestotliwosc_docelowa_hz);
        if (profil->typ == ANTENA_TYP_QUAD_2EL)
        {
            zalecenia->wymiary[2].ma_zakres = 1U;
            zalecenia->wymiary[2].minimum_mm = ANTENA_ZaokraglijMm(0.15 * lambda_mm);
            zalecenia->wymiary[2].maksimum_mm = ANTENA_ZaokraglijMm(0.25 * lambda_mm);
            zalecenia->wymiary[2].zalecane_mm = ANTENA_ZaokraglijMm(0.20 * lambda_mm);
        }
        else if (profil->typ == ANTENA_TYP_QUAD_3EL)
        {
            zalecenia->wymiary[3].ma_zakres = 1U;
            zalecenia->wymiary[3].minimum_mm = ANTENA_ZaokraglijMm(0.15 * lambda_mm);
            zalecenia->wymiary[3].maksimum_mm = ANTENA_ZaokraglijMm(0.25 * lambda_mm);
            zalecenia->wymiary[3].zalecane_mm = ANTENA_ZaokraglijMm(0.20 * lambda_mm);

            zalecenia->wymiary[4].ma_zakres = 1U;
            zalecenia->wymiary[4].minimum_mm = ANTENA_ZaokraglijMm(0.15 * lambda_mm);
            zalecenia->wymiary[4].maksimum_mm = ANTENA_ZaokraglijMm(0.25 * lambda_mm);
            zalecenia->wymiary[4].zalecane_mm = ANTENA_ZaokraglijMm(0.20 * lambda_mm);
        }
        break;

    case ANTENA_TYP_TWINYAGI_SP2XDQ:
        zalecenia->wymiary[0].zalecane_mm = ANTENA_TWIN_R_MM;
        zalecenia->wymiary[1].zalecane_mm = ANTENA_TWIN_W_MM;
        zalecenia->wymiary[2].zalecane_mm = ANTENA_TWIN_D1_MM;
        zalecenia->wymiary[3].zalecane_mm = ANTENA_TWIN_D2_MM;
        zalecenia->wymiary[4].zalecane_mm = ANTENA_TWIN_D3_MM;
        /* Konstrukcja dwupasmowa: nie wyznaczamy automatycznej korekty mm
           z pojedynczego S11, bo sprzężenie obu pasm nie jest jednoznaczne. */
        break;

    case ANTENA_TYP_YAGI3_DK7ZB_2M:
        zalecenia->wymiary[0].zalecane_mm = ANTENA_YAGI3_R_MM;
        zalecenia->wymiary[1].zalecane_mm = ANTENA_YAGI3_W_MM;
        zalecenia->wymiary[2].zalecane_mm = ANTENA_YAGI3_D_MM;
        zalecenia->wymiary[3].zalecane_mm = ANTENA_YAGI3_RW_MM;
        zalecenia->wymiary[4].zalecane_mm = ANTENA_YAGI3_WD_MM;
        /* Projekt DK7ZB jest zdefiniowany dla 145 MHz i elementów 6 mm.
           S11 służy do weryfikacji wykonania, nie do automatycznego skalowania. */
        break;

    case ANTENA_TYP_EFHW_40_10:
        zalecenia->wymiary[0].zalecane_mm = ANTENA_EFHW_DRUT_MM;
        zalecenia->wymiary[0].regulowane_z_s11 = 1U;
        break;

    default:
        zalecenia->liczba = 0U;
        break;
    }
}

void ANTENA_BAZA_InicjalizujProfil(ANTENA_PROFIL_t *profil, ANTENA_TYP_t typ,
                                   uint32_t czestotliwosc_docelowa_hz)
{
    ANTENA_ZALECENIA_t zalecenia;
    uint8_t i;

    if (profil == NULL)
        return;

    memset(profil, 0, sizeof(*profil));
    profil->wersja = ANTENA_BAZA_WERSJA_PROFILU;
    profil->typ = typ;
    profil->czestotliwosc_docelowa_hz = czestotliwosc_docelowa_hz;
    profil->trap_czestotliwosc_hz = ANTENA_W3DZZ_TRAP_HZ;
    profil->trap_indukcyjnosc_nh = ANTENA_W3DZZ_TRAP_L_NH;
    profil->trap_pojemnosc_pf_x10 = ANTENA_W3DZZ_TRAP_C_PF_X10;

    ANTENA_BAZA_ObliczZalecenia(profil, &zalecenia);
    for (i = 0U; i < zalecenia.liczba; ++i)
        profil->wymiary_mm[i] = zalecenia.wymiary[i].zalecane_mm;
}


uint32_t ANTENA_BAZA_WymiarStrojony(const ANTENA_PROFIL_t *profil)
{
    if (profil == NULL)
        return 0U;

    switch (profil->typ)
    {
    case ANTENA_TYP_DIPOL_POLFALOWY:
    case ANTENA_TYP_EFHW_40_10:
    case ANTENA_TYP_QUAD_1EL:
    case ANTENA_TYP_QUAD_2EL:
    case ANTENA_TYP_QUAD_3EL:
        return profil->wymiary_mm[0];
    case ANTENA_TYP_W3DZZ:
        if (profil->czestotliwosc_docelowa_hz >= 2800000U &&
            profil->czestotliwosc_docelowa_hz <= 4200000U)
            return 2U * (profil->wymiary_mm[0] + profil->wymiary_mm[1]);
        if (profil->czestotliwosc_docelowa_hz >= 6500000U &&
            profil->czestotliwosc_docelowa_hz <= 7800000U)
            return 2U * profil->wymiary_mm[0];
        return 0U;
    default:
        return 0U;
    }
}

float ANTENA_BAZA_RoznicaProcent(uint32_t biezacy_mm, uint32_t zalecany_mm)
{
    if (zalecany_mm == 0U)
        return 0.0f;
    return (((float)biezacy_mm / (float)zalecany_mm) - 1.0f) * 100.0f;
}

static ANTENA_KOREKTA_t ANTENA_KorektaProsta(uint32_t wymiar_mm,
                                              uint32_t f_rezonans_hz,
                                              uint32_t f_docelowa_hz,
                                              ANTENA_REGULACJA_t regulacja,
                                              ANTENA_PEWNOSC_t pewnosc,
                                              uint8_t liczba_miejsc,
                                              uint8_t ostroznosc)
{
    ANTENA_KOREKTA_t wynik;
    float wspolczynnik;

    memset(&wynik, 0, sizeof(wynik));
    if (wymiar_mm == 0U || f_rezonans_hz == 0U || f_docelowa_hz == 0U)
        return wynik;

    wspolczynnik = (float)f_rezonans_hz / (float)f_docelowa_hz;
    if (!isfinite(wspolczynnik) || wspolczynnik <= 0.0f)
        return wynik;

    wynik.dostepna = 1U;
    wynik.pewnosc = pewnosc;
    wynik.regulacja = regulacja;
    wynik.wymiar_biezacy_mm = wymiar_mm;
    wynik.wymiar_nowy_mm = (float)wymiar_mm * wspolczynnik;
    wynik.zmiana_calkowita_mm = wynik.wymiar_nowy_mm - (float)wymiar_mm;
    wynik.liczba_miejsc_regulacji = liczba_miejsc == 0U ? 1U : liczba_miejsc;
    wynik.zmiana_na_miejsce_mm = wynik.zmiana_calkowita_mm /
                                 (float)wynik.liczba_miejsc_regulacji;
    wynik.wymaga_ostroznosci = ostroznosc;

    if (fabsf(wynik.zmiana_calkowita_mm) < 0.5f)
        wynik.kierunek = STROJENIE_ANTENA_KIERUNEK_W_CELU;
    else if (wynik.zmiana_calkowita_mm < 0.0f)
        wynik.kierunek = STROJENIE_ANTENA_KIERUNEK_SKROC;
    else
        wynik.kierunek = STROJENIE_ANTENA_KIERUNEK_WYDLUZ;

    return wynik;
}


static void ANTENA_ZastosujHistorie(const ANTENA_PROFIL_t *profil,
                                    const STROJENIE_ANTENA_WYNIK_t *pomiar,
                                    ANTENA_KOREKTA_t *korekta)
{
    const uint32_t teraz_mm = ANTENA_BAZA_WymiarStrojony(profil);
    const int32_t dl_mm = (int32_t)teraz_mm - (int32_t)profil->poprzedni_wymiar_strojony_mm;
    const int32_t df_hz = (int32_t)pomiar->f_rezonans_hz - (int32_t)profil->poprzedni_rezonans_hz;
    float czulosc;
    float zmiana;

    if (korekta == NULL || !korekta->dostepna || teraz_mm == 0U ||
        profil->poprzedni_wymiar_strojony_mm == 0U || profil->poprzedni_rezonans_hz == 0U ||
        dl_mm == 0 || df_hz == 0)
        return;

    czulosc = (float)df_hz / (float)dl_mm;
    /* Odrzucamy historię o niewiarygodnym znaku lub skrajnej czułości. Dla
       prostych anten wydłużanie powinno obniżać częstotliwość rezonansu. */
    if (!isfinite(czulosc) || czulosc >= -1.0f || fabsf(czulosc) > 50000000.0f)
        return;

    zmiana = ((float)profil->czestotliwosc_docelowa_hz -
              (float)pomiar->f_rezonans_hz) / czulosc;
    if (!isfinite(zmiana))
        return;

    /* Lokalna liniowość jest użyteczna tylko blisko ostatniej regulacji. Nie
       pozwalamy, aby ekstrapolacja żądała zmiany większej niż 20% wymiaru. */
    if (fabsf(zmiana) > 0.20f * (float)teraz_mm)
        return;

    korekta->wymiar_biezacy_mm = teraz_mm;
    korekta->wymiar_nowy_mm = (float)teraz_mm + zmiana;
    korekta->zmiana_calkowita_mm = zmiana;
    korekta->zmiana_na_miejsce_mm = zmiana / (float)korekta->liczba_miejsc_regulacji;
    korekta->uzyto_historii = 1U;
    korekta->czulosc_hz_na_mm = czulosc;

    if (fabsf(zmiana) < 0.5f)
        korekta->kierunek = STROJENIE_ANTENA_KIERUNEK_W_CELU;
    else if (zmiana < 0.0f)
        korekta->kierunek = STROJENIE_ANTENA_KIERUNEK_SKROC;
    else
        korekta->kierunek = STROJENIE_ANTENA_KIERUNEK_WYDLUZ;
}

ANTENA_KOREKTA_t ANTENA_BAZA_WyznaczKorekte(const ANTENA_PROFIL_t *profil,
                                             const STROJENIE_ANTENA_WYNIK_t *pomiar)
{
    ANTENA_KOREKTA_t wynik;
    uint32_t f;

    memset(&wynik, 0, sizeof(wynik));
    if (profil == NULL || pomiar == NULL || !pomiar->rezonans_znaleziony)
        return wynik;

    f = profil->czestotliwosc_docelowa_hz;
    switch (profil->typ)
    {
    case ANTENA_TYP_DIPOL_POLFALOWY:
        wynik = ANTENA_KorektaProsta(profil->wymiary_mm[0], pomiar->f_rezonans_hz, f,
                                     ANTENA_REGULACJA_DIPOL_RAMIONA,
                                     ANTENA_PEWNOSC_WYSOKA, 2U, 0U);
        break;

    case ANTENA_TYP_QUAD_1EL:
    case ANTENA_TYP_QUAD_2EL:
    case ANTENA_TYP_QUAD_3EL:
        /* Z pomiaru S11 wyznaczamy korektę wyłącznie elementu zasilanego.
           Reflektora i direktora nie korygujemy z jednego pomiaru wejściowego,
           bo ich wpływ zależy od sprzężenia wzajemnego i nie jest jednoznaczny. */
        wynik = ANTENA_KorektaProsta(profil->wymiary_mm[0], pomiar->f_rezonans_hz, f,
                                     ANTENA_REGULACJA_QUAD_OBWOD_ZASILANY,
                                     ANTENA_PEWNOSC_WYSOKA, 4U, 0U);
        break;

    case ANTENA_TYP_EFHW_40_10:
        wynik = ANTENA_KorektaProsta(profil->wymiary_mm[0], pomiar->f_rezonans_hz, f,
                                     ANTENA_REGULACJA_EFHW_DRUT,
                                     ANTENA_PEWNOSC_SREDNIA, 1U, 1U);
        break;

    case ANTENA_TYP_W3DZZ:
        if (f >= 2800000U && f <= 4200000U)
        {
            const uint32_t calosc_mm = 2U * (profil->wymiary_mm[0] + profil->wymiary_mm[1]);
            wynik = ANTENA_KorektaProsta(calosc_mm, pomiar->f_rezonans_hz, f,
                                         ANTENA_REGULACJA_W3DZZ_ZEWNETRZNE,
                                         ANTENA_PEWNOSC_SREDNIA, 2U, 1U);
        }
        else if (f >= 6500000U && f <= 7800000U)
        {
            const uint32_t calosc_wewnetrzna_mm = 2U * profil->wymiary_mm[0];
            wynik = ANTENA_KorektaProsta(calosc_wewnetrzna_mm, pomiar->f_rezonans_hz, f,
                                         ANTENA_REGULACJA_W3DZZ_WEWNETRZNE,
                                         ANTENA_PEWNOSC_SREDNIA, 2U, 1U);
        }
        else
        {
            wynik.kierunek = pomiar->kierunek;
            wynik.pewnosc = ANTENA_PEWNOSC_NISKA;
            wynik.wymaga_ostroznosci = 1U;
        }
        break;

    default:
        break;
    }

    /* Z historii korzystamy tylko tam, gdzie model wskazał konkretny wymiar
       regulowany. Dla wyższych pasm W3DZZ nadal nie zgadujemy milimetrów. */
    ANTENA_ZastosujHistorie(profil, pomiar, &wynik);
    return wynik;
}
