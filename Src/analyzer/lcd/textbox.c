#include <string.h>
#include "textbox.h"
#include "LCD.h"
#include "touch.h"
#include "config.h"
#include "wejscia_uzytkownika.h"
#include "jezyk.h"
#include "ui_wspolny.h"

//TODO: make it more portable
#define IS_IN_RAM(ptr) ((uint32_t)ptr >= 0x20000000)

extern void Sleep(uint32_t);

static uint8_t TEXTBOX_repeats;
static void *TEXTBOX_previous;

static UI_STYL_t TEXTBOX_StylPrzycisku(const TEXTBOX_t *pole)
{
    if (pole == 0)
        return UI_STYL_NORMALNY;

    /*
     * Starsze ekrany przekazuja historyczne kolory LCD_RED/GREEN/YELLOW,
     * a nowsze bezposrednio kolory z aktualnego motywu. Rozpoznajemy oba
     * sposoby, aby wspolny mechanizm TEXTBOX nie gubil znaczenia przycisku
     * po przejsciu na motywy EU1KY-PL 2026.
     */
    if (pole->rola == TEXTBOX_ROLA_WSTECZ ||
        pole->bgcolor == LCD_RED ||
        pole->bgcolor == UI_KolorTlaPrzycisku(UI_STYL_POWROT))
        return UI_STYL_POWROT;

    if (pole->bgcolor == LCD_GREEN ||
        pole->bgcolor == UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY))
        return UI_STYL_AKTYWNY;

    if (pole->bgcolor == LCD_YELLOW ||
        pole->bgcolor == UI_KolorTlaPrzycisku(UI_STYL_AKCENT))
        return UI_STYL_AKCENT;

    if (pole->bgcolor == UI_KolorTlaPrzycisku(UI_STYL_OSTRZEZENIE))
        return UI_STYL_OSTRZEZENIE;

    if (pole->bgcolor == UI_KolorTlaPrzycisku(UI_STYL_NIEAKTYWNY))
        return UI_STYL_NIEAKTYWNY;

    return UI_STYL_NORMALNY;
}

static const char *TEXTBOX_PobierzTekst(const TEXTBOX_t *pole)
{
    if (pole == 0)
        return "";
    if (pole->tekst_id != 0)
        return JEZYK_Tekst((TEKST_ID_t)(pole->tekst_id - 1U));
    return pole->text != 0 ? pole->text : "";
}

/*
 * Starsze moduły mają Wstecz wpisane w dziesiątkach różnych miejsc i
 * rozmiarów. Nie kopiujemy tej geometrii do każdego ekranu. Pole TEXTBOX
 * oznaczone jednoznacznie jako TEKST_WSTECZ jest renderowane i trafiane
 * przez wspólny obszar 0,220,70,45. Callback oraz pozycja w łańcuchu
 * pozostają bez zmian, więc nie naruszamy logiki historycznych ekranów.
 */
static uint8_t TEXTBOX_CzyStandardowyWstecz(const TEXTBOX_t *pole)
{
    if (pole == 0 || pole->rola != TEXTBOX_ROLA_WSTECZ)
        return 0U;
    if (pole->tekst_id == TEXTBOX_TEKST(TEKST_WSTECZ))
        return 1U;
    if (pole->text != 0 &&
        (!strcmp(pole->text, "Wstecz") || !strcmp(pole->text, "Back") || !strcmp(pole->text, "Exit")))
        return 1U;
    return 0U;
}

static UI_PROSTOKAT_t TEXTBOX_ObszarRzeczywisty(const TEXTBOX_t *pole)
{
    UI_PROSTOKAT_t obszar = {0U, 0U, 0U, 0U};
    if (pole == 0)
        return obszar;
    if (TEXTBOX_CzyStandardowyWstecz(pole))
        return UI_ObszarWsteczDolny();
    obszar.x = pole->x0;
    obszar.y = pole->y0;
    obszar.szerokosc = pole->width;
    obszar.wysokosc = pole->height;
    return obszar;
}

