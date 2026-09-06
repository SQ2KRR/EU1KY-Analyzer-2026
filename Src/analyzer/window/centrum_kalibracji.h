#ifndef _CENTRUM_KALIBRACJI_H_
#define _CENTRUM_KALIBRACJI_H_

/*
 * Sekcja Ustawienia → Kalibracja EU1KY-PL 2026.
 * Ekran zawiera wyłącznie czynności kalibracyjne: HW, OSL, S21, TDR Vf
 * oraz stan generatora/IF potrzebny do ich wykonania. Diagnostyka sprzętu
 * i weryfikacja wzorcami mają własne miejsce w Ustawienia > Diagnostyka.
 */
void CENTRUM_KALIBRACJI_Otworz(void);

/*
 * Bezpośrednie wejścia używane przez hierarchiczne menu 2026. Każde z nich
 * przechodzi przez te same kontrole wstępne co Centrum Kalibracji; menu nie
 * omija więc sprawdzenia SD, Si5351 ani zależności HW -> OSL.
 */
void CENTRUM_KALIBRACJI_OtworzHW(void);
void CENTRUM_KALIBRACJI_OtworzOSL(void);
void CENTRUM_KALIBRACJI_OtworzS21(void);
void CENTRUM_KALIBRACJI_OtworzLC(void);
void CENTRUM_KALIBRACJI_OtworzTDRVf(void);
void CENTRUM_KALIBRACJI_OtworzStan(void);
void CENTRUM_KALIBRACJI_OtworzWeryfikacje(void);
void CENTRUM_KALIBRACJI_UstawDokladneWzorceDC(void);

#endif
