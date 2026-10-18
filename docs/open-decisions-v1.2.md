# WF01 Açık Kararlar ve Kapanış Kaydı — v1.2

**Tarih:** 2026-10-18  
**Önceki sürüm:** `docs/open-decisions-v1.1.md` (değiştirilmedi)  
**Kapı sonucu:** `G1-JOB01/REP01 BLOCKED`; `G1-PORTFOLIO BLOCKED`  
**Durum sözlüğü:** `PASS`, `BLOCKED`, `UNKNOWN`, `NOT_RUN`. `N/A` yalnız kapsam, `TBD` yalnız rol/kimlik alanıdır.
**CF-09 araştırma dayanağı:** `evidence/WF01-CF09-Mimari-Karar-Matrisi-v1.1.xlsx`; SHA-256 `2C29D3DF41C9681B7E848469C6A7F2959BFACE1615343142E9010D25648C159C`. Matris dal/topoloji/parça/pin seçmez.

## 1. Onay kaydı

Kullanıcı 2026-10-18’de `WF01-G1-Karar-Paketi-v1.3.docx` içindeki sınır önerilerinin insan inceleyenlerce onaylandığını bildirdi. Bu kayıt karar sınırını yürürlüğe alır; eksik kanıta PASS vermez. İmzalayan kişilerin adları sağlanmadı ve `TBD` bırakıldı. Codex onaylayan değildir.

| ID | Onaylanan sınır | Durum | Kalan etki |
|---|---|---|---|
| D-G1-SPLIT | `G1-JOB01/REP01` ile `G1-PORTFOLIO` ayrı kapılardır. | `PASS` | Her iki kapının sonucu `BLOCKED`. |
| D-PKGC-JOB01 | PKG-C, yalnız JOB-01 kapısında kapsam dışıdır. | `PASS` | PKG-C portföy kapısında kapanmalıdır. |
| D-TFT-SCOPE | İlk revizyonda gelecek TFT konnektörü DNP’dir. | `PASS` | Teknik TFT alanları G2’de `UNKNOWN`; GPIO ayrılmaz. |
| D-G1-HANDOFF | Exact-part/rail/timing/pin freeze G2’ye; şematik/ERC/model/pre-layout G3’e; bench kapsamı G5’e devredilir. | `PASS` | Devir, downstream teknik PASS değildir. |

## 2. Müşteri soruları

