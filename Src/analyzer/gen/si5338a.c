/*
 * Si5338A - bezpieczny sterownik wyjsciowych MultiSynth dla EU1KY-PL 2026.
 *
 * Uklad Si5338 ma bardzo elastyczna petle PLL. Nie znamy jednak konfiguracji
 * wszystkich klonow Mini600/Mini1300. Dlatego ten sterownik nie zmienia PLL,
 * wejscia referencyjnego ani trybu driverow. Wykrywa prawdziwy Si5338 po ID
 * i programuje wylacznie MS0..MS3 przy podanej rzeczywistej czestotliwosci VCO.
 *
 * Format P1/P2/P3 jest zgodny z dokumentacja Skyworks i implementacja bladeRF.
 */
#include "si5338a.h"

#include <stdint.h>
#include <string.h>

#include "config.h"
#include "rational.h"

extern void CAMERA_IO_Init(void);
extern void CAMERA_IO_Write(uint8_t addr, uint8_t reg, uint8_t value);
extern uint8_t CAMERA_IO_Read(uint8_t addr, uint8_t reg);
extern uint8_t CAMERA_IO_IsDeviceReady(uint8_t addr);

#define SI5338_ADDR0_8BIT          0xE0U /* 7-bit 0x70 */
#define SI5338_ADDR1_8BIT          0xE2U /* 7-bit 0x71 */
#define SI5338_REG_PAGE            255U
#define SI5338_REG_DEVICE_ID       2U
#define SI5338_DEVICE_ID_MASK      0x3FU
#define SI5338_DEVICE_ID_EXPECTED  38U
#define SI5338_REG_REVISION        0U
#define SI5338_REG_HIGHSPEED       51U
#define SI5338_REG_R_BASE          31U
#define SI5338_REG_ENABLE_BASE     36U
#define SI5338_REG_MS_BASE         53U
#define SI5338_MS_STRIDE           11U
#define SI5338_MIN_MS_OUT_HZ       5000000U
#define SI5338_MAX_DIV             568U
#define SI5338_MAX_FRAC            0x3FFFFFFFU

static uint8_t g_addr = SI5338_ADDR0_8BIT;
static uint8_t g_present = 0U;
static uint8_t g_revision = 0U;

typedef struct
{
    uint32_t a, b, c;
    uint8_t r;
    uint8_t high_speed;
    uint8_t regs[10];
} SI5338_MS_t;

static uint8_t SI5338A_CzyToUklad(uint8_t addr)
{
    uint8_t id;
    if (!CAMERA_IO_IsDeviceReady(addr))
        return 0U;
    CAMERA_IO_Write(addr, SI5338_REG_PAGE, 0U);
    id = CAMERA_IO_Read(addr, SI5338_REG_DEVICE_ID);
    return ((id & SI5338_DEVICE_ID_MASK) == SI5338_DEVICE_ID_EXPECTED) ? 1U : 0U;
}

uint8_t SI5338A_Detect(void)
{
    const uint8_t ustawiony = (uint8_t)CFG_GetParam(CFG_PARAM_SI5338_BUS_ADDR);
    CAMERA_IO_Init();

    if (ustawiony != 0U)
    {
        if (SI5338A_CzyToUklad(ustawiony))
        {
            g_addr = ustawiony;
            g_present = 1U;
        }
        else
        {
            g_present = 0U;
        }
    }
    else if (SI5338A_CzyToUklad(SI5338_ADDR0_8BIT))
    {
        g_addr = SI5338_ADDR0_8BIT;
        g_present = 1U;
    }
    else if (SI5338A_CzyToUklad(SI5338_ADDR1_8BIT))
    {
        g_addr = SI5338_ADDR1_8BIT;
        g_present = 1U;
    }
    else
    {
        g_present = 0U;
    }

    if (g_present)
    {
        CAMERA_IO_Write(g_addr, SI5338_REG_PAGE, 0U);
        g_revision = CAMERA_IO_Read(g_addr, SI5338_REG_REVISION);
    }
    return g_present;
}

