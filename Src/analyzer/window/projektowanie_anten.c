#include "projektowanie_anten.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "baza_anten.h"
#include "config.h"
#include "ff.h"
#include "font.h"
#include "gen.h"
#include "jezyk.h"
#include "komunikaty.h"
#include "measurement.h"
#include "num_keypad.h"
#include "touch.h"
#include "ui_edytor_liczby.h"
#include "ui_wspolny.h"
#include "wejscia_uzytkownika.h"

extern void Sleep(uint32_t);

#define ANTENY_KATALOG             "/aa/anteny"
#define ANTENY_PROFIL_PLIK         "/aa/anteny/ostatnia.ant"
#define ANTENY_PROFIL_TMP          "/aa/anteny/ostatnia.tmp"
#define ANTENY_PROFIL_BAK          "/aa/anteny/ostatnia.bak"
#define ANTENY_RAPORT_AWARYJNY     "/aa/anteny/raport.txt"
#define ANTENY_MAGIC               "EU1KY_ANT1"
#define ANTENY_LISTA_X             8U
#define ANTENY_LISTA_Y             40U
#define ANTENY_LISTA_W             464U
#define ANTENY_LISTA_H             225U
#define ANTENY_WIDOCZNE            5U

static ANTENA_PROFIL_t g_antena;
static STROJENIE_ANTENA_WYNIK_t g_ostatni_pomiar;
static ANTENA_KOREKTA_t g_ostatnia_korekta;
static uint8_t g_ma_profil = 0U;
static uint8_t g_ma_pomiar = 0U;
static STROJENIE_ANTENA_WYNIK_t g_twin_pomiar_2m;
static STROJENIE_ANTENA_WYNIK_t g_twin_pomiar_70;
static uint8_t g_twin_ma_pomiar = 0U;

static const char *ANT_T(const char *pl, const char *en, const char *de, const char *ru)
{
    return JEZYK_Wybierz(pl, en, de, ru);
}

static const char *ANTENY_NazwaTypu(ANTENA_TYP_t typ)
{
    switch (typ)
    {
    case ANTENA_TYP_DIPOL_POLFALOWY:
        return ANT_T("Dipol 1/2 lambda", "Half-wave dipole", "Halbwellendipol", "Полуволновой диполь");
    case ANTENA_TYP_W3DZZ:
        return "W3DZZ 80-10 m";
    case ANTENA_TYP_QUAD_1EL:
        return "Cubical Quad 1 el.";
    case ANTENA_TYP_QUAD_2EL:
        return "Cubical Quad 2 el.";
    case ANTENA_TYP_QUAD_3EL:
        return "Cubical Quad 3 el.";
    case ANTENA_TYP_TWINYAGI_SP2XDQ:
        return "TwinYagi 145/435 SP2XDQ";
    case ANTENA_TYP_YAGI3_DK7ZB_2M:
        return "Yagi 3 el. DK7ZB 2 m";
    case ANTENA_TYP_EFHW_40_10:
        return "EFHW 40/20/15/10 m";
    default:
        return "?";
    }
}

static const char *ANTENY_NazwaPola(ANTENA_POLE_t pole)
{
    switch (pole)
    {
    case ANTENA_POLE_DIPOL_DLUGOSC_CALKOWITA:
        return ANT_T("Długość całkowita", "Total length", "Gesamtlänge", "Общая длина");
    case ANTENA_POLE_W3DZZ_ODCINEK_WEWNETRZNY:
        return ANT_T("Wewnętrzny / ramię", "Inner / arm", "Innen / Schenkel", "Внутр. / плечо");
    case ANTENA_POLE_W3DZZ_ODCINEK_ZEWNETRZNY:
        return ANT_T("Zewnętrzny / ramię", "Outer / arm", "Außen / Schenkel", "Внешн. / плечо");
    case ANTENA_POLE_QUAD_OBWOD_ZASILANY:
        return ANT_T("Obwód zasilany", "Driven perimeter", "Strahlerumfang", "Периметр вибратора");
    case ANTENA_POLE_QUAD_OBWOD_REFLEKTORA:
        return ANT_T("Obwód reflektora", "Reflector perimeter", "Reflektorumfang", "Периметр рефлектора");
    case ANTENA_POLE_QUAD_OBWOD_DIREKTORA:
        return ANT_T("Obwód direktora", "Director perimeter", "Direktorumfang", "Периметр директора");
    case ANTENA_POLE_QUAD_ODSTEP_R_Z:
        return ANT_T("Odstęp reflektor-zasilany", "Reflector-driven spacing", "Reflektor-Strahler Abstand", "Зазор рефлектор-вибратор");
    case ANTENA_POLE_QUAD_ODSTEP_Z_D:
        return ANT_T("Odstęp zasilany-direktor", "Driven-director spacing", "Strahler-Direktor Abstand", "Зазор вибратор-директор");
    case ANTENA_POLE_TWIN_REFLEKTOR: return ANT_T("Reflektor R", "Reflector R", "Reflektor R", "Рефлектор R");
    case ANTENA_POLE_TWIN_WIBRATOR:  return ANT_T("Wibrator W", "Driven W", "Strahler W", "Вибратор W");
    case ANTENA_POLE_TWIN_D1:        return "D1";
    case ANTENA_POLE_TWIN_D2:        return "D2";
    case ANTENA_POLE_TWIN_D3:        return "D3";
    case ANTENA_POLE_YAGI_REFLEKTOR: return ANT_T("Reflektor R", "Reflector R", "Reflektor R", "Рефлектор R");
    case ANTENA_POLE_YAGI_WIBRATOR:  return ANT_T("Wibrator W", "Driven W", "Strahler W", "Вибратор W");
    case ANTENA_POLE_YAGI_DIREKTOR:  return ANT_T("Direktor D", "Director D", "Direktor D", "Директор D");
    case ANTENA_POLE_YAGI_ODSTEP_R_W:return ANT_T("Odstęp R-W", "R-W spacing", "R-W Abstand", "Зазор R-W");
    case ANTENA_POLE_YAGI_ODSTEP_W_D:return ANT_T("Odstęp W-D", "W-D spacing", "W-D Abstand", "Зазор W-D");
    case ANTENA_POLE_EFHW_DRUT:      return ANT_T("Długość drutu", "Wire length", "Drahtlänge", "Длина провода");
    default:
        return "?";
    }
}

static uint32_t ANTENY_DomyslnyCel(ANTENA_TYP_t typ)
{
    uint32_t cel;
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    switch (typ)
    {
    case ANTENA_TYP_W3DZZ: cel = 7050000U; break;
    case ANTENA_TYP_QUAD_1EL:
    case ANTENA_TYP_QUAD_2EL:
    case ANTENA_TYP_QUAD_3EL: cel = 145000000U; break;
    case ANTENA_TYP_TWINYAGI_SP2XDQ: cel = 145500000U; break;
    case ANTENA_TYP_YAGI3_DK7ZB_2M: cel = 145000000U; break;
    case ANTENA_TYP_EFHW_40_10: cel = 7100000U; break;
    case ANTENA_TYP_DIPOL_POLFALOWY:
    default: cel = 7100000U; break;
    }

    if (cel < fmin) cel = fmin;
    if (cel > fmax) cel = fmax;
    return cel;
}

static uint8_t ANTENY_ZapewnijKatalog(void)
{
    FRESULT wynik;

    if (!CFG_CzyKartaSDDostepna() && !CFG_SD_SprobujPrzywrocic())
        return 0U;

    wynik = f_mkdir("/aa");
    if (wynik != FR_OK && wynik != FR_EXIST)
        return 0U;
    wynik = f_mkdir(ANTENY_KATALOG);
    return (wynik == FR_OK || wynik == FR_EXIST) ? 1U : 0U;
}

static uint8_t ANTENY_ZapiszBufor(FIL *plik, const char *tekst)
{
    UINT zapisano = 0U;
    UINT dlugosc;

    if (plik == NULL || tekst == NULL)
        return 0U;
    dlugosc = (UINT)strlen(tekst);
    return f_write(plik, tekst, dlugosc, &zapisano) == FR_OK && zapisano == dlugosc;
}

