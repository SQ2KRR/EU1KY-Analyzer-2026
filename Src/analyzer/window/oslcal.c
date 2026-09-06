/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "gen.h"
#include "LCD.h"
#include "font.h"
#include "touch.h"
#include "hit.h"
#include "textbox.h"
#include "oslfile.h"
#include "osl70cm.h"
#include "oslcal.h"
#include "kalibracja_meta.h"
#include "num_keypad.h"
#include "komunikaty.h"
#include "jezyk.h"
#include "ui_wspolny.h"
#include "wejscia_uzytkownika.h"
#include "ff.h"
#include "wersja_projektu.h"
#include "sprawdzenie_if.h"
#include "zworka_cal.h"

extern void Sleep(uint32_t);

static uint32_t rqExit = 0;
static uint32_t shortScanned = 0;
static uint32_t loadScanned = 0;
static uint32_t openScanned = 0;
static char progresstxt[16];
static int progressval;
static TEXTBOX_t hbEx;
static TEXTBOX_t hbScanShort;
static TEXTBOX_t tb_S21_CALIBRATION[];
static uint32_t hbScanShortIdx;
static uint32_t hbHwDalejIdx;
static uint8_t hw_dalej_wybrano;
static TEXTBOX_t hbScanOpen;
static TEXTBOX_t hbScanLoad;
static TEXTBOX_t hbScanProgress;

static uint32_t hbScanProgressId;
static TEXTBOX_t hbSave;
static TEXTBOX_CTX_t osl_ctx;
static uint8_t osl_tryb_70cm = 0U;
extern volatile uint32_t autosleep_timer;



static void _hit_ex(void)
{
    hw_dalej_wybrano = 0U;
    if (shortScanned || openScanned || loadScanned)
    {
        if (osl_tryb_70cm)
            (void)OSL70_Reload();
        else
            (void)OSL_ReloadSelected();
    }
    rqExit = 1;
}

void progress_cb(uint32_t new_percent)
{
    if (new_percent == progressval || new_percent > 100)
        return;
    progressval = new_percent;
    sprintf(progresstxt, "%u%%", (unsigned int)progressval);
    TEXTBOX_SetText(&osl_ctx, hbScanProgressId, progresstxt);
    autosleep_timer = 30000; //CFG_GetParam(CFG_PARAM_LOWPWR_TIME);
}

static int progval, indextb;
static char progTxt[20];

void S21progress_cb(uint32_t new_percent)
{
    if (new_percent == progval || new_percent > 100)
        return;
    progval = new_percent;
    sprintf(progTxt, "%u%%", (unsigned int)progval);
    TEXTBOX_SetText(&osl_ctx, indextb, progTxt);
    autosleep_timer = 30000; //CFG_GetParam(CFG_PARAM_LOWPWR_TIME);
}

static void PokazBladSkanuOSL(int32_t wynik)
{
    if (wynik == -4)
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_KALIBRACJA_PRZERWANA_BRAK_SYGNALU);
    else if (wynik == -6)
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Zmieniono metode pomiaru", "Measurement method changed",
                          "Messmethode geändert", "Метод измерения изменён"),
            JEZYK_Wybierz("Wszystkie trzy wzorce 70 cm muszą być zmierzone tą samą metodą. Zacznij kalibrację od początku.",
                          "All three 70 cm standards must be measured with the same method. Restart calibration.",
                          "Alle drei 70-cm-Normale müssen mit derselben Methode gemessen werden. Kalibrierung neu starten.",
                          "Все три эталона 70 см должны измеряться одним методом. Начните калибровку заново."));
    else
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_WYBIERZ_PROFIL_OSL);
}

static uint8_t OSL_ZworkaPozwalaNaSkan(void)
{
    ZWORKA_CAL_WYNIK_t w = ZWORKA_CAL_Sprawdz();
    char tresc[320];

    if (w.status == ZWORKA_CAL_STATUS_BRAK_WZORCA)
        return 1U; /* Stare instalacje bez poprawnego HW zachowują dotychczasowe działanie. */

    if (w.status == ZWORKA_CAL_STATUS_BLAD)
    {
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Kontrola zworki", "Jumper check", "Brueckenpruefung", "Проверка перемычки"),
            JEZYK_Wybierz("Nie udało się wykonać kontroli 440 MHz. Kalibracja może być kontynuowana, ale sprawdź ręcznie pozycję WORK.",
                          "The 440 MHz jumper check failed. Calibration may continue, but verify WORK manually.",
                          "Die 440-MHz-Brueckenpruefung ist fehlgeschlagen. Kalibrierung kann fortgesetzt werden; WORK manuell pruefen.",
                          "Проверка перемычки на 440 МГц не выполнена. Калибровку можно продолжить, но проверьте WORK вручную."));
        return 1U;
    }

    if (w.status == ZWORKA_CAL_STATUS_CAL_PEWNE)
    {
        snprintf(tresc, sizeof(tresc),
                 JEZYK_Wybierz(
                     "Dwukrotny pomiar RAW przy 440 MHz odpowiada zapisanej sygnaturze CAL. dV/I=%+.2f/%+.2f dB, dFaza=%+.1f/%+.1f deg.\n\nUstaw zworkę w pozycji WORK i ponów krok. Pomiar wzorca został zablokowany.",
                     "Two RAW checks at 440 MHz match the stored CAL signature. dV/I=%+.2f/%+.2f dB, dPhase=%+.1f/%+.1f deg.\n\nSet the jumper to WORK and repeat this step. Standard measurement was blocked.",
                     "Zwei RAW-Pruefungen bei 440 MHz entsprechen der gespeicherten CAL-Signatur. dV/I=%+.2f/%+.2f dB, dPhase=%+.1f/%+.1f Grad.\n\nBruecke auf WORK stellen und Schritt wiederholen. Messung wurde gesperrt.",
                     "Две проверки RAW на 440 МГц совпадают с сохранённой сигнатурой CAL. dV/I=%+.2f/%+.2f дБ, dФаза=%+.1f/%+.1f град.\n\nУстановите перемычку WORK и повторите шаг. Измерение эталона заблокировано."),
                 (double)w.delta_vi_db, (double)w.delta_vi_db_2,
                 (double)w.delta_faza_deg, (double)w.delta_faza_deg_2);
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Wykryto CAL", "CAL detected", "CAL erkannt", "Обнаружен CAL"), tresc);
        return 0U;
    }

    if (w.status == ZWORKA_CAL_STATUS_PODEJRZENIE)
    {
        snprintf(tresc, sizeof(tresc),
                 JEZYK_Wybierz(
                     "Wynik kontroli 440 MHz jest blisko sygnatury CAL, ale nie spełnia warunku pewnej blokady. dV/I=%+.2f/%+.2f dB, dFaza=%+.1f/%+.1f deg.\n\nSprawdź zworkę. Po zamknięciu ostrzeżenia kalibracja będzie kontynuowana.",
                     "The 440 MHz result is close to the CAL signature but does not meet the hard-block criterion. dV/I=%+.2f/%+.2f dB, dPhase=%+.1f/%+.1f deg.\n\nCheck the jumper. Calibration will continue after closing this warning.",
                     "Das 440-MHz-Ergebnis liegt nahe der CAL-Signatur, erreicht aber nicht die Sperrschwelle. dV/I=%+.2f/%+.2f dB, dPhase=%+.1f/%+.1f Grad.\n\nBruecke pruefen. Danach wird die Kalibrierung fortgesetzt.",
                     "Результат 440 МГц близок к сигнатуре CAL, но не достигает порога жёсткой блокировки. dV/I=%+.2f/%+.2f дБ, dФаза=%+.1f/%+.1f град.\n\nПроверьте перемычку. После закрытия предупреждения калибровка продолжится."),
                 (double)w.delta_vi_db, (double)w.delta_vi_db_2,
                 (double)w.delta_faza_deg, (double)w.delta_faza_deg_2);
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Sprawdź zworkę", "Check jumper", "Bruecke pruefen", "Проверьте перемычку"), tresc);
    }

    return 1U;
}

static void _hb_scan_short(void)
{
    if (!OSL_ZworkaPozwalaNaSkan())
        return;
    progressval = 100;
    progress_cb(0);

    hbScanShort.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_OSTRZEZENIE);
    TEXTBOX_DrawContext(&osl_ctx);

    int32_t wynik = osl_tryb_70cm ? OSL70_ScanShort(progress_cb) : OSL_ScanShort(progress_cb);
    if (wynik != 0)
    {
        PokazBladSkanuOSL(wynik);
        return;
    }
    shortScanned = 1;
    hbScanShort.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY);
    if (shortScanned && openScanned && loadScanned)
    {
        hbSave.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY);
        hbSave.fgcolor = UI_KolorTekstu(UI_STYL_AKTYWNY);
    }
    progresstxt[0] = '\0';
    TEXTBOX_SetText(&osl_ctx, hbScanProgressId, progresstxt);
    TEXTBOX_DrawContext(&osl_ctx);
}

static void _hb_scan_open(void)
{
    if (!OSL_ZworkaPozwalaNaSkan())
        return;

    progressval = 100;
    progress_cb(0);

    hbScanOpen.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_OSTRZEZENIE);
    TEXTBOX_DrawContext(&osl_ctx);

    int32_t wynik = osl_tryb_70cm ? OSL70_ScanOpen(progress_cb) : OSL_ScanOpen(progress_cb);
    if (wynik != 0)
    {
        PokazBladSkanuOSL(wynik);
        return;
    }
    openScanned = 1;
    hbScanOpen.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY);
    if (shortScanned && openScanned && loadScanned)
    {
        hbSave.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY);
        hbSave.fgcolor = UI_KolorTekstu(UI_STYL_AKTYWNY);
    }
    progresstxt[0] = '\0';
    TEXTBOX_SetText(&osl_ctx, hbScanProgressId, progresstxt);
    TEXTBOX_DrawContext(&osl_ctx);
}

