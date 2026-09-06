/**
  ******************************************************************************
  * @file    sd_diskio.c
  * @author  MCD Application Team
  * @version V1.3.0
  * @date    08-May-2015
  * @brief   SD Disk I/O driver
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2015 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <string.h>
#include "ff_gen_drv.h"
#include "stm32f7xx.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Block Size in Bytes */
#define BLOCK_SIZE                512U
#define CACHE_LINE_SIZE           32U
#define SD_MAX_DMA_BLOCKS         128U
#define DTCM_START_ADDRESS         0x20000000UL
#define DTCM_END_ADDRESS           0x20010000UL

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;

/*
 * SDMMC pracuje przez DMA, a Cortex-M7 ma 32-bajtowe linie D-cache.
 * FatFs może przekazać bufor zaczynający się pod dowolnym adresem. Dla takiego
 * bufora nie wolno wołać funkcji cache "by_Addr" wprost: wymagają adresu
 * wyrównanego do 32 B, a przy odczycie wyrównywanie zakresu na zewnątrz mogłoby
 * unieważnić cudze dane z tej samej linii cache.
 *
 * Dlatego niewyrównane sektory przechodzą przez jeden wyrównany bufor
 * pośredni. Zapis/odczyt jest blokujący już w BSP, więc jeden sektor wystarcza
 * i nie wprowadza współdzielenia bufora pomiędzy transferami.
 */

/* Private function prototypes -----------------------------------------------*/
DSTATUS SD_initialize (BYTE);
DSTATUS SD_status (BYTE);
DRESULT SD_read (BYTE, BYTE*, DWORD, UINT);
#if _USE_WRITE == 1
  DRESULT SD_write (BYTE, const BYTE*, DWORD, UINT);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT SD_ioctl (BYTE, BYTE, void*);
#endif  /* _USE_IOCTL == 1 */

