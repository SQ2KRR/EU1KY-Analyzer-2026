# EU1KY Analyzer 2026

<p align="center">
  <img src="images/eu1ky-analyzer-2026-banner.png" alt="EU1KY Analyzer 2026" width="900">
</p>

Multilingual firmware for the **EU1KY Antenna Analyzer**, based on STM32F746G-DISCO.

This project continues the development of the original EU1KY antenna analyzer firmware and introduces a redesigned user interface, expanded measurement functions, calibration improvements and multilingual support.

---

## Current stable release

### EU1KY Analyzer 2026 v2.1

**Supported interface languages:**

- 🇵🇱 Polish
- 🇬🇧 English
- 🇩🇪 German
- 🇷🇺 Russian

➡️ **[Download the latest firmware and source package](https://github.com/SQ2KRR/EU1KY-Analyzer-2026/releases/latest)**

The current release contains:

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

Version 2.1 also introduces a redesigned and unified visual interface and numerous usability and stability improvements.

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

For normal use, download the current release from:

### ➡️ [Latest Release](https://github.com/SQ2KRR/EU1KY-Analyzer-2026/releases/latest)

The release provides:

- `EU1KY_Analyzer_2026_v2.1.bin`
- `EU1KY_Analyzer_2026_v2.1.hex`
- `EU1KY_Analyzer_2026_v2.1_sources.zip`

The `.bin` file is the simplest choice for users who only want to install the firmware.

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

The documentation will describe the individual measurement modes step by step using actual screenshots from the analyzer.

Older manuals may describe previous firmware revisions and should therefore be treated as reference material only.

---

## Hardware

EU1KY Analyzer 2026 is intended primarily for analyzers based on:

- STM32F746G-DISCO
- EU1KY RF front end
- Si5351 frequency synthesizer

Support for additional controls and diagnostic functions is also included.

---

## Calibration notice

Calibration data belongs to the individual analyzer hardware.

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