static void _hb_scan_load(void)
{
    if (!OSL_ZworkaPozwalaNaSkan())
        return;

    progressval = 100;
    progress_cb(0);

    hbScanLoad.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_OSTRZEZENIE);
    TEXTBOX_DrawContext(&osl_ctx);

    int32_t wynik = osl_tryb_70cm ? OSL70_ScanLoad(progress_cb) : OSL_ScanLoad(progress_cb);
    if (wynik != 0)
    {
        PokazBladSkanuOSL(wynik);
        return;
    }
    loadScanned = 1;
    hbScanLoad.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY);
    if (shortScanned && openScanned && loadScanned)
    {
        hbSave.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY);
        hbSave.fgcolor = UI_KolorTekstu(UI_STYL_AKTYWNY);
    }
    progresstxt[0] = '\0';
    TEXTBOX_SetText(&osl_ctx, hbScanProgressId, progresstxt);
    TEXTBOX_DrawContext(&osl_ctx);
}

static void _hit_save(void)
{
    int32_t wynik;

    if (!(shortScanned && openScanned && loadScanned))
    {
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_KALIBRACJA_OSL_NIEPELNA);
        return;
    }

    if (osl_tryb_70cm)
    {
        wynik = OSL70_Calculate();
        if (wynik != 0)
        {
            if (wynik == -5)
                KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_KALIBRACJA_PRZERWANA_BRAK_SYGNALU);
            else
                KOMUNIKAT_Pokaz(TEKST_BLAD,
                                CFG_CzyKartaSDDostepna() ? TEKST_BLAD_ZAPISU_PLIKU : TEKST_BRAK_KARTY_SD);
            return;
        }

        if (OSL70_Reload() != 0 || !OSL70_IsValid())
        {
            KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ODCZYTU_PLIKU);
            return;
        }
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("OSL 70 cm zapisana", "70 cm OSL saved",
                          "70-cm-OSL gespeichert", "OSL 70 см сохранена"),
            JEZYK_Wybierz("Zakres 420-450 MHz, krok 50 kHz. Będzie wybierana automatycznie tylko dla pomiarów mieszczących się w całości w tym paśmie.",
                          "Range 420-450 MHz, 50 kHz step. It will be selected automatically only for measurements fully contained in this band.",
                          "Bereich 420-450 MHz, Schritt 50 kHz. Sie wird nur für vollständig in diesem Band liegende Messungen automatisch gewählt.",
                          "Диапазон 420-450 МГц, шаг 50 кГц. Автовыбор только для измерений, целиком находящихся в этом диапазоне."));
        Sleep(150U);
        rqExit = 1;
        return;
    }

    {
        const int32_t profil_zapisywany = OSL_GetSelected();
        wynik = OSL_Calculate();
        if (wynik != 0)
        {
            if (wynik == -5)
                KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_KALIBRACJA_PRZERWANA_BRAK_SYGNALU);
            else
                KOMUNIKAT_Pokaz(TEKST_BLAD,
                                CFG_CzyKartaSDDostepna() ? TEKST_BLAD_ZAPISU_PLIKU : TEKST_BRAK_KARTY_SD);
            return;
        }

        /* Po zapisie wymuszamy ponowny odczyt dokładnie tego samego profilu. */
        if (profil_zapisywany < 0 || OSL_GetSelected() != profil_zapisywany ||
            OSL_ReloadSelected() != 0 || OSL_GetSelected() != profil_zapisywany ||
            !OSL_IsSelectedValid())
        {
            KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ODCZYTU_PLIKU);
            return;
        }
    }
    Sleep(250);
    rqExit = 1;
}

#define OSL_PROFIL_LICZBA 16U
#define OSL_PROFIL_NA_STRONE UI_SIATKA_KOMPAKT_NA_STRONE
#define OSL_PROFIL_LICZBA_STRON ((OSL_PROFIL_LICZBA + OSL_PROFIL_NA_STRONE - 1U) / OSL_PROFIL_NA_STRONE)

static uint8_t OSL_ProfilPlikIstnieje(uint16_t indeks)
{
    FILINFO info;
    char sciezka[32];

    if (indeks >= OSL_PROFIL_LICZBA)
        return 0U;
    snprintf(sciezka, sizeof(sciezka), "%s/%c.osl", g_cfg_osldir, (char)('A' + indeks));
    memset(&info, 0, sizeof(info));
    return f_stat(sciezka, &info) == FR_OK ? 1U : 0U;
}

static uint8_t OSL_ProfilLiczbaNaStronie(uint8_t strona)
{
    const uint16_t pierwszy = (uint16_t)strona * OSL_PROFIL_NA_STRONE;
    const uint16_t pozostalo = pierwszy < OSL_PROFIL_LICZBA
        ? (uint16_t)(OSL_PROFIL_LICZBA - pierwszy) : 0U;

    return (uint8_t)(pozostalo > OSL_PROFIL_NA_STRONE
        ? OSL_PROFIL_NA_STRONE : pozostalo);
}

static bool OSL_PotwierdzUsuniecieProfilu(uint8_t indeks)
{
    UI_AKCJA_t akcje[2];
    uint8_t fokus = 0U;
    char tytul[64];
    char opis[220];

    snprintf(tytul, sizeof(tytul),
             JEZYK_Wybierz("Usunąć profil OSL %c?", "Delete OSL profile %c?",
                           "OSL-Profil %c löschen?", "Удалить профиль OSL %c?"),
             (char)('A' + indeks));
    snprintf(opis, sizeof(opis), "%s",
             JEZYK_Wybierz(
                 "Plik kalibracji i jego metadane zostaną usunięte. Tej operacji nie można cofnąć.",
                 "The calibration file and its metadata will be deleted. This operation cannot be undone.",
                 "Kalibrierdatei und Metadaten werden gelöscht. Dieser Vorgang kann nicht rückgängig gemacht werden.",
                 "Файл калибровки и его метаданные будут удалены. Отменить это действие нельзя."));

    while (TOUCH_IsPressed())
        Sleep(10U);
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int16_t akcja;

        akcje[0] = (UI_AKCJA_t){ .id = 0,
            .tekst = JEZYK_Wybierz("Nie - wróć", "No - back", "Nein - zurück", "Нет - назад"),
            .styl = UI_STYL_POWROT, .aktywna = true, .zaznaczona = fokus == 0U };
        akcje[1] = (UI_AKCJA_t){ .id = 1,
            .tekst = JEZYK_Wybierz("Tak - usuń", "Yes - delete", "Ja - löschen", "Да - удалить"),
            .styl = UI_STYL_OSTRZEZENIE, .aktywna = true, .zaznaczona = fokus == 1U };

        UI_WyczyscEkran();
        UI_RysujPasekGorny(tytul, true, false, 0);
        UI_RysujPoleInformacyjne(14U, 58U, 452U, 128U,
            JEZYK_Wybierz("Potwierdzenie", "Confirmation", "Bestätigung", "Подтверждение"),
            opis);
        UI_RysujPasekAkcji(UI_DOLNY_PASEK_Y, UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 2U);

        for (;;)
        {
            if (TOUCH_Poll(&punkt))
            {
                akcja = UI_ZnajdzAkcjePaska(punkt, UI_DOLNY_PASEK_Y,
                                            UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 2U);
                if (UI_CzyDotknietoWstecz(punkt) || akcja == 0)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return false;
                }
                if (akcja == 1)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return true;
                }
            }

            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                return false;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
                zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                fokus ^= 1U;
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
                return fokus == 1U;
            Sleep(10U);
        }
    }
}

static bool OSL_UsunProfilZPotwierdzeniem(uint8_t indeks)
{
    if (!OSL_ProfilPlikIstnieje(indeks))
        return false;
    if (!OSL_PotwierdzUsuniecieProfilu(indeks))
        return false;
    if (!OSL_UsunProfil(indeks))
    {
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Nie usunięto profilu", "Profile not deleted",
                          "Profil nicht gelöscht", "Профиль не удалён"),
            JEZYK_Wybierz("Sprawdź kartę SD i spróbuj ponownie.",
                          "Check the SD card and try again.",
                          "SD-Karte prüfen und erneut versuchen.",
                          "Проверьте SD-карту и повторите попытку."));
        return false;
    }
    return true;
}

static void OSL_RysujWyborProfilu(uint8_t fokus, uint8_t strona, int32_t obecny)
{
    UI_AKCJA_t akcje[6];
    char nazwa[28];
    char status[28];
    char numer_strony[12];
    const uint16_t pierwszy = (uint16_t)strona * OSL_PROFIL_NA_STRONE;
    const uint8_t liczba = OSL_ProfilLiczbaNaStronie(strona);
    uint8_t i;

    UI_WyczyscEkran();
    UI_RysujPasekGorny(JEZYK_Wybierz("Wybierz profil OSL", "Select OSL profile",
                                      "OSL-Profil wählen", "Выберите профиль OSL"),
                        true, false, 0);

    for (i = 0U; i < liczba; ++i)
    {
        const uint16_t indeks = (uint16_t)(pierwszy + i);
        const UI_PROSTOKAT_t o = UI_ObszarKaflaKompaktowego(i);

        snprintf(nazwa, sizeof(nazwa), "%s %c",
                 JEZYK_Wybierz("Profil", "Profile", "Profil", "Профиль"),
                 (char)('A' + indeks));

        if ((int32_t)indeks == obecny)
        {
            snprintf(status, sizeof(status), "%s",
                     JEZYK_Wybierz("aktywny", "active", "aktiv", "активный"));
        }
        else
        {
            snprintf(status, sizeof(status), "%s",
                     OSL_ProfilPlikIstnieje(indeks)
                        ? JEZYK_Wybierz("istnieje", "exists", "vorhanden", "есть")
                        : JEZYK_Wybierz("nowy", "new", "neu", "новый"));
        }

        UI_RysujKafelMenuZWartoscia(o.x, o.y, o.szerokosc, o.wysokosc,
                                    UI_IKONA_MENU_LICZBA, nazwa, status,
                                    fokus == indeks);
    }

    snprintf(numer_strony, sizeof(numer_strony), "%u/%u",
             (unsigned)(strona + 1U), (unsigned)OSL_PROFIL_LICZBA_STRON);

    akcje[0] = (UI_AKCJA_t){ .id = 0, .tekst = JEZYK_Tekst(TEKST_WSTECZ),
                             .styl = UI_STYL_POWROT, .aktywna = true, .zaznaczona = false };
    akcje[1] = (UI_AKCJA_t){ .id = 1, .tekst = "<",
                             .styl = UI_STYL_NORMALNY, .aktywna = strona > 0U, .zaznaczona = false };
    akcje[2] = (UI_AKCJA_t){ .id = 2, .tekst = numer_strony,
                             .styl = UI_STYL_NIEAKTYWNY, .aktywna = false, .zaznaczona = false };
    akcje[3] = (UI_AKCJA_t){ .id = 3, .tekst = JEZYK_Wybierz("Dalej", "Next", "Weiter", "Далее"),
                             .styl = UI_STYL_AKCENT, .aktywna = true, .zaznaczona = false };
    akcje[4] = (UI_AKCJA_t){ .id = 4, .tekst = JEZYK_Wybierz("Usuń", "Delete", "Löschen", "Удалить"),
                             .styl = UI_STYL_OSTRZEZENIE,
                             .aktywna = OSL_ProfilPlikIstnieje(fokus) != 0U,
                             .zaznaczona = false };
    akcje[5] = (UI_AKCJA_t){ .id = 5, .tekst = ">",
                             .styl = UI_STYL_NORMALNY,
                             .aktywna = (uint8_t)(strona + 1U) < OSL_PROFIL_LICZBA_STRON,
                             .zaznaczona = false };

    UI_RysujPasekAkcji(UI_DOLNY_PASEK_Y, UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 6U);
}

