#!/usr/bin/env python3
"""Sprawdza, czy gotowy BIN mieści się w 1 MiB pamięci Flash STM32F746NG."""
from pathlib import Path
import sys

LIMIT_B = 1024 * 1024

if len(sys.argv) != 2:
    raise SystemExit('Użycie: sprawdz_budzet_flash.py <plik.bin>')

plik = Path(sys.argv[1])
if not plik.is_file():
    raise SystemExit(f'Błąd: nie znaleziono pliku {plik}.')

rozmiar = plik.stat().st_size
zapas = LIMIT_B - rozmiar
procent = 100.0 * rozmiar / LIMIT_B
print(f'Flash: {rozmiar} B / {LIMIT_B} B ({procent:.2f}%).')
if zapas < 0:
    raise SystemExit(f'Błąd: przekroczono Flash o {-zapas} B.')
print(f'Zapas: {zapas} B ({zapas / 1024:.2f} KiB).')
