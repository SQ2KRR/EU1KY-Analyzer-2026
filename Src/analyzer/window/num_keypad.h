/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#ifndef NUMKEYPAD_H_
#define NUMKEYPAD_H_

#include <stdint.h>

uint32_t NumKeypad(uint32_t initial, uint32_t min_value, uint32_t max_value, const char *header_text);
/* Edycja rezystancji z rozdzielczoscia 1 mOhm. Wewnetrznie 49873 oznacza
   49,873 Ohm, natomiast na ekranie separator jest lokalizowany. */
uint32_t NumKeypadMiliohm(uint32_t initial_mohm, uint32_t min_mohm,
                          uint32_t max_mohm, const char *header_text);
#endif