/*
 * Wybór profilu OSL jest ekranem dotykowym, dlatego używa standardowych
 * kafli 150 x 84 px zamiast listy. Szesnaście profili zajmuje trzy strony.
 * Dotknięcie kafla tylko ustawia wybór; zapis następuje po "Dalej" lub OK.
 */
uint8_t OSL_WybierzProfilWnd(void)
{
    int32_t obecny = OSL_GetSelected();
    uint8_t fokus = (obecny >= 0 && obecny < OSL_PROFIL_LICZBA)
        ? (uint8_t)obecny : 0U;
    uint8_t strona = (uint8_t)(fokus / OSL_PROFIL_NA_STRONE);

    while (TOUCH_IsPressed())
        Sleep(0);
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        const uint16_t pierwszy = (uint16_t)strona * OSL_PROFIL_NA_STRONE;
        const uint8_t liczba = OSL_ProfilLiczbaNaStronie(strona);
        int16_t wybor_lokalny = -1;
        int16_t akcja = -1;

        OSL_RysujWyborProfilu(fokus, strona, obecny);

        for (;;)
        {
            if (TOUCH_Poll(&punkt))
            {
                UI_AKCJA_t akcje[6];
                char numer_strony[12];

                snprintf(numer_strony, sizeof(numer_strony), "%u/%u",
                         (unsigned)(strona + 1U), (unsigned)OSL_PROFIL_LICZBA_STRON);
                akcje[0] = (UI_AKCJA_t){ .id = 0, .tekst = JEZYK_Tekst(TEKST_WSTECZ), .styl = UI_STYL_POWROT, .aktywna = true, .zaznaczona = false };
                akcje[1] = (UI_AKCJA_t){ .id = 1, .tekst = "<", .styl = UI_STYL_NORMALNY, .aktywna = strona > 0U, .zaznaczona = false };
                akcje[2] = (UI_AKCJA_t){ .id = 2, .tekst = numer_strony, .styl = UI_STYL_NIEAKTYWNY, .aktywna = false, .zaznaczona = false };
                akcje[3] = (UI_AKCJA_t){ .id = 3, .tekst = JEZYK_Wybierz("Dalej", "Next", "Weiter", "Далее"), .styl = UI_STYL_AKCENT, .aktywna = true, .zaznaczona = false };
                akcje[4] = (UI_AKCJA_t){ .id = 4, .tekst = JEZYK_Wybierz("Usuń", "Delete", "Löschen", "Удалить"), .styl = UI_STYL_OSTRZEZENIE, .aktywna = OSL_ProfilPlikIstnieje(fokus) != 0U, .zaznaczona = false };
                akcje[5] = (UI_AKCJA_t){ .id = 5, .tekst = ">", .styl = UI_STYL_NORMALNY, .aktywna = (uint8_t)(strona + 1U) < OSL_PROFIL_LICZBA_STRON, .zaznaczona = false };

                if (UI_CzyDotknietoWstecz(punkt))
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return 0U;
                }

                wybor_lokalny = UI_KafelKompaktowyPoDotyku(punkt, liczba);
                akcja = UI_ZnajdzAkcjePaska(punkt, UI_DOLNY_PASEK_Y,
                                            UI_DOLNY_PRZYCISK_WYSOKOSC,
                                            akcje, 6U);

                if (wybor_lokalny >= 0)
                {
                    fokus = (uint8_t)(pierwszy + (uint16_t)wybor_lokalny);
                    TOUCH_CzekajNaPuszczenie(30U);
                    break;
                }
                if (akcja == 0)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return 0U;
                }
                if (akcja == 1 && strona > 0U)
                {
                    --strona;
                    fokus = (uint8_t)((uint16_t)strona * OSL_PROFIL_NA_STRONE);
                    TOUCH_CzekajNaPuszczenie(30U);
                    break;
                }
                if (akcja == 3)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    if (!OSL_WybierzProfilBezpiecznie((int32_t)fokus))
                    {
                        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ZAPISU_PLIKU);
                        break;
                    }
                    return 1U;
                }
                if (akcja == 4)
                {
                    const int32_t kasowany = (int32_t)fokus;
                    TOUCH_CzekajNaPuszczenie(30U);
                    if (OSL_UsunProfilZPotwierdzeniem(fokus))
                    {
                        if (obecny == kasowany)
                            obecny = -1;
                    }
                    break;
                }
                if (akcja == 5 && (uint8_t)(strona + 1U) < OSL_PROFIL_LICZBA_STRON)
                {
                    ++strona;
                    fokus = (uint8_t)((uint16_t)strona * OSL_PROFIL_NA_STRONE);
                    if (fokus >= OSL_PROFIL_LICZBA)
                        fokus = OSL_PROFIL_LICZBA - 1U;
                    TOUCH_CzekajNaPuszczenie(30U);
                    break;
                }
            }

            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                return 0U;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
            {
                fokus = fokus == 0U ? OSL_PROFIL_LICZBA - 1U : (uint8_t)(fokus - 1U);
                strona = (uint8_t)(fokus / OSL_PROFIL_NA_STRONE);
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                fokus = (uint8_t)((fokus + 1U) % OSL_PROFIL_LICZBA);
                strona = (uint8_t)(fokus / OSL_PROFIL_NA_STRONE);
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
            {
                if (!OSL_WybierzProfilBezpiecznie((int32_t)fokus))
                {
                    KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ZAPISU_PLIKU);
                    break;
                }
                return 1U;
            }
            Sleep(10U);
        }
    }
}

static bool OSL_EdytujWartoscWzorca(uint8_t indeks, int32_t profil_roboczy)
{
    uint32_t biezacy;
    uint32_t min_mohm;
    uint32_t max_mohm;
    uint32_t nowy;
    const char *tytul;
    bool (*ustaw)(uint32_t);

    switch (indeks)
    {
    case 0U:
        biezacy = CFG_GetOslRshortMilliOhm();
        min_mohm = 1000U;
        max_mohm = 20000U;
        tytul = JEZYK_Wybierz("Wzorzec niski [Ohm]", "Low standard [ohm]",
                              "Niedriges Normal [Ohm]", "Низкий эталон [Ом]");
        ustaw = CFG_UstawOslRshortZPomiaruZewnetrznego;
        break;
    case 1U:
        biezacy = CFG_GetOslRloadMilliOhm();
        min_mohm = 10000U;
        max_mohm = 200000U;
        tytul = JEZYK_Wybierz("Wzorzec środkowy [Ohm]", "Middle standard [ohm]",
                              "Mittleres Normal [Ohm]", "Средний эталон [Ом]");
        ustaw = CFG_UstawOslRloadZPomiaruZewnetrznego;
        break;
    case 2U:
        biezacy = CFG_GetOslRopenMilliOhm();
        min_mohm = 100000U;
        max_mohm = 2000000U;
        tytul = JEZYK_Wybierz("Wzorzec wysoki [Ohm]", "High standard [ohm]",
                              "Hohes Normal [Ohm]", "Высокий эталон [Ом]");
        ustaw = CFG_UstawOslRopenZPomiaruZewnetrznego;
        break;
    default:
        return false;
    }

    nowy = NumKeypadMiliohm(biezacy, min_mohm, max_mohm, tytul);
    if (nowy == 0U)
        return false;
    if (!ustaw(nowy))
    {
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Nie zapisano", "Not saved", "Nicht gespeichert", "Не сохранено"),
            JEZYK_Wybierz("Wartość jest poza dopuszczalnym zakresem tego wzorca.",
                          "The value is outside the allowed range for this standard.",
                          "Der Wert liegt außerhalb des zulässigen Bereichs dieses Normals.",
                          "Значение вне допустимого диапазона этого эталона."));
        return false;
    }

    /* Settery dokładnych wartości celowo wyłączają bieżącą OSL. Jeżeli
     * użytkownik edytuje wartości podczas przygotowania konkretnego profilu,
     * kasujemy jego stare metadane: stary plik pozostaje jako dane awaryjne,
     * ale nie może po restarcie zostać uznany za aktualną kalibrację wykonaną
     * z nowymi wartościami wzorców. */
    if (profil_roboczy >= 0 && profil_roboczy < (int32_t)OSL_PROFIL_LICZBA)
        (void)KAL_META_Usun(KAL_META_OSL, profil_roboczy);

    if (!CFG_FlushSprawdzony())
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ZAPISU_PLIKU);
        return false;
    }
    return true;
}

static bool OSL_UstawProfilRoboczy(int32_t profil_roboczy)
{
    if (profil_roboczy < 0 || profil_roboczy >= (int32_t)OSL_PROFIL_LICZBA)
        return false;
    if (!OSL_WybierzProfilBezpiecznie(profil_roboczy))
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ZAPISU_PLIKU);
        return false;
    }
    return true;
}

