/*
 * EU1KY-PL 2026 - sterownik dwoch syntezerow ADF4351.
 *
 * Topologia zgodna z pierwotnym pomyslem EU1KY dla toru mikrofalowego:
 * - ADF4351 #0 generuje F0,
 * - ADF4351 #1 generuje LO,
 * - oba uklady otrzymuja wspolne 27 MHz z CLK2 ukladu Si5351,
 * - programowanie odbywa sie przez SPI2, z osobnymi liniami LE.
 *
 * Rejestry ADF4351 sa programowane bez dynamicznej pamieci. Dla uproszczenia
 * i powtarzalnosci ustawiamy PFD = 100 kHz (27 MHz / 270). Przy VCO >= 2,2 GHz
 * daje to bardzo duza wartosc INT, dlatego bezpiecznie mozemy uzywac preskalera
 * 8/9 w calym zakresie wyjsciowym.
 */
#include "adf4351.h"
#include "si5351.h"
#include "custom_spi2.h"
#include "rational.h"
#include "stm32746g_discovery.h"
#include "config.h"

#define ADF4351_MIN_OUT_FREQ 35000000ul
#define ADF4351_MAX_OUT_FREQ 4294000000ul
#define ADF4351_FPFD_CEL     100000ul
#define ADF4351_FVCO_MIN     2200000000ull
#define ADF4351_FVCO_MAX     4400000000ull

extern void Sleep(uint32_t);

static uint32_t ADF4351_RefClk(void)
{
    uint32_t f = CFG_GetParam(CFG_PARAM_ADF_REF_FREQ);
    return (f >= 1000000U && f <= 100000000U) ? f : 27000000U;
}

static uint32_t ADF4351_RValue(void)
{
    uint32_t r = (ADF4351_RefClk() + ADF4351_FPFD_CEL / 2U) / ADF4351_FPFD_CEL;
    if (r < 1U) r = 1U;
    if (r > 1023U) r = 1023U;
    return r;
}

static uint32_t ADF4351_Fpfd(void)
{
    return ADF4351_RefClk() / ADF4351_RValue();
}

static void ADF4351_Wyslij(uint32_t slowo, SPI2_Slave_t uklad)
{
    uint8_t bajty[4];
    bajty[0] = (uint8_t)((slowo >> 24) & 0xFFU);
    bajty[1] = (uint8_t)((slowo >> 16) & 0xFFU);
    bajty[2] = (uint8_t)((slowo >> 8) & 0xFFU);
    bajty[3] = (uint8_t)(slowo & 0xFFU);

    SPI2_SelectSlave(uklad);
    SPI2_Transmit(bajty, 4U);
    SPI2_DeselectSlave();
    Sleep(0U);
}

static void ADF4351_R5(SPI2_Slave_t uklad)
{
    /* DB20:19 musza byc ustawione na 11. LD pin: digital lock detect. */
    ADF4351_Wyslij(0x00580005ul, uklad);
}

static void ADF4351_R4(uint32_t wlacz, uint32_t dzielnik_rf, SPI2_Slave_t uklad)
{
    uint32_t slowo = 0U;
    slowo |= (3U << 3);                       /* moc RF OUT: +5 dBm */
    slowo |= ((wlacz & 1U) << 5);             /* RF OUT enable */
    slowo |= (0U << 8);                       /* AUX disabled */
    slowo |= (1U << 10);                      /* mute till lock detect */
    slowo |= (1U << 12);                      /* band select divider = 1 */
    slowo |= ((dzielnik_rf & 7U) << 20);      /* /1 ... /64 */
    slowo |= (1U << 23);                      /* feedback from fundamental VCO */
    ADF4351_Wyslij(slowo | 0x04U, uklad);
}

static void ADF4351_R3(SPI2_Slave_t uklad)
{
    ADF4351_Wyslij(0x00000003ul, uklad);
}

static void ADF4351_R2(uint32_t wylacz, SPI2_Slave_t uklad)
{
    uint32_t slowo = 0U;
    slowo |= ((wylacz & 1U) << 5);            /* power down */
    slowo |= (1U << 6);                       /* dodatnia polaryzacja PFD */
    slowo |= (8U << 9);                       /* prad pompy ladunkowej */
    slowo |= ((ADF4351_RValue() & 0x3FFU) << 14);
    ADF4351_Wyslij(slowo | 0x02U, uklad);
}

static void ADF4351_R1(uint32_t mod, SPI2_Slave_t uklad)
{
    uint32_t slowo = 0U;
    if (mod < 2U)
        mod = 2U;
    if (mod > 4095U)
        mod = 4095U;
    slowo |= ((mod & 0xFFFU) << 3);
    slowo |= (1U << 15);                      /* PHASE = 1 */
    slowo |= (1U << 27);                      /* preskaler 8/9 */
    ADF4351_Wyslij(slowo | 0x01U, uklad);
}

