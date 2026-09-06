#ifndef MENEDZER_PLIKOW_H_
#define MENEDZER_PLIKOW_H_

/* Otwiera przeglądarkę od katalogu głównego karty. */
void PLIKI_Otworz(void);

/* Otwiera przeglądarkę od wskazanego katalogu, np. /aa z diagnostyki. */
void PLIKI_OtworzKatalog(const char *sciezka_poczatkowa);

#endif /* MENEDZER_PLIKOW_H_ */
