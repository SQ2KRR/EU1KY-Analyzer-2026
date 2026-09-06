/* ----------------------------------------------------------------------
* Copyright (C) 2010-2014 ARM Limited. All rights reserved.
*
* $Date:        19. March 2015
* $Revision:     V.1.4.5
*
* Project:         CMSIS DSP Library
* Title:        arm_cfft_init_f32.c
*
* Description:    Split Radix Decimation in Frequency CFFT Floating point processing function
*
* Target Processor: Cortex-M4/Cortex-M3/Cortex-M0
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*   - Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   - Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in
*     the documentation and/or other materials provided with the
*     distribution.
*   - Neither the name of ARM LIMITED nor the names of its contributors
*     may be used to endorse or promote products derived from this
*     software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
* ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
* -------------------------------------------------------------------- */

#include <stddef.h>
#include "arm_math.h"
#include "arm_common_tables.h"

/**
 * @ingroup groupTransforms
 */

/**
 * @addtogroup RealFFT
 * @{
 */

/**
* @brief  Initialization function for the floating-point real FFT.
* @param[in,out] *S             points to an arm_rfft_fast_instance_f32 structure.
* @param[in]     fftLen         length of the Real Sequence.
* @return        The function returns ARM_MATH_SUCCESS if initialization is successful or ARM_MATH_ARGUMENT_ERROR if <code>fftLen</code> is not a supported value.
*
* \par Description:
* \par
* The parameter <code>fftLen</code> specifies length of RFFT/CIFFT process. This project build supports 512, 1024 and 2048.
* \par
* This Function also initializes Twiddle factor table pointer and Bit reversal table pointer.
*/
arm_status arm_rfft_fast_init_f32(
    arm_rfft_fast_instance_f32 * S,
    uint16_t fftLen)
{
    arm_cfft_instance_f32 *Sint;

    if (S == NULL)
        return ARM_MATH_ARGUMENT_ERROR;

    Sint = &(S->Sint);
    Sint->fftLen = fftLen / 2U;
    S->fftLenRFFT = fftLen;

    /*
     * EU1KY-PL 2026 używa RFFT 512 i 1024 w torach pomiarowych oraz 2048
     * w teście częstotliwości. Oryginalna funkcja CMSIS odwoływała się do
     * tablic dla wszystkich długości 32..4096, przez co linker musiał trzymać
     * około 79 KiB tabel. Jawne ograniczenie zachowuje potrzebne funkcje i
     * pozwala usunąć nieużywane tablice przez --gc-sections.
     */
    switch (fftLen)
    {
    case 512U:
        Sint->bitRevLength = ARMBITREVINDEXTABLE_256_TABLE_LENGTH;
        Sint->pBitRevTable = (uint16_t *)armBitRevIndexTable256;
        Sint->pTwiddle = (float32_t *)twiddleCoef_256;
        S->pTwiddleRFFT = (float32_t *)twiddleCoef_rfft_512;
        break;

    case 1024U:
        Sint->bitRevLength = ARMBITREVINDEXTABLE_512_TABLE_LENGTH;
        Sint->pBitRevTable = (uint16_t *)armBitRevIndexTable512;
        Sint->pTwiddle = (float32_t *)twiddleCoef_512;
        S->pTwiddleRFFT = (float32_t *)twiddleCoef_rfft_1024;
        break;

    case 2048U:
        Sint->bitRevLength = ARMBITREVINDEXTABLE1024_TABLE_LENGTH;
        Sint->pBitRevTable = (uint16_t *)armBitRevIndexTable1024;
        Sint->pTwiddle = (float32_t *)twiddleCoef_1024;
        S->pTwiddleRFFT = (float32_t *)twiddleCoef_rfft_2048;
        break;

    default:
        Sint->fftLen = 0U;
        Sint->bitRevLength = 0U;
        Sint->pBitRevTable = NULL;
        Sint->pTwiddle = NULL;
        S->fftLenRFFT = 0U;
        S->pTwiddleRFFT = NULL;
        return ARM_MATH_ARGUMENT_ERROR;
    }

    return ARM_MATH_SUCCESS;
}

/**
 * @} end of RealFFT group
 */
