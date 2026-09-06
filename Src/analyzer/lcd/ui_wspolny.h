#ifndef _UI_WSPOLNY_H_
#define _UI_WSPOLNY_H_

#include <stdbool.h>
#include <stdint.h>
#include "LCD.h"
#include "ui_lista_logika.h"

typedef enum
{
    UI_STYL_NORMALNY = 0,
    UI_STYL_AKCENT,
    UI_STYL_POWROT,
    UI_STYL_AKTYWNY,
    UI_STYL_NIEAKTYWNY,
    UI_STYL_OSTRZEZENIE
} UI_STYL_t;

typedef enum
{
    UI_MOTYW_KLASYCZNY = 0,
    UI_MOTYW_LICZBA
} UI_MOTYW_t;

typedef enum
{
    UI_ROLA_TEKSTU_TYTUL = 0,
    UI_ROLA_TEKSTU_PRZYCISK,
    UI_ROLA_TEKSTU_ETYKIETA,
    UI_ROLA_TEKSTU_WARTOSC,
    UI_ROLA_TEKSTU_WARTOSC_GLOWNA,
    UI_ROLA_TEKSTU_POMOC
} UI_ROLA_TEKSTU_t;

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t szerokosc;
    uint16_t wysokosc;
} UI_PROSTOKAT_t;

typedef struct
{
    int16_t id;
    UI_PROSTOKAT_t obszar;
    const char *tekst;
    UI_STYL_t styl;
    UI_ROLA_TEKSTU_t rola_tekstu;
    bool aktywna;
    bool zaznaczona;
} UI_KONTROLKA_t;

typedef struct
{
    const char *nazwa;
    const char *opis;
    bool dostepna;
} UI_METODA_OPCJA_t;

typedef struct
{
    int16_t id;
    const char *tekst;
    UI_STYL_t styl;
    bool aktywna;
    bool zaznaczona;
} UI_AKCJA_t;


typedef enum
{
    UI_IKONA_POJEDYNCZY = 0,
    UI_IKONA_WYKRES_SWR,
    UI_IKONA_WIELE_PASM,
    UI_IKONA_STROJENIE,
    UI_IKONA_S21,
    UI_IKONA_SZUKAJ_F,
    UI_IKONA_KWARC,
    UI_IKONA_TDR,
    UI_IKONA_LC,
    UI_IKONA_DSP,
    UI_IKONA_USTAWIENIA,
    UI_IKONA_ZRZUTY,
    UI_IKONA_USB,
    UI_IKONA_GENERATOR,
    UI_IKONA_CYFROWE,
    UI_IKONA_DZIAL_POMIAR,
    UI_IKONA_DZIAL_ANALIZA,
    UI_IKONA_DZIAL_NARZEDZIA,
    UI_IKONA_DZIAL_KALIBRACJA,
    UI_IKONA_DZIAL_USTAWIENIA,
    UI_IKONA_DZIAL_PLIKI,
    UI_IKONA_SMITH,
    UI_IKONA_ZAPISANE,
    UI_IKONA_DEKODER_CW,
    UI_IKONA_KAL_OSL,
    UI_IKONA_KAL_SPRZETOWA,
    UI_IKONA_KAL_S21,
    UI_IKONA_KAL_STAN,
    UI_IKONA_KAL_WERYFIKACJA,
    UI_IKONA_URZADZENIE,
    UI_IKONA_METODY,
    UI_IKONA_POMOC,
    UI_IKONA_SERWIS,
    UI_IKONA_MENEDZER,
    UI_IKONA_I2C,
    UI_IKONA_SI5351,
    UI_IKONA_TESTY,
    UI_IKONA_ASYSTENT,
    UI_IKONA_STAN_SYSTEMU,
    UI_IKONA_RAPORT,
    /*
     * Ikony dodane w v2.03.2-test21. Są dopisane na końcu, aby nie
     * zmieniać historycznych numerów bitmap 0..39 używanych przez starsze
     * konfiguracje i testy zgodności.
     */
    UI_IKONA_WYGLAD_OBSLUGA,
    UI_IKONA_LACZNOSC_VNA,
    UI_IKONA_WZORCE_DOKLADNE,
    UI_IKONA_PROJEKTOWANIE_ANTEN,
    UI_IKONA_AKUMULATOR,

    /*
     * ANTICON1: osobne symbole typów anten używane w module
     * „Projektowanie anten”. Są dopisane na końcu, aby nie zmieniać
     * numerów wszystkich dotychczasowych ikon i zgodności konfiguracji.
     */
    UI_IKONA_ANT_DIPOL,
    UI_IKONA_ANT_W3DZZ,
    UI_IKONA_ANT_QUAD_1,
    UI_IKONA_ANT_QUAD_2,
    UI_IKONA_ANT_QUAD_3,
    UI_IKONA_ANT_TWINYAGI,
    UI_IKONA_ANT_YAGI3,
    UI_IKONA_ANT_EFHW,

    /* Osobna ikona RTC dla ustawienia daty i czasu. Dopisana na końcu, aby
     * nie zmieniać historycznych numerów 0..52. */
    UI_IKONA_DATA_CZAS,

    /*
     * Osobne ikony ustawień. Dopisane na końcu, aby zachować numery 0..53.
     * DSP pozostaje ikoną narzędzia obróbki sygnału, a zwykły dźwięk ma
     * czytelny symbol słuchawek. Profil użytkownika nie udaje urządzenia.
     */
    UI_IKONA_DZWIEK,
    UI_IKONA_UZYTKOWNIK,
    UI_IKONA_MENU_LICZBA
} UI_IKONA_MENU_t;