static uint8_t OSL_PrzygotowanieWnd(void)
{
    UI_AKCJA_t akcje[4];
    uint8_t fokus = 6U;
    int32_t profil_roboczy = OSL_GetSelected();
    char profil[32];
    char niski[40];
    char srodkowy[40];
    char wysoki[40];

    if (profil_roboczy < 0)
    {
        if (!OSL_WybierzProfilWnd())
            return 0U;
        profil_roboczy = OSL_GetSelected();
    }

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int16_t akcja;
        const UI_PROSTOKAT_t pole_profil = {14U, 42U, 452U, 40U};
        const UI_PROSTOKAT_t pole_niski = {14U, 88U, 452U, 34U};
        const UI_PROSTOKAT_t pole_srodkowy = {14U, 128U, 452U, 34U};
        const UI_PROSTOKAT_t pole_wysoki = {14U, 168U, 452U, 34U};

        if (profil_roboczy >= 0 && profil_roboczy < (int32_t)OSL_PROFIL_LICZBA)
            snprintf(profil, sizeof(profil), "%s %c",
                     JEZYK_Wybierz("Profil", "Profile", "Profil", "Профиль"),
                     (char)('A' + profil_roboczy));
        else
            snprintf(profil, sizeof(profil), "-");

        CFG_FormatujOslRshort(niski, sizeof(niski), true);
        CFG_FormatujOslRload(srodkowy, sizeof(srodkowy), true);
        CFG_FormatujOslRopen(wysoki, sizeof(wysoki), true);

        akcje[0] = (UI_AKCJA_t){ .id = 0, .tekst = JEZYK_Tekst(TEKST_WSTECZ),
                                 .styl = UI_STYL_POWROT, .aktywna = true,
                                 .zaznaczona = fokus == 0U };
        akcje[1] = (UI_AKCJA_t){ .id = 1, .tekst = JEZYK_Wybierz("Profil", "Profile", "Profil", "Профиль"),
                                 .styl = UI_STYL_NORMALNY, .aktywna = true,
                                 .zaznaczona = fokus == 1U };
        akcje[2] = (UI_AKCJA_t){ .id = 2, .tekst = "70 cm",
                                 .styl = OSL70_IsValid() ? UI_STYL_AKTYWNY : UI_STYL_AKCENT,
                                 .aktywna = true, .zaznaczona = fokus == 5U };
        akcje[3] = (UI_AKCJA_t){ .id = 3,
                                 .tekst = JEZYK_Wybierz("Szeroka", "Wide", "Breitband", "Широкая"),
                                 .styl = UI_STYL_AKCENT, .aktywna = true,
                                 .zaznaczona = fokus == 6U };

        UI_WyczyscEkran();
        UI_RysujPasekGorny(JEZYK_Wybierz("Przygotowanie OSL", "OSL preparation",
                                          "OSL vorbereiten", "Подготовка OSL"),
                            true, false, 0);
        UI_RysujPoleStatusu(pole_profil.x, pole_profil.y, pole_profil.szerokosc, pole_profil.wysokosc,
                            JEZYK_Wybierz("Profil kalibracji szerokiej", "Wide calibration profile",
                                          "Breitband-Kalibrierprofil", "Профиль широкой калибровки"),
                            profil, fokus == 1U ? UI_STYL_AKTYWNY : UI_STYL_AKCENT);
        UI_RysujPoleStatusu(pole_niski.x, pole_niski.y, pole_niski.szerokosc, pole_niski.wysokosc,
                            JEZYK_Wybierz("Wzorzec niski", "Low standard", "Niedriges Normal", "Низкий эталон"),
                            niski, fokus == 2U ? UI_STYL_AKTYWNY : UI_STYL_AKCENT);
        UI_RysujPoleStatusu(pole_srodkowy.x, pole_srodkowy.y, pole_srodkowy.szerokosc, pole_srodkowy.wysokosc,
                            JEZYK_Wybierz("Wzorzec środkowy", "Middle standard", "Mittleres Normal", "Средний эталон"),
                            srodkowy, fokus == 3U ? UI_STYL_AKTYWNY : UI_STYL_AKCENT);
        UI_RysujPoleStatusu(pole_wysoki.x, pole_wysoki.y, pole_wysoki.szerokosc, pole_wysoki.wysokosc,
                            JEZYK_Wybierz("Wzorzec wysoki", "High standard", "Hohes Normal", "Высокий эталон"),
                            wysoki, fokus == 4U ? UI_STYL_AKTYWNY : UI_STYL_AKCENT);
        UI_RysujPasekAkcji(222U, 40U, akcje, 4U);

        while (TOUCH_IsPressed())
            Sleep(10U);
        WEJSCIA_WyczyscZdarzenia();

        for (;;)
        {
            if (TOUCH_Poll(&punkt))
            {
                if (UI_CzyPunktWObszarze(punkt, &pole_profil))
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    if (OSL_WybierzProfilWnd())
                        profil_roboczy = OSL_GetSelected();
                    break;
                }
                if (UI_CzyPunktWObszarze(punkt, &pole_niski))
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    (void)OSL_EdytujWartoscWzorca(0U, profil_roboczy);
                    break;
                }
                if (UI_CzyPunktWObszarze(punkt, &pole_srodkowy))
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    (void)OSL_EdytujWartoscWzorca(1U, profil_roboczy);
                    break;
                }
                if (UI_CzyPunktWObszarze(punkt, &pole_wysoki))
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    (void)OSL_EdytujWartoscWzorca(2U, profil_roboczy);
                    break;
                }

                akcja = UI_ZnajdzAkcjePaska(punkt, 222U, 40U, akcje, 4U);
                if (UI_CzyDotknietoWstecz(punkt) || akcja == 0)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return 0U;
                }
                if (akcja == 1)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    if (OSL_WybierzProfilWnd())
                        profil_roboczy = OSL_GetSelected();
                    break;
                }
                if (akcja == 2)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    if (!OSL_UstawProfilRoboczy(profil_roboczy))
                        break;
                    return 2U;
                }
                if (akcja == 3)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    if (!OSL_UstawProfilRoboczy(profil_roboczy))
                        break;
                    return 1U;
                }
            }

            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                return 0U;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
            {
                fokus = fokus == 0U ? 6U : (uint8_t)(fokus - 1U);
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                fokus = fokus == 6U ? 0U : (uint8_t)(fokus + 1U);
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
            {
                if (fokus == 0U)
                    return 0U;
                if (fokus == 1U)
                {
                    if (OSL_WybierzProfilWnd())
                        profil_roboczy = OSL_GetSelected();
                    break;
                }
                if (fokus >= 2U && fokus <= 4U)
                {
                    (void)OSL_EdytujWartoscWzorca((uint8_t)(fokus - 2U), profil_roboczy);
                    break;
                }
                if (!OSL_UstawProfilRoboczy(profil_roboczy))
                    break;
                return fokus == 5U ? 2U : 1U;
            }
            Sleep(10U);
        }
    }
}
//OSL calibration window **********************************************************************************************
void OSL_CalWnd(void)
{
    char tytul[96];
    static char shortvalue[64];
    static char loadvalue[64];
    static char openvalue[64];
    char rshort_dokladny[32];
    char rload_dokladny[32];
    char ropen_dokladny[32];
    const LCDColor tekst = UI_KolorTekstu(UI_STYL_NORMALNY);
    const LCDColor tlo = UI_KolorTlaEkranu();

    {
        const uint8_t tryb = OSL_PrzygotowanieWnd();
        if (tryb == 0U)
            return;
        osl_tryb_70cm = tryb == 2U ? 1U : 0U;
    }
    rqExit = 0;
    shortScanned = 0;
    openScanned = 0;
    loadScanned = 0;
    progresstxt[0] = '\0';

    UI_WyczyscEkran();
    while (TOUCH_IsPressed())
        ;

    if (osl_tryb_70cm)
        snprintf(tytul, sizeof(tytul), "%s 420-450 MHz / 50 kHz",
                 JEZYK_Wybierz("OSL 70 cm", "70 cm OSL", "70-cm-OSL", "OSL 70 см"));
    else
        snprintf(tytul, sizeof(tytul), JEZYK_Tekst(TEKST_OSL_TYTUL_FMT), OSL_GetSelectedName());
    UI_RysujNaglowek(tytul);
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), tlo, 12, 36,
               JEZYK_Tekst(TEKST_OSL_INSTRUKCJA));

    TEXTBOX_InitContext(&osl_ctx);

    CFG_FormatujOslRshort(rshort_dokladny, sizeof(rshort_dokladny), true);
    CFG_FormatujOslRload(rload_dokladny, sizeof(rload_dokladny), true);
    CFG_FormatujOslRopen(ropen_dokladny, sizeof(ropen_dokladny), true);
    snprintf(shortvalue, sizeof(shortvalue), JEZYK_Tekst(TEKST_OSL_ZWARCIE_FMT), rshort_dokladny);
    snprintf(loadvalue, sizeof(loadvalue), JEZYK_Tekst(TEKST_OSL_OBCIAZENIE_FMT), rload_dokladny);
    if (!CFG_CzyOslRopenZPomiaruZewnetrznego() && CFG_GetParam(CFG_PARAM_OSL_ROPEN) >= 10000U)
        snprintf(openvalue, sizeof(openvalue), "%s", JEZYK_Tekst(TEKST_OSL_ROZWARCIE_NIESKONCZONOSC));
    else
        snprintf(openvalue, sizeof(openvalue), JEZYK_Tekst(TEKST_OSL_ROZWARCIE_FMT), ropen_dokladny);

    /*
     * Trzy wzorce sa pokazane jako kolejne, jednakowe kroki. Stan wykonania
     * zmienia kolor danego kroku, wiec uzytkownik od razu widzi, co zostalo
     * juz zmierzone i czego jeszcze brakuje do zapisu profilu.
     */
    hbScanShort = (TEXTBOX_t){.x0 = 14, .y0 = 62, .text = shortvalue,
        .font = FONT_FRANBIG, .width = 452, .height = 40, .center = 1, .border = 1,
        .fgcolor = tekst, .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_NORMALNY), .cb = _hb_scan_short};
    hbScanLoad = (TEXTBOX_t){.x0 = 14, .y0 = 109, .text = loadvalue,
        .font = FONT_FRANBIG, .width = 452, .height = 40, .center = 1, .border = 1,
        .fgcolor = tekst, .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_NORMALNY), .cb = _hb_scan_load};
    hbScanOpen = (TEXTBOX_t){.x0 = 14, .y0 = 156, .text = openvalue,
        .font = FONT_FRANBIG, .width = 452, .height = 40, .center = 1, .border = 1,
        .fgcolor = tekst, .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_NORMALNY), .cb = _hb_scan_open};

    /* Dolne akcje mają tę samą geometrię co Wstecz w całym interfejsie.
     * Krótsze etykiety pozostają czytelne na przycisku 70 x 45 px. */
    hbEx = (TEXTBOX_t){.x0 = 0, .y0 = 220,
        .text = (char *)JEZYK_Tekst(TEKST_ANULUJ),
        .rola = TEXTBOX_ROLA_WSTECZ, .font = FONT_FRAN, .width = 70, .height = 45,
        .center = 1, .border = 1, .fgcolor = UI_KolorTekstu(UI_STYL_POWROT),
        .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_POWROT), .cb = _hit_ex};
    hbSave = (TEXTBOX_t){.x0 = 82, .y0 = 220,
        .text = (char *)JEZYK_Tekst(TEKST_ZAPISZ),
        .font = FONT_FRAN, .width = 70, .height = 45, .center = 1, .border = 1,
        .fgcolor = UI_KolorTekstu(UI_STYL_NIEAKTYWNY),
        .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_NIEAKTYWNY), .cb = _hit_save};
    hbScanProgress = (TEXTBOX_t){.x0 = 198, .y0 = 199, .text = progresstxt,
        .font = FONT_FRANBIG, .nowait = 1, .fgcolor = UI_KolorRamki(UI_STYL_AKCENT),
        .bgcolor = tlo};

    TEXTBOX_Append(&osl_ctx, &hbEx);
    TEXTBOX_Append(&osl_ctx, &hbScanShort);
    TEXTBOX_Append(&osl_ctx, &hbScanLoad);
    TEXTBOX_Append(&osl_ctx, &hbScanOpen);
    TEXTBOX_Append(&osl_ctx, &hbSave);
    hbScanProgressId = TEXTBOX_Append(&osl_ctx, &hbScanProgress);
    TEXTBOX_DrawContext(&osl_ctx);

    for (;;)
    {
        if (TEXTBOX_HitTest(&osl_ctx))
        {
            if (rqExit)
            {
                /* Profil jest juz zapisany i ponownie wczytany z karty. */
                return;
            }
            Sleep(50);
        }
        autosleep_timer = 30000;
        Sleep(0);
    }
}