static uint8_t ANTENY_ZapiszProfil(void)
{
    FIL plik;
    char bufor[128];
    uint8_t i;
    FRESULT fr;

    if (!g_ma_profil || !ANTENY_ZapewnijKatalog())
        return 0U;

    /* Profil jest mały, ale nie powinien zostać utracony po zaniku zasilania
       w trakcie zapisu. Najpierw powstaje kompletny plik tymczasowy, potem
       dopiero zastępuje poprzedni profil. */
    (void)f_unlink(ANTENY_PROFIL_TMP);
    if (f_open(&plik, ANTENY_PROFIL_TMP, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return 0U;

#define ANT_ZAPISZ_LINIE(...) do { \
        snprintf(bufor, sizeof(bufor), __VA_ARGS__); \
        if (!ANTENY_ZapiszBufor(&plik, bufor)) { \
            (void)f_close(&plik); \
            (void)f_unlink(ANTENY_PROFIL_TMP); \
            return 0U; \
        } \
    } while (0)

    ANT_ZAPISZ_LINIE("%s\n", ANTENY_MAGIC);
    ANT_ZAPISZ_LINIE("wersja=%lu\n", (unsigned long)g_antena.wersja);
    ANT_ZAPISZ_LINIE("typ=%u\n", (unsigned)g_antena.typ);
    ANT_ZAPISZ_LINIE("cel_hz=%lu\n", (unsigned long)g_antena.czestotliwosc_docelowa_hz);
    for (i = 0U; i < ANTENA_BAZA_MAX_WYMIAROW; ++i)
        ANT_ZAPISZ_LINIE("w%u_mm=%lu\n", (unsigned)i, (unsigned long)g_antena.wymiary_mm[i]);
    ANT_ZAPISZ_LINIE("trap_hz=%lu\n", (unsigned long)g_antena.trap_czestotliwosc_hz);
    ANT_ZAPISZ_LINIE("trap_l_nh=%lu\n", (unsigned long)g_antena.trap_indukcyjnosc_nh);
    ANT_ZAPISZ_LINIE("trap_c_pf_x10=%lu\n", (unsigned long)g_antena.trap_pojemnosc_pf_x10);
    ANT_ZAPISZ_LINIE("hist_wymiar_mm=%lu\n", (unsigned long)g_antena.poprzedni_wymiar_strojony_mm);
    ANT_ZAPISZ_LINIE("hist_rezonans_hz=%lu\n", (unsigned long)g_antena.poprzedni_rezonans_hz);

#undef ANT_ZAPISZ_LINIE

    fr = f_sync(&plik);
    if (f_close(&plik) != FR_OK || fr != FR_OK)
    {
        (void)f_unlink(ANTENY_PROFIL_TMP);
        return 0U;
    }

    (void)f_unlink(ANTENY_PROFIL_BAK);
    (void)f_rename(ANTENY_PROFIL_PLIK, ANTENY_PROFIL_BAK);
    if (f_rename(ANTENY_PROFIL_TMP, ANTENY_PROFIL_PLIK) != FR_OK)
    {
        (void)f_unlink(ANTENY_PROFIL_TMP);
        (void)f_rename(ANTENY_PROFIL_BAK, ANTENY_PROFIL_PLIK);
        return 0U;
    }
    (void)f_unlink(ANTENY_PROFIL_BAK);
    return 1U;
}

static uint8_t ANTENY_PobierzU32(const char *tekst, const char *klucz, uint32_t *wartosc)
{
    const char *p;
    char *koniec;
    unsigned long liczba;

    if (tekst == NULL || klucz == NULL || wartosc == NULL)
        return 0U;
    p = strstr(tekst, klucz);
    if (p == NULL)
        return 0U;
    p += strlen(klucz);
    liczba = strtoul(p, &koniec, 10);
    if (koniec == p || liczba > UINT32_MAX)
        return 0U;
    *wartosc = (uint32_t)liczba;
    return 1U;
}

static uint8_t ANTENY_ProfilJestPoprawny(const ANTENA_PROFIL_t *profil)
{
    uint8_t i;
    const uint8_t liczba = ANTENA_BAZA_LiczbaWymiarow(profil);
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    if (profil == NULL || liczba == 0U)
        return 0U;
    if (profil->czestotliwosc_docelowa_hz < fmin ||
        profil->czestotliwosc_docelowa_hz > fmax)
        return 0U;

    for (i = 0U; i < liczba; ++i)
    {
        if (profil->wymiary_mm[i] < 10U || profil->wymiary_mm[i] > 200000U)
            return 0U;
    }

    if (profil->typ == ANTENA_TYP_W3DZZ)
    {
        if (profil->trap_czestotliwosc_hz < 1000000U || profil->trap_czestotliwosc_hz > 60000000U ||
            profil->trap_indukcyjnosc_nh < 100U || profil->trap_indukcyjnosc_nh > 100000U ||
            profil->trap_pojemnosc_pf_x10 < 10U || profil->trap_pojemnosc_pf_x10 > 10000U)
            return 0U;
    }

    if (profil->poprzedni_wymiar_strojony_mm > 800000U)
        return 0U;
    if (profil->poprzedni_rezonans_hz != 0U &&
        (profil->poprzedni_rezonans_hz < fmin || profil->poprzedni_rezonans_hz > fmax))
        return 0U;

    return 1U;
}

static uint8_t ANTENY_WczytajProfil(void)
{
    FIL plik;
    char bufor[768];
    UINT odczytano = 0U;
    ANTENA_PROFIL_t profil;
    uint32_t typ;
    uint32_t wartosc;
    uint8_t i;

    if (!ANTENY_ZapewnijKatalog())
        return 0U;
    if (f_open(&plik, ANTENY_PROFIL_PLIK, FA_READ) != FR_OK)
        return 0U;
    if (f_read(&plik, bufor, (UINT)(sizeof(bufor) - 1U), &odczytano) != FR_OK)
    {
        f_close(&plik);
        return 0U;
    }
    f_close(&plik);
    bufor[odczytano] = '\0';

    if (strncmp(bufor, ANTENY_MAGIC, strlen(ANTENY_MAGIC)) != 0)
        return 0U;

    memset(&profil, 0, sizeof(profil));
    if (!ANTENY_PobierzU32(bufor, "wersja=", &profil.wersja) ||
        profil.wersja != ANTENA_BAZA_WERSJA_PROFILU ||
        !ANTENY_PobierzU32(bufor, "typ=", &typ) || typ >= ANTENA_TYP_LICZBA ||
        !ANTENY_PobierzU32(bufor, "cel_hz=", &profil.czestotliwosc_docelowa_hz))
        return 0U;

    profil.typ = (ANTENA_TYP_t)typ;
    for (i = 0U; i < ANTENA_BAZA_MAX_WYMIAROW; ++i)
    {
        char klucz[16];
        snprintf(klucz, sizeof(klucz), "w%u_mm=", (unsigned)i);
        if (ANTENY_PobierzU32(bufor, klucz, &wartosc))
            profil.wymiary_mm[i] = wartosc;
    }
    (void)ANTENY_PobierzU32(bufor, "trap_hz=", &profil.trap_czestotliwosc_hz);
    (void)ANTENY_PobierzU32(bufor, "trap_l_nh=", &profil.trap_indukcyjnosc_nh);
    (void)ANTENY_PobierzU32(bufor, "trap_c_pf_x10=", &profil.trap_pojemnosc_pf_x10);
    (void)ANTENY_PobierzU32(bufor, "hist_wymiar_mm=", &profil.poprzedni_wymiar_strojony_mm);
    (void)ANTENY_PobierzU32(bufor, "hist_rezonans_hz=", &profil.poprzedni_rezonans_hz);

    if (!ANTENY_ProfilJestPoprawny(&profil))
        return 0U;

    g_antena = profil;
    g_ma_profil = 1U;
    g_ma_pomiar = 0U;
    memset(&g_ostatni_pomiar, 0, sizeof(g_ostatni_pomiar));
    memset(&g_ostatnia_korekta, 0, sizeof(g_ostatnia_korekta));
    g_twin_ma_pomiar = 0U;
    return 1U;
}

static UI_STYL_t ANTENY_StylRoznicy(float proc)
{
    const float a = fabsf(proc);
    if (a <= 1.0f) return UI_STYL_AKTYWNY;
    if (a <= 3.0f) return UI_STYL_AKCENT;
    return UI_STYL_OSTRZEZENIE;
}

static void ANTENY_WyczyscHistorieRegulacji(void)
{
    g_antena.poprzedni_wymiar_strojony_mm = 0U;
    g_antena.poprzedni_rezonans_hz = 0U;
}

static uint8_t ANTENY_CzyWymiarStrojony(uint8_t indeks)
{
    switch (g_antena.typ)
    {
    case ANTENA_TYP_DIPOL_POLFALOWY:
    case ANTENA_TYP_EFHW_40_10:
    case ANTENA_TYP_QUAD_1EL:
    case ANTENA_TYP_QUAD_2EL:
    case ANTENA_TYP_QUAD_3EL:
        return indeks == 0U ? 1U : 0U;

    case ANTENA_TYP_W3DZZ:
        if (g_antena.czestotliwosc_docelowa_hz >= 2800000U &&
            g_antena.czestotliwosc_docelowa_hz <= 4200000U)
            return indeks == 1U ? 1U : 0U; /* na 80 m regulujemy końce zewnętrzne */
        if (g_antena.czestotliwosc_docelowa_hz >= 6500000U &&
            g_antena.czestotliwosc_docelowa_hz <= 7800000U)
            return indeks == 0U ? 1U : 0U; /* na 40 m przede wszystkim odcinki wewnętrzne */
        return 0U;

    default:
        return 0U;
    }
}

static void ANTENY_UtworzNowy(ANTENA_TYP_t typ)
{
    ANTENA_BAZA_InicjalizujProfil(&g_antena, typ, ANTENY_DomyslnyCel(typ));
    g_ma_profil = 1U;
    g_ma_pomiar = 0U;
    memset(&g_ostatni_pomiar, 0, sizeof(g_ostatni_pomiar));
    memset(&g_ostatnia_korekta, 0, sizeof(g_ostatnia_korekta));
    g_twin_ma_pomiar = 0U;
    (void)ANTENY_ZapiszProfil();
}



static UI_IKONA_MENU_t ANTENY_IkonaTypu(uint8_t indeks)
{
    /*
     * Każdy typ anteny ma własny symbol odwołujący się do rzeczywistej
     * geometrii konstrukcji. Nie używamy już ikon zastępczych Smith/SWR.
     */
    switch ((ANTENA_TYP_t)indeks)
    {
    case ANTENA_TYP_DIPOL_POLFALOWY:  return UI_IKONA_ANT_DIPOL;
    case ANTENA_TYP_W3DZZ:            return UI_IKONA_ANT_W3DZZ;
    case ANTENA_TYP_QUAD_1EL:         return UI_IKONA_ANT_QUAD_1;
    case ANTENA_TYP_QUAD_2EL:         return UI_IKONA_ANT_QUAD_2;
    case ANTENA_TYP_QUAD_3EL:         return UI_IKONA_ANT_QUAD_3;
    case ANTENA_TYP_TWINYAGI_SP2XDQ: return UI_IKONA_ANT_TWINYAGI;
    case ANTENA_TYP_YAGI3_DK7ZB_2M:  return UI_IKONA_ANT_YAGI3;
    case ANTENA_TYP_EFHW_40_10:       return UI_IKONA_ANT_EFHW;
    default:                           return UI_IKONA_PROJEKTOWANIE_ANTEN;
    }
}

static const char *ANTENY_EtykietaKafla(uint8_t indeks)
{
    return indeks < (uint8_t)ANTENA_TYP_LICZBA
        ? ANTENY_NazwaTypu((ANTENA_TYP_t)indeks) : "?";
}

#define ANTENY_WYBOR_NA_STRONE       UI_SIATKA_KOMPAKT_NA_STRONE
#define ANTENY_WYBOR_LICZBA_STRON    (((uint8_t)ANTENA_TYP_LICZBA + ANTENY_WYBOR_NA_STRONE - 1U) / ANTENY_WYBOR_NA_STRONE)
#define ANTENY_WYBOR_FOKUS_SD        ((uint8_t)ANTENA_TYP_LICZBA)
#define ANTENY_WYBOR_LICZBA_FOKUSOW  ((uint8_t)ANTENA_TYP_LICZBA + 1U)

static uint8_t g_anteny_wybor_strona = 0U;

static void ANTENY_RysujWybor(uint8_t fokus)
{
    uint8_t lokalny;
    const uint8_t pierwszy = (uint8_t)(g_anteny_wybor_strona * ANTENY_WYBOR_NA_STRONE);
    const uint8_t pozostalo = (uint8_t)ANTENA_TYP_LICZBA > pierwszy
                            ? (uint8_t)ANTENA_TYP_LICZBA - pierwszy : 0U;
    const uint8_t liczba = pozostalo > ANTENY_WYBOR_NA_STRONE ? ANTENY_WYBOR_NA_STRONE : pozostalo;
    const UI_PROSTOKAT_t poprzednia = UI_ObszarPrzyciskuDolnego(1U);
    const UI_PROSTOKAT_t nastepna = UI_ObszarPrzyciskuDolnego(2U);
    const UI_PROSTOKAT_t wczytaj = UI_ObszarPrzyciskuDolnego(3U);
    char tytul[72];

    if (ANTENY_WYBOR_LICZBA_STRON > 1U)
        snprintf(tytul, sizeof(tytul), "%s  %u/%u",
                 ANT_T("Projektowanie anten", "Antenna design", "Antennenentwurf", "Проектирование антенн"),
                 (unsigned)(g_anteny_wybor_strona + 1U), (unsigned)ANTENY_WYBOR_LICZBA_STRON);
    else
        snprintf(tytul, sizeof(tytul), "%s",
                 ANT_T("Projektowanie anten", "Antenna design", "Antennenentwurf", "Проектирование антенн"));

    UI_WyczyscEkran();
    UI_RysujPasekGorny(tytul, true, false, 0);

    for (lokalny = 0U; lokalny < liczba; ++lokalny)
    {
        const uint8_t indeks = (uint8_t)(pierwszy + lokalny);
        UI_RysujKafelKompaktowy(lokalny, ANTENY_IkonaTypu(indeks), ANTENY_EtykietaKafla(indeks),
                                fokus == indeks, true);
    }

    UI_RysujWsteczDolny(false);
    UI_RysujPrzycisk(poprzednia.x, poprzednia.y, poprzednia.szerokosc, poprzednia.wysokosc,
                     "<", g_anteny_wybor_strona > 0U ? UI_STYL_NORMALNY : UI_STYL_NIEAKTYWNY, FONT_FRAN);
    UI_RysujPrzycisk(nastepna.x, nastepna.y, nastepna.szerokosc, nastepna.wysokosc,
                     ">", (uint8_t)(g_anteny_wybor_strona + 1U) < ANTENY_WYBOR_LICZBA_STRON ? UI_STYL_NORMALNY : UI_STYL_NIEAKTYWNY, FONT_FRAN);
    UI_RysujPrzycisk(wczytaj.x, wczytaj.y, wczytaj.szerokosc, wczytaj.wysokosc,
                     ANT_T("Wczytaj z SD", "Load from SD", "Von SD laden", "Загрузить с SD"),
                     CFG_CzyKartaSDDostepna() ? (fokus == ANTENY_WYBOR_FOKUS_SD ? UI_STYL_AKCENT : UI_STYL_NORMALNY)
                                             : UI_STYL_NIEAKTYWNY,
                     FONT_FRAN);
}

static int16_t ANTENY_KafelPoDotyku(LCDPoint punkt)
{
    const uint8_t pierwszy = (uint8_t)(g_anteny_wybor_strona * ANTENY_WYBOR_NA_STRONE);
    const uint8_t pozostalo = (uint8_t)ANTENA_TYP_LICZBA > pierwszy
                            ? (uint8_t)ANTENA_TYP_LICZBA - pierwszy : 0U;
    const uint8_t liczba = pozostalo > ANTENY_WYBOR_NA_STRONE ? ANTENY_WYBOR_NA_STRONE : pozostalo;
    const int16_t lokalny = UI_KafelKompaktowyPoDotyku(punkt, liczba);
    return lokalny >= 0 ? (int16_t)(pierwszy + (uint8_t)lokalny) : -1;
}

static uint8_t ANTENY_NastepnyFokus(uint8_t fokus, int8_t kierunek)
{
    uint8_t proba;
    for (proba = 0U; proba < ANTENY_WYBOR_LICZBA_FOKUSOW; ++proba)
    {
        if (kierunek > 0)
            fokus = (uint8_t)((fokus + 1U) % ANTENY_WYBOR_LICZBA_FOKUSOW);
        else
            fokus = (uint8_t)((fokus + ANTENY_WYBOR_LICZBA_FOKUSOW - 1U) % ANTENY_WYBOR_LICZBA_FOKUSOW);

        if (fokus != ANTENY_WYBOR_FOKUS_SD || CFG_CzyKartaSDDostepna())
        {
            if (fokus < (uint8_t)ANTENA_TYP_LICZBA)
                g_anteny_wybor_strona = (uint8_t)(fokus / ANTENY_WYBOR_NA_STRONE);
            return fokus;
        }
    }
    return 0U;
}

static int16_t ANTENY_WybierzTyp(void)
{
    uint8_t fokus = 0U;

    g_anteny_wybor_strona = 0U;
    ANTENY_RysujWybor(fokus);
    WEJSCIA_WyczyscZdarzenia();
    while (TOUCH_IsPressed()) Sleep(10U);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;

        if (TOUCH_Poll(&punkt))
        {
            int16_t indeks;
            const UI_PROSTOKAT_t poprzednia = UI_ObszarPrzyciskuDolnego(1U);
            const UI_PROSTOKAT_t nastepna = UI_ObszarPrzyciskuDolnego(2U);
            const UI_PROSTOKAT_t wczytaj = UI_ObszarPrzyciskuDolnego(3U);
            if (UI_CzyDotknietoWstecz(punkt))
            {
                TOUCH_CzekajNaPuszczenie(35U);
                return -1;
            }
            if (g_anteny_wybor_strona > 0U && UI_CzyPunktWObszarze(punkt, &poprzednia))
            {
                --g_anteny_wybor_strona;
                fokus = (uint8_t)(g_anteny_wybor_strona * ANTENY_WYBOR_NA_STRONE);
                TOUCH_CzekajNaPuszczenie(35U);
                ANTENY_RysujWybor(fokus);
                continue;
            }
            if ((uint8_t)(g_anteny_wybor_strona + 1U) < ANTENY_WYBOR_LICZBA_STRON &&
                UI_CzyPunktWObszarze(punkt, &nastepna))
            {
                ++g_anteny_wybor_strona;
                fokus = (uint8_t)(g_anteny_wybor_strona * ANTENY_WYBOR_NA_STRONE);
                TOUCH_CzekajNaPuszczenie(35U);
                ANTENY_RysujWybor(fokus);
                continue;
            }
            if (CFG_CzyKartaSDDostepna() && UI_CzyPunktWObszarze(punkt, &wczytaj))
            {
                TOUCH_CzekajNaPuszczenie(35U);
                return 100;
            }
            indeks = ANTENY_KafelPoDotyku(punkt);
            if (indeks >= 0)
            {
                TOUCH_CzekajNaPuszczenie(35U);
                return indeks;
            }
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            return -1;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
            zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            fokus = ANTENY_NastepnyFokus(
                fokus, zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? 1 : -1);
            ANTENY_RysujWybor(fokus);
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            if (fokus == ANTENY_WYBOR_FOKUS_SD)
            {
                if (CFG_CzyKartaSDDostepna())
                    return 100;
            }
            else
                return (int16_t)fokus;
        }
        Sleep(10U);
    }
}