/*
 * Finalny wygląd V2.1 został ujednolicony do jednego dopracowanego wariantu
 * Retro. Pozostawiamy historyczne pole konfiguracji, ale renderer nie
 * udostępnia już drugiego, słabszego wizualnie zestawu.
 */
#define UI_LICZBA_ZESTAWOW_IKON 1U

/* Nazwy zgodne z zatwierdzonym mapowaniem, bez zmiany historycznych numerów. */
#define UI_IKONA_KWARCE UI_IKONA_KWARC
#define UI_IKONA_DSP_AUDIO UI_IKONA_DZWIEK
#define UI_IKONA_WYGLAD UI_IKONA_USTAWIENIA
#define UI_IKONA_TRYBY_CYFROWE UI_IKONA_CYFROWE

typedef enum
{
    UI_WIERSZ_ZWYKLY = 0,
    UI_WIERSZ_Z_WARTOSCIA,
    UI_WIERSZ_Z_PRZELACZNIKIEM
} UI_WIERSZ_TYP_t;

typedef struct
{
    int16_t id;
    const char *tekst;
    const char *wartosc;
    UI_STYL_t styl;
    UI_WIERSZ_TYP_t typ;
    bool aktywny;
    bool przelacznik_wlaczony;
    uint16_t ikona_plus_jeden; /* 0 = bez ikony; inaczej UI_IKONA_MENU_t + 1. */
} UI_WIERSZ_LISTY_t;

#define UI_WYSOKOSC_PASKA_GORNEGO 32U
#define UI_WYSOKOSC_WIERSZA_LISTY 40U
#define UI_ODSTEP_WIERSZY_LISTY 4U
#define UI_SZEROKOSC_PASKA_PRZEWIJANIA 4U
#define UI_MIN_WYSOKOSC_UCHWYTU_PRZEWIJANIA 12U

typedef struct
{
    const char *nazwa;
    const char *wersja;
    const char *znak;
    uint32_t data;
    uint32_t czas;
    bool rtc_obecny;
    bool karta_sd_obecna;
    bool rf_aktywne;
    bool bateria_obecna;
    float napiecie_baterii;
    int procent_baterii;
} UI_STATUS_t;

/*
 * Dostawca bieżącego statusu urządzenia. Warstwa UI nie odczytuje sprzętu
 * bezpośrednio: ekran główny rejestruje jedną funkcję, a wszystkie ekrany
 * korzystają z tych samych danych o czasie, SD, RF i akumulatorze.
 */
typedef bool (*UI_DOSTAWCA_STATUSU_t)(UI_STATUS_t *status);

/*
 * Wspolne elementy interfejsu EU1KY-PL 2026.
 * Wszystkie ekrany powinny korzystac z tych funkcji zamiast rysowac
 * przyciski na wlasna reke. Dzieki temu zmiana wygladu w jednym miejscu
 * poprawia caly program i nie narusza logiki pomiarowej.
 */
UI_MOTYW_t UI_PobierzMotyw(void);
void UI_UstawMotyw(UI_MOTYW_t motyw);
UI_MOTYW_t UI_PrzelaczMotyw(void);
const char *UI_PobierzNazweMotywu(void);

