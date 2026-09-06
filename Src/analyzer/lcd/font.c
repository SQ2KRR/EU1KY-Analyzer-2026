/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include "font.h"
#include "fran.h"
#include "franbig.h"
#include "consbig.h"
#include "sdigits.h"
#include "bdigits.h"
#include "menufont.h"
#include "rufont.h"
#include "kafelfont.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static LCDColor _fgColor = LCD_WHITE;
static LCDColor _bgColor = LCD_BLACK;
static FONTS _font = FONT_FRAN;



static void FONT_RysujGlifRU(const RU_FONT_GLIF_t *glif, LCDColor fg, uint16_t x, uint16_t y)
{
    uint8_t yy;

    if (glif == NULL || glif->dane == NULL)
        return;

    for (yy = 0U; yy < glif->wysokosc; ++yy)
    {
        uint8_t xx;
        const uint8_t *wiersz = &glif->dane[(uint32_t)yy * glif->bajtow_na_wiersz];
        for (xx = 0U; xx < glif->szerokosc; ++xx)
        {
            if ((wiersz[xx / 8U] & (uint8_t)(0x80U >> (xx % 8U))) != 0U)
                LCD_SetPixel(LCD_MakePoint((uint16_t)(x + xx), (uint16_t)(y + yy)), fg);
        }
    }
}

static void FONT_DrawByte(uint8_t byte, uint8_t nBits, LCDColor fg, uint16_t x, uint16_t y)
{
    uint8_t mask = 1;
    while (nBits--)
    {
        if (byte & mask)
            LCD_SetPixel(LCD_MakePoint(x, y), fg);
        mask <<= 1;
        x -= 1;
        if (0 == mask)
            break;
    }
}

struct _fontparams
{
    const uint8_t **pFont;
    uint8_t charHeight;
    uint8_t charSpacing;
};

/*
 * Oryginalne fonty EU1KY są jednobajtowe i zawierają m.in. cyrylicę.
 * Nowe teksty zapisujemy w UTF-8, a polskie diakrytyki składamy z bazowych
 * liter ASCII. Dzięki temu nie zależymy od nieprawidłowo założonej strony
 * kodowej i zachowujemy zgodność ze starymi napisami firmware.
 */
typedef enum
{
    FONT_AKENT_BRAK = 0,
    FONT_AKENT_KRESKA,
    FONT_AKENT_KROPKA,
    FONT_AKENT_OGONEK,
    FONT_AKENT_PRZEKRESLENIE,
    FONT_AKENT_UMLAUT,
    FONT_AKENT_TYLDA
} FONT_AKENT_t;

/*
 * Dekoder zwraca kod Unicode, zamiast próbować mapować UTF-8 na historyczną
 * tablicę znaków. Oryginalne fonty EU1KY od 0x80 wzwyż zawierają głównie
 * cyrylicę, więc mapowanie polskich liter na rzekome Windows-1250 dawało na
 * ekranie rosyjskie glify. Polskie diakrytyki składamy poniżej z liter ASCII.
 */