const Diskio_drvTypeDef  SD_Driver =
{
  SD_initialize,
  SD_status,
  SD_read,
#if  _USE_WRITE == 1
  SD_write,
#endif /* _USE_WRITE == 1 */

#if  _USE_IOCTL == 1
  SD_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  lun : not used
  * @retval DSTATUS: Operation status
  */
DSTATUS SD_initialize(BYTE lun)
{
  Stat = STA_NOINIT;

  /* Configure the uSD device */
  if(BSP_SD_Init() == MSD_OK)
  {
    Stat &= ~STA_NOINIT;
  }

  return Stat;
}

/**
  * @brief  Gets Disk Status
  * @param  lun : not used
  * @retval DSTATUS: Operation status
  */
DSTATUS SD_status(BYTE lun)
{
  Stat = STA_NOINIT;

  if(BSP_SD_GetStatus() == MSD_OK)
  {
    Stat &= ~STA_NOINIT;
  }

  return Stat;
}

/**
  * @brief  Reads Sector(s)
  * @param  lun : not used
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read
  * @retval DRESULT: Operation result
  */

/*
 * Tryb zachowawczy: transfer blokujacy przez bufor w wewnetrznym SRAM.
 * Rozwiazanie priorytetyzuje stabilnosc zapisu i odpornosc na nietypowe karty.
 */
static uint8_t __attribute__((section(".dma_sram"), aligned(32))) sd_bufor_io[BLOCK_SIZE];

static DRESULT SD_CzytajSektorPolling(BYTE *bufor, DWORD sektor)
{
    if (BSP_SD_ReadBlocks((uint32_t *)sd_bufor_io,
                          (uint64_t)sektor * BLOCK_SIZE,
                          BLOCK_SIZE,
                          1U) != MSD_OK)
        return RES_ERROR;

    memcpy(bufor, sd_bufor_io, BLOCK_SIZE);
    return RES_OK;
}

#if _USE_WRITE == 1
static DRESULT SD_ZapiszSektorPolling(const BYTE *bufor, DWORD sektor)
{
    /*
     * Pojedyncze sektory FatFs (bufor częściowego sektora, katalog i FAT)
     * zapisujemy pollingowo. To celowe: właśnie f_close() wykonuje serię
     * małych operacji metadanych. Uruchamianie dla każdego sektora DMA,
     * przerwań i późniejszego CMD13 okazało się mniej odporne na długiej
     * kampanii niż prosty CMD24.
     *
     * Sama ścieżka HAL_SD_WriteBlocks() została poniżej zabezpieczona
     * rzeczywistymi timeoutami, więc polling nie może już zawiesić programu
     * bez końca. Większe, ciągłe bloki danych nadal korzystają z DMA.
     */
    memcpy(sd_bufor_io, bufor, BLOCK_SIZE);

    if (BSP_SD_WriteBlocks((uint32_t *)sd_bufor_io,
                           (uint64_t)sektor * BLOCK_SIZE,
                           BLOCK_SIZE,
                           1U) != MSD_OK)
        return RES_ERROR;

    return RES_OK;
}

/*
 * Ścieżka zapisu zgodna z mechanizmem, który był stabilny w rc6-test9/test13.
 *
 * Ważne: samo dzielenie BMP na f_write() po 1440 B nie wystarcza. FatFs może
 * przekazać do disk_write() kilka pełnych sektorów naraz. W gałęziach
 * wcześniejszej weryfikacji zapisu SD pojawił się wtedy pollingowy CMD25. Na długiej serii sprzętowej
 * potrafił zakończyć się FR_DISK_ERR mimo poprawnie zamontowanego FAT i dużej
 * ilości wolnego miejsca.
 *
 * Dla buforów dostępnych dla DMA wracamy więc do blokującej ścieżki
 * BSP_SD_WriteBlocks_DMA(). Transfer jest dzielony na najwyżej 128 sektorów,
 * tak jak w sprawdzonym mechanizmie test9. Bufor w DTCM albo niewyrównany nie
 * może być źródłem DMA - taki przypadek przechodzi przez bezpieczny zapis
 * sektor po sektorze z buforem pośrednim.
 */
static uint8_t SD_CzyBuforDostepnyDlaDMA(const BYTE *bufor, size_t rozmiar)
{
    const uintptr_t poczatek = (uintptr_t)bufor;
    const uintptr_t koniec = poczatek + rozmiar;

    if (bufor == NULL || rozmiar == 0U)
        return 0U;

    /* HAL SD operuje słowami 32-bitowymi. */
    if ((poczatek & 0x3U) != 0U)
        return 0U;

    /* DMA STM32F7 nie widzi DTCM 0x20000000..0x2000FFFF. */
    if (poczatek < DTCM_END_ADDRESS && koniec > DTCM_START_ADDRESS)
        return 0U;

    return 1U;
}

static void SD_WyczyscCachePrzedDMA(const BYTE *bufor, size_t rozmiar)
{
    const uintptr_t poczatek = (uintptr_t)bufor;
    const uintptr_t poczatek_linii = poczatek & ~(uintptr_t)(CACHE_LINE_SIZE - 1U);
    const uintptr_t koniec = poczatek + rozmiar;
    const uintptr_t koniec_linii = (koniec + CACHE_LINE_SIZE - 1U) &
                                   ~(uintptr_t)(CACHE_LINE_SIZE - 1U);

    /*
     * SDRAM wyświetlacza jest skonfigurowany jako non-cacheable, więc to
     * wywołanie niczego tam nie psuje. Dla cache'owalnego SRAM gwarantuje,
     * że SDMMC DMA zobaczy najnowszą zawartość pamięci.
     */
    SCB_CleanDCache_by_Addr((uint32_t *)poczatek_linii,
                            (int32_t)(koniec_linii - poczatek_linii));
    __DSB();
}

static DRESULT SD_ZapiszBlokiDMA(const BYTE *bufor, DWORD sektor, UINT liczba)
{
    UINT pozostalo = liczba;
    DWORD biezacy_sektor = sektor;
    const BYTE *biezacy_bufor = bufor;
    const size_t caly_rozmiar = (size_t)liczba * BLOCK_SIZE;

    if (!SD_CzyBuforDostepnyDlaDMA(bufor, caly_rozmiar))
    {
        UINT i;
        for (i = 0U; i < liczba; ++i)
        {
            if (SD_ZapiszSektorPolling(&bufor[(size_t)i * BLOCK_SIZE],
                                       sektor + i) != RES_OK)
                return RES_ERROR;
        }
        return RES_OK;
    }

    while (pozostalo != 0U)
    {
        const UINT porcja = pozostalo > SD_MAX_DMA_BLOCKS ?
                            SD_MAX_DMA_BLOCKS : pozostalo;
        const size_t rozmiar = (size_t)porcja * BLOCK_SIZE;

        SD_WyczyscCachePrzedDMA(biezacy_bufor, rozmiar);

        if (BSP_SD_WriteBlocks_DMA((uint32_t *)(uintptr_t)biezacy_bufor,
                                  (uint64_t)biezacy_sektor * BLOCK_SIZE,
                                  BLOCK_SIZE,
                                  porcja) != MSD_OK)
            return RES_ERROR;

        biezacy_bufor += rozmiar;
        biezacy_sektor += porcja;
        pozostalo -= porcja;
    }

    return RES_OK;
}
#endif

DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
    UINT i;
    (void)lun;
    if (buff == NULL || count == 0U) return RES_PARERR;
    for (i = 0U; i < count; ++i)
        if (SD_CzytajSektorPolling(&buff[(size_t)i * BLOCK_SIZE], sector + i) != RES_OK)
            return RES_ERROR;
    return RES_OK;
}