static void ANTENY_ZmienCel(void)
{
    ANTENA_ZALECENIA_t stare;
    ANTENA_ZALECENIA_t nowe;
    uint32_t nowy_hz = g_antena.czestotliwosc_docelowa_hz;
    uint8_t i;

    if (g_antena.typ == ANTENA_TYP_TWINYAGI_SP2XDQ)
    {
        KOMUNIKAT_PokazTekst(ANT_T("TwinYagi SP2XDQ", "TwinYagi SP2XDQ", "TwinYagi SP2XDQ", "TwinYagi SP2XDQ"),
                             ANT_T("Ten profil odwzorowuje oryginalny projekt 145/435 MHz. Częstotliwości 145.5 i 435 MHz są stałe; program nie skaluje automatycznie geometrii dwupasmowej.",
                                   "This profile reproduces the original 145/435 MHz design. 145.5 and 435 MHz are fixed; the program does not automatically scale the dual-band geometry.",
                                   "Dieses Profil bildet den originalen 145/435-MHz-Entwurf ab. 145,5 und 435 MHz sind fest; die Zweiband-Geometrie wird nicht automatisch skaliert.",
                                   "Профиль повторяет исходный проект 145/435 МГц. Частоты 145,5 и 435 МГц фиксированы; геометрия двухдиапазонной антенны автоматически не масштабируется."));
        return;
    }
    if (g_antena.typ == ANTENA_TYP_YAGI3_DK7ZB_2M)
    {
        KOMUNIKAT_PokazTekst(ANT_T("Yagi DK7ZB 2 m", "DK7ZB Yagi 2 m", "DK7ZB-Yagi 2 m", "Yagi DK7ZB 2 м"),
                             ANT_T("To jest konkretny projekt 3-elementowy dla 2 m, 50 Ohm i elementów 6 mm. Profil pozostaje przy 145 MHz; nie skaluje automatycznie geometrii.",
                                   "This is a specific 3-element 2 m, 50 Ohm design for 6 mm elements. The profile stays at 145 MHz and is not automatically scaled.",
                                   "Dies ist ein konkreter 3-Element-Entwurf fuer 2 m, 50 Ohm und 6-mm-Elemente. Das Profil bleibt bei 145 MHz und wird nicht automatisch skaliert.",
                                   "Это конкретный 3-элементный проект на 2 м, 50 Ом, элементы 6 мм. Профиль остаётся на 145 МГц и автоматически не масштабируется."));
        return;
    }

    ANTENA_BAZA_ObliczZalecenia(&g_antena, &stare);
    if (!UI_EdytujCzestotliwoscHzEx(nowy_hz,
                                     CFG_GetParam(CFG_PARAM_BAND_FMIN),
                                     CFG_GetParam(CFG_PARAM_BAND_FMAX),
                                     1000U,
                                     ANT_T("Częstotliwość docelowa", "Target frequency",
                                           "Zielfrequenz", "Целевая частота"),
                                     &nowy_hz) ||
        nowy_hz == g_antena.czestotliwosc_docelowa_hz)
        return;

    g_antena.czestotliwosc_docelowa_hz = nowy_hz;
    ANTENA_BAZA_ObliczZalecenia(&g_antena, &nowe);

    /* W świeżym profilu wymiary nadal są wartościami książkowymi. Wtedy
       skalujemy je razem z częstotliwością. Wymiar wpisany przez użytkownika
       pozostaje nietknięty, aby zmiana celu nie fałszowała istniejącej anteny. */
    for (i = 0U; i < stare.liczba && i < nowe.liczba; ++i)
    {
        if (g_antena.wymiary_mm[i] == stare.wymiary[i].zalecane_mm)
            g_antena.wymiary_mm[i] = nowe.wymiary[i].zalecane_mm;
    }

    ANTENY_WyczyscHistorieRegulacji();
    g_ma_pomiar = 0U;
    (void)ANTENY_ZapiszProfil();
}

static uint8_t ANTENY_LiczbaWierszyWymiarow(const ANTENA_ZALECENIA_t *zalecenia)
{
    if (zalecenia == NULL)
        return 0U;
    return (uint8_t)(zalecenia->liczba + (g_antena.typ == ANTENA_TYP_W3DZZ ? 3U : 0U));
}

static void ANTENY_RysujWymiary(UI_LISTA_STAN_t *stan)
{
    ANTENA_ZALECENIA_t zalecenia;
    UI_WIERSZ_LISTY_t wiersze[ANTENA_BAZA_MAX_WYMIAROW];
    char wartosci[ANTENA_BAZA_MAX_WYMIAROW][72];
    uint8_t i;
    uint8_t liczba;

    memset(wiersze, 0, sizeof(wiersze));
    ANTENA_BAZA_ObliczZalecenia(&g_antena, &zalecenia);
    liczba = ANTENY_LiczbaWierszyWymiarow(&zalecenia);

    for (i = 0U; i < zalecenia.liczba; ++i)
    {
        const ANTENA_WYMIAR_ZALECANY_t *z = &zalecenia.wymiary[i];
        if (z->ma_zakres)
        {
            snprintf(wartosci[i], sizeof(wartosci[i]), "%lu mm | zal. %lu (%lu-%lu)",
                     (unsigned long)g_antena.wymiary_mm[i],
                     (unsigned long)z->zalecane_mm,
                     (unsigned long)z->minimum_mm, (unsigned long)z->maksimum_mm);
            wiersze[i].styl = (g_antena.wymiary_mm[i] >= z->minimum_mm &&
                               g_antena.wymiary_mm[i] <= z->maksimum_mm)
                              ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE;
        }
        else
        {
            const float roznica = ANTENA_BAZA_RoznicaProcent(g_antena.wymiary_mm[i],
                                                              z->zalecane_mm);
            snprintf(wartosci[i], sizeof(wartosci[i]), "%lu / %lu mm  %+.1f%%",
                     (unsigned long)g_antena.wymiary_mm[i],
                     (unsigned long)z->zalecane_mm, (double)roznica);
            wiersze[i].styl = ANTENY_StylRoznicy(roznica);
        }
        wiersze[i].id = i;
        wiersze[i].tekst = ANTENY_NazwaPola(z->pole);
        wiersze[i].wartosc = wartosci[i];
        wiersze[i].typ = UI_WIERSZ_Z_WARTOSCIA;
        wiersze[i].aktywny = true;
    }

    if (g_antena.typ == ANTENA_TYP_W3DZZ)
    {
        const uint8_t b = zalecenia.liczba;
        const float roznica_f = ANTENA_BAZA_RoznicaProcent(g_antena.trap_czestotliwosc_hz, 7200000U);
        const float roznica_l = ANTENA_BAZA_RoznicaProcent(g_antena.trap_indukcyjnosc_nh, 8200U);
        const float roznica_c = ANTENA_BAZA_RoznicaProcent(g_antena.trap_pojemnosc_pf_x10, 600U);

        snprintf(wartosci[b], sizeof(wartosci[b]), "%.3f / 7.200 MHz  %+.1f%%",
                 (double)g_antena.trap_czestotliwosc_hz / 1000000.0, (double)roznica_f);
        wiersze[b].id = b;
        wiersze[b].tekst = ANT_T("Rezonans pułapki", "Trap resonance", "Sperrkreisresonanz", "Резонанс трапа");
        wiersze[b].wartosc = wartosci[b];
        wiersze[b].styl = ANTENY_StylRoznicy(roznica_f);
        wiersze[b].typ = UI_WIERSZ_Z_WARTOSCIA;
        wiersze[b].aktywny = true;

        snprintf(wartosci[b + 1U], sizeof(wartosci[b + 1U]), "%.3f / 8.200 uH  %+.1f%%",
                 (double)g_antena.trap_indukcyjnosc_nh / 1000.0, (double)roznica_l);
        wiersze[b + 1U].id = b + 1U;
        wiersze[b + 1U].tekst = "L trap";
        wiersze[b + 1U].wartosc = wartosci[b + 1U];
        wiersze[b + 1U].styl = ANTENY_StylRoznicy(roznica_l);
        wiersze[b + 1U].typ = UI_WIERSZ_Z_WARTOSCIA;
        wiersze[b + 1U].aktywny = true;

        snprintf(wartosci[b + 2U], sizeof(wartosci[b + 2U]), "%.1f / 60.0 pF  %+.1f%%",
                 (double)g_antena.trap_pojemnosc_pf_x10 / 10.0, (double)roznica_c);
        wiersze[b + 2U].id = b + 2U;
        wiersze[b + 2U].tekst = "C trap";
        wiersze[b + 2U].wartosc = wartosci[b + 2U];
        wiersze[b + 2U].styl = ANTENY_StylRoznicy(roznica_c);
        wiersze[b + 2U].typ = UI_WIERSZ_Z_WARTOSCIA;
        wiersze[b + 2U].aktywny = true;
    }

    UI_WyczyscEkran();
    UI_RysujPasekGorny(ANT_T("Wymiary: moje / literatura", "Dimensions: mine / reference", "Maße: Ist / Literatur", "Размеры: мои / литература"), true, false, 0);
    UI_RysujListe(ANTENY_LISTA_X, ANTENY_LISTA_Y, ANTENY_LISTA_W, ANTENY_LISTA_H,
                  wiersze, liczba, stan);
}

static void ANTENY_EdytujWymiary(void)
{
    ANTENA_ZALECENIA_t zalecenia;
    UI_LISTA_STAN_t stan;
    uint8_t aktywne[ANTENA_BAZA_MAX_WYMIAROW] = {1U, 1U, 1U, 1U, 1U};
    uint8_t liczba;

    ANTENA_BAZA_ObliczZalecenia(&g_antena, &zalecenia);
    liczba = ANTENY_LiczbaWierszyWymiarow(&zalecenia);
    UI_LISTA_Init(&stan, liczba, ANTENY_WIDOCZNE);
    ANTENY_RysujWymiary(&stan);
    WEJSCIA_WyczyscZdarzenia();
    while (TOUCH_IsPressed()) Sleep(10U);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int16_t indeks = -1;

        if (TOUCH_Poll(&punkt))
        {
            if (UI_CzyDotknietoWstecz(punkt))
            {
                TOUCH_CzekajNaPuszczenie(35U);
                break;
            }
            indeks = UI_ListaIndeksPoDotyku(punkt, ANTENY_LISTA_X, ANTENY_LISTA_Y,
                                            ANTENY_LISTA_W, ANTENY_LISTA_H, &stan);
            if (indeks >= 0)
                TOUCH_CzekajNaPuszczenie(35U);
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
            zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            if (UI_LISTA_PrzesunFokus(&stan,
                                      zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? 1 : -1,
                                      aktywne))
                ANTENY_RysujWymiary(&stan);
            continue;
        }
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK && stan.fokus_widoczny)
            indeks = (int16_t)stan.zaznaczony;

        if (indeks >= 0 && (uint16_t)indeks < liczba)
        {
            uint32_t nowy = 0U;
            uint8_t zmieniono = 0U;

            if ((uint16_t)indeks < zalecenia.liczba)
            {
                const char *nazwa = ANTENY_NazwaPola(zalecenia.wymiary[indeks].pole);
                nowy = UI_EdytujLiczbePolami(g_antena.wymiary_mm[indeks], 10U, 200000U,
                                                   nazwa, "mm");
                if (nowy >= 10U && nowy != g_antena.wymiary_mm[indeks])
                {
                    /* Historia lokalnej czułości jest wiarygodna tylko wtedy,
                       gdy między dwoma pomiarami zmieniono ten wymiar, który
                       model rzeczywiście stroi. Zmiana reflektora, direktora
                       lub odstępu unieważnia tę prostą zależność. */
                    if (!ANTENY_CzyWymiarStrojony((uint8_t)indeks))
                        ANTENY_WyczyscHistorieRegulacji();
                    g_antena.wymiary_mm[indeks] = nowy;
                    zmieniono = 1U;
                }
            }
            else if (g_antena.typ == ANTENA_TYP_W3DZZ)
            {
                const uint8_t pole = (uint8_t)((uint16_t)indeks - zalecenia.liczba);
                if (pole == 0U)
                {
                    {
                        uint32_t nowy_hz = g_antena.trap_czestotliwosc_hz;
                        if (UI_EdytujCzestotliwoscHzEx(nowy_hz, 1000000U, 60000000U,
                                                        1000U,
                                                        ANT_T("Rezonans pułapki", "Trap resonance",
                                                              "Sperrkreis", "Резонанс трапа"),
                                                        &nowy_hz) &&
                            nowy_hz != g_antena.trap_czestotliwosc_hz)
                        {
                            g_antena.trap_czestotliwosc_hz = nowy_hz;
                            zmieniono = 1U;
                        }
                    }
                }
                else if (pole == 1U)
                {
                    nowy = UI_EdytujLiczbePolami(g_antena.trap_indukcyjnosc_nh,
                                                       100U, 100000U,
                                                       ANT_T("Indukcyjność pułapki", "Trap inductance", "Sperrkreis-L", "Индуктивность трапа"),
                                                       "nH");
                    if (nowy != 0U && nowy != g_antena.trap_indukcyjnosc_nh)
                    {
                        g_antena.trap_indukcyjnosc_nh = nowy;
                        zmieniono = 1U;
                    }
                }
                else if (pole == 2U)
                {
                    nowy = UI_EdytujLiczbePolami((g_antena.trap_pojemnosc_pf_x10 + 5U) / 10U,
                                                       1U, 1000U,
                                                       ANT_T("Pojemność pułapki", "Trap capacitance", "Sperrkreis-C", "Ёмкость трапа"),
                                                       "pF");
                    if (nowy != 0U && nowy * 10U != g_antena.trap_pojemnosc_pf_x10)
                    {
                        g_antena.trap_pojemnosc_pf_x10 = nowy * 10U;
                        zmieniono = 1U;
                    }
                }

                if (zmieniono)
                {
                    /* Zmiana pułapki zmienia model elektryczny W3DZZ, więc
                       poprzednia zależność częstotliwość/długość już nie jest
                       dobrym punktem odniesienia. */
                    ANTENY_WyczyscHistorieRegulacji();
                }
            }

            if (zmieniono)
            {
                g_ma_pomiar = 0U;
                (void)ANTENY_ZapiszProfil();
            }
            WEJSCIA_WyczyscZdarzenia();
            ANTENY_RysujWymiary(&stan);
        }
        Sleep(10U);
    }
}