static uint32_t FONT_DekodujZnak(const char **tekst)
{
    const uint8_t *p = (const uint8_t *)*tekst;
    uint32_t unicode;

    if (p[0] < 0x80)
    {
        (*tekst)++;
        return p[0];
    }

    if ((p[0] & 0xE0) == 0xC0 && p[1] != 0 && (p[1] & 0xC0) == 0x80)
    {
        unicode = ((uint32_t)(p[0] & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
        *tekst += 2;
        return unicode;
    }

    if ((p[0] & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0 &&
        (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
    {
        unicode = ((uint32_t)(p[0] & 0x0F) << 12) |
                  ((uint32_t)(p[1] & 0x3F) << 6) |
                  (uint32_t)(p[2] & 0x3F);
        *tekst += 3;
        return unicode;
    }

    /*
     * Zachowujemy zgodność ze starymi, jednobajtowymi napisami firmware.
     * Jeśli bajt nie rozpoczyna poprawnej sekwencji UTF-8, traktujemy go tak,
     * jak robił to oryginalny renderer.
     */
    (*tekst)++;
    return p[0];
}

static uint8_t FONT_ZnakBazowy(uint32_t unicode, FONT_AKENT_t *akcent)
{
    *akcent = FONT_AKENT_BRAK;

    switch (unicode)
    {
    case 0x0104: *akcent = FONT_AKENT_OGONEK;       return 'A';
    case 0x0105: *akcent = FONT_AKENT_OGONEK;       return 'a';
    case 0x0106: *akcent = FONT_AKENT_KRESKA;       return 'C';
    case 0x0107: *akcent = FONT_AKENT_KRESKA;       return 'c';
    case 0x0118: *akcent = FONT_AKENT_OGONEK;       return 'E';
    case 0x0119: *akcent = FONT_AKENT_OGONEK;       return 'e';
    case 0x0141: *akcent = FONT_AKENT_PRZEKRESLENIE; return 'L';
    case 0x0142: *akcent = FONT_AKENT_PRZEKRESLENIE; return 'l';
    case 0x0143: *akcent = FONT_AKENT_KRESKA;       return 'N';
    case 0x0144: *akcent = FONT_AKENT_KRESKA;       return 'n';
    case 0x00D3: *akcent = FONT_AKENT_KRESKA;       return 'O';
    case 0x00F3: *akcent = FONT_AKENT_KRESKA;       return 'o';
    case 0x015A: *akcent = FONT_AKENT_KRESKA;       return 'S';
    case 0x015B: *akcent = FONT_AKENT_KRESKA;       return 's';
    case 0x0179: *akcent = FONT_AKENT_KRESKA;       return 'Z';
    case 0x017A: *akcent = FONT_AKENT_KRESKA;       return 'z';
    case 0x017B: *akcent = FONT_AKENT_KROPKA;       return 'Z';
    case 0x017C: *akcent = FONT_AKENT_KROPKA;       return 'z';

    /* Niemiecki: umlaut składamy na bazowej literze ASCII. */
    case 0x00C4: *akcent = FONT_AKENT_UMLAUT;        return 'A';
    case 0x00E4: *akcent = FONT_AKENT_UMLAUT;        return 'a';
    case 0x00D6: *akcent = FONT_AKENT_UMLAUT;        return 'O';
    case 0x00F6: *akcent = FONT_AKENT_UMLAUT;        return 'o';
    case 0x00DC: *akcent = FONT_AKENT_UMLAUT;        return 'U';
    case 0x00FC: *akcent = FONT_AKENT_UMLAUT;        return 'u';

    /* Hiszpański: akcenty ostre i tylda. */
    case 0x00C1: *akcent = FONT_AKENT_KRESKA;        return 'A';
    case 0x00E1: *akcent = FONT_AKENT_KRESKA;        return 'a';
    case 0x00C9: *akcent = FONT_AKENT_KRESKA;        return 'E';
    case 0x00E9: *akcent = FONT_AKENT_KRESKA;        return 'e';
    case 0x00CD: *akcent = FONT_AKENT_KRESKA;        return 'I';
    case 0x00ED: *akcent = FONT_AKENT_KRESKA;        return 'i';
    case 0x00DA: *akcent = FONT_AKENT_KRESKA;        return 'U';
    case 0x00FA: *akcent = FONT_AKENT_KRESKA;        return 'u';
    case 0x00D1: *akcent = FONT_AKENT_TYLDA;         return 'N';
    case 0x00F1: *akcent = FONT_AKENT_TYLDA;         return 'n';
    case 0x00BF: return '?'; /* Font bazowy nie ma odwróconego pytajnika. */
    case 0x00A1: return '!'; /* Czytelny fallback dla odwróconego wykrzyknika. */

    /*
     * Cyrylica w historycznych fontach EU1KY jest zakodowana jak Windows-1251.
     * Teksty źródłowe pozostają w UTF-8, więc mapujemy jawnie Unicode na indeks
     * starego glifu. Nie zmieniamy plików fontów i nie łamiemy starszych ekranów.
     */
    case 0x0401: return 0xA8; /* Ё */
    case 0x0451: return 0xB8; /* ё */
    default:
        if (unicode >= 0x0410U && unicode <= 0x042FU)
            return (uint8_t)(0xC0U + (unicode - 0x0410U));
        if (unicode >= 0x0430U && unicode <= 0x044FU)
            return (uint8_t)(0xE0U + (unicode - 0x0430U));
        if (unicode <= 0xFFU)
            return (uint8_t)unicode;
        return (uint8_t)'?';
    }
}

static void FONT_RysujAkcent(FONT_AKENT_t akcent, LCDColor fg,
                             uint16_t x, uint16_t y,
                             uint8_t szerokosc, uint8_t wysokosc)
{
    uint16_t cx;

    if (akcent == FONT_AKENT_BRAK || szerokosc < 3U || wysokosc < 5U)
        return;

    cx = (uint16_t)(x + szerokosc / 2U);

    switch (akcent)
    {
    case FONT_AKENT_KRESKA:
        /* Krótka kreska w prawą stronę, wewnątrz górnego marginesu glifu. */
        LCD_SetPixel(LCD_MakePoint(cx, y), fg);
        LCD_SetPixel(LCD_MakePoint(cx + 1U, y), fg);
        LCD_SetPixel(LCD_MakePoint(cx - 1U, y + 1U), fg);
        break;

    case FONT_AKENT_KROPKA:
        LCD_SetPixel(LCD_MakePoint(cx, y), fg);
        LCD_SetPixel(LCD_MakePoint(cx + 1U, y), fg);
        break;

    case FONT_AKENT_OGONEK:
        /* Ogonek przy prawym dolnym narożniku litery. */
        LCD_SetPixel(LCD_MakePoint(x + szerokosc - 2U, y + wysokosc - 3U), fg);
        LCD_SetPixel(LCD_MakePoint(x + szerokosc - 1U, y + wysokosc - 2U), fg);
        LCD_SetPixel(LCD_MakePoint(x + szerokosc - 2U, y + wysokosc - 1U), fg);
        break;

    case FONT_AKENT_PRZEKRESLENIE:
        /* Ukośna kreska przez środek L/l. */
        LCD_SetPixel(LCD_MakePoint(x + 1U, y + wysokosc / 2U + 1U), fg);
        LCD_SetPixel(LCD_MakePoint(x + 2U, y + wysokosc / 2U), fg);
        if (szerokosc > 4U)
            LCD_SetPixel(LCD_MakePoint(x + 3U, y + wysokosc / 2U - 1U), fg);
        break;

    case FONT_AKENT_UMLAUT:
        /* Dwie kropki mieszczą się w górnym marginesie glifu. */
        LCD_SetPixel(LCD_MakePoint(cx - 2U, y), fg);
        LCD_SetPixel(LCD_MakePoint(cx + 2U, y), fg);
        break;

    case FONT_AKENT_TYLDA:
        /* Krótka tylda dla Ñ/ñ; celowo prosta, aby była czytelna także w FONT_FRAN. */
        if (cx > x)
            LCD_SetPixel(LCD_MakePoint(cx - 1U, y + 1U), fg);
        LCD_SetPixel(LCD_MakePoint(cx, y), fg);
        LCD_SetPixel(LCD_MakePoint(cx + 1U, y), fg);
        LCD_SetPixel(LCD_MakePoint(cx + 2U, y + 1U), fg);
        break;

    default:
        break;
    }
}

static void FONT_GetParams(FONTS fnt, struct _fontparams *pRes)
{
    if (0 == pRes)
    {
        return;
    }
    switch (fnt)
    {
    default:
    case FONT_FRAN:
        pRes->pFont = (const uint8_t **)fran;
        pRes->charHeight = fran_height;
        pRes->charSpacing = fran_spacing;
        break;
    case FONT_FRANBIG:
        pRes->pFont = (const uint8_t **)franbig;
        pRes->charHeight = franbig_height;
        pRes->charSpacing = franbig_spacing;
        break;
    case FONT_CONSBIG:
        pRes->pFont = (const uint8_t **)consbig;
        pRes->charHeight = consbig_height;
        pRes->charSpacing = consbig_spacing;
        break;
    case FONT_SDIGITS:
        pRes->pFont = (const uint8_t **)sdigits;
        pRes->charHeight = sdigits_height;
        pRes->charSpacing = sdigits_spacing;
        break;
    case FONT_BDIGITS:
        pRes->pFont = (const uint8_t **)bdigits;
        pRes->charHeight = bdigits_height;
        pRes->charSpacing = bdigits_spacing;
        break;
    case FONT_MENUS:
        pRes->pFont = (const uint8_t **)menufont;
        pRes->charHeight = menufont_height;
        pRes->charSpacing = menufont_spacing;
        break;
    case FONT_KAFEL:
        /*
         * Krój podpisów kafli - szeryfowy, pogrubiony (PT Serif Bold, OFL),
         * wygenerowany do wysokości komórki 16 px identycznej z FONT_FRAN,
         * żeby pasek etykiety pod ikoną nie musiał zmieniać rozmiaru.
         * Pokrywa tylko ASCII 32-126; polskie ogonki dokłada FONT_ZnakBazowy
         * tak samo jak dla pozostałych fontów.
         */
        pRes->pFont = (const uint8_t **)kafelfont;
        pRes->charHeight = kafelfont_height;
        pRes->charSpacing = kafelfont_spacing;
        break;
    }
}

uint16_t FONT_GetHeight(FONTS fnt)
{
    switch (fnt)
    {
    default:
    case FONT_FRAN:
        return fran_height;
    case FONT_FRANBIG:
        return franbig_height;
    case FONT_CONSBIG:
        return consbig_height;
    case FONT_SDIGITS:
        return sdigits_height;
    case FONT_BDIGITS:
        return bdigits_height;
    case FONT_MENUS:
        return menufont_height;
    case FONT_KAFEL:
        return kafelfont_height;
    }
    return 0;
}

int FONT_Write(FONTS fnt, LCDColor fg, LCDColor bg, uint16_t x, uint16_t y, const char *pStr)
{
    if (pStr == NULL)
        return 0;
    return FONT_Write_N(fnt, fg, bg, x, y, pStr, 32767);
}

//Right Align, by KD8CEC
int FONT_Write_RightAlign(FONTS fnt, LCDColor fg, LCDColor bg, uint16_t x, uint16_t y, uint16_t x2, const char *pStr)
{
    int drawX = x2 - FONT_GetStrPixelWidth(fnt, pStr);
    //DBG_Printf("x:%d, drawX:%d, targetWidth:%d, FontWidth:%d", x, drawX, targetWidth, FONT_GetStrPixelWidth(fnt, pStr));
    if (drawX < x)
        drawX = x;

    FONT_Write(fnt, fg, bg, drawX, y, pStr);
    return 0;
}

void FONT_ClearLine(FONTS fnt, LCDColor bg, uint16_t y0)
{
    //Fill the rectangle with background color
    struct _fontparams fp = {0};
    FONT_GetParams(fnt, &fp);
    LCD_FillRect(LCD_MakePoint(0, y0), LCD_MakePoint(LCD_GetWidth() - 1, y0 + fp.charHeight), bg);
}
void FONT_ClearHalfLine(FONTS fnt, LCDColor bg, uint16_t y0) // WK
{
    //Fill the rectangle with background color
    struct _fontparams fp = {0};
    FONT_GetParams(fnt, &fp);
    LCD_FillRect(LCD_MakePoint(0, y0), LCD_MakePoint(LCD_GetWidth() / 2 - 1, y0 + fp.charHeight), bg);
}

int FONT_Write_N(FONTS fnt, LCDColor fg, LCDColor bg, uint16_t x, uint16_t y, const char *pStr, int nChars)
{
    int nPrinted = 0;
    struct _fontparams fp = {0};
    uint8_t ch;
    const char *kursor;

    if (pStr == NULL || nChars <= 0)
        return 0;

    FONT_GetParams(fnt, &fp);

    //Fill the rectangle with background color
    if (0 != bg)
        LCD_FillRect(LCD_MakePoint(x, y), LCD_MakePoint(x + FONT_GetStrPixelWidth(fnt, pStr), y + fp.charHeight - 1), bg);

    kursor = pStr;
    while (*kursor != '\0' && nChars-- > 0)
    {
        uint32_t unicode = FONT_DekodujZnak(&kursor);
        RU_FONT_GLIF_t glif_ru = {0};
        FONT_AKENT_t akcent;
        uint8_t charWidth;

        if (RU_FONT_Pobierz(unicode, fnt, &glif_ru))
        {
            charWidth = glif_ru.szerokosc;
            if ((x + charWidth) > LCD_GetWidth())
                return nPrinted;

            FONT_RysujGlifRU(&glif_ru, fg, x, y);
            ++nPrinted;
            if (*kursor == '\0')
                break;
            x = (uint16_t)(x + charWidth + fp.charSpacing);
            continue;
        }
        else
        {
            uint8_t charIndex = FONT_ZnakBazowy(unicode, &akcent);
            const uint8_t *pCharData = fp.pFont[charIndex];
            uint8_t charWidthCurrent;
            uint16_t yCurr;
            uint8_t lines = fp.charHeight;

            charWidth = pCharData[0];
            if ((x + charWidth) > LCD_GetWidth())
                return nPrinted;

            ++pCharData;
            yCurr = y;
            while (lines--)
            {
                charWidthCurrent = charWidth;
                while (charWidthCurrent)
                {
                    uint8_t currByteWidth = charWidthCurrent >= 8 ? 8 : charWidthCurrent;
                    ch = *pCharData++;
                    if (0 != ch)
                        FONT_DrawByte(ch, currByteWidth, fg, x + charWidthCurrent, yCurr);
                    if (charWidthCurrent >= 8)
                        charWidthCurrent -= 8;
                    else
                        charWidthCurrent = 0;
                }
                yCurr++;
            }

            FONT_RysujAkcent(akcent, fg, x, y, charWidth, fp.charHeight);
        }

        ++nPrinted;
        if (*kursor == '\0')
            break;
        x = (uint16_t)(x + charWidth + fp.charSpacing);
    }
    return nPrinted;
}

void FONT_SetAttributes(FONTS fnt, LCDColor fg, LCDColor bg)
{
    _fgColor = fg;
    _bgColor = bg;
    _font = fnt;
}

static char tmpBuf[256];

int FONT_Printf(uint16_t x, uint16_t y, const char *fmt, ...)
{

    int np = 0;
    va_list ap;
    va_start(ap, fmt);
    np = vsnprintf(tmpBuf, 255, fmt, ap);
    tmpBuf[np] = '\0';
    va_end(ap);
    return FONT_Write(_font, _fgColor, _bgColor, x, y, tmpBuf);
}

int FONT_Print(FONTS fnt, LCDColor fg, LCDColor bg, uint16_t x, uint16_t y, const char *fmt, ...)
{
    int np = 0;
    va_list ap;
    va_start(ap, fmt);
    np = vsnprintf(tmpBuf, 255, fmt, ap);
    tmpBuf[np] = '\0';
    va_end(ap);
    return FONT_Write(fnt, fg, bg, x, y, tmpBuf);
}

int FONT_GetStrPixelWidth(FONTS fnt, const char *pStr)
{
    int w = 0;
    struct _fontparams fp = {0};
    const char *kursor;

    if (pStr == NULL)
        return 0;

    FONT_GetParams(fnt, &fp);
    kursor = pStr;

    while (*kursor != '\0')
    {
        uint32_t unicode = FONT_DekodujZnak(&kursor);
        RU_FONT_GLIF_t glif_ru = {0};
        if (RU_FONT_Pobierz(unicode, fnt, &glif_ru))
        {
            w += glif_ru.szerokosc;
        }
        else
        {
            FONT_AKENT_t akcent;
            uint8_t charIndex = FONT_ZnakBazowy(unicode, &akcent);
            const uint8_t *pCharData = fp.pFont[charIndex];
            w += pCharData[0];
        }
        if (*kursor == '\0')
            break;
        w += fp.charSpacing;
    }
    return w;
}

