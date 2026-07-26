# OpenCCVoice Project

[日本語 (Japanese)](#openccvoice-について) | [English](#about-openccvoice)

---

## OpenCCVoice について
OpenCCVoice は、**JA2CCV** 局の設計考案を基礎に、複数局の協力により発展してきたプロジェクトです。
このプロジェクトは、技術を共有し、改善し合い、より良いものへ発展させていくことを目的としています。

### 理念：ブラックボックスにしない、共有の精神
OpenCCVoice はアマチュア無線の精神に基づき、以下の理念を掲げています。

> **「誰でも自由に使え、誰でも改良でき、そしてその成果をオープンに共有できる」**

特定の人だけが技術を抱え込む「ブラックボックス化」を避け、多くの局が改善に参加することで、技術の相互発展を促します。
この理念を象徴するものとして**「OpenCCVoice」**という名称が付けられています。

---

## 基板バージョンとファームウェアの対応

> ⚠️ **基板 Ver.4 と Ver.5 ではピンアサインが異なります。**
> **必ず使用する基板バージョンに対応したファームウェア（スケッチ）を使用してください。**

| 基板バージョン | 対応ファームウェア | 状態 |
|---|---|---|
| Ver.4 | v1.73e 以前（**1系**） | 旧バージョン |
| **Ver.5** | **v2.01 以降（2系）** | **現行・推奨** |

### Ver.4 と Ver.5 のピンアサイン比較

| 信号 | Ver.4（1系） | **Ver.5（2系）** |
|------|-------------|-----------------|
| テストSW | D3 | **D2** |
| DFPlayer BUSYミラー出力 | D2 | **D3** |
| ModBusy LED | D4 | D4（変更なし） |
| PTT出力 | D5 | D5（変更なし） |
| 抑止 LED | D13 | **D6** |
| A0検知 LED | D12 | **D7** |
| DFPlayer BUSY入力 | D7 | **D10** |
| TM BUSY入力（Digital入力） | D6 | **D11** |
| Arduino TX → DFPlayer RX | D11 | **D13** |
| Arduino RX ← DFPlayer TX | D10 | **D12** |
| アナログBUSY | A0 | A0（変更なし） |
| I²C SDA（DS3231/AT24C32） | 未使用 | **A4**（v2.10追加） |
| I²C SCL（DS3231/AT24C32） | 未使用 | **A5**（v2.10追加） |

> **注意:** Ver.5 基板に 1系ファームウェアを書き込むと、ピンアサインの不一致により正常動作しません。逆も同様です。

### ライセンスと権利について
公開予定の回路図、Arduino スケッチ、基板データ、関連ドキュメントの著作権は **JA2CCV 局および OpenCCVoice プロジェクト** に帰属します。

OpenCCVoice は、技術共有と発展を目的としたオープンプロジェクトであり、公開物はファイルの種類に応じて次のライセンスのもとで取り扱われます。**ハードウェア（回路図・基板データ・BOM・ケース）は CERN-OHL-S v2**、**ソフトウェア（Arduino スケッチ）は GPL v3** です。

#### 1. 利用と改変について
* どなたでも **自由に利用** できます。
* 必要に応じて **改変・機能追加・組み込み** が可能です。

#### 2. 再配布・派生物公開の条件（コピーレフト）
改変版や派生物を公開・配布する際は、以下の条件を遵守してください。

1.  **原著作者（JA2CCV／OpenCCVoice）を明示すること**
2.  **改変した点を明記すること**
3.  **派生物も同じくオープンな形で公開すること**
    * ブラックボックス化せず、ソースコード・回路図・基板データ等を公開してください。

#### 3. ライセンス文書について
正式なライセンス文書はリポジトリに同梱されています。ハードウェアは `LICENSE-HARDWARE.txt`（CERN-OHL-S v2）、ソフトウェアは `LICENSE`（GPL v3）を参照してください。

OpenCCVoice が多くの局とともに発展していくことを願っています。

### 免責事項 (Disclaimer)
本プロジェクトで公開されているハードウェア設計（回路図・基板データ）、ソフトウェア（ソースコード）、および関連ドキュメントの利用にあたっては、以下の事項をあらかじめご了承ください。

1.  **無保証（No Warranty）**
    本成果物は「現状のまま（As Is）」で提供されます。製作者およびOpenCCVoiceプロジェクトは、特定の目的への適合性、動作の確実性、およびバグや不具合がないことを保証しません。
2.  **損害に対する非責任**
    本成果物の使用、または使用不能によって生じた、いかなる損害（無線機・PC等のハードウェア故障、データの消失、火災、怪我など）についても、製作者およびプロジェクトメンバーは一切の責任を負いません。自作機器の接続は、ご自身の無線機を破損させるリスクがあることを理解した上で行ってください。
3.  **法令の遵守（Legal Compliance）**
    本装置を使用して無線局を運用する場合、利用者は各国の電波法および関連法規（日本国内においては電波法、無線局運用規則など）を遵守する責任を負います。
    * 送信間隔、送出内容、変調レベルなどが法的基準を満たしているか、利用者の責任において確認・調整してください。
    * 本装置の使用による法令違反について、製作者は一切関知しません。
4.  **ライセンス**
    本プロジェクトのハードウェアは **CERN-OHL-S v2 (CERN Open Hardware Licence Version 2 - Strongly Reciprocal)**、ソフトウェアは **GPL v3 (GNU General Public License v3)** の下で公開されています。再配布や改変に関する条件は、それぞれのライセンス条項に従ってください。

---

## About OpenCCVoice
The OpenCCVoice project is based on the design and concept by **JA2CCV**, and has been developed through the cooperation of multiple stations.
The goal of this project is to share technology, improve upon it together, and foster further development.

### Philosophy: No Black Box, Spirit of Sharing
Based on the spirit of Amateur Radio, OpenCCVoice upholds the following philosophy:

> **"Free to use, free to improve, and results are shared openly."**

We aim to avoid "black boxing"—where technology is hoarded by a few—and instead encourage mutual technological advancement by allowing many stations to participate in improvements.
The name **"OpenCCVoice"** was chosen to symbolize this philosophy.

---

## Board Version and Firmware Compatibility

> ⚠️ **Board Ver.4 and Ver.5 have different pin assignments.**
> **Always use the firmware that matches your board version.**

| Board Version | Compatible Firmware | Status |
|---|---|---|
| Ver.4 | v1.73e or earlier (**1.x series**) | Legacy |
| **Ver.5** | **v2.01 or later (2.x series)** | **Current / Recommended** |

### Pin Assignment Comparison: Ver.4 vs Ver.5

| Signal | Ver.4 (1.x series) | **Ver.5 (2.x series)** |
|--------|--------------------|------------------------|
| Test SW | D3 | **D2** |
| DFPlayer BUSY mirror output | D2 | **D3** |
| ModBusy LED | D4 | D4 (unchanged) |
| PTT output | D5 | D5 (unchanged) |
| Suppression LED | D13 | **D6** |
| A0 detect LED | D12 | **D7** |
| DFPlayer BUSY input | D7 | **D10** |
| TM BUSY input (Digital) | D6 | **D11** |
| Arduino TX → DFPlayer RX | D11 | **D13** |
| Arduino RX ← DFPlayer TX | D10 | **D12** |
| Analog BUSY | A0 | A0 (unchanged) |
| I²C SDA (DS3231/AT24C32) | N/A | **A4** (added in v2.10) |
| I²C SCL (DS3231/AT24C32) | N/A | **A5** (added in v2.10) |

> **Warning:** Writing 1.x series firmware to a Ver.5 board will cause malfunction due to pin assignment mismatch. The reverse is also true.

### License and Rights
The copyright of the schematics, Arduino sketches, PCB data, and related documentation belongs to **JA2CCV and the OpenCCVoice Project**.

OpenCCVoice is an open project aimed at technology sharing and development. Published materials are handled under different licenses depending on the file type: **hardware (schematics, PCB data, BOM, case) is licensed under CERN-OHL-S v2**, and **software (Arduino sketches) is licensed under GPL v3**.

#### 1. Usage and Modification
* Anyone is **free to use** this project.
* You may **modify, add features, or integrate** it as needed.

#### 2. Conditions for Redistribution (Copyleft)
When publishing or distributing modified versions or derivatives, you must observe the following conditions:

1.  **Credit the original authors (JA2CCV / OpenCCVoice).**
2.  **Clearly state the modifications made.**
3.  **Release the derivative works under the same open terms.**
    * Do not "black box" the project; you must publish the source code, schematics, and PCB data.

#### 3. About License Documents
The official license documents are included in the repository: see `LICENSE-HARDWARE.txt` (CERN-OHL-S v2) for hardware, and `LICENSE` (GPL v3) for software.

We hope that OpenCCVoice will continue to grow together with many stations.

### Disclaimer
Please read the following terms carefully before using the hardware designs (schematics, PCB data), software (source code), and documentation provided by the OpenCCVoice project.

1.  **No Warranty**
    This project is provided "AS IS", without warranty of any kind, express or implied, including but not limited to the warranties of merchantability or fitness for a particular purpose. The entire risk as to the quality and performance of the project is with you.
2.  **Limitation of Liability**
    In no event shall the authors or copyright holders be liable for any claim, damages, or other liability, whether in an action of contract, tort, or otherwise, arising from, out of, or in connection with the software/hardware or the use or other dealings in the project.
    * **Hardware Risk:** Connecting custom circuits to your radio equipment carries inherent risks. You assume full responsibility for any damage to your transceiver, computer, or other devices.
3.  **Regulatory Compliance**
    Users are solely responsible for complying with the radio laws and regulations of their respective countries or regions.
    * It is the user's responsibility to ensure that transmission intervals, audio levels, and operation methods comply with local legal requirements.
    * The authors assume no responsibility for any legal violations committed by the user.
4.  **License**
    The hardware of this project is released under the **CERN-OHL-S v2 (CERN Open Hardware Licence Version 2 - Strongly Reciprocal)**, and the software under the **GNU General Public License v3 (GPL v3)**. Please refer to each license for terms regarding redistribution and modification.

---
**Project Contributors:** JA2CCV, JA9HYM, JA2DML, JG1XWV, JI2TAB


---

## ライセンス構成 / License Structure

本プロジェクトは、ファイルの種類によって適用されるライセンスが異なります。
This project applies different licenses depending on the type of file.

### ハードウェア / Hardware
回路図、PCB基板データ、BOM（部品表）、ケースデータ
（対象ディレクトリ: `circuit/`, `pcb/`, `bom/`, `case/`）

**CERN Open Hardware Licence Version 2 - Strongly Reciprocal (CERN-OHL-S v2)**
→ `LICENSE-HARDWARE.txt` を参照 / See `LICENSE-HARDWARE.txt`

### ソフトウェア / Software
Arduino スケッチ等のソースコード
（例: `CCVoice_*/`, `old_firmware/`, `DMR Arduino ID V4-2/`）

**GNU General Public License v3.0 (GPL-3.0)**
→ `LICENSE` を参照 / See `LICENSE`

> 上記の記述と本 README 中の他のライセンス記述が矛盾する場合は、本「ライセンス構成」セクションが優先されます。
> > In case of conflict between this section and other license descriptions in this README, this "License Structure" section takes precedence.
> > 