static uint8_t hw_scan_sukces_sesji;

/*
 * Ostatnia linia obrony przed kalibracja HW wykonana dla zlego nominalu
 * rezonatora Si5351. Ta kontrola siedzi bezposrednio w OSL_CalErrCorr(),
 * wiec obejmuje Ustawienia → Kalibracja, Metrologie 2026 i wywolania serwisowe.
 * Nie polegamy na tym, czy uzytkownik wczesniej wszedl do Diagnostyki.
 */
static uint8_t OSL_HW_SprawdzGeneratorPrzedSkanem(void)
{
    SPRAWDZENIE_IF_WYNIK_t w;
    char tresc[220];

    /*
     * Pomiar tonu IF jest właściwy dla oryginalnego toru Si5351. Nie wolno
     * nim blokować konfiguracji alternatywnego generatora, której ten test
     * z definicji nie potrafi zweryfikować.
     */
    if (CFG_GetParam(CFG_PARAM_SYNTH_TYPE) != CFG_SYNTH_SI5351)
        return 1U;

    memset(&w, 0, sizeof(w));
    SPRAWDZENIE_IF_Wykonaj(&w);

    if (w.status != SPRAWDZENIE_IF_OK)
    {
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Najpierw generator", "Check generator first",
                          "Zuerst Generator pruefen", "Сначала проверьте генератор"),
            JEZYK_Wybierz("Automatyczne sprawdzenie Si5351/IF nie potwierdzilo poprawnego sygnalu. Kalibracja HW zostala zablokowana. Sprawdz Si5351, tor IF i ustawienie 25/27 MHz.",
                          "Automatic Si5351/IF check did not confirm a valid signal. HW calibration is blocked. Check Si5351, IF path and 25/27 MHz setting.",
                          "Die automatische Si5351/IF-Pruefung bestaetigte kein gueltiges Signal. HW-Kalibrierung gesperrt. Si5351, IF-Pfad und 25/27 MHz pruefen.",
                          "Автотест Si5351/IF не подтвердил корректный сигнал. HW-калибровка заблокирована. Проверьте Si5351, IF и 25/27 МГц."));
        return 0U;
    }

    if (w.mozna_sugerowac_nominal &&
        w.sugerowany_nominal_hz != 0U &&
        w.sugerowany_nominal_hz != w.xtal_ustawiony_hz)
    {
        snprintf(tresc, sizeof(tresc),
                 "XTAL menu: %.3f MHz\nTest wskazuje: %.3f MHz\nIF: %.1f / %.1f Hz\n\nUstaw poprawny rezonator i dopiero wtedy wykonaj HW.",
                 (double)w.xtal_ustawiony_hz / 1000000.0,
                 (double)w.sugerowany_nominal_hz / 1000000.0,
                 (double)w.zmierzona_if_hz, (double)w.oczekiwana_if_hz);
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Zly nominal Si5351", "Wrong Si5351 nominal",
                          "Falscher Si5351-Nennwert", "Неверный номинал Si5351"),
            tresc);
        return 0U;
    }

    /*
     * Udana kalibracja HW zawsze przechodzi przez to sprawdzenie. Zachowujemy więc
     * jego trwały ślad także wtedy, gdy użytkownik nie uruchomił osobnego
     * ekranu „Sprawdź IF”. Dzięki temu stan generatora po restarcie odpowiada
     * temu, co rzeczywiście zostało sprawdzone przed HW.
     */
    (void)KAL_META_Zapisz(KAL_META_IF, -1);

    return 1U;
}

static uint8_t OSL_HW_SprawdzZakresGeneratora(void)
{
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);
    const uint8_t dol_ok = GEN_CzyCzestotliwoscObslugiwana(fmin) ? 1U : 0U;
    const uint8_t gora_ok = GEN_CzyCzestotliwoscObslugiwana(fmax) ? 1U : 0U;
    char tresc[360];

    /*
     * Edytory normalnie nie pozwalają zapisać takich wartości, ale config.bin
     * może pochodzić ze starszej wersji. Nie nazywamy tego "niespójnym planem":
     * pokazujemy dokładnie, które dwie granice są zapisane i dlaczego skan nie
     * może wystartować.
     */
    if (fmin < BAND_FMIN || fmax <= fmin || fmax > MAX_BAND_FREQ)
    {
        snprintf(tresc, sizeof(tresc),
                 "Zapisany zakres HW: %.3f - %.3f MHz.\nDozwolone granice danych: od %.3f do %.0f MHz, przy czym Fmax musi być większe od Fmin.\n\nPopraw tylko zakres kalibracji; pozostałych nastaw generatora program nie zmienia.",
                 (double)fmin / 1000000.0, (double)fmax / 1000000.0,
                 (double)BAND_FMIN / 1000000.0, (double)MAX_BAND_FREQ / 1000000.0);
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Nieprawidłowe granice zakresu HW",
                          "Invalid HW range limits",
                          "Ungültige Grenzen des HW-Bereichs",
                          "Неверные границы диапазона HW"),
            tresc);
        return 0U;
    }

    if (dol_ok && gora_ok)
        return 1U;

    if (typ == CFG_SYNTH_SI5351)
    {
        const uint32_t limit = CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);
        const uint8_t hmax = GEN_MaksHarmoniczna();
        const uint32_t maks_plan = GEN_MaksCzestotliwoscEfektywna();
        snprintf(tresc, sizeof(tresc),
                 "Zakres HW: %.3f - %.0f MHz\nPlan Si5351: bezpośrednio do %lu MHz, H%u -> do %lu MHz.\n\nNie można wygenerować %s granicy tego zakresu. Zmień zakres, limit bezpośredni lub harmoniczną. Program nie zmienia tych nastaw automatycznie.",
                 (double)fmin / 1000000.0, (double)fmax / 1000000.0,
                 (unsigned long)(limit / 1000000U), (unsigned)hmax,
                 (unsigned long)(maks_plan / 1000000U),
                 !dol_ok ? "dolnej" : "górnej");
    }
    else
    {
        snprintf(tresc, sizeof(tresc),
                 "Zakres HW: %.3f - %.0f MHz\nGenerator: %s, zakres sterownika około %.3f - %.0f MHz.\n\nNie można wygenerować %s granicy. Zmień zakres albo konfigurację generatora.",
                 (double)fmin / 1000000.0, (double)fmax / 1000000.0,
                 GEN_PobierzNazweSyntezera(),
                 (double)GEN_MinCzestotliwoscEfektywna() / 1000000.0,
                 (double)GEN_MaksCzestotliwoscEfektywna() / 1000000.0,
                 !dol_ok ? "dolnej" : "górnej");
    }

    KOMUNIKAT_PokazTekst(
        JEZYK_Wybierz("Zakres poza możliwościami generatora",
                      "Range outside generator capability",
                      "Bereich außerhalb der Generatorgrenzen",
                      "Диапазон вне возможностей генератора"),
        tresc);
    return 0U;
}

/*
 * Po udanym HW nie wystarcza samo przypomnienie. Użytkownik może zamknąć
 * komunikat i odruchowo wyjść do pomiaru, pozostawiając zworę CAL. Dlatego
 * przy próbie wyjścia wykonujemy tę samą, podwójną kontrolę 440 MHz.
 *
 * Twardo blokujemy wyjście wyłącznie przy statusie CAL_PEWNE, czyli po dwóch
 * niezależnych trafieniach w ciasne okno fingerprintu. PODEJRZENIE pozostaje
 * ostrzeżeniem: nie tworzymy sytuacji, w której dryft/naprawa toru uniemożliwi
 * użytkownikowi opuszczenie kalibracji.
 */
