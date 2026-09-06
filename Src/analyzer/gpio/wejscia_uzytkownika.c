#include "wejscia_uzytkownika.h"

#include "stm32f7xx_hal.h"

/* Enkoder: Arduino D2 = PG6, D4 = PG7. */
#define ENKODER_A_GPIO GPIOG
#define ENKODER_A_PIN GPIO_PIN_6
#define ENKODER_B_GPIO GPIOG
#define ENKODER_B_PIN GPIO_PIN_7

/* Przycisk enkodera: Arduino D6 = PH6. */
#define ENKODER_SW_GPIO GPIOH
#define ENKODER_SW_PIN GPIO_PIN_6

/* Przycisk WSTECZ: Arduino D10 = PA8. */
#define PRZYCISK_WSTECZ_GPIO GPIOA
#define PRZYCISK_WSTECZ_PIN GPIO_PIN_8

/* Przycisk START/STOP: Arduino A1 = PF10, uzywany jako zwykle GPIO. */
#define PRZYCISK_START_GPIO GPIOF
#define PRZYCISK_START_PIN GPIO_PIN_10

#define CZAS_DRGAN_STYKOW_MS 20U
#define ROZMIAR_KOLEJKI_ZDARZEN 8U

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t stan_surowy;
    uint8_t stan_stabilny;
    uint32_t czas_ostatniej_zmiany_ms;
    WEJSCIE_ZDARZENIE_t zdarzenie_nacisniecia;
} PRZYCISK_t;

static PRZYCISK_t przycisk_ok = {
    .port = ENKODER_SW_GPIO,
    .pin = ENKODER_SW_PIN,
    .zdarzenie_nacisniecia = WEJSCIE_ZDARZENIE_OK,
};

static PRZYCISK_t przycisk_wstecz = {
    .port = PRZYCISK_WSTECZ_GPIO,
    .pin = PRZYCISK_WSTECZ_PIN,
    .zdarzenie_nacisniecia = WEJSCIE_ZDARZENIE_WSTECZ,
};

static PRZYCISK_t przycisk_start = {
    .port = PRZYCISK_START_GPIO,
    .pin = PRZYCISK_START_PIN,
    .zdarzenie_nacisniecia = WEJSCIE_ZDARZENIE_START_STOP,
};

static WEJSCIE_ZDARZENIE_t kolejka_zdarzen[ROZMIAR_KOLEJKI_ZDARZEN];
static uint8_t kolejka_poczatek;
static uint8_t kolejka_koniec;

static uint8_t poprzedni_stan_enkodera;
static int8_t suma_enkodera;
static uint8_t wejscia_gotowe;

static void DodajZdarzenie(WEJSCIE_ZDARZENIE_t zdarzenie)
{
    uint8_t nowy_koniec;

    if (WEJSCIE_ZDARZENIE_BRAK == zdarzenie)
        return;

    nowy_koniec = (uint8_t)((kolejka_koniec + 1U) % ROZMIAR_KOLEJKI_ZDARZEN);
    if (nowy_koniec == kolejka_poczatek)
    {
        /*
         * Kolejka jest pelna. Zamiast blokowac sterowanie odrzucamy
         * najstarsze zdarzenie. Fizyczne wejscia nie moga zatrzymac pomiaru.
         */
        kolejka_poczatek = (uint8_t)((kolejka_poczatek + 1U) % ROZMIAR_KOLEJKI_ZDARZEN);
    }

    kolejka_zdarzen[kolejka_koniec] = zdarzenie;
    kolejka_koniec = nowy_koniec;
}

