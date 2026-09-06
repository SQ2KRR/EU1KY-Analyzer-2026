# EU1KY Analyzer 2026

<p align="center">
  <img src="images/eu1ky_analyzer_2026_v2_1_banner.png"
       alt="EU1KY Analyzer 2026 v2.1"
       width="900">
</p>

<p align="center">
  <b>Multilingual firmware for the EU1KY Antenna Analyzer</b><br>
  STM32F746G-DISCO • PL / EN / DE / RU
</p>

EU1KY Analyzer 2026 is a continued development of the original EU1KY antenna analyzer firmware.

The project introduces a redesigned graphical interface, expanded measurement and calibration functions, improved usability and multilingual support while remaining compatible with the established EU1KY hardware platform.

---

## Current stable release

### EU1KY Analyzer 2026 v2.1

**Supported interface languages:**

- 🇵🇱 Polish
- 🇬🇧 English
- 🇩🇪 German
- 🇷🇺 Russian

### ➡️ [Download EU1KY Analyzer 2026 v2.1](https://github.com/SQ2KRR/EU1KY-Analyzer-2026/releases/latest)

The current release provides:

- ready-to-flash `.bin` firmware
- Intel HEX `.hex` firmware
- complete source code
- minimal source archive for offline use

---

## Main features

EU1KY Analyzer 2026 includes:

- single-frequency impedance measurement
- SWR and impedance graphs
- multi-band measurements
- antenna tuning tools
- Smith chart
- S21 measurements
- TDR cable measurements
- LC measurements
- frequency search
- signal generator
- OSL calibration
- hardware calibration
- S21 calibration
- calibration status and verification
- measurement data storage
- USB functions
- expanded diagnostics
- multilingual graphical interface

Version 2.1 also introduces a redesigned and unified visual interface together with numerous usability and stability improvements.

---

## Screenshots

<table>
<tr>
<td align="center">
<b>Main menu</b><br>
<img src="images/screenshots/01-main-menu-en.png" width="480">
</td>
<td align="center">
<b>Single measurement</b><br>
<img src="images/screenshots/02-single-measurement-en.png" width="480">
</td>
</tr>

<tr>
<td align="center">
<b>Smith chart</b><br>
<img src="images/screenshots/03-smith-chart-en.png" width="480">
</td>
<td align="center">
<b>Metrology 2026 A/B comparison</b><br>
<img src="images/screenshots/04-metrology-2026-ab-en.png" width="480">
</td>
</tr>
</table>

---

## Firmware download

The recommended way to obtain the firmware is through the GitHub Releases page:

### ➡️ [Latest Release](https://github.com/SQ2KRR/EU1KY-Analyzer-2026/releases/latest)

The current release contains:

- `EU1KY_Analyzer_2026_v2.1.bin`
- `EU1KY_Analyzer_2026_v2.1.hex`
- `EU1KY_Analyzer_2026_v2.1_sources.zip`

For users who only want to install the firmware, the `.bin` file is normally the simplest choice.

---

## Source code

The complete source code required to build the firmware is available directly in this repository.

Main directories:

- `Src/` – firmware source code
- `tools/` – build and verification tools
- `firmware/` – compiled firmware files
- `images/` – project graphics and screenshots

Build configuration files are located in the repository root.

---

## Documentation

A new detailed user manual for **EU1KY Analyzer 2026 v2.1** is currently being prepared.

The new documentation will describe the individual measurement, calibration and diagnostic functions step by step using actual screenshots captured directly from the analyzer.

Documentation for older releases remains useful as reference material, but some menus, functions and screen layouts may differ from version 2.1.

---

## Hardware

EU1KY Analyzer 2026 is intended primarily for analyzers based on:

- STM32F746G-DISCO
- EU1KY RF front end
- Si5351 frequency synthesizer

Support for additional controls and diagnostic functions is also included.

---

## Calibration notice

Calibration data is specific to the individual analyzer hardware.

After installing new firmware, always verify the calibration status before performing precision measurements.

A low SWR value does not by itself indicate antenna efficiency.

Always observe RF safety precautions when working with antennas and connected transmitters.

---

## Project origin

This project is a continuation and modification of work created by the EU1KY amateur radio community.

Major contributors to earlier EU1KY development include:

- **Yury Kuchura, EU1KY**
- **Wolfgang Kiefer, DH1AKF**
- **Ian Lee, KD8CEC**
- other contributors to the EU1KY project

EU1KY Analyzer 2026 does not claim authorship of the original analyzer design.

---

## This development

Additional development, interface redesign, testing, translations and documentation:

**Marek, SQ2KRR**

Feedback from EU1KY users is very welcome, especially reports from different hardware versions.

---

## License

This project is distributed under the **GNU General Public License v3.0**.

See the [`LICENSE`](LICENSE) file for details.