#if _USE_WRITE == 1
DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
    (void)lun;
    if (buff == NULL || count == 0U) return RES_PARERR;
    return SD_ZapiszBlokiDMA(buff, sector, count);
}
#endif

/**
  * @brief  I/O control operation
  * @param  lun : not used
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff)
{
  DRESULT res = RES_ERROR;
  SD_CardInfo CardInfo;

  if (Stat & STA_NOINIT) return RES_NOTRDY;

  switch (cmd)
  {
  /* Make sure that no pending write process */
  case CTRL_SYNC :
    /*
     * SD_write() jest w tym projekcie operacją synchroniczną: funkcja
     * wraca dopiero po zakończeniu HAL_SD_CheckWriteOperation(). Nie ma
     * więc żadnego "zapisu w tle", który trzeba byłoby ponownie sondować
     * komendą CMD13 podczas f_sync()/f_close().
     *
     * Poprzednia implementacja wywoływała tutaj BSP_SD_GetStatus(). Była
     * to dodatkowa, niepotrzebna transakcja SDMMC dokładnie w najbardziej
     * wrażliwym miejscu — przy zamykaniu pliku. Na części kart CMD13 potrafi
     * pozostać bez odpowiedzi i zamrozić cały interfejs mimo poprawnego
     * wcześniejszego zapisu sektorów. Skoro disk_write() sam potwierdza
     * zakończenie transferu, CTRL_SYNC może bezpiecznie zwrócić RES_OK.
     */
    res = RES_OK;
    break;

  /* Get number of sectors on the disk (DWORD) */
  case GET_SECTOR_COUNT :
    BSP_SD_GetCardInfo(&CardInfo);
    *(DWORD*)buff = CardInfo.CardCapacity / BLOCK_SIZE;
    res = RES_OK;
    break;

  /* Get R/W sector size (WORD) */
  case GET_SECTOR_SIZE :
    *(WORD*)buff = BLOCK_SIZE;
    res = RES_OK;
    break;

  /* Get erase block size in unit of sector (DWORD) */
  case GET_BLOCK_SIZE :
    /*
     * FatFs oczekuje tutaj liczby sektorów, a nie liczby bajtów. Poprzednie
     * 512 oznaczało błędnie blok kasowania 256 KiB. Bezpieczna wartość 1
     * jest zgodna z jednostką API i wystarcza dla operacji używanych tutaj.
     */
    *(DWORD*)buff = 1U;
    res = RES_OK;
    break;

  default:
    res = RES_PARERR;
  }

  return res;
}
#endif /* _USE_IOCTL == 1 */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