static uint8_t OSL_HW_PozwalaWyjscPoKalibracji(void)
{
    ZWORKA_CAL_WYNIK_t w;
    char tresc[320];

    if (!hw_scan_sukces_sesji)
        return 1U;

    w = ZWORKA_CAL_Sprawdz();
    if (w.status == ZWORKA_CAL_STATUS_CAL_PEWNE)
    {
        snprintf(tresc, sizeof(tresc),
                 JEZYK_Wybierz(
                     "Zworka nadal wygląda jak CAL. Dwa pomiary 440 MHz: dV/I=%+.2f/%+.2f dB, dFaza=%+.1f/%+.1f deg.\n\nPrzejście zostało zablokowane. Przełóż zworkę na WORK i wybierz Dalej ponownie.",
                     "The jumper still looks like CAL. Two 440 MHz checks: dV/I=%+.2f/%+.2f dB, dPhase=%+.1f/%+.1f deg.\n\nContinue is blocked. Move the jumper to WORK and choose Next again.",
                     "Die Bruecke sieht weiterhin wie CAL aus. Zwei 440-MHz-Pruefungen: dV/I=%+.2f/%+.2f dB, dPhase=%+.1f/%+.1f Grad.\n\nWeiter ist gesperrt. Bruecke auf WORK stellen und erneut Weiter waehlen.",
                     "Перемычка всё ещё определяется как CAL. Две проверки 440 МГц: dV/I=%+.2f/%+.2f дБ, dФаза=%+.1f/%+.1f град.\n\nПереход заблокирован. Установите WORK и снова выберите Далее."),
                 (double)w.delta_vi_db, (double)w.delta_vi_db_2,
                 (double)w.delta_faza_deg, (double)w.delta_faza_deg_2);
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Najpierw ustaw WORK", "Set WORK first",
                          "Zuerst WORK einstellen", "Сначала установите WORK"),
            tresc);
        return 0U;
    }

    if (w.status == ZWORKA_CAL_STATUS_PODEJRZENIE)
    {
        snprintf(tresc, sizeof(tresc),
                 JEZYK_Wybierz(
                     "Kontrola po HW jest blisko sygnatury CAL, ale nie spełnia warunku pewnej blokady. dV/I=%+.2f/%+.2f dB, dFaza=%+.1f/%+.1f deg.\n\nSprawdź ręcznie, czy zworka jest w WORK. Wyjście pozostaje możliwe.",
                     "The post-HW check is close to the CAL signature but does not meet the hard-block criterion. dV/I=%+.2f/%+.2f dB, dPhase=%+.1f/%+.1f deg.\n\nVerify manually that the jumper is in WORK. Exit remains available.",
                     "Die Kontrolle nach HW liegt nahe der CAL-Signatur, erreicht aber nicht die Sperrschwelle. dV/I=%+.2f/%+.2f dB, dPhase=%+.1f/%+.1f Grad.\n\nWORK manuell pruefen. Verlassen bleibt moeglich.",
                     "Проверка после HW близка к сигнатуре CAL, но не достигает порога блокировки. dV/I=%+.2f/%+.2f дБ, dФаза=%+.1f/%+.1f град.\n\nПроверьте WORK вручную. Выход остаётся доступен."),
                 (double)w.delta_vi_db, (double)w.delta_vi_db_2,
                 (double)w.delta_faza_deg, (double)w.delta_faza_deg_2);
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Sprawdź WORK", "Check WORK", "WORK pruefen", "Проверьте WORK"),
            tresc);
    }
    else if (w.status == ZWORKA_CAL_STATUS_BLAD || w.status == ZWORKA_CAL_STATUS_BRAK_WZORCA)
    {
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Kontrola WORK", "WORK check", "WORK-Pruefung", "Проверка WORK"),
            JEZYK_Wybierz("Automatyczna kontrola położenia zworki nie dała wiarygodnego wyniku. Sprawdź ręcznie pozycję WORK przed pomiarem lub OSL.",
                          "Automatic jumper verification did not produce a reliable result. Verify WORK manually before measurement or OSL.",
                          "Die automatische Brueckenpruefung lieferte kein sicheres Ergebnis. WORK vor Messung oder OSL manuell pruefen.",
                          "Автоматическая проверка перемычки не дала надёжного результата. Перед измерением или OSL проверьте WORK вручную."));
    }

    return 1U;
}

static void _hit_hw_dalej(void)
{
    if (!hw_scan_sukces_sesji)
        return;
    hw_dalej_wybrano = 1U;
    rqExit = 1U;
}

static void _hit_err_scan(void) // ************************************************************************
{
    /*
     * Wejście do HW jest zawsze dostępne dla legalnej konfiguracji. Dopiero
     * naciśnięcie START sprawdza, czy wybrany generator potrafi objąć cały
     * zadany zakres. To chroni zapis bez narzucania jednej "słusznej" nastawy.
     */
    if (!OSL_HW_SprawdzZakresGeneratora())
        return;
    if (!OSL_HW_SprawdzGeneratorPrzedSkanem())
        return;

    progressval = 100;
    progress_cb(0);

    hbScanShort.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_OSTRZEZENIE);
    TEXTBOX_DrawContext(&osl_ctx);

    int32_t wynik = OSL_ScanErrCorr(progress_cb);
    if (wynik != 0)
    {
        hbScanShort.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_OSTRZEZENIE);
        TEXTBOX_DrawContext(&osl_ctx);
        if (wynik == -4)
            KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_KALIBRACJA_PRZERWANA_BRAK_SYGNALU);
        else
            KOMUNIKAT_Pokaz(TEKST_BLAD, CFG_CzyKartaSDDostepna() ? TEKST_BLAD_ZAPISU_PLIKU : TEKST_BRAK_KARTY_SD);
        return;
    }

    hw_scan_sukces_sesji = 1U;
    hbScanShort.bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY);
    TEXTBOX_SetText(&osl_ctx, hbScanShortIdx,
                    JEZYK_Wybierz("Sukces - ustaw WORK", "Success - set WORK",
                                  "Erfolg - WORK einstellen", "Готово - установите WORK"));

    progresstxt[0] = '\0';
    TEXTBOX_SetText(&osl_ctx, hbScanProgressId, progresstxt);
    {
        TEXTBOX_t *dalej = TEXTBOX_Find(&osl_ctx, hbHwDalejIdx);
        if (dalej != NULL)
        {
            dalej->fgcolor = UI_KolorTekstu(UI_STYL_AKTYWNY);
            dalej->bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY);
            dalej->cb = _hit_hw_dalej;
        }
    }
    TEXTBOX_DrawContext(&osl_ctx);

    KOMUNIKAT_PokazTekst(
        JEZYK_Wybierz("Kalibracja HW gotowa", "HW calibration complete",
                      "HW-Kalibrierung fertig", "HW-калибровка готова"),
        JEZYK_Wybierz("Przełóż zworkę z CAL do WORK przed OSL. Przy każdym kroku OSL analizator wykona automatyczną kontrolę RAW przy 440 MHz i zablokuje pomiar, jeśli dwukrotnie rozpozna sygnaturę CAL.",
                      "Move the jumper from CAL to WORK before OSL. Before every OSL standard the analyzer will run a 440 MHz RAW check and block the measurement if the CAL signature is detected twice.",
                      "Vor OSL die Bruecke von CAL auf WORK stellen. Vor jedem OSL-Normal erfolgt eine RAW-Pruefung bei 440 MHz; bei zweimal erkannter CAL-Signatur wird die Messung gesperrt.",
                      "Перед OSL переставьте перемычку CAL в WORK. Перед каждым эталоном OSL анализатор проверит RAW на 440 МГц и заблокирует измерение при двойном обнаружении сигнатуры CAL."));
}

//Hardware error calibration window **********************************************************************
uint8_t OSL_CalErrCorr(void)
{
    TEXTBOX_t hbExLokalny;
    TEXTBOX_t hbDalejLokalny;
    const LCDColor tlo = UI_KolorTlaEkranu();

    rqExit = 0;
    shortScanned = 0; /* Zapobiega ponownemu ladowaniu profilu OSL przy wyjsciu. */
    openScanned = 0;
    loadScanned = 0;
    progresstxt[0] = '\0';
    hw_scan_sukces_sesji = 0U;
    hw_dalej_wybrano = 0U;

    UI_WyczyscEkran();
    while (TOUCH_IsPressed())
        ;

    UI_RysujNaglowek(JEZYK_Tekst(TEKST_HW_CAL_TYTUL));
    UI_RysujPanel(14, 46, 452, 58, 0, UI_STYL_OSTRZEZENIE);
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_OSTRZEZENIE), UI_KolorTlaPola(),
               24, 64, JEZYK_Tekst(TEKST_HW_CAL_INSTRUKCJA));

    TEXTBOX_InitContext(&osl_ctx);

    hbScanShort = (TEXTBOX_t){.x0 = 70, .y0 = 122,
        .tekst_id = TEXTBOX_TEKST(TEKST_HW_CAL_START), .font = FONT_FRANBIG,
        .width = 340, .height = 52, .center = 1, .border = 1,
        .fgcolor = UI_KolorTekstu(UI_STYL_AKCENT),
        .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKCENT), .cb = _hit_err_scan};
    hbScanShortIdx = TEXTBOX_Append(&osl_ctx, &hbScanShort);

    hbScanProgress = (TEXTBOX_t){.x0 = 198, .y0 = 187, .text = progresstxt,
        .font = FONT_FRANBIG, .nowait = 1, .fgcolor = UI_KolorRamki(UI_STYL_AKCENT),
        .bgcolor = tlo};
    hbScanProgressId = TEXTBOX_Append(&osl_ctx, &hbScanProgress);

    hbExLokalny = (TEXTBOX_t){.x0 = 0, .y0 = 220,
        .tekst_id = TEXTBOX_TEKST(TEKST_WSTECZ), .rola = TEXTBOX_ROLA_WSTECZ,
        .font = FONT_FRAN, .width = 70, .height = 45, .center = 1, .border = 1,
        .fgcolor = UI_KolorTekstu(UI_STYL_POWROT),
        .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_POWROT), .cb = _hit_ex};
    TEXTBOX_Append(&osl_ctx, &hbExLokalny);

    hbDalejLokalny = (TEXTBOX_t){.x0 = 330, .y0 = 220,
        .tekst_id = TEXTBOX_TEKST(TEKST_DALEJ), .font = FONT_FRANBIG,
        .width = 140, .height = 45, .center = 1, .border = 1,
        .fgcolor = UI_KolorTekstu(UI_STYL_NIEAKTYWNY),
        .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_NIEAKTYWNY), .cb = 0};
    hbHwDalejIdx = TEXTBOX_Append(&osl_ctx, &hbDalejLokalny);

    TEXTBOX_DrawContext(&osl_ctx);

    for (;;)
    {
        if (TEXTBOX_HitTest(&osl_ctx))
        {
            if (rqExit)
            {
                if (OSL_HW_PozwalaWyjscPoKalibracji())
                {
                    if (!hw_scan_sukces_sesji)
                        return 0U;
                    return hw_dalej_wybrano ? 2U : 1U;
                }

                /* CAL potwierdzone: pozostajemy w oknie HW i czekamy na WORK. */
                rqExit = 0U;
                TEXTBOX_DrawContext(&osl_ctx);
            }
            Sleep(50);
        }
        Sleep(0);
    }
}

