#include "splash.h"

#include <stddef.h>
#include <stdint.h>

#include "LCD.h"
#include "font.h"
#include "jezyk.h"

/*
 * Ekran startowy UI18.
 *
 * Tlo 480x272 pozostaje osobnym obrazem z antenami delta. Napisy NIE sa juz
 * skalowanymi maskami alfa. Na fizycznym TFT skalowanie 5/4 i progowanie
 * antyaliasowanej maski dawalo poszarpane/nieostre krawedzie, szczegolnie dla
 * "2026". UI18 rysuje tekst natywnymi bitmapowymi fontami analizatora w skali
 * 1:1. FONT_Write z bg=0 zachowuje przezroczyste tlo, wiec grafika anteny nie
 * jest przykrywana prostokatem.
 */
#define SPLASH_TYTUL_X          18U
#define SPLASH_TYTUL_Y          190U
#define SPLASH_OPIS_X           20U
#define SPLASH_OPIS_Y           222U

static void SPLASH_RysujTekstOstry(FONTS font, uint16_t x, uint16_t y,
                                    const char *tekst, LCDColor kolor)
{
    const LCDColor obrys = LCD_RGB(3, 18, 30);

    if (tekst == NULL || tekst[0] == '\0')
        return;

    /*
     * Jednopikselowy obrys w czterech kierunkach daje kontrast na jasnym
     * niebie, ale nie tworzy przesunietej kopii litery jak dawny cien.
     * Sam glif jest rysowany na koncu dokladnie 1:1.
     */
    if (x > 0U)
        FONT_Write(font, obrys, 0, (uint16_t)(x - 1U), y, tekst);
    FONT_Write(font, obrys, 0, (uint16_t)(x + 1U), y, tekst);
    if (y > 0U)
        FONT_Write(font, obrys, 0, x, (uint16_t)(y - 1U), tekst);
    FONT_Write(font, obrys, 0, x, (uint16_t)(y + 1U), tekst);
    FONT_Write(font, kolor, 0, x, y, tekst);
}

static void SPLASH_RysujTekstPogrubiony(FONTS font, uint16_t x, uint16_t y,
                                         const char *tekst, LCDColor kolor)
{
    if (tekst == NULL || tekst[0] == '\0')
        return;

    /*
     * Subtelne pogrubienie sprawia, że podpis wygląda na odrobinę większy
     * i czytelniejszy, ale bez wprowadzania kolejnej skali czy drugiego fontu.
     */
    SPLASH_RysujTekstOstry(font, x, y, tekst, kolor);
    SPLASH_RysujTekstOstry(font, (uint16_t)(x + 1U), y, tekst, kolor);
}

void SPLASH_RysujNapisy(void)
{
    char nazwa[32];
    const char *opis;

    JEZYK_FormatujNazweProjektu(nazwa, sizeof(nazwa));
    opis = JEZYK_Tekst(TEKST_PODTYTUL_ANALIZATOR_ANTENOWY);

    /*
     * Jeden, jednolity tytul jest znacznie czytelniejszy niz osobne maski
     * "EU1KY-", kod jezyka i konturowy rok. Bialy bitmapowy FONT_FRANBIG ma
     * wysokosc 32 px i na LCD F746 pozostaje ostry piksel w piksel.
     */
    SPLASH_RysujTekstOstry(FONT_FRANBIG, SPLASH_TYTUL_X, SPLASH_TYTUL_Y,
                              nazwa, LCD_RGB(255, 255, 255));

    /* Podtytul korzysta z tego samego sprawdzonego fontu co ekrany robocze.
     * FONT_MENUS na fizycznym LCD znieksztalcal pierwsza litere A. */
    SPLASH_RysujTekstPogrubiony(FONT_FRAN, SPLASH_OPIS_X, SPLASH_OPIS_Y,
                                   opis, LCD_RGB(235, 248, 255));
}
