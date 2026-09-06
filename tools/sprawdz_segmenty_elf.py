#!/usr/bin/env python3
"""Kontroluje, czy segmenty ładowalne ELF nie mają praw zapisu i wykonania jednocześnie."""
from pathlib import Path
import shutil
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit('Użycie: sprawdz_segmenty_elf.py <plik.elf>')

plik = Path(sys.argv[1])
if not plik.is_file():
    raise SystemExit(f'Błąd: nie znaleziono pliku {plik}.')

readelf = shutil.which('arm-none-eabi-readelf')
if readelf is None:
    raise SystemExit('Błąd: nie znaleziono arm-none-eabi-readelf w PATH.')

wynik = subprocess.run([readelf, '-lW', str(plik)], check=True, text=True, capture_output=True).stdout
naruszenia = []
for wiersz in wynik.splitlines():
    tekst = wiersz.strip()
    if not tekst.startswith('LOAD'):
        continue
    pola = tekst.split()
    # Wariant GNU readelf może zapisywać flagi jako osobne R/W/E albo jako RWE.
    flaga = ''.join(p for p in pola if set(p) <= set('RWE') and p)
    if 'W' in flaga and 'E' in flaga:
        naruszenia.append(tekst)

if naruszenia:
    print('Błąd: wykryto segment LOAD z jednoczesnymi prawami W i E:')
    for wiersz in naruszenia:
        print(wiersz)
    raise SystemExit(1)

print('Segmenty ELF: OK — brak ładowalnego segmentu z jednoczesnymi prawami W+E.')