/*
 * test12: ekran profilu anteny jest wyłącznie nawigacją. Dane konstrukcyjne
 * nie są już wciskane pomiędzy ramki i małe przyciski. Każdy temat ma osobny
 * ekran, dzięki czemu tekst pozostaje czytelny także na fizycznym LCD 480x272.
 */
#define ANTENY_LICZBA_AKCJI UI_SIATKA_KOMPAKT_NA_STRONE

static int16_t ANTENY_AkcjaPoDotyku(LCDPoint punkt)
{
    return UI_KafelKompaktowyPoDotyku(punkt, ANTENY_LICZBA_AKCJI);
}

static const char *ANTENY_NazwaEkranuBudowy(void)
{
    switch (g_antena.typ)
    {
    case ANTENA_TYP_DIPOL_POLFALOWY:
        return ANT_T("Ramiona / montaż", "Arms / assembly", "Schenkel / Aufbau", "Плечи / монтаж");
    case ANTENA_TYP_W3DZZ:
        return ANT_T("Pułapki / pasma", "Traps / bands", "Sperrkreise / Bänder", "Трапы / диапазоны");
    case ANTENA_TYP_QUAD_1EL:
        return ANT_T("Pętla zasilana", "Driven loop", "Strahlerschleife", "Активная рамка");
    case ANTENA_TYP_QUAD_2EL:
    case ANTENA_TYP_QUAD_3EL:
        return ANT_T("Elementy / odstępy", "Elements / spacing", "Elemente / Abstände", "Элементы / зазоры");
    case ANTENA_TYP_TWINYAGI_SP2XDQ:
        return ANT_T("Elementy / balun", "Elements / balun", "Elemente / Balun", "Элементы / балун");
    case ANTENA_TYP_YAGI3_DK7ZB_2M:
        return ANT_T("Elementy / zasilanie", "Elements / feed", "Elemente / Speisung", "Элементы / питание");
    case ANTENA_TYP_EFHW_40_10:
        return ANT_T("Transformator 49:1", "49:1 transformer", "49:1 Transformator", "Трансформатор 49:1");
    default:
        return ANT_T("Budowa", "Construction", "Aufbau", "Конструкция");
    }
}

static void ANTENY_RysujMenuProfilu(uint8_t fokus)
{
    char cel[40];
    uint8_t i;
    static const UI_IKONA_MENU_t ikony[ANTENY_LICZBA_AKCJI] =
    {
        UI_IKONA_SZUKAJ_F,
        UI_IKONA_USTAWIENIA,
        UI_IKONA_KWARC,
        UI_IKONA_TDR,
        UI_IKONA_POJEDYNCZY,
        UI_IKONA_ZRZUTY
    };
    const char *etykiety[ANTENY_LICZBA_AKCJI];

    if (g_antena.typ == ANTENA_TYP_TWINYAGI_SP2XDQ)
        snprintf(cel, sizeof(cel), "145.5 / 435 MHz");
    else
        UI_FormatujCzestotliwoscMHzKrotko(g_antena.czestotliwosc_docelowa_hz, cel, sizeof(cel));

    etykiety[0] = ANT_T("Cel", "Target", "Ziel", "Цель");
    etykiety[1] = ANT_T("Wymiary", "Dimensions", "Maße", "Размеры");
    etykiety[2] = g_antena.typ == ANTENA_TYP_TWINYAGI_SP2XDQ
                 ? ANT_T("Projekt SP2XDQ", "SP2XDQ design", "SP2XDQ-Entwurf", "Проект SP2XDQ")
                 : g_antena.typ == ANTENA_TYP_YAGI3_DK7ZB_2M
                 ? ANT_T("Projekt DK7ZB", "DK7ZB design", "DK7ZB-Entwurf", "Проект DK7ZB")
                 : g_antena.typ == ANTENA_TYP_EFHW_40_10
                 ? ANT_T("Projekt EFHW", "EFHW design", "EFHW-Entwurf", "Проект EFHW")
                 : ANT_T("Literatura", "Reference", "Literatur", "Литература");
    etykiety[3] = ANTENY_NazwaEkranuBudowy();
    etykiety[4] = g_antena.typ == ANTENA_TYP_TWINYAGI_SP2XDQ
                 ? ANT_T("Pomiar 2 m / 70 cm", "Measure 2 m / 70 cm", "Messung 2 m / 70 cm", "Измерение 2 м / 70 см")
                 : ANT_T("Pomiar / strojenie", "Measure / tune", "Messen / Abstimmen", "Измерение / настройка");
    etykiety[5] = ANT_T("Raport", "Report", "Bericht", "Отчёт");

    UI_WyczyscEkran();
    UI_RysujPasekGorny(ANTENY_NazwaTypu(g_antena.typ), true, false, 0);

    for (i = 0U; i < ANTENY_LICZBA_AKCJI; ++i)
    {
        if (i == 0U)
        {
            const UI_PROSTOKAT_t obszar = UI_ObszarKaflaKompaktowego(i);
            UI_RysujKafelMenuZWartoscia(obszar.x, obszar.y, obszar.szerokosc, obszar.wysokosc,
                                        ikony[i], etykiety[i], cel, fokus == i);
        }
        else
        {
            UI_RysujKafelKompaktowy(i, ikony[i], etykiety[i], fokus == i, true);
        }
    }
}

static void ANTENY_CzekajNaWstecz(void)
{
    while (TOUCH_IsPressed()) Sleep(10U);
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        LCDPoint punkt;
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ || zdarzenie == WEJSCIE_ZDARZENIE_OK)
            break;
        if (TOUCH_Poll(&punkt) && UI_CzyDotknietoWstecz(punkt))
        {
            TOUCH_CzekajNaPuszczenie(35U);
            break;
        }
        Sleep(10U);
    }
    WEJSCIA_WyczyscZdarzenia();
}

#define ANTENY_SZCZEGOL_Y0       38U
#define ANTENY_SZCZEGOL_DY       36U
#define ANTENY_SZCZEGOL_WYSOKOSC 34U
#define ANTENY_SZCZEGOL_LICZBA    5U

static void ANTENY_RysujWierszSzczegolu(uint8_t indeks, const char *etykieta,
                                        const char *wartosc, UI_STYL_t styl)
{
    const uint16_t y = (uint16_t)(ANTENY_SZCZEGOL_Y0 + indeks * ANTENY_SZCZEGOL_DY);

    if (indeks >= ANTENY_SZCZEGOL_LICZBA)
        return;

    /* Pięć wierszy kończy się na y=215, więc stały pasek od y=220 pozostaje
     * całkowicie wolny dla Wstecz i innych akcji. */
    UI_RysujPoleStatusu(8U, y, 464U, ANTENY_SZCZEGOL_WYSOKOSC, etykieta, wartosc, styl);
}

static void ANTENY_RysujLiterature(void)
{
    ANTENA_ZALECENIA_t zal;
    char a[96], b[96], c[96], d[96], e[96];

    ANTENA_BAZA_ObliczZalecenia(&g_antena, &zal);
    UI_WyczyscEkran();
    UI_RysujPasekGorny(ANT_T("Literatura anteny", "Antenna reference", "Antennenliteratur", "Справочник антенн"),
                       true, false, 0);

    switch (g_antena.typ)
    {
    case ANTENA_TYP_DIPOL_POLFALOWY:
        snprintf(a, sizeof(a), "L = 143 / f  [m, MHz]");
        snprintf(b, sizeof(b), "%lu mm", (unsigned long)zal.wymiary[0].zalecane_mm);
        snprintf(c, sizeof(c), "%.1f mm", (double)zal.wymiary[0].zalecane_mm / 2.0);
        snprintf(d, sizeof(d), "%s", ANT_T("Stała empiryczna uwzględnia efekt końcowy.",
                                             "Empirical constant includes end effect.",
                                             "Empirische Konstante berücksichtigt Endeffekt.",
                                             "Эмпирическая константа учитывает концевой эффект."));
        snprintf(e, sizeof(e), "%s", ANT_T("Średnica, izolacja i otoczenie zmieniają wynik.",
                                             "Diameter, insulation and surroundings shift the result.",
                                             "Durchmesser, Isolation und Umgebung beeinflussen das Ergebnis.",
                                             "Диаметр, изоляция и окружение влияют на результат."));
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Wzór", "Formula", "Formel", "Формула"), a, UI_STYL_AKCENT);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Długość książkowa", "Reference length", "Literaturlänge", "Расчётная длина"), b, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Jedno ramię", "One arm", "Ein Schenkel", "Одно плечо"), c, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Założenie", "Assumption", "Annahme", "Допущение"), d, UI_STYL_NIEAKTYWNY);
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Uwaga", "Note", "Hinweis", "Примечание"), e, UI_STYL_OSTRZEZENIE);
        break;

    case ANTENA_TYP_W3DZZ:
        snprintf(a, sizeof(a), "%s", ANT_T("Na ramię: 9700 mm + 6700 mm", "Per arm: 9700 mm + 6700 mm",
                                             "Je Schenkel: 9700 mm + 6700 mm", "На плечо: 9700 мм + 6700 мм"));
        snprintf(b, sizeof(b), "7.200 MHz");
        snprintf(c, sizeof(c), "L = 8.2 uH");
        snprintf(d, sizeof(d), "C = 60.0 pF");
        snprintf(e, sizeof(e), "%s", ANT_T("Zmiana jednego odcinka może wpływać na kilka pasm.",
                                             "Changing one section can affect several bands.",
                                             "Eine Abschnittsänderung kann mehrere Bänder beeinflussen.",
                                             "Изменение одного участка влияет на несколько диапазонов."));
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Wymiary bazowe", "Base dimensions", "Grundmaße", "Базовые размеры"), a, UI_STYL_AKCENT);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Pułapka", "Trap", "Sperrkreis", "Трап"), b, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Indukcyjność", "Inductance", "Induktivität", "Индуктивность"), c, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Pojemność", "Capacitance", "Kapazität", "Ёмкость"), d, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Strojenie", "Tuning", "Abstimmung", "Настройка"), e, UI_STYL_OSTRZEZENIE);
        break;

    case ANTENA_TYP_QUAD_1EL:
    case ANTENA_TYP_QUAD_2EL:
    case ANTENA_TYP_QUAD_3EL:
        snprintf(a, sizeof(a), "Lz = 301 / f  [m, MHz]");
        snprintf(b, sizeof(b), "Lr = 309 / f  [m, MHz]");
        snprintf(c, sizeof(c), "Ld = 293 / f  [m, MHz]");
        snprintf(d, sizeof(d), "0.15-0.25 lambda; %s 0.20 lambda",
                 ANT_T("start", "start", "Start", "старт"));
        snprintf(e, sizeof(e), "%s", ANT_T("S11 koryguje bezpośrednio tylko element zasilany.",
                                             "S11 directly corrects only the driven element.",
                                             "S11 korrigiert direkt nur das gespeiste Element.",
                                             "S11 напрямую корректирует только активный элемент."));
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Element zasilany", "Driven element", "Strahler", "Вибратор"), a, UI_STYL_AKCENT);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Reflektor", "Reflector", "Reflektor", "Рефлектор"), b,
                                    g_antena.typ == ANTENA_TYP_QUAD_1EL ? UI_STYL_NIEAKTYWNY : UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Direktor", "Director", "Direktor", "Директор"), c,
                                    g_antena.typ == ANTENA_TYP_QUAD_3EL ? UI_STYL_NORMALNY : UI_STYL_NIEAKTYWNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Odstępy", "Spacing", "Abstände", "Зазоры"), d,
                                    g_antena.typ == ANTENA_TYP_QUAD_1EL ? UI_STYL_NIEAKTYWNY : UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Ograniczenie pomiaru", "Measurement limit", "Messgrenze", "Ограничение измерения"), e, UI_STYL_OSTRZEZENIE);
        break;

    case ANTENA_TYP_TWINYAGI_SP2XDQ:
        snprintf(a, sizeof(a), "145 / 435 MHz");
        snprintf(b, sizeof(b), "G ~7 dBd / ~8 dBd");
        snprintf(c, sizeof(c), "boom ~900 mm; 5 elementow");
        snprintf(d, sizeof(d), "SP2XDQ / Antenna Optimizer K6STI");
        snprintf(e, sizeof(e), "%s", ANT_T("Direktory 6 mm; elementy pasywne izolowane od boomu.",
                                             "Directors 6 mm; parasitic elements insulated from boom.",
                                             "Direktoren 6 mm; parasitaere Elemente vom Boom isoliert.",
                                             "Директоры 6 мм; пассивные элементы изолированы от траверсы."));
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Pasma", "Bands", "Baender", "Диапазоны"), a, UI_STYL_AKCENT);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Zysk projektu", "Design gain", "Entwurfsgewinn", "Усиление проекта"), b, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Nośnik", "Boom", "Boom", "Траверса"), c, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Źródło", "Source", "Quelle", "Источник"), d, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Ważne", "Important", "Wichtig", "Важно"), e, UI_STYL_OSTRZEZENIE);
        break;

    case ANTENA_TYP_YAGI3_DK7ZB_2M:
        snprintf(a, sizeof(a), "R 1024 / W 973 / D 868 mm");
        snprintf(b, sizeof(b), "x: 0 / 480 / 850 mm");
        snprintf(c, sizeof(c), "50 Ohm; elementy 6 mm; boom 850 mm");
        snprintf(d, sizeof(d), "DK7ZB: ~5.2 dBd; F/B ~18 dB");
        snprintf(e, sizeof(e), "%s", ANT_T("Profil stały 2 m: nie skaluj automatycznie na inne średnice/pasma.",
                                             "Fixed 2 m profile: do not auto-scale to other diameters/bands.",
                                             "Festes 2-m-Profil: nicht automatisch auf andere Durchmesser/Baender skalieren.",
                                             "Фиксированный профиль 2 м: не масштабировать автоматически на другие диаметры/диапазоны."));
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Elementy", "Elements", "Elemente", "Элементы"), a, UI_STYL_AKCENT);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Pozycje", "Positions", "Positionen", "Позиции"), b, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Wariant", "Variant", "Variante", "Вариант"), c, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Parametry projektu", "Design figures", "Entwurfsdaten", "Параметры проекта"), d, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Ważne", "Important", "Wichtig", "Важно"), e, UI_STYL_OSTRZEZENIE);
        break;

    case ANTENA_TYP_EFHW_40_10:
        snprintf(a, sizeof(a), "40 / 20 / 15 / 10 m");
        snprintf(b, sizeof(b), "~20.1 m (66 ft) start");
        snprintf(c, sizeof(c), "Z feed ~2500 Ohm");
        snprintf(d, sizeof(d), "transformator ~49:1");
        snprintf(e, sizeof(e), "%s", ANT_T("Montaż, izolacja i otoczenie przesuwają rezonanse; stroić po instalacji.",
                                             "Installation, insulation and surroundings shift resonances; tune after installation.",
                                             "Montage, Isolation und Umgebung verschieben Resonanzen; nach Montage abstimmen.",
                                             "Монтаж, изоляция и окружение сдвигают резонансы; настройка после установки."));
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Pasma", "Bands", "Baender", "Диапазоны"), a, UI_STYL_AKCENT);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Drut startowy", "Starting wire", "Startdraht", "Начальная длина"), b, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Impedancja końca", "End impedance", "Endimpedanz", "Импеданс конца"), c, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Dopasowanie", "Matching", "Anpassung", "Согласование"), d, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Strojenie", "Tuning", "Abstimmung", "Настройка"), e, UI_STYL_OSTRZEZENIE);
        break;

    default:
        break;
    }

}