static uint8_t CzyNacisniety(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static void InicjalizujPrzycisk(PRZYCISK_t *przycisk)
{
    uint8_t stan = CzyNacisniety(przycisk->port, przycisk->pin);
    przycisk->stan_surowy = stan;
    przycisk->stan_stabilny = stan;
    przycisk->czas_ostatniej_zmiany_ms = HAL_GetTick();
}

static void AktualizujPrzycisk(PRZYCISK_t *przycisk, uint32_t teraz_ms)
{
    uint8_t stan = CzyNacisniety(przycisk->port, przycisk->pin);

    if (stan != przycisk->stan_surowy)
    {
        przycisk->stan_surowy = stan;
        przycisk->czas_ostatniej_zmiany_ms = teraz_ms;
        return;
    }

    if (stan == przycisk->stan_stabilny)
        return;

    if ((uint32_t)(teraz_ms - przycisk->czas_ostatniej_zmiany_ms) < CZAS_DRGAN_STYKOW_MS)
        return;

    przycisk->stan_stabilny = stan;
    if (stan)
        DodajZdarzenie(przycisk->zdarzenie_nacisniecia);
}

static uint8_t OdczytajStanEnkodera(void)
{
    uint8_t a = CzyNacisniety(ENKODER_A_GPIO, ENKODER_A_PIN);
    uint8_t b = CzyNacisniety(ENKODER_B_GPIO, ENKODER_B_PIN);
    return (uint8_t)((a << 1U) | b);
}

static void AktualizujEnkoder(void)
{
    /*
     * Tablica dekoduje poprawne przejscia kodu Graya. Niepoprawne skoki,
     * typowe dla drgan stykow, nie zmieniaja wyniku. Zdarzenie generujemy
     * dopiero po pelnym cyklu czterech poprawnych zboczy.
     */
    static const int8_t zmiana[16] = {
        0, -1, 1, 0,
        1, 0, 0, -1,
        -1, 0, 0, 1,
        0, 1, -1, 0,
    };

    uint8_t stan = OdczytajStanEnkodera();
    uint8_t indeks = (uint8_t)((poprzedni_stan_enkodera << 2U) | stan);
    int8_t krok = zmiana[indeks & 0x0FU];

    poprzedni_stan_enkodera = stan;
    if (0 == krok)
        return;

    suma_enkodera = (int8_t)(suma_enkodera + krok);

    if (suma_enkodera >= 4)
    {
        suma_enkodera = 0;
        DodajZdarzenie(WEJSCIE_ZDARZENIE_OBROT_PRAWO);
    }
    else if (suma_enkodera <= -4)
    {
        suma_enkodera = 0;
        DodajZdarzenie(WEJSCIE_ZDARZENIE_OBROT_LEWO);
    }
}

void WEJSCIA_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_LOW;

    gpio.Pin = ENKODER_A_PIN | ENKODER_B_PIN;
    HAL_GPIO_Init(GPIOG, &gpio);

    gpio.Pin = ENKODER_SW_PIN;
    HAL_GPIO_Init(GPIOH, &gpio);

    gpio.Pin = PRZYCISK_WSTECZ_PIN;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = PRZYCISK_START_PIN;
    HAL_GPIO_Init(GPIOF, &gpio);

    WEJSCIA_WyczyscZdarzenia();
    poprzedni_stan_enkodera = OdczytajStanEnkodera();
    suma_enkodera = 0;

    InicjalizujPrzycisk(&przycisk_ok);
    InicjalizujPrzycisk(&przycisk_wstecz);
    InicjalizujPrzycisk(&przycisk_start);
    wejscia_gotowe = 1U;
}

void WEJSCIA_Aktualizuj(void)
{
    uint32_t teraz_ms;

    if (!wejscia_gotowe)
        return;

    teraz_ms = HAL_GetTick();

    AktualizujEnkoder();
    AktualizujPrzycisk(&przycisk_ok, teraz_ms);
    AktualizujPrzycisk(&przycisk_wstecz, teraz_ms);
    AktualizujPrzycisk(&przycisk_start, teraz_ms);
}

WEJSCIE_ZDARZENIE_t WEJSCIA_PobierzZdarzenie(void)
{
    WEJSCIE_ZDARZENIE_t wynik;

    if (kolejka_poczatek == kolejka_koniec)
        return WEJSCIE_ZDARZENIE_BRAK;

    wynik = kolejka_zdarzen[kolejka_poczatek];
    kolejka_poczatek = (uint8_t)((kolejka_poczatek + 1U) % ROZMIAR_KOLEJKI_ZDARZEN);
    return wynik;
}

void WEJSCIA_WyczyscZdarzenia(void)
{
    kolejka_poczatek = 0;
    kolejka_koniec = 0;
}
