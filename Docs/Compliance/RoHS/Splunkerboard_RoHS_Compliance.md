# RoHS Compliance – Splunkerboard

| | |
|---|---|
| **Document** | RoHS Declaration of Conformity and Compliance File |
| **Product** | Splunkerboard |
| **Model / BOM reference** | `Splunkerboard 1` sheet, *Muru Product Compliance.xlsx* |
| **Hardware revision** | `V1` |
| **Document revision** | 1 |
| **Date** | 2026-10-03 |

---

## 1. Product identification

| | |
|---|---|
| Product name | Splunkerboard |
| Product description | Board based on a Seeed XIAO ESP32-C5 module (Wi-Fi 6 / BLE) with an MCP23017 GPIO expander, 0.96" OLED display, microSD card slot, tactile and slide switches, and a MAX17048 battery fuel gauge. `` |
| Manufacturer (legal entity) | `Muru Global OU` |
| Address | `Tondi 8a, Tallinn, 11313, Harju county, Estonia` |
| Contact | `Oliver-Gerd-Vald, info@muru.global` |
| Proposed RoHS category (Annex I) | Category 3 – IT and telecommunications equipment, or Category 11 – other EEE  |
| Identification on product | `model name` |

## 2. Declaration of Conformity (RoHS)

> We, `Muru Global OU`, `Tondi 8a, Tallinn, 11313, Harju county, Estonia`, declare under our sole responsibility that the product
>
> **Splunkerboard** – hardware revision `V1`
>
> to which this declaration relates is in conformity with **Directive 2011/65/EU** of the European Parliament and of the Council of 8 June 2011 on the restriction of the use of certain hazardous substances in electrical and electronic equipment (RoHS), as amended by **Commission Delegated Directive (EU) 2015/863** (RoHS 3).
>
> The product does not contain any of the restricted substances listed in Annex II above the maximum concentration values given below, except for applications exempted under Annex III or Annex IV of the Directive as documented in the technical file.
>
> The technical documentation was prepared in accordance with the harmonised standard **EN IEC 63000:2018** – *Technical documentation for the assessment of electrical and electronic products with respect to the restriction of hazardous substances*.
>
> Place and date of issue: `Tallinn`, `2026-10-03`
>
> Signed for and on behalf of `Muru Global OU`:
>
> Name: `[name]` &nbsp;&nbsp; Function: `[function]` &nbsp;&nbsp; Signature: ____________________

### Restricted substances (Annex II, as amended by (EU) 2015/863)

| Substance | Max. concentration (by weight in homogeneous material) |
|---|---|
| Lead (Pb) | 0.1 % (1000 ppm) |
| Mercury (Hg) | 0.1 % (1000 ppm) |
| Cadmium (Cd) | 0.01 % (100 ppm) |
| Hexavalent chromium (Cr VI) | 0.1 % (1000 ppm) |
| Polybrominated biphenyls (PBB) | 0.1 % (1000 ppm) |
| Polybrominated diphenyl ethers (PBDE) | 0.1 % (1000 ppm) |
| Bis(2-ethylhexyl) phthalate (DEHP) | 0.1 % (1000 ppm) |
| Butyl benzyl phthalate (BBP) | 0.1 % (1000 ppm) |
| Dibutyl phthalate (DBP) | 0.1 % (1000 ppm) |
| Diisobutyl phthalate (DIBP) | 0.1 % (1000 ppm) |

### Exemptions (Annex III)

No device-level exemption is claimed. Some supplier declarations may rely on Annex III exemptions. These are typically **7(c)-I**, lead in glass or ceramic of thick-film resistors and ceramic capacitors, and **6(c)**, lead in copper alloys of connectors. Each exemption a supplier uses must be recorded against the component in the technical file and checked for validity on the date the product is placed on the market.

## 3. Basis of the assessment

This assessment follows the EN IEC 63000 supplier-information approach:

1. The bill of materials (BOM) of the device was taken from *Muru Product Compliance.xlsx*, sheet `Splunkerboard 1`.
2. For each component, the RoHS status was taken from the distributor (DigiKey) listing for the part number linked in the BOM.
3. Where available, a manufacturer RoHS certificate of compliance, declaration or material declaration was linked as evidence.
4. No analytical (XRF / laboratory) testing has been performed on the assembled product.
5. Risk assessment: components from large manufacturers with published certificates are low risk. Components with no published document are medium risk: no-name or second-source semiconductors, display modules, and hobby breakout boards or modules. For medium-risk components, a signed supplier declaration or a third-party test report (IEC 62321 series) must be obtained before signing.

## 4. Bill of materials and compliance evidence

BOM lines: **15** · Components with a fixed quantity: **27** · Lines without a published RoHS document: **1**