static void ANTENY_PokazLiterature(void)
{
    ANTENY_RysujLiterature();
    ANTENY_CzekajNaWstecz();
}

static void ANTENY_RysujBudowe(void)
{
    ANTENA_ZALECENIA_t zal;
    char a[96], b[96], c[96], d[96], e[96];
    float roznica;

    ANTENA_BAZA_ObliczZalecenia(&g_antena, &zal);
    UI_WyczyscEkran();
    UI_RysujPasekGorny(ANTENY_NazwaEkranuBudowy(), true, false, 0);

    switch (g_antena.typ)
    {
    case ANTENA_TYP_DIPOL_POLFALOWY:
        roznica = ANTENA_BAZA_RoznicaProcent(g_antena.wymiary_mm[0], zal.wymiary[0].zalecane_mm);
        snprintf(a, sizeof(a), "%lu mm", (unsigned long)g_antena.wymiary_mm[0]);
        snprintf(b, sizeof(b), "%.1f mm", (double)g_antena.wymiary_mm[0] / 2.0);
        snprintf(c, sizeof(c), "%lu mm", (unsigned long)zal.wymiary[0].zalecane_mm);
        snprintf(d, sizeof(d), "%+.2f%%", (double)roznica);
        snprintf(e, sizeof(e), "%s", ANT_T("Reguluj oba ramiona symetrycznie.", "Adjust both arms symmetrically.",
                                             "Beide Schenkel symmetrisch ändern.", "Изменяйте оба плеча симметрично."));
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Moja długość", "My length", "Meine Länge", "Моя длина"), a, UI_STYL_AKTYWNY);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Jedno ramię", "One arm", "Ein Schenkel", "Одно плечо"), b, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Literatura", "Reference", "Literatur", "Литература"), c, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Różnica", "Difference", "Abweichung", "Разница"), d, ANTENY_StylRoznicy(roznica));
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Regulacja", "Adjustment", "Abgleich", "Регулировка"), e, UI_STYL_AKCENT);
        break;

    case ANTENA_TYP_W3DZZ:
        snprintf(a, sizeof(a), "%lu mm", (unsigned long)g_antena.wymiary_mm[0]);
        snprintf(b, sizeof(b), "%lu mm", (unsigned long)g_antena.wymiary_mm[1]);
        snprintf(c, sizeof(c), "%lu mm", (unsigned long)(2UL * (g_antena.wymiary_mm[0] + g_antena.wymiary_mm[1])));
        snprintf(d, sizeof(d), "%.3f MHz", (double)g_antena.trap_czestotliwosc_hz / 1000000.0);
        snprintf(e, sizeof(e), "%.1f uH / %.1f pF",
                 (double)g_antena.trap_indukcyjnosc_nh / 1000.0,
                 (double)g_antena.trap_pojemnosc_pf_x10 / 10.0);
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Wewnętrzny / ramię", "Inner / arm", "Innen / Schenkel", "Внутр. / плечо"), a, UI_STYL_AKTYWNY);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Zewnętrzny / ramię", "Outer / arm", "Außen / Schenkel", "Внешн. / плечо"), b, UI_STYL_AKTYWNY);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Rozpiętość elektryczna", "Electrical span", "Elektrische Gesamtlänge", "Электрическая длина"), c, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Rezonans pułapki", "Trap resonance", "Sperrkreisresonanz", "Резонанс трапа"), d, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Pułapka L / C", "Trap L / C", "Sperrkreis L / C", "Трап L / C"), e, UI_STYL_NORMALNY);
        break;

    case ANTENA_TYP_QUAD_1EL:
        snprintf(a, sizeof(a), "%lu mm", (unsigned long)g_antena.wymiary_mm[0]);
        snprintf(b, sizeof(b), "%.1f mm", (double)g_antena.wymiary_mm[0] / 4.0);
        snprintf(c, sizeof(c), "%lu mm / %.1f mm", (unsigned long)zal.wymiary[0].zalecane_mm,
                 (double)zal.wymiary[0].zalecane_mm / 4.0);
        roznica = ANTENA_BAZA_RoznicaProcent(g_antena.wymiary_mm[0], zal.wymiary[0].zalecane_mm);
        snprintf(d, sizeof(d), "%+.2f%%", (double)roznica);
        snprintf(e, sizeof(e), "%s", ANT_T("Korektę obwodu rozdziel symetrycznie na boki.",
                                             "Distribute perimeter correction symmetrically over the sides.",
                                             "Umfangskorrektur symmetrisch auf die Seiten verteilen.",
                                             "Распределяйте поправку периметра симметрично."));
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Obwód mój", "My perimeter", "Mein Umfang", "Мой периметр"), a, UI_STYL_AKTYWNY);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Bok mój", "My side", "Meine Seite", "Моя сторона"), b, UI_STYL_AKTYWNY);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Literatura: obwód / bok", "Reference: perimeter / side", "Literatur: Umfang / Seite", "Расчёт: периметр / сторона"), c, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Różnica obwodu", "Perimeter difference", "Umfangsabweichung", "Разница периметра"), d, ANTENY_StylRoznicy(roznica));
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Regulacja", "Adjustment", "Abgleich", "Регулировка"), e, UI_STYL_AKCENT);
        break;

    case ANTENA_TYP_QUAD_2EL:
        snprintf(a, sizeof(a), "%lu / %.1f mm", (unsigned long)g_antena.wymiary_mm[0], (double)g_antena.wymiary_mm[0] / 4.0);
        snprintf(b, sizeof(b), "%lu / %.1f mm", (unsigned long)g_antena.wymiary_mm[1], (double)g_antena.wymiary_mm[1] / 4.0);
        snprintf(c, sizeof(c), "%lu mm", (unsigned long)g_antena.wymiary_mm[2]);
        snprintf(d, sizeof(d), "%lu mm", (unsigned long)zal.wymiary[2].zalecane_mm);
        snprintf(e, sizeof(e), "%lu-%lu mm", (unsigned long)zal.wymiary[2].minimum_mm,
                 (unsigned long)zal.wymiary[2].maksimum_mm);
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Zasilany: obwód / bok", "Driven: perimeter / side", "Strahler: Umfang / Seite", "Вибратор: периметр / сторона"), a, UI_STYL_AKTYWNY);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Reflektor: obwód / bok", "Reflector: perimeter / side", "Reflektor: Umfang / Seite", "Рефлектор: периметр / сторона"), b, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Odstęp R-Z mój", "My R-D spacing", "Mein R-S Abstand", "Мой зазор R-В"), c, UI_STYL_AKTYWNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Odstęp zalecany", "Recommended spacing", "Empfohlener Abstand", "Рекомендуемый зазор"), d, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Zakres literatury", "Reference range", "Literaturbereich", "Диапазон литературы"), e, UI_STYL_NORMALNY);
        break;

    case ANTENA_TYP_QUAD_3EL:
        snprintf(a, sizeof(a), "%.1f / %.1f / %.1f mm",
                 (double)g_antena.wymiary_mm[0] / 4.0,
                 (double)g_antena.wymiary_mm[1] / 4.0,
                 (double)g_antena.wymiary_mm[2] / 4.0);
        snprintf(b, sizeof(b), "%lu mm", (unsigned long)g_antena.wymiary_mm[3]);
        snprintf(c, sizeof(c), "%lu mm", (unsigned long)g_antena.wymiary_mm[4]);
        snprintf(d, sizeof(d), "%lu mm", (unsigned long)zal.wymiary[3].zalecane_mm);
        snprintf(e, sizeof(e), "%lu-%lu mm", (unsigned long)zal.wymiary[3].minimum_mm,
                 (unsigned long)zal.wymiary[3].maksimum_mm);
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Boki Z / R / D", "Sides D / R / Dir", "Seiten S / R / D", "Стороны В / R / D"), a, UI_STYL_AKTYWNY);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Odstęp R-Z mój", "My R-D spacing", "Mein R-S Abstand", "Мой зазор R-В"), b, UI_STYL_AKTYWNY);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Odstęp Z-D mój", "My D-Dir spacing", "Mein S-D Abstand", "Мой зазор В-D"), c, UI_STYL_AKTYWNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Odstęp zalecany", "Recommended spacing", "Empfohlener Abstand", "Рекомендуемый зазор"), d, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Zakres literatury", "Reference range", "Literaturbereich", "Диапазон литературы"), e, UI_STYL_NORMALNY);
        break;

    case ANTENA_TYP_TWINYAGI_SP2XDQ:
        snprintf(a, sizeof(a), "R  %lu mm   x=0", (unsigned long)g_antena.wymiary_mm[0]);
        snprintf(b, sizeof(b), "W  %lu mm   x=221 mm   szczelina 10 mm", (unsigned long)g_antena.wymiary_mm[1]);
        snprintf(c, sizeof(c), "D1 %lu mm   x=411 mm", (unsigned long)g_antena.wymiary_mm[2]);
        snprintf(d, sizeof(d), "D2 %lu mm   x=635 mm", (unsigned long)g_antena.wymiary_mm[3]);
        snprintf(e, sizeof(e), "D3 %lu mm   x=836 mm", (unsigned long)g_antena.wymiary_mm[4]);
        ANTENY_RysujWierszSzczegolu(0U, "R", a, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(1U, "W", b, UI_STYL_AKCENT);
        ANTENY_RysujWierszSzczegolu(2U, "D1", c, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(3U, "D2", d, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(4U, "D3", e, UI_STYL_NORMALNY);
        break;

    case ANTENA_TYP_YAGI3_DK7ZB_2M:
        snprintf(a, sizeof(a), "R %lu mm   x=0", (unsigned long)g_antena.wymiary_mm[0]);
        snprintf(b, sizeof(b), "W %lu mm   x=%lu mm", (unsigned long)g_antena.wymiary_mm[1], (unsigned long)g_antena.wymiary_mm[3]);
        snprintf(c, sizeof(c), "D %lu mm   x=%lu mm", (unsigned long)g_antena.wymiary_mm[2], (unsigned long)(g_antena.wymiary_mm[3] + g_antena.wymiary_mm[4]));
        snprintf(d, sizeof(d), "%s", ANT_T("Elementy 6 mm; projekt 50 Ohm", "6 mm elements; 50 Ohm design", "6-mm-Elemente; 50-Ohm-Entwurf", "Элементы 6 мм; проект 50 Ом"));
        snprintf(e, sizeof(e), "%s", ANT_T("Weryfikuj S11; nie koryguj pasywnych elementów z jednego pomiaru.",
                                             "Verify S11; do not correct passive elements from one measurement.",
                                             "S11 pruefen; passive Elemente nicht aus einer Messung korrigieren.",
                                             "Проверяйте S11; не корректируйте пассивные элементы по одному измерению."));
        ANTENY_RysujWierszSzczegolu(0U, "R", a, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(1U, "W", b, UI_STYL_AKCENT);
        ANTENY_RysujWierszSzczegolu(2U, "D", c, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Wykonanie", "Construction", "Aufbau", "Конструкция"), d, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Strojenie", "Tuning", "Abstimmung", "Настройка"), e, UI_STYL_OSTRZEZENIE);
        break;

    case ANTENA_TYP_EFHW_40_10:
        snprintf(a, sizeof(a), "%lu mm", (unsigned long)g_antena.wymiary_mm[0]);
        snprintf(b, sizeof(b), "49:1   50 Ohm -> ~2.5 kOhm");
        snprintf(c, sizeof(c), "40/20/15/10 m");
        snprintf(d, sizeof(d), "%s", ANT_T("Zasilanie na końcu; transformator przy punkcie zasilania",
                                             "End-fed; transformer at feed point",
                                             "Endgespeist; Transformator am Speisepunkt",
                                             "Питание с конца; трансформатор у точки питания"));
        snprintf(e, sizeof(e), "%s", ANT_T("Po skróceniu/wydłużeniu sprawdź ponownie wszystkie pasma.",
                                             "After trimming/lengthening recheck all bands.",
                                             "Nach Kuerzen/Verlaengern alle Baender erneut pruefen.",
                                             "После изменения длины снова проверьте все диапазоны."));
        ANTENY_RysujWierszSzczegolu(0U, ANT_T("Drut", "Wire", "Draht", "Провод"), a, UI_STYL_AKTYWNY);
        ANTENY_RysujWierszSzczegolu(1U, ANT_T("Transformator", "Transformer", "Transformator", "Трансформатор"), b, UI_STYL_AKCENT);
        ANTENY_RysujWierszSzczegolu(2U, ANT_T("Pasma", "Bands", "Baender", "Диапазоны"), c, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(3U, ANT_T("Zasilanie", "Feed", "Speisung", "Питание"), d, UI_STYL_NORMALNY);
        ANTENY_RysujWierszSzczegolu(4U, ANT_T("Ważne", "Important", "Wichtig", "Важно"), e, UI_STYL_OSTRZEZENIE);
        break;

    default:
        break;
    }

}

static void ANTENY_RysujTwinZasilanie(void)
{
    UI_WyczyscEkran();
    UI_RysujPasekGorny(ANT_T("TwinYagi - zasilanie", "TwinYagi - feed", "TwinYagi - Speisung", "TwinYagi - питание"), true, false, 0);
    ANTENY_RysujWierszSzczegolu(0U, ANT_T("Kabel", "Cable", "Kabel", "Кабель"),
                                "RG-58 50 Ohm: 340 mm koncentryka", UI_STYL_AKCENT);
    ANTENY_RysujWierszSzczegolu(1U, ANT_T("Obliczenie", "Calculation", "Berechnung", "Расчёт"),
                                "1/4 lambda x VF; 145.5 MHz; VF=0.66", UI_STYL_NORMALNY);
    ANTENY_RysujWierszSzczegolu(2U, ANT_T("Końcówki", "Leads", "Anschluesse", "Выводы"),
                                ANT_T("+ ok. 15 mm na obu końcach", "+ about 15 mm at both ends", "+ ca. 15 mm an beiden Enden", "+ около 15 мм с обоих концов"), UI_STYL_NORMALNY);
    ANTENY_RysujWierszSzczegolu(3U, ANT_T("Nawinięcie", "Winding", "Wicklung", "Намотка"),
                                ANT_T("PVC ~22 mm; liczba zwojów nie jest krytyczna", "PVC ~22 mm; turn count is not critical", "PVC ~22 mm; Windungszahl ist unkritisch", "ПВХ ~22 мм; число витков некритично"), UI_STYL_NORMALNY);
    ANTENY_RysujWierszSzczegolu(4U, ANT_T("Połączenie", "Connection", "Anschluss", "Подключение"),
                                ANT_T("UC1 -> 34 cm RG-58 -> wibrator; kabel może być prosty", "UC1 -> 34 cm RG-58 -> driven element; cable may be straight", "UC1 -> 34 cm RG-58 -> Strahler; Kabel darf gerade sein", "UC1 -> 34 см RG-58 -> вибратор; кабель может быть прямым"), UI_STYL_OSTRZEZENIE);
}

static void ANTENY_PokazBudowe(void)
{
    if (g_antena.typ != ANTENA_TYP_TWINYAGI_SP2XDQ)
    {
        ANTENY_RysujBudowe();
        ANTENY_CzekajNaWstecz();
        return;
    }

    {
        uint8_t strona = 0U;
        while (TOUCH_IsPressed()) Sleep(10U);
        WEJSCIA_WyczyscZdarzenia();
        for (;;)
        {
            UI_AKCJA_t akcje[3];
            LCDPoint punkt;
            WEJSCIE_ZDARZENIE_t zdarzenie;
            int16_t akcja = -1;

            if (strona == 0U) ANTENY_RysujBudowe();
            else ANTENY_RysujTwinZasilanie();

            akcje[0] = (UI_AKCJA_t){1, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false};
            akcje[1] = (UI_AKCJA_t){2, "<", UI_STYL_NORMALNY, strona > 0U, false};
            akcje[2] = (UI_AKCJA_t){3, ">", UI_STYL_AKCENT, strona == 0U, false};
            UI_RysujPasekAkcji(UI_DOLNY_PASEK_Y, UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 3U);

            for (;;)
            {
                if (TOUCH_Poll(&punkt))
                {
                    akcja = UI_ZnajdzAkcjePaska(punkt, UI_DOLNY_PASEK_Y,
                                                UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 3U);
                    if (akcja > 0) TOUCH_CzekajNaPuszczenie(35U);
                }
                zdarzenie = WEJSCIA_PobierzZdarzenie();
                if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ) akcja = 1;
                if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO) akcja = 2;
                if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO || zdarzenie == WEJSCIE_ZDARZENIE_OK) akcja = 3;

                if (akcja == 1) return;
                if (akcja == 2 && strona > 0U) { --strona; break; }
                if (akcja == 3 && strona == 0U) { ++strona; break; }
                Sleep(10U);
            }
        }
    }
}