/*
 * Tymczasowe wymuszenie zestawu ikon wyłącznie na potrzeby renderowania.
 * Nie zmienia CFG_PARAM_ZESTAW_IKON i nie zapisuje niczego do config.bin.
 * -1 = normalny styl użytkownika, 0 = Retro, 1 = Klasyczny niebieski.
 */
void UI_UstawZestawIkonTymczasowy(int8_t zestaw);
void UI_FormatujCzestotliwoscMHz(uint32_t czestotliwosc_hz, char *bufor, uint32_t rozmiar_bufora);
void UI_FormatujCzestotliwoscMHzKrotko(uint32_t czestotliwosc_hz, char *bufor, uint32_t rozmiar_bufora);

LCDColor UI_KolorTlaEkranu(void);
LCDColor UI_KolorTlaPola(void);
LCDColor UI_KolorTlaPrzycisku(UI_STYL_t styl);
LCDColor UI_KolorRamki(UI_STYL_t styl);
LCDColor UI_KolorTekstu(UI_STYL_t styl);

void UI_WyczyscEkran(void);
void UI_UstawDostawceStatusu(UI_DOSTAWCA_STATUSU_t dostawca);
bool UI_PobierzBiezacyStatus(UI_STATUS_t *status);
void UI_RysujNaglowek(const char *tytul);
void UI_RysujPasekStanu(const UI_STATUS_t *status);
/* Nowy pasek gorny UX 2026: staly przycisk Wstecz, tytul i kompaktowy status. */
void UI_RysujPasekGorny(const char *tytul, bool pokaz_wstecz, bool fokus_wstecz,
                        const UI_STATUS_t *status);
bool UI_CzyDotknietoWstecz(LCDPoint punkt);
void UI_RysujPrzycisk(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                      const char *tekst, UI_STYL_t styl, uint32_t font);
void UI_RysujPrzyciskZaznaczony(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                                const char *tekst, UI_STYL_t styl, uint32_t font, uint8_t zaznaczony);
void UI_RysujPoleWartosci(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                          const char *etykieta, const char *wartosc);
void UI_RysujPoleInformacyjne(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                              const char *etykieta, const char *wartosc);
void UI_RysujPolePasywne(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                         const char *tekst, uint32_t font);
void UI_RysujPoleStatusu(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                        const char *etykieta, const char *wartosc, UI_STYL_t styl);
void UI_RysujWierszDanych(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                         const char *etykieta, const char *wartosc, UI_STYL_t styl);
void UI_RysujPoleLiczboweGlowne(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                                const char *etykieta, const char *liczba, const char *jednostka);
void UI_RysujPanel(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                   const char *tytul, UI_STYL_t styl);
void UI_RysujEkranPrzejsciowy(const char *tytul, const char *opis);
void UI_RysujKafelMenu(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                       UI_IKONA_MENU_t ikona, const char *tekst, bool zaznaczony);

/*
 * Kafel poziomu 0 ma celowo nieco bogatszą oprawę niż kafle podmenu.
 * To ekran powitalny programu, dlatego dostaje wyraźniejszą hierarchię:
 * osobny akcent, spokojniejszy pasek podpisu i mocniejszy fokus enkodera.
 * Geometria i hit-test pozostają po stronie mainwnd.c.
 */
void UI_RysujKafelMenuGlownego(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                               UI_IKONA_MENU_t ikona, const char *tekst, bool zaznaczony);

/* Dolny pas menu głównego: wskazówka bez fokusu albo opis wybranego działu. */
void UI_RysujPodpowiedzMenuGlownego(const char *tytul, const char *opis, bool aktywna);

/*
 * Standardowa siatka kafelków dla ekranów wyboru.
 *
 * 3 x 2 daje sześć dużych pól 150 x 84 px. To jest świadomy kompromis
 * między czytelnością a liczbą pozycji: dotyk ma pierwszeństwo przed
 * upychaniem większej liczby funkcji na jednym ekranie. Jeżeli pozycji jest
 * więcej niż sześć, moduł ma pokazać następną stronę zamiast zmniejszać kafle.
 *
 * Historyczne nazwy funkcji z członem „Kompaktowy” pozostają w API, aby nie
 * powodować niepotrzebnej migracji wielu modułów. Ich geometria od wcześniejszej weryfikacji
 * jest jednak geometrią standardowego kafla 3 x 2.
 */