| # | Ref. | Qty | Description | Manufacturer | Part number (as linked) | RoHS status | Evidence |
|---|---|---|---|---|---|---|---|
| 1 | – | 1 | Seeed XIAO ESP32-C5 | Seeed Technology | 100010048 (XIAO ESP32-C5) | ROHS3 Compliant | [Seeed RoHS certificate of conformity](https://files.seeedstudio.com/Seeed_Certificate/documents_certificate/100010048-ROHS.pdf) |
| 2 | GPIO exp | 1 | MCP23017-EML | Microchip Technology | MCP23017-E/ML | ROHS3 Compliant | [Microchip RoHS CoC](https://ww1.microchip.com/downloads/aemDocuments/documents/corporate-responsibilty/environmental/product-regulatory-information/Semiconductor_EnvCoC.pdf) |
| 3 | – | 1 | OLED-128O064D | DLC Display | DLC0096DNOG-W-5 | RoHS Compliant | **Missing** – request from DLC Display |
| 4 | – | 1 | MicroSD MSD-11-A | Same Sky (CUI Devices) | MSD-11-A | RoHS Compliant | [Same Sky RoHS/REACH declaration](https://www.sameskydevices.com/product/resource/material-compliance/msd-11-a) |
| 5 | – | 1 | XKB TS-1187A tactile switch | C&K (Littelfuse) | PTS526 SK15 SMTR2 LFS | ROHS3 Compliant | [Littelfuse RoHS CoC](https://www.littelfuse.com/assetdocs/coc-rohs10-08-18-2022?assetguid=6d7cc3c6-97e3-4940-98fc-322868282297) |
| 6 | – | 1 | CK OS102011MA1Q SPDT slide | C&K (Littelfuse) | OS102011MS2QN1 | ROHS3 Compliant | [Littelfuse RoHS CoC](https://www.littelfuse.com/assetdocs/coc-rohs10-08-18-2022?assetguid=6d7cc3c6-97e3-4940-98fc-322868282297) |
| 7 | – | 1 | MAX17048G+T10 | Analog Devices (Maxim) | MAX17048G+T10 | ROHS3 Compliant | [Maxim RoHS CoC](https://www.maximintegrated.com/content/dam/files/design/emmi/rohs-certificate-of-compliance.pdf) |
| 8 | R1 | 1 | 10 K | Stackpole Electronics | RMCF0805FT10K0 | ROHS3 Compliant | [Stackpole RoHS status list](https://www.seielect.com/catalog/SEI-RoHS_Compliance_Status.pdf) |
| 9 | R2, R3, R5, R6, R7, R8, R9, R10, R11 | 9 | 4.7 K | YAGEO | RC0805FR-074K7L | ROHS3 Compliant | [YAGEO RoHS compliance statement](https://yageogroup.com/content/Resource%20Library/Product%20Environmental%20Compliance/RoHS_Compliance_YAGEO.pdf) |
| 10 | R4 | 1 | 390 K | YAGEO | RC0805JR-07390KL | ROHS3 Compliant | [YAGEO RoHS compliance statement](https://yageogroup.com/content/Resource%20Library/Product%20Environmental%20Compliance/RoHS_Compliance_YAGEO.pdf) |
| 11 | C1, C8, C9 | 3 | 0.1 µF | KEMET (YAGEO) | C0805C104K5RACTU | ROHS3 Compliant | [KEMET part RoHS certificate](https://yageogroup.com/component-documentation/download/rohs/C0805C104K5RACTU.pdf) |
| 12 | C7 | 1 | 10 µF | KEMET (YAGEO) | C0805C106K8PACTU | ROHS3 Compliant | [KEMET part RoHS certificate](https://yageogroup.com/component-documentation/download/rohs/C0805C106K8PACTU.pdf) |
| 13 | C2, C3, C6 | 3 | 2.2 µF | Taiyo Yuden | UMK212BB7225KG-T | ROHS3 Compliant | [Taiyo Yuden MLCC RoHS declaration](https://www.yuden.co.jp/en/product/certificate/upload/MLCC-RoHS.pdf) |
| 14 | C4 | 1 | 1 µF | Samsung Electro-Mechanics | CL10B103KB8NNNC | ROHS3 Compliant | [Samsung MLCC RoHS compliance](https://www.samsungsem.com/files/fileDown.do?key=ed1dfdfa-645e-4252-b707-17f9b1502dd1) |
| 15 | C5 | 1 | 0.1 µF | KEMET (YAGEO) | C0805C104K5RACTU | ROHS3 Compliant | [KEMET part RoHS certificate](https://yageogroup.com/component-documentation/download/rohs/C0805C104K5RACTU.pdf) |
| 16 | PCB | 1 | PCB | JLCPCB | Bare PCB | [x] | [x] |

*"Part number (as linked)" is the part that the DigiKey link in the BOM points to. The RoHS evidence applies to that part. If you buy a different part number or brand, its evidence must be replaced.*

## 5. Assessment summary

All 15 BOM lines are listed as RoHS compliant by the distributor (ROHS3, or "RoHS Compliant" for the OLED and microSD socket). A manufacturer RoHS document is linked for 14 of the 15 lines. The OLED display (DLC Display) has a distributor status only. **Conclusion:** the components are expected to be compliant. The declaration can be signed once the open items below are closed.

## 6. Open items before the declaration can be signed

1. **OLED display (DLC Display DLC0096DNOG-W-5):** no published RoHS document. Request a RoHS declaration or SGS test report from DLC Display.
2. **C&K switches:** the linked Littelfuse RoHS certificate could not be downloaded for verification. Confirm that it covers the C&K part numbers used, or request a part-specific CoC from Littelfuse (envrequests@littelfuse.com).
3. **PCB, solder and assembly:** the BOM has no PCB line. Add the bare PCB and get the fabricator's and assembler's RoHS declarations (section 8).
4. **Battery and connectors:** the MAX17048 fuel gauge implies a Li-ion/LiPo battery, but no battery, battery connector or pin headers are listed. Add every part that ships with the product.
5. **C4 (1 µF):** the DigiKey link points to CL10B103KB8NNNC (10 nF). Correct the part and confirm its evidence.
6. **Alternate brands** (section 7): if the XKB tactile switch or Walsin capacitor is actually purchased, get their RoHS evidence instead.
7. Fill in the company details, hardware revision and signatory in sections 1 and 2.



## 8. Manufacturing and assembly

| # | Item | Details | Supplier / product | RoHS evidence | Status |
|---|---|---|---|---|---|
| 1 | Bare PCB | Part no. / rev: `[ ]` · Surface finish: `[ENIG / lead-free HASL]` | `[PCB fab]` | See BOM table, PCB line | `[Open / Closed]` |
| 2 | Assembly | `[In-house / EMS name]` · Lead-free process only, no leaded solder used | `[ ]` | Assembly statement (this table, signed in section 2) | `[Open / Closed]` |
| 3 | Solder paste (reflow) | Lead-free alloy: `[e.g. SAC305]` | `[Manufacturer, product name]` | `[link / file to RoHS CoC]` | `[Open / Closed]` |
| 4 | Solder wire (hand soldering / rework) | Lead-free alloy: `[e.g. SAC305 / Sn99.3Cu0.7]` | `[Manufacturer, product name]` | `[link / file to RoHS CoC]` | `[Open / Closed]` |
| 5 | Flux | `[No-clean / rosin / water-soluble]` | `[Manufacturer, product name]` | `[link / file to RoHS CoC or SDS]` | `[Open / Closed]` |
| 6 | Cleaning agent | `[IPA / none]` | `[ ]` | `[N/A – evaporates, not in product]` | `[Open / Closed / N/A]` |
| 7 | Casing | 3D-printed, food-grade PETG · Colour: `[ ]` | Elegoo PETG `[product name]` | `[link / file to Elegoo RoHS declaration]` | `[Open / Closed]` |
| 8 | Fasteners | None – device is shipped without screws | – | N/A | N/A |
| 9 | Coating / adhesive / thermal pads | `[None / product name]` | `[ ]` | `[link / file or N/A]` | `[Open / Closed / N/A]` |
| 10 | Labels / marking | `[None / label material, printing method]` | `[ ]` | `[link / file or N/A]` | `[Open / Closed / N/A]` |
| 11 | Battery (if supplied) | `[Chemistry, capacity, part no. / N/A]` | `[ ]` | Battery Regulation (EU) 2023/1542 – outside RoHS scope | `[Open / Closed / N/A]` |
| 12 | Cables / accessories shipped | `[None / USB cable etc.]` | `[ ]` | `[link / file or N/A]` | `[Open / Closed / N/A]` |

**Assembly statement:** all assembly and rework of this product is done with lead-free solder and lead-free process materials only. No leaded solder is used, and no part is substituted without updating the BOM table and this document.

## 9. Related regulatory obligations (outside RoHS)

RoHS is one part of CE marking. The following also apply, or should be checked, for this product:

| Requirement | Applicability |
|---|---|
| Radio Equipment Directive 2014/53/EU | Applies, because the device contains a radio module (Seeed XIAO ESP32 with Wi-Fi/BLE). RoHS conformity is declared in the single EU DoC under RED. The module's own certification does not replace the product's RED assessment (article 3.1a safety, 3.1b EMC, 3.2 radio). |
| EMC Directive 2014/30/EU | Applies. EMC testing is needed (the "EMC Test link" column in the BOM is currently empty). |
| Low Voltage Directive 2014/35/EU | Not applicable if the device is supplied only by USB (5 V) or a battery below 50 V AC / 75 V DC. Safety is covered via the General Product Safety Regulation (EU) 2023/988, or via RED art. 3.1a for radio devices. |
| Battery Regulation (EU) 2023/1542 | Applies if a battery is supplied with the product. Batteries are outside the scope of RoHS, but the regulation restricts Hg, Cd and Pb in batteries and sets labelling, removability and take-back requirements. |
| REACH (EC) 1907/2006, Art. 33 | Confirm that no SVHC above 0.1 % w/w is present in the article. Get the REACH statements from suppliers (several are available alongside the RoHS documents above). |
| WEEE 2012/19/EU | Crossed-out wheelie-bin symbol on the product or packaging, producer registration in each EU country of sale, and take-back information. |

## 10. Technical documentation and record keeping

Detailed per object documentation can be found at LINK

## 11. Revision history

| Rev. | Date | Description | Author |
|---|---|---|---|
| 0.1 | 2026-10-03 | Initial draft generated from the BOM in *Muru Product Compliance.xlsx* | `[name]` |