static void ANTENY_OpisKorekty(const ANTENA_KOREKTA_t *k, char *bufor, size_t rozmiar)
{
    const char *czasownik;

    if (k == NULL || bufor == NULL || rozmiar == 0U)
        return;
    bufor[0] = '\0';

    if (!k->dostepna)
    {
        snprintf(bufor, rozmiar, "%s",
                 ANT_T("Brak uczciwej poprawki w mm dla tego pasma. Zmień tylko po dodatkowej weryfikacji.",
                       "No defensible mm correction for this band. Change only after additional verification.",
                       "Keine belastbare mm-Korrektur für dieses Band. Nur nach zusätzlicher Prüfung ändern.",
                       "Для этого диапазона нет надёжной поправки в мм. Нужна дополнительная проверка."));
        return;
    }

    if (k->kierunek == STROJENIE_ANTENA_KIERUNEK_W_CELU)
    {
        snprintf(bufor, rozmiar, "%s", ANT_T("Rezonans jest w celu; korekta długości nie jest potrzebna.",
                                             "Resonance is on target; no length correction is needed.",
                                             "Resonanz liegt am Ziel; keine Längenkorrektur nötig.",
                                             "Резонанс у цели; коррекция длины не нужна."));
        return;
    }

    czasownik = k->kierunek == STROJENIE_ANTENA_KIERUNEK_SKROC
              ? ANT_T("Skróć", "Shorten", "Kürzen", "Укоротить")
              : ANT_T("Wydłuż", "Lengthen", "Verlängern", "Удлинить");

    switch (k->regulacja)
    {
    case ANTENA_REGULACJA_DIPOL_RAMIONA:
        snprintf(bufor, rozmiar, ANT_T("%s każde ramię o ok. %.1f mm. Nowa długość całkowita %.1f mm.%s",
                                      "%s each arm by about %.1f mm. New total length %.1f mm.%s",
                                      "%s jeden Schenkel um ca. %.1f mm. Neue Gesamtlänge %.1f mm.%s",
                                      "%s каждое плечо примерно на %.1f мм. Новая общая длина %.1f мм.%s"),
                 czasownik, (double)fabsf(k->zmiana_na_miejsce_mm),
                 (double)k->wymiar_nowy_mm,
                 k->uzyto_historii ? ANT_T(" Wyliczono z poprzedniej regulacji.", " Based on previous adjustment.", " Aus vorheriger Korrektur berechnet.", " По предыдущей регулировке.") : "");
        break;

    case ANTENA_REGULACJA_QUAD_OBWOD_ZASILANY:
        snprintf(bufor, rozmiar, ANT_T("%s obwód elementu zasilanego o %.1f mm (ok. %.1f mm na bok). Nowy obwód %.1f mm.%s",
                                      "%s driven perimeter by %.1f mm (about %.1f mm per side). New perimeter %.1f mm.%s",
                                      "%s Strahlerumfang um %.1f mm (ca. %.1f mm je Seite). Neu %.1f mm.%s",
                                      "%s периметр вибратора на %.1f мм (ок. %.1f мм на сторону). Новый %.1f мм.%s"),
                 czasownik, (double)fabsf(k->zmiana_calkowita_mm),
                 (double)fabsf(k->zmiana_na_miejsce_mm), (double)k->wymiar_nowy_mm,
                 k->uzyto_historii ? ANT_T(" Z historii regulacji.", " From adjustment history.", " Aus Abgleichhistorie.", " По истории регулировки.") : "");
        break;

    case ANTENA_REGULACJA_W3DZZ_ZEWNETRZNE:
        snprintf(bufor, rozmiar, ANT_T("%s po %.1f mm na każdym zewnętrznym końcu. To przybliżenie dla 80 m; pułapki wpływają na wynik.",
                                      "%s %.1f mm at each outer end. Approximation for 80 m; traps affect the result.",
                                      "%s je %.1f mm an den äußeren Enden. Näherung für 80 m; Sperrkreise wirken mit.",
                                      "%s по %.1f мм на каждом внешнем конце. Приближение для 80 м; трапы влияют."),
                 czasownik, (double)fabsf(k->zmiana_na_miejsce_mm));
        break;

    case ANTENA_REGULACJA_W3DZZ_WEWNETRZNE:
        snprintf(bufor, rozmiar, ANT_T("%s po %.1f mm na każdym odcinku wewnętrznym. Najpierw sprawdź rezonans pułapek około %.3f MHz.",
                                      "%s %.1f mm on each inner section. Verify trap resonance near %.3f MHz first.",
                                      "%s je %.1f mm an den inneren Abschnitten. Zuerst Sperrkreisresonanz bei %.3f MHz prüfen.",
                                      "%s по %.1f мм на каждом внутреннем отрезке. Сначала проверьте трапы около %.3f МГц."),
                 czasownik, (double)fabsf(k->zmiana_na_miejsce_mm),
                 (double)g_antena.trap_czestotliwosc_hz / 1000000.0);
        break;

    case ANTENA_REGULACJA_EFHW_DRUT:
        snprintf(bufor, rozmiar, ANT_T("%s drut o ok. %.0f mm (nowa długość %.0f mm). EFHW jest wielopasmowa: po zmianie sprawdź 40/20/15/10 m; montaż wpływa na rezonanse.",
                                      "%s wire by about %.0f mm (new length %.0f mm). EFHW is multiband: recheck 40/20/15/10 m; installation shifts resonances.",
                                      "%s Draht um ca. %.0f mm (neue Laenge %.0f mm). EFHW ist mehrbandig: 40/20/15/10 m erneut pruefen; Montage beeinflusst Resonanzen.",
                                      "%s провод примерно на %.0f мм (новая длина %.0f мм). EFHW многодиапазонная: снова проверьте 40/20/15/10 м; монтаж влияет на резонансы."),
                 czasownik, (double)fabsf(k->zmiana_calkowita_mm), (double)k->wymiar_nowy_mm);
        break;

    default:
        snprintf(bufor, rozmiar, "%s", ANT_T("Korekta wymaga ręcznej interpretacji.", "Manual interpretation required.", "Manuelle Interpretation erforderlich.", "Нужна ручная интерпретация."));
        break;
    }
}

static void ANTENY_RysujWynikPomiaru(void)
{
    char cel[32];
    char rez[96];
    char punkt[96];
    char wskazowka[220];
    UI_AKCJA_t akcje[2];

    UI_FormatujCzestotliwoscMHz(g_antena.czestotliwosc_docelowa_hz, cel, sizeof(cel));
    if (g_ostatni_pomiar.rezonans_znaleziony)
    {
        snprintf(rez, sizeof(rez), "%.6f MHz  R %.1f Ohm  SWR %.2f",
                 (double)g_ostatni_pomiar.f_rezonans_hz / 1000000.0,
                 (double)g_ostatni_pomiar.r_rezonans_ohm,
                 (double)g_ostatni_pomiar.swr_rezonans);
    }
    else
        snprintf(rez, sizeof(rez), "%s", ANT_T("Nie znaleziono X=0 w skanie", "No X=0 found in scan", "Kein X=0 im Scan", "X=0 не найден в скане"));

    if (g_ostatni_pomiar.punkt_docelowy_znaleziony)
        snprintf(punkt, sizeof(punkt), "SWR %.2f  R %.1f  X %+.1f Ohm",
                 (double)g_ostatni_pomiar.swr_docelowy,
                 (double)g_ostatni_pomiar.r_docelowy_ohm,
                 (double)g_ostatni_pomiar.x_docelowy_ohm);
    else
        snprintf(punkt, sizeof(punkt), "--");

    ANTENY_OpisKorekty(&g_ostatnia_korekta, wskazowka, sizeof(wskazowka));

    UI_WyczyscEkran();
    UI_RysujPasekGorny(ANT_T("Wynik i zalecenie", "Result and guidance", "Ergebnis und Hinweis", "Результат и рекомендация"), true, false, 0);
    UI_RysujPoleStatusu(8, 36, 228, 40, ANT_T("Cel", "Target", "Ziel", "Цель"), cel, UI_STYL_NORMALNY);
    UI_RysujPoleStatusu(244, 36, 228, 40, ANT_T("Typ", "Type", "Typ", "Тип"), ANTENY_NazwaTypu(g_antena.typ), UI_STYL_AKCENT);

    /* Dwa wyniki elektryczne zajmują jedno większe pole. Dzięki temu zalecenie
       konstrukcyjne ma dość miejsca na 3-4 linie zamiast być obcinane po
       pierwszym wierszu na ekranie 480x272. */
    {
        char elektryczne[196];
        snprintf(elektryczne, sizeof(elektryczne), "%s\n%s: %s", rez,
                 ANT_T("Cel", "Target", "Ziel", "Цель"), punkt);
        UI_RysujPoleStatusu(8, 80, 464, 58,
                            ANT_T("Pomiar", "Measurement", "Messung", "Измерение"),
                            elektryczne,
                            g_ostatni_pomiar.rezonans_znaleziony ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);
    }
    UI_RysujPoleStatusu(8, 142, 464, 78, ANT_T("Co zrobić", "What to do", "Was tun", "Что делать"), wskazowka,
                        g_ostatnia_korekta.dostepna ? UI_STYL_AKCENT : UI_STYL_OSTRZEZENIE);

    akcje[0] = (UI_AKCJA_t){1, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false};
    akcje[1] = (UI_AKCJA_t){2, ANT_T("Raport", "Report", "Bericht", "Отчёт"), UI_STYL_NORMALNY, true, false};
    UI_RysujPasekAkcji(224U, 44U, akcje, 2U);
}

