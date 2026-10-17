# WF01 Açık Kararlar ve Kapanış Kaydı — v1.1

**Tarih:** 2026-10-18  
**Önceki sürüm:** `docs/open-decisions.md` (değiştirilmedi)  
**Kapı sonucu:** `G1-JOB01/REP01 BLOCKED`; `G1-PORTFOLIO BLOCKED`  
**Durum sözlüğü:** `PASS`, `BLOCKED`, `UNKNOWN`, `NOT_RUN`. `N/A` yalnız kapsam, `TBD` yalnız rol/kimlik alanıdır.

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
| Q-C08 | RS-232 baud/kablo ve TTL yük tipi? | CUSTOMER — kişi `TBD` | G2 sürücü seçimi | `UNKNOWN` |
| Q-C09 | ESD/EFT hedef seviyesi? | CUSTOMER — kişi `TBD` | G2/G3 koruma ve test eşiği | `UNKNOWN` |
| Q-C10 | Üretici ve montaj sınıfı? | CUSTOMER / PROJECT — kişi `TBD` | G2/G4 stackup/DFM/maliyet | `UNKNOWN` |
| Q-C11 | TFT panel/arayüz/sinyal/pin/touch/PSRAM/backlight gereksinimi? | CUSTOMER — kişi `TBD` | G1 kapsamı kapanmış; G2 teknik sözleşme | `UNKNOWN` |

Q-C01–03, WF01 REQ-001 taslağı mevcut olduğu için araştırma ve karşılaştırmayı durdurmaz. Yalnız exact-part ve teknik PASS’i bloke eder.

## 3. Mimari kararlar ve çatışmalar

| ID | Karar / kanıt | Onaylı taban | Nihai durum |
|---|---|---|---|
| CF-09 | `CLAUDE-03 C03-020`; `CLAUDE-06 REQ-004-C`; `CONFLICT` | Topoloji ve pin seçilmez. | G1 `BLOCKED`: akış anlamı eksik. Baud/framing/concurrency/GPIO/serializer G2 `BLOCKED`. |
| D-JTAG | `CLAUDE-11 C11-014`; GPIO39–42 | Kalıcı atama yok. | G2 `UNKNOWN`; CF-09 nedeniyle pin freeze `BLOCKED`. |
| D-USB-EFUSE-BASE | `C11-012 SOURCE_EXPLICIT`; `C11-015 DERIVED` | Geçici G2 tabanı: USB_PHY_SEL yakılmaz. | Taban `PASS`; nihai teknik G2 dondurması `UNKNOWN`; G3 işlev testi `NOT_RUN`. |
| D-EFUSE-IO35-37 | `C11-003/C11-004`; `DERIVED` | Resmî varsayılan doğrulanmadan atama yok. | `UNKNOWN` |
| D-ANTENNA | `C11-011`; keepout ölçüsü kaynak setinde yok | HDG kanıtı alınır. | `UNKNOWN` |
| D-PROP-IDS | `CLAUDE-06 PROP-01…04` | Proje sahibi kimlik atar. | `UNKNOWN` |

GPIO tabanı: USB D± GPIO19/20, UART0 GPIO43/44 ayrılmıştır; GPIO45 N8 açılışında yüksek çekilmez; GPIO3 yüksek empedans bırakılmaz. TFT için sahte/pseudo GPIO satırı veya rezervasyonu eklenmemiştir.

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

- Salt okunur Git gözlemi: `main...origin/main`.
- Karar paketleri v1.1, v1.2 ve v1.3 gözlem anında izlenmeyen dosyalardır.
- Bu çalışma Git init/commit/push, silme veya Drive eşitlemesi yapmaz.
- Şematik, PCB, BOM, exact-part, ERC/DRC, simülasyon ve bench sonucu mevcut değildir; ilgili doğrulamalar `NOT_RUN`.

## 7. Kapanış ve doğrulama matrisi

| Alan | Kapı | Sahip | Kapanış ölçütü | Durum |
|---|---|---|---|---|
| PKG-A/B kaynak kanıtı | G1-JOB01/REP01 | Evidence Owner + bağımsız doğrulayıcı; adlar `TBD` | Bayt, SHA-256, lisans, provenans, tekrar/çelişki | `BLOCKED` |
| CF-09 mantıksal cevap | G1-JOB01/REP01 | Customer + Gate Owner; adlar `TBD` | 5 aynalı veya 10 bağımsız açık seçim | `BLOCKED` |
| Üç paket / beş iş | G1-PORTFOLIO | Portfolio Gate Owner; ad `TBD` | PKG-A/B/C ve JOB-01…05 kanıtları eksiksiz | `BLOCKED` |
| Rail budget / exact-part / timing / pin freeze | G2 | Design Owner; ad `TBD` | Kaynaklı eşikler, hesaplar ve karar izi | `NOT_RUN` |
| Şematik/ERC/model provenansı/simülasyon/pre-layout | G3 | Verification Owner; ad `TBD` | Açıklanmamış hata/uyarı yok | `NOT_RUN` |
| G5’in JOB-01’e uygulanması | G5 | Gate Owner; ad `TBD` | Açık kapsam kararı | `UNKNOWN` |
| Bench planı | G5 | Test Owner; ad `TBD` | Test–kapı–sahip–eşik satırları | `UNKNOWN` |
| Fiziksel bench yürütmesi | G5 | Test Owner; ad `TBD` | Ölçüm ve sonuç kanıtı | `NOT_RUN` |

## 8. Sonraki güvenli adım

Adlandırılmış insan sahipleri kaydet; PKG-A/B gerçek kaynak bayt/hash/lisans/provenans ve tekrar/çelişki kanıtını tamamla; Q-C07’de CF-09’un mantıksal akış anlamını seçtir. Bunlar kapanmadan G1 PASS, pin dondurma, exact-part, şematik veya PCB başlangıcı kaydedilmez.