void TEXTBOX_InitContext(TEXTBOX_CTX_t *ctx)
{
    ctx->start = 0;
    ctx->wybrany = 0;
    ctx->wybor_widoczny = 0;
    TEXTBOX_repeats = 0; // 5 times slow, then rapid
    TEXTBOX_previous = 0;
}

TEXTBOX_t *TEXTBOX_Find(TEXTBOX_CTX_t *ctx, uint32_t idx)
{
    if (0 == ctx)
        return 0;
    TEXTBOX_t *pbox = ctx->start;
    uint32_t ctr = 0;
    if (0 == pbox)
        return 0;
    else
    {
        while (0 != pbox)
        {
            if (ctr == idx)
                return pbox;
            pbox = pbox->next;
            ctr++;
        }
    }
    return 0;
}

//Returns textbox ID in context
uint32_t TEXTBOX_Append(TEXTBOX_CTX_t *ctx, TEXTBOX_t *hbox)
{
    TEXTBOX_t *pbox = ctx->start;
    uint32_t idx = 0;
    if (0 == pbox)
        ctx->start = hbox;
    else
    {
        while (0 != pbox->next)
        {
            pbox = pbox->next;
            idx++;
        }
        if (IS_IN_RAM(pbox))
            pbox->next = hbox;
        if (IS_IN_RAM(hbox))
            hbox->next = 0;
        idx++;
    }
    if (TEXTBOX_TYPE_TEXT == hbox->type)
    {
        if (IS_IN_RAM(hbox) && 0 == hbox->width)
            hbox->width = FONT_GetStrPixelWidth(hbox->font, TEXTBOX_PobierzTekst(hbox));
        if (IS_IN_RAM(hbox) && 0 == hbox->height)
            hbox->height = FONT_GetHeight(hbox->font);
    }
    return idx;
}

void TEXTBOX_Clear(TEXTBOX_CTX_t *ctx, uint32_t idx)
{
    TEXTBOX_t *tb = TEXTBOX_Find(ctx, idx);
    if (0 == tb)
        return;
    if (TEXTBOX_TYPE_HITRECT == tb->type)
        return;
    LCD_FillRect(LCD_MakePoint(tb->x0, tb->y0),
                 LCD_MakePoint(tb->x0 + tb->width, tb->y0 + tb->height),
                 tb->bgcolor);
}

void TEXTBOX_SetText(TEXTBOX_CTX_t *ctx, uint32_t idx, const char *txt)
{
    if (0 == ctx || 0 == txt)
        return;
    TEXTBOX_t *tb = TEXTBOX_Find(ctx, idx);
    if (0 == tb || TEXTBOX_TYPE_TEXT != tb->type)
        return;

    tb->text = txt;
    tb->tekst_id = 0;

    if (tb->border && tb->width && tb->height)
    {
        if (tb->cb != 0)
            UI_RysujPrzycisk(tb->x0, tb->y0, tb->width, tb->height,
                             TEXTBOX_PobierzTekst(tb), TEXTBOX_StylPrzycisku(tb), tb->font);
        else
            UI_RysujPolePasywne(tb->x0, tb->y0, tb->width, tb->height,
                                TEXTBOX_PobierzTekst(tb), tb->font);
        return;
    }

    TEXTBOX_Clear(ctx, idx);
    if (IS_IN_RAM(tb))
    {
        tb->width = FONT_GetStrPixelWidth(tb->font, TEXTBOX_PobierzTekst(tb));
        tb->height = FONT_GetHeight(tb->font);
    }
    FONT_Write(tb->font, tb->fgcolor, tb->bgcolor, tb->x0, tb->y0, TEXTBOX_PobierzTekst(tb));
}

#include "bitmaps/bitmaps.h"