static uint8_t ANTENY_PrzygotujSciezkeRaportu(char *sciezka, size_t rozmiar_sciezki,
                                               uint32_t data, uint32_t czas)
{
    FRESULT wynik;
    char katalog_daty[40];

    if (sciezka == NULL || rozmiar_sciezki == 0U)
        return 0U;

    if (data >= 20000101U && data <= 20991231U && czas <= 2359U)
    {
        /*
         * FatFs w tym firmware pracuje bez LFN. Każdy człon nazwy musi więc
         * mieścić się w 8.3. Pełną datę przechowujemy jako katalog YYYYMMDD,
         * a raport w nim jako HHMM.TXT. Poprzednia długa nazwa
         * raport_YYYYMMDD_HHMM.txt kończyła się FR_INVALID_NAME.
         */
        snprintf(katalog_daty, sizeof(katalog_daty), ANTENY_KATALOG "/%08lu",
                 (unsigned long)data);
        wynik = f_mkdir(katalog_daty);
        if (wynik != FR_OK && wynik != FR_EXIST)
            return 0U;
        snprintf(sciezka, rozmiar_sciezki, "%s/%04lu.txt",
                 katalog_daty, (unsigned long)czas);
    }
    else
    {
        snprintf(sciezka, rozmiar_sciezki, "%s", ANTENY_RAPORT_AWARYJNY);
    }

    return 1U;
}

static uint8_t ANTENY_ZapiszRaport(char *sciezka_wyj, size_t rozmiar_sciezki)
{
    FIL plik;
    char sciezka[64];
    char linia[256];
    char opis[220];
    ANTENA_ZALECENIA_t zal;
    uint32_t data = CFG_GetParam(CFG_PARAM_Date);
    uint32_t czas = CFG_GetParam(CFG_PARAM_Time);
    uint8_t i;

    if (!g_ma_profil || !ANTENY_ZapewnijKatalog())
        return 0U;

    if (!ANTENY_PrzygotujSciezkeRaportu(sciezka, sizeof(sciezka), data, czas))
        return 0U;

    if (f_open(&plik, sciezka, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return 0U;

#define ANT_RAPORT(...) do { \
        snprintf(linia, sizeof(linia), __VA_ARGS__); \
        if (!ANTENY_ZapiszBufor(&plik, linia)) { f_close(&plik); return 0U; } \
    } while (0)

    ANT_RAPORT("EU1KY-PL 2026 - raport anteny\r\n");
    ANT_RAPORT("Format raportu: ANT-R1\r\n\r\n");
    ANT_RAPORT("Typ: %s\r\n", ANTENY_NazwaTypu(g_antena.typ));
    ANT_RAPORT("Czestotliwosc docelowa: %.6f MHz\r\n",
               (double)g_antena.czestotliwosc_docelowa_hz / 1000000.0);
    ANT_RAPORT("Receptura: baza konstrukcyjna EU1KY, wersja 1\r\n");
    if (g_antena.typ == ANTENA_TYP_DIPOL_POLFALOWY)
        ANT_RAPORT("Wzor startowy: L = 143/f [m, MHz]\r\n");
    else if (g_antena.typ == ANTENA_TYP_W3DZZ)
        ANT_RAPORT("W3DZZ: 9.7 m + 6.7 m na ramie; trap 7.2 MHz, 8.2 uH, 60 pF\r\n");
    else if (g_antena.typ == ANTENA_TYP_TWINYAGI_SP2XDQ)
    {
        ANT_RAPORT("TwinYagi SP2XDQ 145/435 MHz: projekt K6STI Antenna Optimizer\r\n");
        ANT_RAPORT("Pozycje od reflektora [mm]: R=0, W=221, D1=411, D2=635, D3=836\r\n");
        ANT_RAPORT("Zasilanie: 340 mm RG-58 (czesc koncentryczna), VF 0.66, 1/4 lambda dla 145.5 MHz; koncowki ok. 15 mm/strone\r\n");
    }
    else if (g_antena.typ == ANTENA_TYP_YAGI3_DK7ZB_2M)
    {
        ANT_RAPORT("Yagi 3 el. DK7ZB 2 m / 50 Ohm: elementy 6 mm, boom 850 mm\r\n");
        ANT_RAPORT("R=1024 mm @0; W=973 mm @480; D=868 mm @850; projekt ok. 5.2 dBd, F/B ok. 18 dB\r\n");
        ANT_RAPORT("Geometria jest profilem stalym; brak automatycznego skalowania na inne srednice/pasma.\r\n");
    }
    else if (g_antena.typ == ANTENA_TYP_EFHW_40_10)
    {
        ANT_RAPORT("EFHW 40/20/15/10 m: drut startowy ok. 20.1 m (66 ft), zasilanie koncowe\r\n");
        ANT_RAPORT("Dopasowanie: transformator ok. 49:1; typowa impedancja punktu zasilania ok. 2500 Ohm\r\n");
        ANT_RAPORT("Po zmianie dlugosci sprawdz wszystkie pasma; montaz i otoczenie przesuwaja rezonanse.\r\n");
    }
    else
        ANT_RAPORT("Quad: zasilany 301/f, reflektor 309/f, direktor 293/f; odstep 0.15-0.25 lambda\r\n");

    ANT_RAPORT("\r\nWymiary [aktualne / zalecane]:\r\n");
    ANTENA_BAZA_ObliczZalecenia(&g_antena, &zal);
    for (i = 0U; i < zal.liczba; ++i)
    {
        if (zal.wymiary[i].ma_zakres)
            ANT_RAPORT("- %s: %lu mm / zakres %lu-%lu mm\r\n",
                       ANTENY_NazwaPola(zal.wymiary[i].pole),
                       (unsigned long)g_antena.wymiary_mm[i],
                       (unsigned long)zal.wymiary[i].minimum_mm,
                       (unsigned long)zal.wymiary[i].maksimum_mm);
        else
            ANT_RAPORT("- %s: %lu mm / %lu mm (%+.2f%%)\r\n",
                       ANTENY_NazwaPola(zal.wymiary[i].pole),
                       (unsigned long)g_antena.wymiary_mm[i],
                       (unsigned long)zal.wymiary[i].zalecane_mm,
                       (double)ANTENA_BAZA_RoznicaProcent(g_antena.wymiary_mm[i],
                                                          zal.wymiary[i].zalecane_mm));
    }

    if (g_antena.typ == ANTENA_TYP_W3DZZ)
        ANT_RAPORT("Trap: %.6f MHz, L %.3f uH, C %.1f pF\r\n",
                   (double)g_antena.trap_czestotliwosc_hz / 1000000.0,
                   (double)g_antena.trap_indukcyjnosc_nh / 1000.0,
                   (double)g_antena.trap_pojemnosc_pf_x10 / 10.0);

    ANT_RAPORT("\r\nPomiar:\r\n");
    if (g_antena.typ == ANTENA_TYP_TWINYAGI_SP2XDQ && g_twin_ma_pomiar)
    {
        ANT_RAPORT("2 m: min SWR %.3f przy %.6f MHz\r\n", (double)g_twin_pomiar_2m.min_swr, (double)g_twin_pomiar_2m.f_min_swr_hz / 1000000.0);
        ANT_RAPORT("70 cm: min SWR %.3f przy %.6f MHz\r\n", (double)g_twin_pomiar_70.min_swr, (double)g_twin_pomiar_70.f_min_swr_hz / 1000000.0);
        if (g_twin_pomiar_2m.f_min_swr_hz != 0U)
            ANT_RAPORT("Stosunek f70/f2m: %.5f\r\n", (double)g_twin_pomiar_70.f_min_swr_hz / (double)g_twin_pomiar_2m.f_min_swr_hz);
        ANT_RAPORT("Korekta automatyczna mm: wylaczona; projekt dwupasmowy wymaga kontroli obu pasm.\r\n");
    }
    else if (g_ma_pomiar)
    {
        if (g_ostatni_pomiar.rezonans_znaleziony)
            ANT_RAPORT("Rezonans X=0: %.6f MHz, R %.2f Ohm, SWR %.3f\r\n",
                       (double)g_ostatni_pomiar.f_rezonans_hz / 1000000.0,
                       (double)g_ostatni_pomiar.r_rezonans_ohm,
                       (double)g_ostatni_pomiar.swr_rezonans);
        else
            ANT_RAPORT("Rezonans X=0: nie znaleziono w zakresie skanu\r\n");
        ANT_RAPORT("Minimum SWR: %.3f przy %.6f MHz, R %.2f, X %+.2f Ohm\r\n",
                   (double)g_ostatni_pomiar.min_swr,
                   (double)g_ostatni_pomiar.f_min_swr_hz / 1000000.0,
                   (double)g_ostatni_pomiar.r_min_ohm,
                   (double)g_ostatni_pomiar.x_min_ohm);
        ANT_RAPORT("W punkcie docelowym: SWR %.3f, R %.2f, X %+.2f Ohm\r\n",
                   (double)g_ostatni_pomiar.swr_docelowy,
                   (double)g_ostatni_pomiar.r_docelowy_ohm,
                   (double)g_ostatni_pomiar.x_docelowy_ohm);
        ANTENY_OpisKorekty(&g_ostatnia_korekta, opis, sizeof(opis));
        ANT_RAPORT("Zalecenie: %s\r\n", opis);
        if (g_ostatnia_korekta.uzyto_historii)
            ANT_RAPORT("Metoda korekty: lokalna czulosc z dwoch regulacji, %.1f Hz/mm\r\n",
                       (double)g_ostatnia_korekta.czulosc_hz_na_mm);
        else
            ANT_RAPORT("Metoda korekty: lokalna proporcja f ~ 1/L\r\n");
    }
    else
    {
        ANT_RAPORT("Brak pomiaru w biezacej sesji. Raport obejmuje tylko geometrie.\r\n");
    }

    ANT_RAPORT("\r\nUwagi metrologiczne:\r\n");
    ANT_RAPORT("- Surowy pomiar S11 nie jest zmieniany przez baze anten.\r\n");
    ANT_RAPORT("- Automatyczna korekta dotyczy elementu zasilanego.\r\n");
    ANT_RAPORT("- Elementy pasywne Quada i Yagi sa porownywane z receptura, ale nie sa korygowane z jednego S11.\r\n");
    ANT_RAPORT("- Dla W3DZZ zaleznosci miedzy pasmami i trapami wymagaja ostroznosci.\r\n");
    ANT_RAPORT("- EFHW jest wielopasmowa: korekta drutu na jednym pasmie wymaga ponownej kontroli pozostalych pasm.\r\n");

#undef ANT_RAPORT

    if (f_sync(&plik) != FR_OK)
    {
        f_close(&plik);
        return 0U;
    }
    if (f_close(&plik) != FR_OK)
        return 0U;

    if (sciezka_wyj != NULL && rozmiar_sciezki > 0U)
    {
        strncpy(sciezka_wyj, sciezka, rozmiar_sciezki - 1U);
        sciezka_wyj[rozmiar_sciezki - 1U] = '\0';
    }
    return 1U;
}

static void ANTENY_PokazRaport(void)
{
    char sciezka[64];
    if (ANTENY_ZapiszRaport(sciezka, sizeof(sciezka)))
        KOMUNIKAT_PokazTekst(ANT_T("Raport anteny", "Antenna report", "Antennenbericht", "Отчёт антенны"), sciezka);
    else
        KOMUNIKAT_PokazTekst(ANT_T("Błąd zapisu", "Write error", "Schreibfehler", "Ошибка записи"),
                             ANT_T("Nie udało się zapisać raportu. Sprawdź kartę SD.",
                                   "Could not save report. Check the SD card.",
                                   "Bericht konnte nicht gespeichert werden. SD-Karte prüfen.",
                                   "Не удалось сохранить отчёт. Проверьте SD-карту."));
}

static void ANTENY_RysujWynikTwinYagi(void)
{
    char a[96], b[96], c[96], d[128];
    float stosunek = 0.0f;
    UI_STYL_t styl = UI_STYL_AKCENT;

    snprintf(a, sizeof(a), "min SWR %.2f @ %.3f MHz",
             (double)g_twin_pomiar_2m.min_swr,
             (double)g_twin_pomiar_2m.f_min_swr_hz / 1000000.0);
    snprintf(b, sizeof(b), "min SWR %.2f @ %.3f MHz",
             (double)g_twin_pomiar_70.min_swr,
             (double)g_twin_pomiar_70.f_min_swr_hz / 1000000.0);
    if (g_twin_pomiar_2m.f_min_swr_hz != 0U)
        stosunek = (float)g_twin_pomiar_70.f_min_swr_hz / (float)g_twin_pomiar_2m.f_min_swr_hz;
    snprintf(c, sizeof(c), "f70/f2m = %.4f  (idealnie ok. 3)", (double)stosunek);

    if (g_twin_pomiar_2m.f_min_swr_hz >= 145000000U && g_twin_pomiar_2m.f_min_swr_hz <= 146000000U &&
        g_twin_pomiar_70.f_min_swr_hz >= 434000000U && g_twin_pomiar_70.f_min_swr_hz <= 437000000U)
    {
        snprintf(d, sizeof(d), "%s", ANT_T("Oba minima są w typowym obszarze projektu SP2XDQ. Nie koryguj geometrii bez potrzeby.",
                                             "Both minima are in the typical SP2XDQ design range. Do not alter geometry unnecessarily.",
                                             "Beide Minima liegen im typischen SP2XDQ-Bereich. Geometrie nicht unnoetig aendern.",
                                             "Оба минимума находятся в типичном диапазоне проекта SP2XDQ. Не меняйте геометрию без необходимости."));
        styl = UI_STYL_AKTYWNY;
    }
    else
    {
        snprintf(d, sizeof(d), "%s", ANT_T("Nie przeliczam automatycznie długości. Sprawdź 34 cm RG-58, szczelinę 10 mm, średnice 6 mm i położenia elementów; po każdej zmianie mierz oba pasma.",
                                             "No automatic length scaling. Check the 34 cm RG-58 section, 10 mm gap, 6 mm diameters and element positions; measure both bands after every change.",
                                             "Keine automatische Laengenskalierung. 34 cm RG-58, 10-mm-Spalt, 6-mm-Durchmesser und Elementpositionen pruefen; nach jeder Aenderung beide Baender messen.",
                                             "Автоматический пересчёт длин отключён. Проверьте 34 см RG-58, зазор 10 мм, диаметры 6 мм и положения элементов; после каждой правки измеряйте оба диапазона."));
        styl = UI_STYL_OSTRZEZENIE;
    }

    UI_WyczyscEkran();
    UI_RysujPasekGorny("TwinYagi SP2XDQ - 2 m / 70 cm", true, false, 0);
    UI_RysujPoleStatusu(8U, 36U, 464U, 40U, "2 m", a, UI_STYL_AKCENT);
    UI_RysujPoleStatusu(8U, 80U, 464U, 40U, "70 cm", b, UI_STYL_AKCENT);
    UI_RysujPoleStatusu(8U, 124U, 464U, 40U, ANT_T("Stosunek minimów", "Minima ratio", "Minima-Verhaeltnis", "Отношение минимумов"), c, UI_STYL_NORMALNY);
    UI_RysujPoleStatusu(8U, 168U, 464U, 52U, ANT_T("Zalecenie", "Guidance", "Hinweis", "Рекомендация"), d, styl);
    UI_RysujWsteczDolny(false);
}

static void ANTENY_WykonajPomiarTwinYagi(void)
{
    memset(&g_twin_pomiar_2m, 0, sizeof(g_twin_pomiar_2m));
    memset(&g_twin_pomiar_70, 0, sizeof(g_twin_pomiar_70));
    g_twin_ma_pomiar = 0U;

    if (!MEASUREMENT_SkanujRezonansAntena(145500000U, &g_twin_pomiar_2m) ||
        !MEASUREMENT_SkanujRezonansAntena(435000000U, &g_twin_pomiar_70))
    {
        GEN_SetMeasurementFreq(0U);
        KOMUNIKAT_PokazTekst(ANT_T("TwinYagi - pomiar", "TwinYagi measurement", "TwinYagi-Messung", "Измерение TwinYagi"),
                             ANT_T("Nie udało się zakończyć obu skanów. Sprawdź OSL i połączenie anteny.",
                                   "Both scans could not be completed. Check OSL and the antenna connection.",
                                   "Beide Scans konnten nicht abgeschlossen werden. OSL und Antennenanschluss pruefen.",
                                   "Не удалось завершить оба сканирования. Проверьте OSL и подключение антенны."));
        return;
    }

    g_twin_ma_pomiar = 1U;
    ANTENY_RysujWynikTwinYagi();
    ANTENY_CzekajNaWstecz();
}

static void ANTENY_WykonajPomiar(void)
{
    uint32_t wymiar_przed;

    if (g_antena.typ == ANTENA_TYP_TWINYAGI_SP2XDQ)
    {
        ANTENY_WykonajPomiarTwinYagi();
        return;
    }

    memset(&g_ostatni_pomiar, 0, sizeof(g_ostatni_pomiar));
    memset(&g_ostatnia_korekta, 0, sizeof(g_ostatnia_korekta));

    if (!MEASUREMENT_SkanujRezonansAntena(g_antena.czestotliwosc_docelowa_hz,
                                           &g_ostatni_pomiar))
    {
        GEN_SetMeasurementFreq(0U);
        KOMUNIKAT_PokazTekst(ANT_T("Pomiar anteny", "Antenna measurement", "Antennenmessung", "Измерение антенны"),
                             ANT_T("Skan przerwano albo nie uzyskano wystarczającej liczby poprawnych punktów.",
                                   "Scan cancelled or too few valid points were collected.",
                                   "Scan abgebrochen oder zu wenige gültige Punkte.",
                                   "Сканирование отменено или слишком мало корректных точек."));
        return;
    }

    g_ostatnia_korekta = ANTENA_BAZA_WyznaczKorekte(&g_antena, &g_ostatni_pomiar);
    g_ma_pomiar = 1U;

    /* Aktualizujemy historię dopiero po policzeniu bieżącej korekty. Dzięki
       temu drugi pomiar po fizycznej zmianie wykorzysta poprzedni punkt. */
    wymiar_przed = ANTENA_BAZA_WymiarStrojony(&g_antena);
    if (g_ostatni_pomiar.rezonans_znaleziony && wymiar_przed != 0U)
    {
        g_antena.poprzedni_wymiar_strojony_mm = wymiar_przed;
        g_antena.poprzedni_rezonans_hz = g_ostatni_pomiar.f_rezonans_hz;
        (void)ANTENY_ZapiszProfil();
    }

    ANTENY_RysujWynikPomiaru();
    while (TOUCH_IsPressed()) Sleep(10U);
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (TOUCH_Poll(&punkt))
        {
            UI_AKCJA_t akcje[2] = {
                {1, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false},
                {2, ANT_T("Raport", "Report", "Bericht", "Отчёт"), UI_STYL_NORMALNY, true, false}
            };
            int16_t akcja;
            if (UI_CzyDotknietoWstecz(punkt))
            {
                TOUCH_CzekajNaPuszczenie(35U);
                break;
            }
            akcja = UI_ZnajdzAkcjePaska(punkt, 224U, 44U, akcje, 2U);
            if (akcja == 1)
            {
                TOUCH_CzekajNaPuszczenie(35U);
                break;
            }
            if (akcja == 2)
            {
                TOUCH_CzekajNaPuszczenie(35U);
                ANTENY_PokazRaport();
                ANTENY_RysujWynikPomiaru();
            }
        }
        Sleep(10U);
    }
}