uint8_t SI5338A_IsPresent(void) { return g_present; }
uint8_t SI5338A_GetBusAddress(void) { return g_addr; }
uint8_t SI5338A_GetRevision(void) { return g_revision; }

uint32_t SI5338A_MinFreq(void)
{
    const uint32_t vco = CFG_GetParam(CFG_PARAM_SI5338_VCO_FREQ);
    uint32_t min_hz = vco / (SI5338_MAX_DIV * 32U);
    if (min_hz < 100000U) min_hz = 100000U;
    return min_hz;
}

uint32_t SI5338A_MaxFreq(void)
{
    const uint32_t vco = CFG_GetParam(CFG_PARAM_SI5338_VCO_FREQ);
    const uint32_t cfg = CFG_GetParam(CFG_PARAM_SI5338_FMAX);
    const uint32_t bezpieczny = vco / 8U;
    return cfg < bezpieczny ? cfg : bezpieczny;
}

int SI5338A_CanSet(uint32_t fhz)
{
    uint64_t ms_scaled;
    uint32_t a;
    uint8_t r = 1U;
    const uint32_t vco = CFG_GetParam(CFG_PARAM_SI5338_VCO_FREQ);

    if (fhz < SI5338A_MinFreq() || fhz > SI5338A_MaxFreq() || vco == 0U)
        return 0;

    while ((uint64_t)fhz * r < SI5338_MIN_MS_OUT_HZ && r < 32U)
        r <<= 1U;
    if ((uint64_t)fhz * r < SI5338_MIN_MS_OUT_HZ)
        return 0;

    ms_scaled = ((uint64_t)vco * 1000000ULL) / ((uint64_t)fhz * r);
    a = (uint32_t)(ms_scaled / 1000000ULL);
    return (a >= 8U && a <= SI5338_MAX_DIV) ? 1 : 0;
}

static void SI5338A_Pack(SI5338_MS_t *ms)
{
    uint64_t temp;
    uint32_t p1, p2, p3;

    temp = ((uint64_t)ms->a * ms->c + ms->b) * 128ULL;
    p1 = (uint32_t)(temp / ms->c - 512ULL);
    p2 = (uint32_t)(((uint64_t)ms->b * 128ULL) % ms->c);
    p3 = ms->c;

    ms->regs[0] = (uint8_t)(p1 & 0xFFU);
    ms->regs[1] = (uint8_t)((p1 >> 8) & 0xFFU);
    ms->regs[2] = (uint8_t)(((p2 & 0x3FU) << 2) | ((p1 >> 16) & 0x03U));
    ms->regs[3] = (uint8_t)((p2 >> 6) & 0xFFU);
    ms->regs[4] = (uint8_t)((p2 >> 14) & 0xFFU);
    ms->regs[5] = (uint8_t)((p2 >> 22) & 0xFFU);
    ms->regs[6] = (uint8_t)(p3 & 0xFFU);
    ms->regs[7] = (uint8_t)((p3 >> 8) & 0xFFU);
    ms->regs[8] = (uint8_t)((p3 >> 16) & 0xFFU);
    ms->regs[9] = (uint8_t)((p3 >> 24) & 0x3FU);
}

static int SI5338A_Oblicz(uint32_t fhz, SI5338_MS_t *ms)
{
    uint64_t mianownik;
    uint32_t n, d;
    uint8_t r = 1U;
    const uint32_t vco = CFG_GetParam(CFG_PARAM_SI5338_VCO_FREQ);

    memset(ms, 0, sizeof(*ms));
    if (!SI5338A_CanSet(fhz)) return 0;

    while ((uint64_t)fhz * r < SI5338_MIN_MS_OUT_HZ && r < 32U)
        r <<= 1U;

    mianownik = (uint64_t)fhz * r;
    rational_best_approximation(vco, mianownik,
                                0xFFFFFFFFU,
                                SI5338_MAX_FRAC, &n, &d);
    if (d == 0U) return 0;

    ms->a = n / d;
    ms->b = n % d;
    ms->c = d;
    ms->r = r;
    ms->high_speed = 0U;

    if (ms->a < 8U || ms->a > SI5338_MAX_DIV)
        return 0;
    if (ms->c == 0U || ms->c > SI5338_MAX_FRAC || ms->b >= ms->c)
        return 0;

    SI5338A_Pack(ms);
    return 1;
}

