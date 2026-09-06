/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#ifndef _MAINWND_H_
#define _MAINWND_H_

#include <ctype.h>
#include "main.h"

void MainWnd(void);
void MAINWND_USBKartaSDProc(void);
void MAINWND_OtworzKalibracjeBaterii(void);
void MAINWND_RysujMenuDemo(void);
uint32_t MAINWND_DokumentacjaLiczbaStron(void);
const char *MAINWND_DokumentacjaNazwaStrony(uint32_t strona);
void MAINWND_DokumentacjaRysujStrone(uint32_t strona);
extern uint16_t FileNo;
extern volatile int Page;
extern uint32_t date, time;
extern uint32_t RTCpresent;
extern volatile int NoDate;
extern ADC_HandleTypeDef Adc3Handle;
#endif