static void ADF4351_R0(uint32_t wartosc_int, uint32_t frac, SPI2_Slave_t uklad)
{
    uint32_t slowo = 0U;
    if (wartosc_int < 75U)
        wartosc_int = 75U;
    if (wartosc_int > 65535U)
        wartosc_int = 65535U;
    slowo |= ((frac & 0xFFFU) << 3);
    slowo |= ((wartosc_int & 0xFFFFU) << 15);
    ADF4351_Wyslij(slowo, uklad);
}

static int ADF4351_Oblicz(uint32_t hz, uint32_t *selektor_rf,
                          uint32_t *wartosc_int, uint32_t *frac, uint32_t *mod)
{
    uint64_t fvco;
    uint32_t dzielnik = 1U;
    uint32_t selektor = 0U;
    uint32_t licznik;
    uint32_t mianownik;

    if (hz < ADF4351_MIN_OUT_FREQ || hz > ADF4351_MAX_OUT_FREQ)
        return 0;

    fvco = (uint64_t)hz;
    while (fvco < ADF4351_FVCO_MIN && dzielnik < 64U)
    {
        dzielnik <<= 1U;
        ++selektor;
        fvco = (uint64_t)hz * (uint64_t)dzielnik;
    }

    if (fvco < ADF4351_FVCO_MIN || fvco > ADF4351_FVCO_MAX || selektor > 6U)
        return 0;

    rational_best_approximation((uint64_t)hz * (uint64_t)dzielnik,
                                ADF4351_Fpfd(),
                                0xFFFFFFFFU, 4095U,
                                &licznik, &mianownik);
    if (mianownik < 2U)
        mianownik = 2U;

    *wartosc_int = licznik / mianownik;
    *frac = licznik % mianownik;
    *mod = mianownik;
    *selektor_rf = selektor;

    return (*wartosc_int >= 75U && *wartosc_int <= 65535U);
}

static void ADF4351_Ustaw(uint32_t hz, SPI2_Slave_t uklad)
{
    uint32_t selektor_rf;
    uint32_t wartosc_int;
    uint32_t frac;
    uint32_t mod;

    if (!ADF4351_Oblicz(hz, &selektor_rf, &wartosc_int, &frac, &mod))
    {
        ADF4351_R4(0U, 0U, uklad);
        ADF4351_R2(1U, uklad);
        return;
    }

    ADF4351_R4(1U, selektor_rf, uklad);
    ADF4351_R2(0U, uklad);
    ADF4351_R1(mod, uklad);
    ADF4351_R0(wartosc_int, frac, uklad);
    Sleep(2U);
}

void ADF4351_Init(void)
{
    /* Odniesienie moze pochodzic z CLK2 Si5351 albo z zewnetrznego wejscia. */
    if (CFG_GetParam(CFG_PARAM_ADF_REF_SOURCE) == CFG_ADF_REF_SI5351_CLK2)
    {
        si5351_Init();
        si5351_SetF2(ADF4351_RefClk());
    }

    ADF4351_R5(SPI2_SLAVE_0);
    ADF4351_R4(0U, 0U, SPI2_SLAVE_0);
    ADF4351_R3(SPI2_SLAVE_0);
    ADF4351_R2(1U, SPI2_SLAVE_0);
    ADF4351_R1(2U, SPI2_SLAVE_0);
    ADF4351_R0(22000U, 0U, SPI2_SLAVE_0);

    ADF4351_R5(SPI2_SLAVE_1);
    ADF4351_R4(0U, 0U, SPI2_SLAVE_1);
    ADF4351_R3(SPI2_SLAVE_1);
    ADF4351_R2(1U, SPI2_SLAVE_1);
    ADF4351_R1(2U, SPI2_SLAVE_1);
    ADF4351_R0(22000U, 0U, SPI2_SLAVE_1);
}

void ADF4351_Off(void)
{
    ADF4351_R4(0U, 0U, SPI2_SLAVE_0);
    ADF4351_R2(1U, SPI2_SLAVE_0);
    ADF4351_R4(0U, 0U, SPI2_SLAVE_1);
    ADF4351_R2(1U, SPI2_SLAVE_1);
}

void ADF4351_SetF0(uint32_t hz)
{
    ADF4351_Ustaw(hz, SPI2_SLAVE_0);
}

void ADF4351_SetLO(uint32_t hz)
{
    ADF4351_Ustaw(hz, SPI2_SLAVE_1);
}

uint32_t ADF4351_MinFreq(void)
{
    return ADF4351_MIN_OUT_FREQ;
}

uint32_t ADF4351_MaxFreq(void)
{
    return ADF4351_MAX_OUT_FREQ;
}