| ID | Soru | Sahip | Kapı etkisi | Durum |
|---|---|---|---|---|
| Q-C01 | 24 V sürekli min/maks çalışma aralığı? | CUSTOMER — kişi `TBD` | G2 exact-part/PASS | `UNKNOWN` |
| Q-C02 | Load-dump ve OEM darbe profili? | CUSTOMER — kişi `TBD` | G2 koruma mimarisi | `UNKNOWN` |
| Q-C03 | Ters polarite ve jump-start gerilimi/süresi? | CUSTOMER — kişi `TBD` | G2 exact-part/PASS | `UNKNOWN` |
| Q-C04 | USB-A cihazının en yüksek akımı? | CUSTOMER — kişi `TBD` | G2 VBUS ve 5 V bütçe | `UNKNOWN` |
| Q-C05 | LCD mantık gerilimi ve backlight akımı? | CUSTOMER — kişi `TBD` | G2 güç/seviye/GPIO | `UNKNOWN` |
| Q-C06 | Ortam sıcaklığı ve kutu içi soğutma? | CUSTOMER — kişi `TBD` | G2 derating/termal | `UNKNOWN` |
| Q-C07 | Beş TTL ve beş RS-232 TX, beş aynalı mı on bağımsız akış mı; frozen GPIO var mı? | CUSTOMER — kişi `TBD` | Mantıksal cevap G1; teknik seri ayrıntılar G2 | `BLOCKED` |
| Q-C08 | RS-232 baud/kablo ve TTL yük tipi? Eski üst soru; Q-C12 zamanlama ve Q-C14 elektrik/saha alt sorularına ayrıştırıldı. | CUSTOMER — kişi `TBD` | G2 üst soru; kapanış Q-C12/Q-C14 ile izlenir | `UNKNOWN` |
| Q-C09 | ESD/EFT hedef seviyesi? Eski üst soru; Q-C15 güvenli durum/test alt sorusuna ayrıştırıldı. | CUSTOMER — kişi `TBD` | G2/G3/G5 üst soru; kapanış Q-C15 ile izlenir | `UNKNOWN` |
| Q-C10 | Üretici ve montaj sınıfı? | CUSTOMER / PROJECT — kişi `TBD` | G2/G4 stackup/DFM/maliyet | `UNKNOWN` |
| Q-C11 | TFT panel/arayüz/sinyal/pin/touch/PSRAM/backlight gereksinimi? | CUSTOMER — kişi `TBD` | G1 kapsamı kapanmış; G2 teknik sözleşme | `UNKNOWN` |
| Q-C07-MAP | MIRRORED_5 seçilirse çift bazlı `TTL_i ↔ RS232_i` mi, global `1→10` fan-out mu? | CUSTOMER — kişi `TBD` | G1/G2 eşleme sözleşmesi | `UNKNOWN` |
| Q-C12 | Azami eşzamanlı TX, kanal bazlı baud/framing, burst/duty ve izinli jitter/gecikme? | CUSTOMER — kişi `TBD` | G2 kaynak ve veri yolu bütçesi | `UNKNOWN` |
| Q-C13 | Tek RX standardı/kanalı, flow-control, DTE/DCE, konnektör ve pinout? | CUSTOMER — kişi `TBD` | G2 fiziksel bağlantı sözleşmesi | `UNKNOWN` |
| Q-C14 | TTL elektrik tanımı; kablo, yük, toprak/shield ve izolasyon gereksinimi? | CUSTOMER — kişi `TBD` | G2 elektriksel mimari | `UNKNOWN` |
| Q-C15 | Boot/idle/disable, fault recovery ve ESD/EFT/surge hedefleri? | CUSTOMER — kişi `TBD` | G2/G3/G5 güvenli durum ve test | `UNKNOWN` |

Q-C01–03, WF01 REQ-001 taslağı mevcut olduğu için araştırma ve karşılaştırmayı durdurmaz. Yalnız exact-part ve teknik PASS’i bloke eder.

## 3. Mimari kararlar ve çatışmalar

| ID | Karar / kanıt | Onaylı taban | Nihai durum |
|---|---|---|---|
| CF-09 | `CLAUDE-03 C03-020`; `CLAUDE-06 REQ-004-C`; `CONFLICT` | Topoloji ve pin seçilmez. | G1 `BLOCKED`: akış anlamı eksik. Baud/framing/concurrency/GPIO/serializer G2 `BLOCKED`. |
| D-CF09-ARCH-REF | `WF01-CF09-Mimari-Karar-Matrisi-v1.1.xlsx`; 7 senaryo, 17 aday aile, 11 doğrulama satırı, 25 kaynak kaydı | Matris araştırma ve devir kanıtıdır. | `PASS` kayıt bağı; dal `UNDECIDED`, gereksinim ve mimari `BLOCKED`, bench `NOT_RUN`. |
| D-CF09-RMT | `C-RMT-01`, `MIR-HYB-1`, `V-RMT-01` | RMT-as-UART resmî desteklenmiş özellik sayılmaz. | Prototip `NOT_RUN`; kapasite kredisi `BLOCKED`. |
| D-CF09-BUS | `MIR-EXT-1`, `IND-EXT-1`, `V-BUS-01` | SPI/I²C/paralel/helper-MCU yalnız aday sınıflardır. | Host bus seçimi `BLOCKED`; CS/adres/IRQ/saat/GPIO/güç girdileri `UNKNOWN`. |
| D-CF09-RS232 | `C-RS-01`, `C-RS-02`, `C-RS-03`, `C-RS-04`, `C-RS-05`, `C-RS-06`, `V-RS232-01` | 5T/3R beş fiziksel TX + bir RX sayımını karşılayabilir; mantıksal eşlemeyi çözmez. | Exact-part `UNKNOWN`; fiziksel doğrulama `NOT_RUN`. |
| D-CF09-SRC-01 | `SRC-ESP-04` resmî FAQ “48 bytes”; veri sayfası/diğer resmî kayıt “48 word” | Matris 48 word kaydını temel alır; çelişki görünür tutulur. | Kaynak uzlaştırma `BLOCKED`; bu fark teknik PASS üretmez. |
| D-JTAG | `CLAUDE-11 C11-014`; GPIO39–42 | Kalıcı atama yok. | G2 `UNKNOWN`; CF-09 nedeniyle pin freeze `BLOCKED`. |
| D-USB-EFUSE-BASE | `C11-012 SOURCE_EXPLICIT`; `C11-015 DERIVED` | Geçici G2 tabanı: USB_PHY_SEL yakılmaz. | Taban `PASS`; nihai teknik G2 dondurması `UNKNOWN`; G3 işlev testi `NOT_RUN`. |
| D-EFUSE-IO35-37 | `C11-003/C11-004`; `DERIVED` | Resmî varsayılan doğrulanmadan atama yok. | `UNKNOWN` |
| D-ANTENNA | `C11-011`; keepout ölçüsü kaynak setinde yok | HDG kanıtı alınır. | `UNKNOWN` |
| D-PROP-IDS | `CLAUDE-06 PROP-01…04` | Proje sahibi kimlik atar. | `UNKNOWN` |