static void ANTENY_ObsluzProfil(void)
{
    uint8_t fokus = 0U;

    ANTENY_RysujMenuProfilu(fokus);
    WEJSCIA_WyczyscZdarzenia();
    while (TOUCH_IsPressed()) Sleep(10U);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int16_t akcja = -1;

        if (TOUCH_Poll(&punkt))
        {
            if (UI_CzyDotknietoWstecz(punkt))
            {
                TOUCH_CzekajNaPuszczenie(35U);
                break;
            }
            akcja = ANTENY_AkcjaPoDotyku(punkt);
            if (akcja >= 0)
                TOUCH_CzekajNaPuszczenie(35U);
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
            zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
                fokus = (uint8_t)((fokus + 1U) % ANTENY_LICZBA_AKCJI);
            else
                fokus = (uint8_t)((fokus + ANTENY_LICZBA_AKCJI - 1U) % ANTENY_LICZBA_AKCJI);
            ANTENY_RysujMenuProfilu(fokus);
            continue;
        }
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
            akcja = (int16_t)fokus;

        switch (akcja)
        {
        case 0: ANTENY_ZmienCel(); break;
        case 1: ANTENY_EdytujWymiary(); break;
        case 2: ANTENY_PokazLiterature(); break;
        case 3: ANTENY_PokazBudowe(); break;
        case 4: ANTENY_WykonajPomiar(); break;
        case 5: ANTENY_PokazRaport(); break;
        default: break;
        }

        if (akcja >= 0)
        {
            WEJSCIA_WyczyscZdarzenia();
            ANTENY_RysujMenuProfilu(fokus);
            while (TOUCH_IsPressed()) Sleep(10U);
        }
        Sleep(10U);
    }
}

void ANTENY_Otworz(void)
{
    for (;;)
    {
        const int16_t wybor = ANTENY_WybierzTyp();
        if (wybor < 0)
            break;

        if (wybor == 100)
        {
            if (!ANTENY_WczytajProfil())
            {
                KOMUNIKAT_PokazTekst(ANT_T("Profil anteny", "Antenna profile", "Antennenprofil", "Профиль антенны"),
                                     ANT_T("Nie udało się odczytać /aa/anteny/ostatnia.ant",
                                           "Could not read /aa/anteny/ostatnia.ant",
                                           "/aa/anteny/ostatnia.ant konnte nicht gelesen werden",
                                           "Не удалось прочитать /aa/anteny/ostatnia.ant"));
                continue;
            }
        }
        else
        {
            ANTENY_UtworzNowy((ANTENA_TYP_t)wybor);
        }

        ANTENY_ObsluzProfil();
        WEJSCIA_WyczyscZdarzenia();
    }

    GEN_SetMeasurementFreq(0U);
    WEJSCIA_WyczyscZdarzenia();
}


/* ========================================================================
 * Renderery do instrukcji. Używają profilu tymczasowego i przywracają stan
 * użytkownika po narysowaniu. Nie zapisują SD i nie uruchamiają RF.
 * ======================================================================== */
typedef struct
{
    ANTENA_PROFIL_t profil;
    STROJENIE_ANTENA_WYNIK_t pomiar;
    ANTENA_KOREKTA_t korekta;
    uint8_t ma_profil;
    uint8_t ma_pomiar;
    STROJENIE_ANTENA_WYNIK_t twin_2m;
    STROJENIE_ANTENA_WYNIK_t twin_70;
    uint8_t twin_ma_pomiar;
} ANTENY_DOK_STAN_t;

static void ANTENY_DokZapiszStan(ANTENY_DOK_STAN_t *stan)
{
    if (stan == NULL) return;
    stan->profil = g_antena;
    stan->pomiar = g_ostatni_pomiar;
    stan->korekta = g_ostatnia_korekta;
    stan->ma_profil = g_ma_profil;
    stan->ma_pomiar = g_ma_pomiar;
    stan->twin_2m = g_twin_pomiar_2m;
    stan->twin_70 = g_twin_pomiar_70;
    stan->twin_ma_pomiar = g_twin_ma_pomiar;
}

static void ANTENY_DokPrzywrocStan(const ANTENY_DOK_STAN_t *stan)
{
    if (stan == NULL) return;
    g_antena = stan->profil;
    g_ostatni_pomiar = stan->pomiar;
    g_ostatnia_korekta = stan->korekta;
    g_ma_profil = stan->ma_profil;
    g_ma_pomiar = stan->ma_pomiar;
    g_twin_pomiar_2m = stan->twin_2m;
    g_twin_pomiar_70 = stan->twin_70;
    g_twin_ma_pomiar = stan->twin_ma_pomiar;
}

static void ANTENY_DokUstawProfil(ANTENA_TYP_t typ)
{
    const uint32_t cel = (typ == ANTENA_TYP_W3DZZ) ? 7050000U :
                         (typ == ANTENA_TYP_DIPOL_POLFALOWY) ? 7100000U :
                         (typ == ANTENA_TYP_EFHW_40_10) ? 7100000U :
                         (typ == ANTENA_TYP_TWINYAGI_SP2XDQ) ? 145500000U : 145000000U;
    ANTENA_BAZA_InicjalizujProfil(&g_antena, typ, cel);
    g_ma_profil = 1U;
    g_ma_pomiar = 0U;
    memset(&g_ostatni_pomiar, 0, sizeof(g_ostatni_pomiar));
    memset(&g_ostatnia_korekta, 0, sizeof(g_ostatnia_korekta));
    g_twin_ma_pomiar = 0U;
}

uint32_t ANTENY_DokumentacjaLiczbaStron(void)
{
    /* wybór + starsze profile + TwinYagi + Yagi DK7ZB + EFHW */
    return 21U;
}

const char *ANTENY_DokumentacjaNazwaStrony(uint32_t strona)
{
    static const char *const nazwy[] = {
        "anteny_wybor",
        "dipol_menu", "dipol_literatura",
        "w3dzz_menu", "w3dzz_pulapki",
        "quad1_menu", "quad1_budowa",
        "quad2_menu", "quad2_elementy",
        "quad3_menu", "quad3_elementy",
        "twinyagi_menu", "twinyagi_projekt", "twinyagi_elementy", "twinyagi_balun",
        "yagi3_menu", "yagi3_projekt", "yagi3_budowa",
        "efhw_menu", "efhw_projekt", "efhw_transformator"
    };
    return strona < 21U ? nazwy[strona] : "anteny";
}

void ANTENY_DokumentacjaRysujStrone(uint32_t strona)
{
    ANTENY_DOK_STAN_t stan;
    ANTENY_DokZapiszStan(&stan);

    if (strona == 0U)
    {
        ANTENY_RysujWybor(0U);
        ANTENY_DokPrzywrocStan(&stan);
        return;
    }

    if (strona <= 2U) ANTENY_DokUstawProfil(ANTENA_TYP_DIPOL_POLFALOWY);
    else if (strona <= 4U) ANTENY_DokUstawProfil(ANTENA_TYP_W3DZZ);
    else if (strona <= 6U) ANTENY_DokUstawProfil(ANTENA_TYP_QUAD_1EL);
    else if (strona <= 8U) ANTENY_DokUstawProfil(ANTENA_TYP_QUAD_2EL);
    else if (strona <= 10U) ANTENY_DokUstawProfil(ANTENA_TYP_QUAD_3EL);
    else if (strona <= 14U) ANTENY_DokUstawProfil(ANTENA_TYP_TWINYAGI_SP2XDQ);
    else if (strona <= 17U) ANTENY_DokUstawProfil(ANTENA_TYP_YAGI3_DK7ZB_2M);
    else ANTENY_DokUstawProfil(ANTENA_TYP_EFHW_40_10);

    switch (strona)
    {
    case 11U: case 15U: case 18U:
        ANTENY_RysujMenuProfilu(0U); break;
    case 12U: case 16U: case 19U:
        ANTENY_RysujLiterature(); break;
    case 13U: case 17U: case 20U:
        ANTENY_RysujBudowe(); break;
    case 14U:
        ANTENY_RysujTwinZasilanie(); break;
    default:
        if ((strona & 1U) != 0U) ANTENY_RysujMenuProfilu(0U);
        else if (strona == 2U) ANTENY_RysujLiterature();
        else ANTENY_RysujBudowe();
        break;
    }

    ANTENY_DokPrzywrocStan(&stan);
}