//=====================================================================
//TX Calibration for |S21| window
//KD8CEC
//---------------------------------------------------------------------
static void _hit_att_scan(void);
static TEXTBOX_t *hbscan;
static int progress;

static void _hit_tx_scan(void) // ************************************************************************
{
    indextb = 0; // index of textbox
    progval = 100;
    S21progress_cb(0);
    hbscan = (TEXTBOX_t *)&tb_S21_CALIBRATION[1];
    hbscan->bgcolor = UI_KolorTlaPrzycisku(UI_STYL_OSTRZEZENIE);
    TEXTBOX_DrawContext(&osl_ctx);

    if (OSL_ScanTXCorr(S21progress_cb) != 0)
    {
        hbscan->bgcolor = UI_KolorTlaPrzycisku(UI_STYL_OSTRZEZENIE);
        TEXTBOX_DrawContext(&osl_ctx);
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BRAK_SYGNALU_S21);
        progresstxt[0] = '\0';
        TEXTBOX_SetText(&osl_ctx, 0, progresstxt);
        TEXTBOX_DrawContext(&osl_ctx);
        return;
    }

    hbscan->bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY);
    snprintf(progresstxt, sizeof(progresstxt), "%s 1", JEZYK_Tekst(TEKST_SUKCES));
    TEXTBOX_SetText(&osl_ctx, 0, progresstxt);
    LCD_FillRect(LCD_MakePoint(12, 45), LCD_MakePoint(467, 92), UI_KolorTlaPola());
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), UI_KolorTlaPola(),
               18, 58, JEZYK_Tekst(TEKST_S21_CAL_WLOZ_TLUMIK));
    Sleep(3000);
    progress = 1;
    progresstxt[0] = '\0';
    TEXTBOX_SetText(&osl_ctx, 0, progresstxt);
    TEXTBOX_DrawContext(&osl_ctx);
}

static void _hit_att_scan(void) // ************************************************************************
{
    if (progress != 1)
    {
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_S21_CAL_INSTRUKCJA);
        return;
    }
    progval = 100;
    S21progress_cb(0);
    hbscan = (TEXTBOX_t *)&tb_S21_CALIBRATION[2];
    hbscan->bgcolor = UI_KolorTlaPrzycisku(UI_STYL_OSTRZEZENIE);
    TEXTBOX_DrawContext(&osl_ctx);

    {
        const int32_t wynik = OSL_ScanTXAttenuator(S21progress_cb);
        if (wynik != 0)
        {
            hbscan->bgcolor = UI_KolorTlaPrzycisku(UI_STYL_OSTRZEZENIE);
            TEXTBOX_DrawContext(&osl_ctx);
            if (wynik == -1 || wynik == -4)
                KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BRAK_SYGNALU_S21);
            else
                KOMUNIKAT_Pokaz(TEKST_BLAD, CFG_CzyKartaSDDostepna() ? TEKST_BLAD_ZAPISU_PLIKU : TEKST_BRAK_KARTY_SD);
            return;
        }
    }

    progress = 2;
    hbscan->bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY);
    snprintf(progresstxt, sizeof(progresstxt), "%s 2", JEZYK_Tekst(TEKST_SUKCES));
    LCD_FillRect(LCD_MakePoint(12, 45), LCD_MakePoint(467, 92), UI_KolorTlaPola());
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_AKTYWNY), UI_KolorTlaPola(),
               18, 58, JEZYK_Tekst(TEKST_S21_CAL_GOTOWE));
    TEXTBOX_SetText(&osl_ctx, 0, progresstxt);
    tb_S21_CALIBRATION[3].tekst_id = 0U;
    tb_S21_CALIBRATION[3].text = JEZYK_Wybierz("Zakończ", "Finish", "Fertig", "Завершить");
    tb_S21_CALIBRATION[3].rola = TEXTBOX_ROLA_WSTECZ;
    tb_S21_CALIBRATION[3].fgcolor = UI_KolorTekstu(UI_STYL_AKTYWNY);
    tb_S21_CALIBRATION[3].bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY);
    TEXTBOX_DrawContext(&osl_ctx);
}

static int rq21Exit;

static void hit_exS21(void)
{
    /*
     * Wyjście po samym kroku 1 nie może zapisać niepełnej kalibracji.
     * Przywracamy ostatni kompletny plik, jeżeli taki istnieje.
     */
    if (progress == 1)
        OSL_LoadTXCorr();
    rq21Exit = 1;
}

static TEXTBOX_t tb_S21_CALIBRATION[] =
    {
        (TEXTBOX_t){.x0 = 320, .y0 = 50, .text = progresstxt, .font = FONT_FRANBIG, .width = 140, .fgcolor = LCD_WHITE, .bgcolor = LCD_BLACK, .next = (void *)&tb_S21_CALIBRATION[1]},
        (TEXTBOX_t){.x0 = 0, .y0 = 110, .tekst_id = TEXTBOX_TEKST(TEKST_S21_CAL_BEZ_TLUMIKA), .font = FONT_FRANBIG, .width = 476, .height = 34, .center = 1, .border = 1, .fgcolor = LCD_RED, .bgcolor = LCD_RGB(64, 64, 64), .cb = _hit_tx_scan, .cbparam = 1, .next = (void *)&tb_S21_CALIBRATION[2]},
        (TEXTBOX_t){.x0 = 0, .y0 = 160, .tekst_id = TEXTBOX_TEKST(TEKST_S21_CAL_Z_TLUMIKIEM), .font = FONT_FRANBIG, .width = 476, .height = 34, .center = 1, .border = 1, .fgcolor = LCD_RED, .bgcolor = LCD_RGB(64, 64, 64), .cb = _hit_att_scan, .cbparam = 1, .next = (void *)&tb_S21_CALIBRATION[3]},
        (TEXTBOX_t){
            .border = 1,
            .cbparam = 1,
            .center = 1,
            .nowait = 100,
            .x0 = 10,
            .y0 = 220,
            .tekst_id = TEXTBOX_TEKST(TEKST_WSTECZ), .rola = TEXTBOX_ROLA_WSTECZ,
            .font = FONT_FRANBIG,
            .fgcolor = LCD_BLUE,
            .bgcolor = LCD_YELLOW,
            .cb = hit_exS21,
            .width = 100,
            .height = 34,
            .next = NULL,
        },
};

void OSL_CalTXCorr(void)
{
    rq21Exit = 0;
    progress = 0;
    S21progress_cb(0);
    progresstxt[0] = '\0';
    indextb = 0; // percent field

    UI_WyczyscEkran();
    while (TOUCH_IsPressed())
        ;

    UI_RysujNaglowek(JEZYK_Tekst(TEKST_S21_CAL_TYTUL));
    UI_RysujPanel(10, 40, 460, 58, JEZYK_Tekst(TEKST_INFORMACJA), UI_STYL_NORMALNY);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(),
               18, 62, JEZYK_Tekst(TEKST_S21_CAL_INSTRUKCJA));
    {
        char tlumik_info[96];
        snprintf(tlumik_info, sizeof(tlumik_info), "%s %lu dB",
                 JEZYK_Wybierz("Tłumik wzorcowy:", "Reference attenuator:",
                               "Referenzdämpfer:", "Эталонный аттенюатор:"),
                 (unsigned long)CFG_GetParam(CFG_PARAM_ATTENUATOR));
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
                   18, 82, tlumik_info);
    }

    tb_S21_CALIBRATION[0].fgcolor = UI_KolorTekstu(UI_STYL_NORMALNY);
    tb_S21_CALIBRATION[0].bgcolor = UI_KolorTlaPola();
    tb_S21_CALIBRATION[1].fgcolor = UI_KolorTekstu(UI_STYL_AKCENT);
    tb_S21_CALIBRATION[1].bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKCENT);
    tb_S21_CALIBRATION[2].fgcolor = UI_KolorTekstu(UI_STYL_AKCENT);
    tb_S21_CALIBRATION[2].bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKCENT);
    tb_S21_CALIBRATION[3].tekst_id = TEXTBOX_TEKST(TEKST_WSTECZ);
    tb_S21_CALIBRATION[3].text = 0;
    tb_S21_CALIBRATION[3].rola = TEXTBOX_ROLA_WSTECZ;
    tb_S21_CALIBRATION[3].fgcolor = UI_KolorTekstu(UI_STYL_POWROT);
    tb_S21_CALIBRATION[3].bgcolor = UI_KolorTlaPrzycisku(UI_STYL_POWROT);

    TEXTBOX_InitContext(&osl_ctx);
    TEXTBOX_Append(&osl_ctx, (TEXTBOX_t *)tb_S21_CALIBRATION);

    TEXTBOX_DrawContext(&osl_ctx);

    for (;;)
    {
        if (TEXTBOX_HitTest(&osl_ctx) == 2)
        { // function executed?
            if (rq21Exit)
                break;
        }
        Sleep(10);
    }
}