Araştırma hükmü:

- `MIRRORED_5` ve `INDEPENDENT_10` uygulanabilirlik senaryolarıdır; yetkili gereksinim kararı değildir.
- Yalnız iç çevre birimleriyle on bağımsız TX yolu `IND-INT-1 BLOCKED` kalır.
- Harici UART bridge, helper MCU/FPGA ve RS-232 aileleri exact-part seçimi değil, karşılaştırma adaylarıdır.
- RS-232 sürücü çıkışları paralellenmez. Aynalama seçilirse mantık tarafında yapılır ve her konnektör ayrı sürücü çıkışı alır.
- GPIO tabanı değişmedi: USB D± GPIO19/20, UART0 GPIO43/44 ayrılmıştır; GPIO45 N8 açılışında yüksek çekilmez; GPIO3 yüksek empedans bırakılmaz; GPIO39–42 koşulludur.
- TFT için sahte/pseudo GPIO satırı veya rezervasyonu eklenmemiştir.

## 4. Kaynak ve kapsam kararları

| ID | Karar | Durum |
|---|---|---|
| Q-S03-J01 | PKG-C, G1-JOB01/REP01 kapsamında mı? | `PASS` — kapsam dışı |
| Q-S03-PORT | PKG-C portföy kanıtı kapandı mı? | `BLOCKED` |
| Q-S04 | Ayrı JOB-01 ve portföy G1 kapıları kullanılacak mı? | `PASS` |
| S-CANON | Yerel CLAUDE-01…11a seri klasörü ana kayıt seçilecek mi? | `UNKNOWN`; yalnız öneri |
| S-OWNER | Gate Owner, Evidence Owner, bağımsız doğrulayıcı ve downstream sahiplerin gerçek kişi adları kayıtlı mı? | `BLOCKED`; adlar `TBD` |

## 5. Salt okunur yerel envanter özeti

**Araştırma kökü:** `C:\Users\jagua\OneDrive\Masaüstü\Model Bağımsız Çalışmaları\15.10.2026\001-Kayıt Notları ve Dokümantasyon\002-Proje Dokümantasyonu\001-Power-Embedded-Interface-Portfolio\06-Claude-Arastirma`

