#ifndef AUDIODSP_H_
#define AUDIODSP_H_

#include <stdint.h>

/*
 * Narzędzie DSP audio. Funkcja korzysta z toru mikrofonowego i słuchawkowego,
 * ale nie zmienia kalibracji RF ani konfiguracji OSL.
 */
void AudioDSP_Proc(void);

/* Nie uruchamia audio; sprawdza tylko spójność aktualnych nastaw DSP. */

#endif /* AUDIODSP_H_ */
