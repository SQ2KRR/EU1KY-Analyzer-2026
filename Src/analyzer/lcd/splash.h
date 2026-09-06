#ifndef SPLASH_H_
#define SPLASH_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Rysuje napisy na ekranie startowym na już narysowanym tle.
 * Tło pozostaje jedną neutralną językowo bitmapą, a podtytuł korzysta
 * z bieżącego języka interfejsu.
 */
void SPLASH_RysujNapisy(void);


#ifdef __cplusplus
}
#endif

#endif