void TEXTBOX_DrawContext(TEXTBOX_CTX_t *ctx)
{
    TEXTBOX_t *pbox = ctx->start;
    while (pbox)
    {
        if (TEXTBOX_TYPE_TEXT == pbox->type)
        {
            const char *tekst = TEXTBOX_PobierzTekst(pbox);

            if (pbox->border && pbox->width && pbox->height)
            {
                if (pbox->cb != 0)
                {
                    if (TEXTBOX_CzyStandardowyWstecz(pbox))
                        UI_RysujWsteczDolny(false);
                    else
                        UI_RysujPrzycisk(pbox->x0, pbox->y0, pbox->width, pbox->height,
                                         tekst, TEXTBOX_StylPrzycisku(pbox), pbox->font);
                }
                else
                    UI_RysujPolePasywne(pbox->x0, pbox->y0, pbox->width, pbox->height,
                                        tekst, pbox->font);
            }
            else
            {
                if (0 != pbox->width)
                {
                    LCD_FillRect(LCD_MakePoint(pbox->x0, pbox->y0),
                                 LCD_MakePoint(pbox->x0 + pbox->width, pbox->y0 + pbox->height),
                                 pbox->bgcolor);
                }
                else if (IS_IN_RAM(pbox))
                {
                    pbox->width = FONT_GetStrPixelWidth(pbox->font, tekst);
                    pbox->height = FONT_GetHeight(pbox->font);
                }

                if (pbox->center && pbox->width && pbox->height)
                {
                    int h = FONT_GetHeight(pbox->font);
                    int w = FONT_GetStrPixelWidth(pbox->font, tekst);
                    int x = (int)pbox->x0 + (int)pbox->width / 2 - w / 2;
                    int y = (int)pbox->y0 + (int)pbox->height / 2 - h / 2;
                    FONT_Write(pbox->font, pbox->fgcolor, pbox->bgcolor, x, y, tekst);
                }
                else
                {
                    FONT_Write(pbox->font, pbox->fgcolor, pbox->bgcolor, pbox->x0, pbox->y0, tekst);
                }
            }
        }
        else if (TEXTBOX_TYPE_BMP == pbox->type)
        {
            LCD_DrawBitmap(LCD_MakePoint(pbox->x0, pbox->y0), pbox->bmp, 3430);
        }
        pbox = pbox->next;
    }
}


static uint8_t TEXTBOX_CzySterowalny(const TEXTBOX_t *pole)
{
    if (0 == pole || 0 == pole->cb)
        return 0;
    if (TEXTBOX_TYPE_HITRECT == pole->type)
        return 0;
    return 1;
}

static void TEXTBOX_OdwrocWybor(TEXTBOX_CTX_t *ctx)
{
    TEXTBOX_t *pole;

    if (0 == ctx || !ctx->wybor_widoczny)
        return;

    pole = ctx->wybrany;
    if (0 == pole)
        return;

    {
        UI_PROSTOKAT_t obszar = TEXTBOX_ObszarRzeczywisty(pole);
        LCD_InvertRect(
            LCD_MakePoint(obszar.x, obszar.y),
            LCD_MakePoint((uint16_t)(obszar.x + obszar.szerokosc - 1U),
                          (uint16_t)(obszar.y + obszar.wysokosc - 1U)));
    }
}

static TEXTBOX_t *TEXTBOX_PierwszySterowalny(TEXTBOX_CTX_t *ctx)
{
    TEXTBOX_t *pole = (0 != ctx) ? ctx->start : 0;
    while (pole)
    {
        if (TEXTBOX_CzySterowalny(pole))
            return pole;
        pole = pole->next;
    }
    return 0;
}

