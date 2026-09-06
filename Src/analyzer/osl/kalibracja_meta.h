#ifndef KALIBRACJA_META_H_
#define KALIBRACJA_META_H_

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    KAL_META_HW = 0,
    KAL_META_OSL,
    KAL_META_S21,
    /* Numer pozostaje zarezerwowany dla zgodnosci historycznych logow.
     * V2.1 nie posiada osobnej kalibracji L/C i nie tworzy plikow .lc. */
    KAL_META_LC,
    KAL_META_TDR,
    KAL_META_BATERIA,
    /* Kalibracja pustego uchwytu kwarcu jest sesyjna, ale jej ślad w logu
     * pomaga odtworzyć diagnostykę pomiaru kwarcu po fakcie. */
    KAL_META_KWARC,
    /*
     * Potwierdzenie poprawności generatora i toru IF nie tworzy osobnego
     * pliku współczynników, ale powinno mieć trwały znacznik czasu. Dzięki
     * temu ekran Kalibracja nie myli „nie wykonano w tej sesji” z rzeczywistym
     * brakiem sprawdzenia urządzenia.
     */
    KAL_META_IF
} KAL_META_TYP_t;

typedef struct
{
    uint8_t istnieje;
    uint8_t format;
    uint8_t czas_z_rtc;
    uint32_t data_yyyymmdd;
    uint32_t czas_hhmmss;
    uint32_t fmin_hz;
    uint32_t fmax_hz;
    uint32_t si5351_max_hz;
    uint32_t typ_syntezy;
    uint32_t plan_harmoniczny;
    uint32_t harmoniczna_max;
    uint32_t si5351_xtal_hz;
    int32_t si5351_korekcja_hz;

    /* Wersja matematyki nakładania korekcji HW. Zmiana algorytmu może
     * unieważnić OSL wykonane na wcześniejszej ścieżce nawet przy tym samym
     * pliku errcorr.osl. */
    uint32_t korekcja_hw_wersja;
    int32_t profil;
    char firmware[24];

    /* Temperatura pochodzi z czujnika wewnętrznego DS3231. Nie jest to
     * temperatura toru RF, dlatego służy wyłącznie do porównań diagnostycznych. */
    uint8_t temperatura_rtc_dostepna;
    int32_t temperatura_rtc_c_x100;

    /* CRC pozwala rozpoznać podmianę/uszkodzenie pliku po wykonaniu kalibracji.
     * Nie uczestniczy w obliczeniach pomiaru i nigdy sam nie blokuje pracy. */
    uint8_t crc_pliku_dostepny;
    uint32_t crc_pliku;

    /* OSL powstaje z użyciem korekcji sprzętowej HW. Zapamiętany CRC
     * pliku HW pozwala później stwierdzić, czy HW zostało od tego czasu
     * przeliczone, nawet gdy zegar RTC był przestawiany. */
    uint8_t crc_hw_zaleznosc_dostepna;
    uint32_t crc_hw_zaleznosc;
} KAL_META_DANE_t;

typedef enum
{
    KAL_META_UWAGA_BRAK = 0U,
    KAL_META_UWAGA_BRAK_META = 1U << 0,
    KAL_META_UWAGA_BRAK_RTC = 1U << 1,
    KAL_META_UWAGA_STARY_FORMAT = 1U << 2,
    KAL_META_UWAGA_KONFIGURACJA = 1U << 3,
    KAL_META_UWAGA_BRAK_PLIKU = 1U << 4,
    KAL_META_UWAGA_PLIK_ZMIENIONY = 1U << 5,
    KAL_META_UWAGA_HW_ZMIENIONE = 1U << 6,
    KAL_META_UWAGA_RTC_COFNIETY = 1U << 7
} KAL_META_UWAGA_t;

typedef struct
{
    uint32_t uwagi;
    uint8_t wiek_dostepny;
    uint32_t wiek_dni;
    uint8_t crc_pliku_biezacy_dostepny;
    uint32_t crc_pliku_biezacy;
    uint8_t crc_hw_biezacy_dostepny;
    uint32_t crc_hw_biezacy;
} KAL_META_OCENA_t;

/*
 * Zapisuje metadane dopiero po poprawnym zakończeniu właściwej kalibracji.
 * Błąd metadanych nie unieważnia samych współczynników kalibracyjnych, bo
 * informacje te są diagnostyczne i nie uczestniczą w obliczeniach pomiaru.
 */
int KAL_META_Zapisz(KAL_META_TYP_t typ, int32_t profil);

/* Usuwa wyłącznie metadane wskazanej kalibracji. Funkcja jest używana przy
 * świadomym kasowaniu profilu OSL oraz po zmianie wartości wzorców, gdy stary
 * profil nie może być dalej przedstawiany jako zgodny z nowym zestawem. */
int KAL_META_Usun(KAL_META_TYP_t typ, int32_t profil);

/* Odczytuje ostatni zapis danego rodzaju. Zwraca 1, gdy metadane istnieją. */
int KAL_META_Pobierz(KAL_META_TYP_t typ, int32_t profil, KAL_META_DANE_t *dane);

/* Porównuje tylko znaczniki pochodzące z wiarygodnego RTC. */
int KAL_META_CzyStarsza(const KAL_META_DANE_t *pierwsza, const KAL_META_DANE_t *druga);

/* Sprawdza, czy zapis powstał dla bieżącego zakresu i ustawień syntezy. */
int KAL_META_CzyZgodnaZKonfiguracja(const KAL_META_DANE_t *dane);

/* Oblicza CRC rzeczywistego pliku współczynników danego rodzaju. Dla kalibracji
 * bez osobnego pliku (TDR, akumulator, kwarc) zwraca 0. */
int KAL_META_ObliczCRCPliku(KAL_META_TYP_t typ, int32_t profil, uint32_t *crc32);

/* Buduje wyłącznie ocenę diagnostyczną. Żaden bit `uwagi` nie wyłącza pomiaru,
 * nie kasuje kalibracji i nie zmienia jej współczynników. */
void KAL_META_Ocen(KAL_META_TYP_t typ, int32_t profil,
                   const KAL_META_DANE_t *dane, KAL_META_OCENA_t *ocena);

void KAL_META_FormatujCzas(const KAL_META_DANE_t *dane, char *bufor, size_t rozmiar);
const char *KAL_META_Nazwa(KAL_META_TYP_t typ);

/*
 * Licznik sesyjny zwiększa się po każdej poprawnie zapisanej kalibracji HW.
 * Pozwala modułom zależnym od HW natychmiast wyłączyć stary profil bez
 * kosztownego liczenia CRC przy każdym pomiarze. Po restarcie zgodność jest
 * ponownie sprawdzana CRC rzeczywistego pliku errcorr.osl.
 */
uint32_t KAL_META_PobierzGeneracjeHW(void);

#endif