/* ========================================================================
 * Ekrany dokumentacyjne kalibracji.
 *
 * Nie wykonują pomiarów ani zapisu. Pokazują kolejne stany tych samych
 * procedur, które użytkownik widzi podczas kalibracji OSL/HW/S21. Dzięki
 * temu instrukcja może opisać każdy krok bez uruchamiania RF w generatorze
 * zrzutów.
 * ======================================================================== */
static const char *const osl_dok_nazwy[] = {
    "osl_start",
    "osl_short_trwa",
    "osl_short_ok",
    "osl_load_trwa",
    "osl_load_ok",
    "osl_open_trwa",
    "osl_gotowe_zapis",
    "hw_start",
    "hw_trwa",
    "hw_gotowe",
    "s21_start",
    "s21_krok1_ok",
    "s21_gotowe"
};

uint32_t OSL_DokumentacjaLiczbaStron(void)
{
    return (uint32_t)(sizeof(osl_dok_nazwy) / sizeof(osl_dok_nazwy[0]));
}

const char *OSL_DokumentacjaNazwaStrony(uint32_t strona)
{
    if (strona >= OSL_DokumentacjaLiczbaStron())
        return "";
    return osl_dok_nazwy[strona];
}

static void OSL_DokRysujPasekPostepu(uint32_t procent)
{
    char p[16];
    uint16_t szer = (uint16_t)((430U * (procent > 100U ? 100U : procent)) / 100U);
    LCD_Rectangle(LCD_MakePoint(24, 198), LCD_MakePoint(456, 218), UI_KolorRamki(UI_STYL_NIEAKTYWNY));
    if (szer > 0U)
        LCD_FillRect(LCD_MakePoint(25, 199), LCD_MakePoint((int)(25U + szer), 217), UI_KolorRamki(UI_STYL_AKCENT));
    snprintf(p, sizeof(p), "%lu%%", (unsigned long)procent);
    /*
     * Procent jest częścią paska postępu. Wcześniej był rysowany przy y=181,
     * czyli na trzecim przycisku OSL. Mała ciemna etykieta na środku paska
     * pozostaje czytelna niezależnie od stopnia wypełnienia.
     */
    LCD_FillRect(LCD_MakePoint(210, 200), LCD_MakePoint(252, 216), UI_KolorTlaPola());
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(), 216, 201, p);
}

static void OSL_DokRysujOSL(uint8_t etap, uint32_t procent)
{
    char tytul[96];
    char shortvalue[64];
    char loadvalue[64];
    char openvalue[64];
    char rshort_dokladny[32];
    char rload_dokladny[32];
    char ropen_dokladny[32];
    UI_STYL_t styl_short = UI_STYL_NORMALNY;
    UI_STYL_t styl_load = UI_STYL_NORMALNY;
    UI_STYL_t styl_open = UI_STYL_NORMALNY;
    UI_STYL_t styl_save = UI_STYL_NIEAKTYWNY;

    if (etap >= 1U) styl_short = etap == 1U ? UI_STYL_OSTRZEZENIE : UI_STYL_AKTYWNY;
    if (etap >= 3U) styl_load = etap == 3U ? UI_STYL_OSTRZEZENIE : UI_STYL_AKTYWNY;
    if (etap >= 5U) styl_open = etap == 5U ? UI_STYL_OSTRZEZENIE : UI_STYL_AKTYWNY;
    if (etap >= 6U) styl_save = UI_STYL_AKTYWNY;

    snprintf(tytul, sizeof(tytul), JEZYK_Tekst(TEKST_OSL_TYTUL_FMT), "A");
    CFG_FormatujOslRshort(rshort_dokladny, sizeof(rshort_dokladny), true);
    CFG_FormatujOslRload(rload_dokladny, sizeof(rload_dokladny), true);
    CFG_FormatujOslRopen(ropen_dokladny, sizeof(ropen_dokladny), true);
    snprintf(shortvalue, sizeof(shortvalue), JEZYK_Tekst(TEKST_OSL_ZWARCIE_FMT), rshort_dokladny);
    snprintf(loadvalue, sizeof(loadvalue), JEZYK_Tekst(TEKST_OSL_OBCIAZENIE_FMT), rload_dokladny);
    if (!CFG_CzyOslRopenZPomiaruZewnetrznego() && CFG_GetParam(CFG_PARAM_OSL_ROPEN) >= 10000U)
        snprintf(openvalue, sizeof(openvalue), "%s", JEZYK_Tekst(TEKST_OSL_ROZWARCIE_NIESKONCZONOSC));
    else
        snprintf(openvalue, sizeof(openvalue), JEZYK_Tekst(TEKST_OSL_ROZWARCIE_FMT), ropen_dokladny);

    UI_WyczyscEkran();
    UI_RysujNaglowek(tytul);
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), UI_KolorTlaEkranu(), 12, 36, JEZYK_Tekst(TEKST_OSL_INSTRUKCJA));
    UI_RysujPrzycisk(14, 62, 452, 40, shortvalue, styl_short, FONT_FRANBIG);
    UI_RysujPrzycisk(14, 109, 452, 40, loadvalue, styl_load, FONT_FRANBIG);
    UI_RysujPrzycisk(14, 156, 452, 40, openvalue, styl_open, FONT_FRANBIG);
    if (etap == 1U || etap == 3U || etap == 5U)
        OSL_DokRysujPasekPostepu(procent);
    UI_RysujWsteczDolny(false);
    {
        const UI_PROSTOKAT_t zapisz = UI_ObszarPrzyciskuDolnego(1U);
        UI_RysujPrzycisk(zapisz.x, zapisz.y, zapisz.szerokosc, zapisz.wysokosc,
                         JEZYK_Tekst(TEKST_ZAPISZ), styl_save, FONT_FRAN);
    }
}

static void OSL_DokRysujHW(uint8_t etap)
{
    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_HW_CAL_TYTUL));
    UI_RysujPanel(14, 46, 452, 58, 0, UI_STYL_OSTRZEZENIE);
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_OSTRZEZENIE), UI_KolorTlaPola(), 24, 64, JEZYK_Tekst(TEKST_HW_CAL_INSTRUKCJA));
    UI_RysujPrzycisk(70, 122, 340, 52,
                     etap == 2U ? JEZYK_Tekst(TEKST_SUKCES) : JEZYK_Tekst(TEKST_HW_CAL_START),
                     etap == 0U ? UI_STYL_AKCENT : (etap == 1U ? UI_STYL_OSTRZEZENIE : UI_STYL_AKTYWNY),
                     FONT_FRANBIG);
    if (etap == 1U)
        OSL_DokRysujPasekPostepu(47U);
    UI_RysujWsteczDolny(false);
    if (etap == 2U)
    {
        const UI_PROSTOKAT_t dalej = UI_ObszarPrzyciskuDolnego(3U);
        UI_RysujPrzycisk(dalej.x, dalej.y, dalej.szerokosc, dalej.wysokosc,
                         JEZYK_Tekst(TEKST_DALEJ), UI_STYL_AKTYWNY, FONT_FRAN);
    }
}

static void OSL_DokRysujS21(uint8_t etap)
{
    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_S21_CAL_TYTUL));
    UI_RysujPanel(10, 40, 460, 58, JEZYK_Tekst(TEKST_INFORMACJA), UI_STYL_NORMALNY);
    FONT_Write(FONT_FRAN, etap == 0U ? UI_KolorTekstu(UI_STYL_NORMALNY) : UI_KolorTekstu(etap == 1U ? UI_STYL_OSTRZEZENIE : UI_STYL_AKTYWNY), UI_KolorTlaPola(), 18, 62,
               etap == 0U ? JEZYK_Tekst(TEKST_S21_CAL_INSTRUKCJA) : (etap == 1U ? JEZYK_Tekst(TEKST_S21_CAL_WLOZ_TLUMIK) : JEZYK_Tekst(TEKST_S21_CAL_GOTOWE)));
    UI_RysujPrzycisk(2, 110, 476, 34, JEZYK_Tekst(TEKST_S21_CAL_BEZ_TLUMIKA), etap >= 1U ? UI_STYL_AKTYWNY : UI_STYL_AKCENT, FONT_FRANBIG);
    UI_RysujPrzycisk(2, 160, 476, 34, JEZYK_Tekst(TEKST_S21_CAL_Z_TLUMIKIEM), etap >= 2U ? UI_STYL_AKTYWNY : UI_STYL_AKCENT, FONT_FRANBIG);
    if (etap >= 2U)
    {
        const UI_PROSTOKAT_t zakoncz = UI_ObszarPrzyciskuDolnego(0U);
        UI_RysujPrzycisk(zakoncz.x, zakoncz.y, zakoncz.szerokosc, zakoncz.wysokosc,
                         JEZYK_Wybierz("Zakończ", "Finish", "Fertig", "Завершить"), UI_STYL_AKTYWNY, FONT_FRAN);
    }
    else
        UI_RysujWsteczDolny(false);
}

void OSL_DokumentacjaRysujStrone(uint32_t strona)
{
    switch (strona)
    {
    case 0U: OSL_DokRysujOSL(0U, 0U); break;
    case 1U: OSL_DokRysujOSL(1U, 35U); break;
    case 2U: OSL_DokRysujOSL(2U, 0U); break;
    case 3U: OSL_DokRysujOSL(3U, 58U); break;
    case 4U: OSL_DokRysujOSL(4U, 0U); break;
    case 5U: OSL_DokRysujOSL(5U, 81U); break;
    case 6U: OSL_DokRysujOSL(6U, 0U); break;
    case 7U: OSL_DokRysujHW(0U); break;
    case 8U: OSL_DokRysujHW(1U); break;
    case 9U: OSL_DokRysujHW(2U); break;
    case 10U: OSL_DokRysujS21(0U); break;
    case 11U: OSL_DokRysujS21(1U); break;
    case 12U: OSL_DokRysujS21(2U); break;
    default: OSL_DokRysujOSL(0U, 0U); break;
    }
}