static TEXTBOX_t *TEXTBOX_NastepnySterowalny(TEXTBOX_CTX_t *ctx, TEXTBOX_t *aktualny, int kierunek)
{
    TEXTBOX_t *pole;
    TEXTBOX_t *poprzedni = 0;
    TEXTBOX_t *ostatni = 0;

    if (0 == ctx)
        return 0;

    if (kierunek > 0)
    {
        pole = (0 != aktualny) ? aktualny->next : ctx->start;
        while (pole)
        {
            if (TEXTBOX_CzySterowalny(pole))
                return pole;
            pole = pole->next;
        }
        return TEXTBOX_PierwszySterowalny(ctx);
    }

    pole = ctx->start;
    while (pole)
    {
        if (TEXTBOX_CzySterowalny(pole))
        {
            ostatni = pole;
            if (pole == aktualny && 0 != poprzedni)
                return poprzedni;
            poprzedni = pole;
        }
        pole = pole->next;
    }

    /* Gdy aktualny byl pierwszym polem, obrot w lewo zawija na koniec. */
    if (aktualny == TEXTBOX_PierwszySterowalny(ctx))
        return ostatni;

    return ostatni;
}

static TEXTBOX_t *TEXTBOX_ZnajdzRole(TEXTBOX_CTX_t *ctx, TEXTBOX_ROLA_t rola)
{
    TEXTBOX_t *pole = (0 != ctx) ? ctx->start : 0;
    while (pole)
    {
        if (TEXTBOX_CzySterowalny(pole) && pole->rola == (uint8_t)rola)
            return pole;
        pole = pole->next;
    }
    return 0;
}

static uint32_t TEXTBOX_UruchomPole(TEXTBOX_t *pole, uint8_t z_dotyku)
{
    uint32_t xx1, xx2, yy1, yy2;

    if (0 == pole || 0 == pole->cb)
        return 0;

    {
        UI_PROSTOKAT_t obszar = TEXTBOX_ObszarRzeczywisty(pole);
        xx1 = obszar.x;
        yy1 = obszar.y;
        xx2 = (uint32_t)obszar.x + obszar.szerokosc - 1U;
        yy2 = (uint32_t)obszar.y + obszar.wysokosc - 1U;
    }

    if (BeepOn1 == 1)
    {
        UB_TIMER2_Init_FRQ(880);
        UB_TIMER2_Start();
        Sleep(60);
        UB_TIMER2_Stop();
    }

    if (z_dotyku && pole->nowait == 0U)
    {
        /*
         * Zwykły przycisk uruchamiamy dopiero po puszczeniu palca. Wcześniej
         * callback otwierał kolejny ekran, gdy ten sam dotyk nadal trwał,
         * więc nowy ekran potrafił odczytać go jako drugie naciśnięcie.
         * Pola nowait zachowują dawną semantykę przytrzymania i powtarzania.
         */
        LCD_InvertRect(LCD_MakePoint(xx1, yy1), LCD_MakePoint(xx2, yy2));
        TOUCH_CzekajNaPuszczenie(35U);
        LCD_InvertRect(LCD_MakePoint(xx1, yy1), LCD_MakePoint(xx2, yy2));
        Sleep(20);
    }

    if (pole->cbparam)
        ((void (*)(const TEXTBOX_t *))pole->cb)(pole);
    else
        pole->cb();

    if (pole->nowait)
    {
        if (TEXTBOX_repeats < 5)
        {
            TEXTBOX_previous = pole;
            TEXTBOX_repeats++;
            Sleep(400);
            return 2;
        }
        if (pole == TEXTBOX_previous && TEXTBOX_repeats >= 5)
        {
            Sleep(pole->nowait);
            return 0;
        }
    }

    if (z_dotyku && pole->nowait != 0U)
    {
        LCD_InvertRect(LCD_MakePoint(xx1, yy1), LCD_MakePoint(xx2, yy2));
        TOUCH_CzekajNaPuszczenie(25U);
        LCD_InvertRect(LCD_MakePoint(xx1, yy1), LCD_MakePoint(xx2, yy2));
    }

    return 1;
}