#define UI_SIATKA_KOMPAKT_KOLUMNY       3U
#define UI_SIATKA_KOMPAKT_WIERSZE       2U
#define UI_SIATKA_KOMPAKT_NA_STRONE     6U
#define UI_KAFEL_STANDARD_SZEROKOSC    150U
#define UI_KAFEL_STANDARD_WYSOKOSC      84U
UI_PROSTOKAT_t UI_ObszarKaflaKompaktowego(uint16_t indeks);
void UI_RysujKafelKompaktowy(uint16_t indeks, UI_IKONA_MENU_t ikona,
                             const char *tekst, bool zaznaczony, bool aktywny);
void UI_RysujKafelKompaktowyZestaw(uint16_t indeks, UI_IKONA_MENU_t ikona,
                                   uint8_t zestaw_ikon, const char *tekst,
                                   bool zaznaczony, bool aktywny);

/*
 * Kontrolka stanu funkcji — mała zielona dioda w rogu kafla, jak lampka
 * stanu na przednim panelu przyrządu.
 *
 * Rozróżnienie jest celowe i nie wolno go mylić:
 *   zaznaczony -> gdzie jestem teraz (fokus, nawigacja),
 *   wlaczona   -> co jest włączone i działa (stan urządzenia).
 * Jedno mówi o kursorze, drugie o przyrządzie. Do tej pory istniało tylko
 * pierwsze, więc z ekranu menu nie dało się odczytać, co jest aktywne.
 */
void UI_RysujKafelKompaktowyZeStanem(uint16_t indeks, UI_IKONA_MENU_t ikona,
                                     const char *tekst, bool zaznaczony,
                                     bool aktywny, bool wlaczona);
void UI_RysujKontrolkeStanuKafla(const UI_PROSTOKAT_t *obszar, bool wlaczona);
void UI_RysujKontrolkeStanuKaflaZestawu(const UI_PROSTOKAT_t *obszar, bool wlaczona,
                                         uint8_t zestaw_ikon);
int16_t UI_KafelKompaktowyPoDotyku(LCDPoint punkt, uint16_t liczba);

/*
 * Jeden standard przycisku powrotu w całym UI 2026. Geometria jest zgodna
 * z wygodnym przyciskiem używanym wcześniej w Strojenie: 70 x 45 px w lewym
 * dolnym rogu.
 */
#define UI_DOLNY_PASEK_Y 220U

/*
 * Ekran startowy ma własną, niższą podpowiedź. Nie może korzystać z
 * UI_DOLNY_PASEK_Y, bo tam siedzi przycisk Wstecz o wysokości 45 px i po
 * przesunięciu wyszedłby poza ekran. Przy 228 px zostaje 44 px na tytuł
 * działu i dwuwierszowy opis, a kafle mogą sięgać do 226.
 */
#define UI_PODPOWIEDZ_GLOWNA_Y 228U

/* Pasek stanu ekranu startowego jest wyższy niż zwykły pasek tytułu,
   żeby napisy i ikony nie siedziały na samej krawędzi obudowy. */
#define UI_WYSOKOSC_PASKA_STANU 38U
#define UI_DOLNY_PRZYCISK_SZEROKOSC 70U
#define UI_DOLNY_PRZYCISK_WYSOKOSC 45U
#define UI_DOLNY_PRZYCISK_ODSTEP 12U
#define UI_DOLNY_PASEK_MAKS_AKCJI 6U
UI_PROSTOKAT_t UI_ObszarWsteczDolny(void);
UI_PROSTOKAT_t UI_ObszarPrzyciskuDolnego(uint8_t pozycja);
void UI_RysujWsteczDolny(bool fokus);
/* Kafel z dodatkową wartością w polu roboczym, np. częstotliwością celu. */
void UI_RysujKafelMenuZWartoscia(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                                 UI_IKONA_MENU_t ikona, const char *etykieta,
                                 const char *wartosc, bool zaznaczony);
void UI_RysujIkoneMenuZestawu(UI_IKONA_MENU_t ikona, uint8_t zestaw,
                              uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc);


/*
 * Fundament spójnego interfejsu od v2.03.
 * Moduły przekazują geometrię i stan, a nie własny sposób rysowania.
 */