| Kayıt | Bayt | SHA-256 | Durum |
|---|---:|---|---|
| CLAUDE-01 | 11.444 | `9867BA94211BDF507CD7BEFFFCE451E6879347923F79FF15836F4DD8B6D74928` | Tek yerel kopya |
| CLAUDE-02 | 4.538 | `9B23EEEC778956D23605FA03DCFA5D65627B068C0E239E19A1552A28E0AD2EA1` | Tek yerel kopya |
| CLAUDE-03 | 5.886 | `18D707572926679871CE4255083345FB67FD0D0233EFAE929AE453E808F1A479` | Tek yerel kopya |
| CLAUDE-04 | 4.171 | `7CB7FD953099B5FEC6D05255F15CE5A3976C31B007AA2A0344B6BD5EBCAF2673` | Tek yerel kopya |
| CLAUDE-05 | 2.594 | `74B0DBE74999F74AAEC4A4952EBFB5276806697242982717A31E45B65CD2C2F0` | Tek yerel kopya |
| CLAUDE-06 | 8.879 | `32F182FEAB89A09E5A7D2AE8F422053ABF79EA645DB781C1EAE146586637C4CE` | Tek yerel kopya |
| CLAUDE-07 | 4.948 | `F91FD4F39B32A2E8D6E8C865BF5E07BA7368320E3BF9EDEFA882B33ACF6761D8` | Tek yerel kopya |
| CLAUDE-08 | 9.615 | `323C43DA698677D123A9B43F21569F233D591333A44A250EFBA6D6244124DAD2` | Tek yerel kopya |
| CLAUDE-09 | 2.947 | `974878AFF812CBFF6652201818458D381A72CF9CA7CDD21AD3F526EC9E152A2F` | Tek yerel kopya |
| CLAUDE-10 | 6.609 | `FBE92157550A09B03A0E4BCFAA34271E85F2CCB3FD16AE828DFFF0DC8242E4C4` | Tek yerel kopya |
| CLAUDE-11 | 6.679 | `D9D85EF5FFD6402BC227F016E3E917D4DEC6E5005995CD9EEF52A72051051F1B` | Tek yerel kopya; eski manifestte yok |
| CLAUDE-11a | 5.799 | `CF4B739F8663F4696CC788CB1B6C51177B2B02F058DFFE118DB042CCA3358CBE` | Tek yerel kopya; eski manifestte yok |

CLAUDE-01…10 manifestle bayt/hash düzeyinde eşleşir. Bu seri klasörü ana kayıt adayıdır; seçim kararı verilmemiştir. Drive ve uzak kopyalar incelenmedi.

## 6. Repo ve Git durumu

**Repo kökü:** `C:\Users\jagua\OneDrive\Masaüstü\Model Bağımsız Çalışmaları\15.10.2026\002-Proje Çıktıları\esp32-s3-24v-interface-board`

- Salt okunur Git gözlemi: `main...origin/main`; izlenen çalışma ağacı ve index temiz, bu oturumun dört v1.2 teslimi `UNTRACKED`.
- Gözlenen kısa durum: `?? docs/interface-contract-v1.2.md`, `?? docs/open-decisions-v1.2.md`, `?? engineering/gpio-budget-v1.2.csv`, `?? requirements/requirements-traceability-v1.2.csv`.
- Gözlenen HEAD/origin: `191b665107f1151e7a0aa6a60a5a123c218aa375` — `evidence: add WF01 closure and architecture matrices`.
- Bu commit mevcut dış durum olarak gözlendi; bu çalışma commit, push veya Git değişikliği yapmadı.
- `evidence/WF01-CF09-Mimari-Karar-Matrisi-v1.1.xlsx` ve `evidence/WF01-G1-JOB01-Kapanis-Kaydi-v1.2.xlsx` Git tarafından izleniyor.
- Aşağıdaki `.inspect.ndjson` dosyaları ignore edilmiş araç artıklarıdır; silinmedi ve Drive’a eşitlenmemelidir:
  - `evidence/WF01-CF09-Mimari-Karar-Matrisi-v1.0.xlsx.inspect.ndjson` — SHA-256 `B3DF7F80DB261BBC810D34E200183334DF4E77E20345415E83C1E0B8EEDF08B2`
  - `evidence/WF01-CF09-Mimari-Karar-Matrisi-v1.1.xlsx.inspect.ndjson` — SHA-256 `298CB29A43B0B3AEA07DB208B2E5B40FD8690DF94A11747488EECFF1D5D31C25`
- Şematik, PCB, BOM, exact-part, ERC/DRC, simülasyon ve bench sonucu mevcut değildir; ilgili doğrulamalar `NOT_RUN`.