static uint32_t TEXTBOX_ObsluzWejsciaFizyczne(TEXTBOX_CTX_t *ctx)
{
    WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
    TEXTBOX_t *nowy;

    if (WEJSCIE_ZDARZENIE_BRAK == zdarzenie)
        return 0;

    if (WEJSCIE_ZDARZENIE_OBROT_LEWO == zdarzenie ||
        WEJSCIE_ZDARZENIE_OBROT_PRAWO == zdarzenie)
    {
        if (ctx->wybor_widoczny)
            TEXTBOX_OdwrocWybor(ctx);

        if (0 == ctx->wybrany)
            nowy = TEXTBOX_PierwszySterowalny(ctx);
        else
            nowy = TEXTBOX_NastepnySterowalny(
                ctx,
                ctx->wybrany,
                (WEJSCIE_ZDARZENIE_OBROT_PRAWO == zdarzenie) ? 1 : -1);

        if (0 == nowy)
            nowy = TEXTBOX_PierwszySterowalny(ctx);

        ctx->wybrany = nowy;
        ctx->wybor_widoczny = (0 != nowy) ? 1U : 0U;
        if (ctx->wybor_widoczny)
            TEXTBOX_OdwrocWybor(ctx);
        return TEXTBOX_WYNIK_NAWIGACJA;
    }

    if (WEJSCIE_ZDARZENIE_OK == zdarzenie)
    {
        if (!ctx->wybor_widoczny || 0 == ctx->wybrany)
            return 0;
        TEXTBOX_OdwrocWybor(ctx);
        ctx->wybor_widoczny = 0;
        return TEXTBOX_UruchomPole(ctx->wybrany, 0);
    }

    if (WEJSCIE_ZDARZENIE_WSTECZ == zdarzenie)
    {
        nowy = TEXTBOX_ZnajdzRole(ctx, TEXTBOX_ROLA_WSTECZ);
        return TEXTBOX_UruchomPole(nowy, 0);
    }

    if (WEJSCIE_ZDARZENIE_START_STOP == zdarzenie)
    {
        nowy = TEXTBOX_ZnajdzRole(ctx, TEXTBOX_ROLA_START_STOP);
        return TEXTBOX_UruchomPole(nowy, 0);
    }

    return 0;
}

uint32_t TEXTBOX_HitTest(TEXTBOX_CTX_t *ctx)
{
    LCDPoint coord;
    uint32_t wynik_wejscia;

    wynik_wejscia = TEXTBOX_ObsluzWejsciaFizyczne(ctx);
    if (wynik_wejscia)
        return wynik_wejscia;

    if (!TOUCH_Poll(&coord))
    { // no touch
        TEXTBOX_previous = 0;
        TEXTBOX_repeats = 0;
        return 0;
    }

    TEXTBOX_t *pbox = ctx->start;
    int ykrit;
    while (pbox)
    {
        if (TEXTBOX_TYPE_TEXT == pbox->type)
        {
            if (0 == pbox->width && IS_IN_RAM(pbox))
                pbox->width = FONT_GetStrPixelWidth(pbox->font, TEXTBOX_PobierzTekst(pbox));
            if (0 == pbox->height && IS_IN_RAM(pbox))
                pbox->height = FONT_GetHeight(pbox->font);
        }
        {
            UI_PROSTOKAT_t obszar = TEXTBOX_ObszarRzeczywisty(pbox);
            if (TEXTBOX_CzyStandardowyWstecz(pbox))
                ykrit = obszar.y;
            else if (pbox->y0 > 240)
                ykrit = 225; //DH1AKF 04.10.2020
            else
                ykrit = obszar.y;

            if (coord.x >= obszar.x && coord.x < obszar.x + obszar.szerokosc &&
                coord.y >= ykrit && coord.y < obszar.y + obszar.wysokosc)
            {
                if (ctx->wybor_widoczny)
                {
                    TEXTBOX_OdwrocWybor(ctx);
                    ctx->wybor_widoczny = 0;
                }
                ctx->wybrany = pbox;
                return TEXTBOX_UruchomPole(pbox, 1);
            }
        }
        pbox = pbox->next;
    }
    return 0;
}