UI_PROSTOKAT_t UI_UtworzObszar(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc);
UI_KONTROLKA_t UI_UtworzKontrolke(int16_t id, uint16_t x, uint16_t y,
                                   uint16_t szerokosc, uint16_t wysokosc,
                                   const char *tekst, UI_STYL_t styl,
                                   UI_ROLA_TEKSTU_t rola_tekstu,
                                   bool aktywna, bool zaznaczona);
uint32_t UI_FontDlaRoli(UI_ROLA_TEKSTU_t rola);
bool UI_CzyPunktWObszarze(LCDPoint punkt, const UI_PROSTOKAT_t *obszar);
int16_t UI_ZnajdzKontrolke(LCDPoint punkt, const UI_KONTROLKA_t *kontrolki, uint16_t liczba);
void UI_RysujKontrolke(const UI_KONTROLKA_t *kontrolka);
void UI_RysujKontrolki(const UI_KONTROLKA_t *kontrolki, uint16_t liczba);

/* Rowny pasek duzych akcji. Rysowanie i hit-test korzystaja z tej samej geometrii. */
void UI_RysujPasekAkcji(uint16_t y, uint16_t wysokosc,
                         const UI_AKCJA_t *akcje, uint8_t liczba);
int16_t UI_ZnajdzAkcjePaska(LCDPoint punkt, uint16_t y, uint16_t wysokosc,
                            const UI_AKCJA_t *akcje, uint8_t liczba);

/*
 * Jeden wzorzec wyboru wariantu/metody w całym firmware.
 * Strzałki po bokach zmieniają pozycję, środek jest polem informacyjnym.
 * Funkcja obsługi omija pozycje oznaczone jako niedostępne.
 */
void UI_RysujWyborMetody(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                         const char *etykieta, const UI_METODA_OPCJA_t *opcje,
                         uint8_t liczba_opcji, uint8_t wybrana);
uint8_t UI_WyborMetodyPoDotyku(LCDPoint punkt, uint16_t x, uint16_t y,
                               uint16_t szerokosc, uint16_t wysokosc,
                               const UI_METODA_OPCJA_t *opcje,
                               uint8_t liczba_opcji, uint8_t wybrana);
uint8_t UI_NastepnaDostepnaMetoda(const UI_METODA_OPCJA_t *opcje,
                                  uint8_t liczba_opcji, uint8_t wybrana,
                                  int8_t kierunek);

/* Przewijane listy UX 2026. Wszystko jest rysowane bez bufora ekranowego. */
void UI_RysujWierszListy(uint16_t x, uint16_t y, uint16_t szerokosc,
                         const UI_WIERSZ_LISTY_t *wiersz, bool fokus);
void UI_RysujListe(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                   const UI_WIERSZ_LISTY_t *wiersze, uint16_t liczba,
                   const UI_LISTA_STAN_t *stan);
int16_t UI_ListaIndeksPoDotyku(LCDPoint punkt, uint16_t x, uint16_t y,
                               uint16_t szerokosc, uint16_t wysokosc,
                               const UI_LISTA_STAN_t *stan);
void UI_RysujPasekPrzewijania(uint16_t x, uint16_t y, uint16_t wysokosc,
                              const UI_LISTA_STAN_t *stan);
void UI_RysujPrzelacznik(uint16_t x, uint16_t y, bool wlaczony, bool fokus, bool aktywny);
void UI_RysujWskaznikPostepu(uint16_t x, uint16_t y, uint16_t szerokosc, uint16_t wysokosc,
                             uint8_t procent, const char *tekst);


/*
 * Wspólny renderer wodospadu 480 px. Bufor linii jest współdzielony przez
 * ekrany, które nigdy nie pracują jednocześnie (DSP audio i skaner RF).
 * Dzięki temu oba narzędzia mają identyczną paletę bez dublowania 1,9 kB RAM.
 * Poziom 0..1023 przechodzi: niebieski -> turkus -> zielony -> żółty -> czerwony.
 */
#define UI_WODOSPAD_SZEROKOSC 480U
uint32_t UI_WodospadKolor(uint16_t poziom);
uint32_t *UI_WodospadBuforLinii(void);
uint16_t *UI_WodospadBuforPoziomow(void);
void UI_WodospadWyczysc(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void UI_WodospadDodajLinie(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                           const uint32_t *linia);
void UI_WodospadHistoriaResetuj(void);
void UI_WodospadHistoriaDodaj(const uint16_t *poziomy);
void UI_WodospadHistoriaRysuj(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

#endif