## 7. Kapanış ve doğrulama matrisi

| Test / alan | Kapı | Sahip | Kapanış ölçütü | Durum |
|---|---|---|---|---|
| PKG-A/B kaynak kanıtı | G1-JOB01/REP01 | Evidence Owner + bağımsız doğrulayıcı; adlar `TBD` | Bayt, SHA-256, lisans, provenans, tekrar/çelişki | `BLOCKED` |
| V-REQ-01 — CF-09 dal anlamı | G1 | Customer / Product Owner; ad `TBD` | MIRRORED_5 veya INDEPENDENT_10 + yetkili kanıt | `BLOCKED` |
| V-MAP-01 — fiziksel/mantıksal eşleme | G1/G2 | Requirements + HW; adlar `TBD` | Her çıkışın mantıksal kaynağı tekil ve açık | `BLOCKED` |
| V-FW-01 — UART/bridge kaynak tahsisi | G2 | FW Owner; ad `TBD` | Tüm zorunlu akışlar atanmış; çakışma yok | `BLOCKED` |
| V-BUS-01 — SPI/I²C kapasitesi | G2/G3 | FW + HW; adlar `TBD` | Kaynaklı worst-case bant genişliği/gecikme bütçesi | `NOT_RUN` |
| V-RMT-01 — RMT-as-UART prototipi | G2/G3 | FW + Test; adlar `TBD` | Kaynaklı framing/hata/yük eşikleri | `NOT_RUN` |
| V-CONC-01 — eşzamanlı TX yükü | G3/G5 | FW + Test; adlar `TBD` | Kayıp/underrun/gecikme sınırları | `NOT_RUN` |
| V-BOOT-01 — boot/reset/brownout TX | G3/G5 | HW + Test; adlar `TBD` | Tanımlı yasak darbe yok | `NOT_RUN` |
| V-RS232-01 — RS-232 kablo/yük | G3/G5 | HW + Test; adlar `TBD` | Kaynaklı seviye/kenar/hata eşiği | `NOT_RUN` |
| V-PWR-01 — power-off/hot-plug/back-power | G3/G5 | HW + Test; adlar `TBD` | Kaynaklı akım/gerilim sınırı | `NOT_RUN` |
| V-EMC-01 — ESD/EFT/surge | G5 | Compliance Owner; ad `TBD` | Yetkili standart, seviye ve test noktası | `NOT_RUN` |
| V-FAIL-01 — bridge/helper fault recovery | G3/G5 | FW + Test; adlar `TBD` | Kaynaklı timeout/veri kaybı/toparlanma sınırı | `NOT_RUN` |
| Üç paket / beş iş | G1-PORTFOLIO | Portfolio Gate Owner; ad `TBD` | PKG-A/B/C ve JOB-01…05 kanıtları eksiksiz | `BLOCKED` |
| Rail budget / diğer exact-part / timing / pin freeze | G2 | Design Owner; ad `TBD` | Kaynaklı eşikler, hesaplar ve karar izi | `NOT_RUN` |
| Şematik/ERC/model provenansı/simülasyon/pre-layout | G3 | Verification Owner; ad `TBD` | Açıklanmamış hata/uyarı yok | `NOT_RUN` |
| G5’in JOB-01’e uygulanması | G5 | Gate Owner; ad `TBD` | Açık kapsam kararı | `UNKNOWN` |

## 8. Sonraki güvenli adım

Adlandırılmış insan sahipleri kaydet; PKG-A/B gerçek kaynak bayt/hash/lisans/provenans ve tekrar/çelişki kanıtını tamamla; CF09 Girdi Formunda Q-C07 dalını ve yalnız MIRRORED_5 seçilirse Q-C07-MAP eşlemesini yetkili kanıtla kapat. Q-C12–15'i G2/G3/G5 devir girdileri olarak sahipli `UNKNOWN` bırakmak G1 PASS'i tek başına engellemez. G1 engelleri kapanmadan pin dondurma, exact-part, şematik veya PCB başlangıcı kaydedilmez.