static uint8_t SI5338A_RPower(uint8_t r)
{
    uint8_t p = 0U;
    while (r > 1U) { r >>= 1U; ++p; }
    return p;
}

static void SI5338A_UstawWyjscie(uint8_t index, uint32_t fhz)
{
    SI5338_MS_t ms;
    uint8_t i, val;
    uint8_t base;

    if (!g_present || index > 3U || !SI5338A_Oblicz(fhz, &ms))
        return;

    CAMERA_IO_Write(g_addr, SI5338_REG_PAGE, 0U);

    /* Zwykly tryb 8..568. Nie dotykamy innych wyjsc ani MS_PEC. */
    val = CAMERA_IO_Read(g_addr, SI5338_REG_HIGHSPEED);
    val = (uint8_t)(val & (uint8_t)~(1U << (4U + index)));
    val = (uint8_t)((val & 0xF8U) | 0x07U); /* MS_PEC=111 wymagane dla nie-factory */
    CAMERA_IO_Write(g_addr, SI5338_REG_HIGHSPEED, val);

    base = (uint8_t)(SI5338_REG_MS_BASE + index * SI5338_MS_STRIDE);
    for (i = 0U; i < 10U; ++i)
        CAMERA_IO_Write(g_addr, (uint8_t)(base + i), ms.regs[i]);

    /* RxDIV: pola [4:2], zachowujemy reszte konfiguracji wyjscia. */
    val = CAMERA_IO_Read(g_addr, (uint8_t)(SI5338_REG_R_BASE + index));
    val = (uint8_t)((val & (uint8_t)~0x1CU) | ((SI5338A_RPower(ms.r) & 7U) << 2));
    CAMERA_IO_Write(g_addr, (uint8_t)(SI5338_REG_R_BASE + index), val);

    /* Wlacz kanal A danego drivera; nie narzucamy standardu elektrycznego. */
    val = CAMERA_IO_Read(g_addr, (uint8_t)(SI5338_REG_ENABLE_BASE + index));
    CAMERA_IO_Write(g_addr, (uint8_t)(SI5338_REG_ENABLE_BASE + index), (uint8_t)(val | 0x01U));
}

static void SI5338A_WylaczWyjscie(uint8_t index)
{
    uint8_t val;
    if (!g_present || index > 3U) return;
    CAMERA_IO_Write(g_addr, SI5338_REG_PAGE, 0U);
    val = CAMERA_IO_Read(g_addr, (uint8_t)(SI5338_REG_ENABLE_BASE + index));
    CAMERA_IO_Write(g_addr, (uint8_t)(SI5338_REG_ENABLE_BASE + index), (uint8_t)(val & (uint8_t)~0x03U));
}

void SI5338A_Init(void)
{
    (void)SI5338A_Detect();
}

void SI5338A_Off(void)
{
    const uint8_t f0 = (uint8_t)CFG_GetParam(CFG_PARAM_SI5338_OUT_F0);
    const uint8_t lo = (uint8_t)CFG_GetParam(CFG_PARAM_SI5338_OUT_LO);
    SI5338A_WylaczWyjscie(f0);
    if (lo != f0) SI5338A_WylaczWyjscie(lo);
}

void SI5338A_SetF0(uint32_t fhz)
{
    SI5338A_UstawWyjscie((uint8_t)CFG_GetParam(CFG_PARAM_SI5338_OUT_F0), fhz);
}

void SI5338A_SetLO(uint32_t fhz)
{
    SI5338A_UstawWyjscie((uint8_t)CFG_GetParam(CFG_PARAM_SI5338_OUT_LO), fhz);
}
