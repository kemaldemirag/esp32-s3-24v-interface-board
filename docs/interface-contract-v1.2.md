# WF01 Arayüz Sözleşmesi — v1.2

**Tarih:** 2026-10-18  
**Durum:** Onaylı kapı sınırı; `G1-JOB01/REP01 BLOCKED`, `G1-PORTFOLIO BLOCKED`  
**Önceki sürüm:** `docs/interface-contract-v1.1.md` (değiştirilmedi)  
**Onay dayanağı:** `docs/WF01-G1-Karar-Paketi-v1.3.docx`; kullanıcı, insan inceleyenlerin sınır önerisini onayladığını bildirdi. Gerçek kişi adları `TBD`; Codex onaylayan değildir.
**CF-09 araştırma dayanağı:** `evidence/WF01-CF09-Mimari-Karar-Matrisi-v1.1.xlsx`; SHA-256 `2C29D3DF41C9681B7E848469C6A7F2959BFACE1615343142E9010D25648C159C`. Matris araştırma ve devir kanıtıdır; dal, topoloji, exact-part veya pin seçimi yapmaz.

Bu sürüm, gereksinim alımı ile teknik tasarım/kanıt kapılarını ayırır. `UNKNOWN` değer atanmamış, `BLOCKED` bağımlılığı kapanmamış, `NOT_RUN` doğrulaması çalıştırılmamış demektir. Kaynakta olmayan pin, akım, eşik veya parça numarası eklenmemiştir.

## 1. Onaylanan kapı sınırı

| Karar | Sonuç | Teknik anlamı |
|---|---|---|
| G1 iki kapıya ayrılır: `G1-JOB01/REP01` ve `G1-PORTFOLIO` | `PASS` | Her iki kapının nihai sonucu hâlâ `BLOCKED` olabilir. |
| PKG-C | JOB-01 kapısında kapsam dışı; portföy kapısında kapsamda | `N/A` yalnız kapsam etiketidir; durum değeri değildir. |
| TFT ilk revizyon kapsamı | `PASS — gelecek DNP` | Panel, arayüz, pin, touch, PSRAM ve backlight G2’de `UNKNOWN`; GPIO ayrılmadı. |
| USB_PHY_SEL | Geçici G2 tabanı: yakılmaz | Nihai teknik G2 dondurması `UNKNOWN`; bu ifade eFuse programlama yetkisi değildir. |
| Pad JTAG GPIO39–42 | G2’ye devredildi | `D-JTAG UNKNOWN`; kalıcı atama yapılmadı. |
| G3 | Şematik, ERC, model-provenanslı simülasyon, pre-layout inceleme | Henüz `NOT_RUN`. |
| G5 | JOB-01 uygulanabilirliği ve bench planı koşullu | Uygulanabilirlik `UNKNOWN`; fiziksel bench `NOT_RUN`. |

## 2. Arayüz sözleşmesi

| ID | Arayüz / REQ | Rol ve kaynak | Pin / güç tabanı | G1 sınırı | Teknik devir ve durum |
|---|---|---|---|---|---|
| IC-01 | 24 V araç girişi / REQ-001 | Araç → kart; `SRC-L01`, `CLAUDE-06 REQ-001-C` | 24 V nominal; min/max, transient, jump-start ve ters gerilim `UNKNOWN` | Gereksinim alımı `PASS` | G2 exact-part/PASS `BLOCKED` — Q-C01–03. Araştırma/karşılaştırma devam edebilir. |
| IC-02 | 5 V ray / REQ-002 | 24→5 V switching; `SOURCE_EXPLICIT` + `INTERNAL_RULE` | Tolerans/akım, deşarj ve backfeed `UNKNOWN` | Topoloji alımı `PASS` | G2 rail budget `BLOCKED` — Q-C04–06. |
| IC-03 | 3,3 V MCU rayı / REQ-002 | 5→3,3 V switching → WROOM-1-N8; `CLAUDE-11 C11-007` | Modül kaynağı 3,0–3,6 V ve dış kaynak ≥0,5 A; uygulama bütçesi açık | Kaynak şartı alımı `PASS` | G2 uygulama/rail budget `BLOCKED`. |
| IC-04 | USB-A host D−/D+ / REQ-003 | Dahili USB PHY; `C11-012/013` | GPIO19=D−, GPIO20=D+ `RESERVED`; 5 V toleranslı değil | Rezervasyon alımı `PASS` | G2 tasarım, G3 enumeration/FAT32/ESD kanıtı `NOT_RUN`. |
| IC-05 | USB VBUS / REQ-003 | Kart → USB cihazı; kart kaynaklı ve akım sınırlı | Limit, kabul eşiği, EN/FAULT ve exact-part `UNKNOWN` | Gereksinim alımı `PASS` | G2 `BLOCKED` — Q-C04; referans tasarım değeri gereksinim değildir. |
| IC-06 | UART0 programlama/log | MCU ↔ programlayıcı/log; `C11-014/015` | GPIO43=U0TXD, GPIO44=U0RXD `RESERVED` | Rezervasyon `PASS` | USB_PHY_SEL yakılmaz geçici tabanı; nihai G2 kararı `UNKNOWN`, G3 işlev kanıtı `NOT_RUN`. |
| IC-07 | 5× TTL TX / REQ-004 | Kart → harici; 5 V TTL; `MIR-EXT-1`, `MIR-HYB-1`, `MIR-GLOBAL-1` | GPIO ve exact-part `UNKNOWN`; OE/güvenli açılış gerekir | `BLOCKED` — CF-09 dal ve eşleme anlamı | Q-C07/Q-C07-MAP `BLOCKED`; Q-C12/Q-C14 `UNKNOWN`; `V-REQ-01`, `V-MAP-01`, `V-FW-01`, `V-BOOT-01` kapanmadan teknik PASS yok. |
| IC-07A | MIRRORED_5 eşleme sözleşmesi / REQ-004 | Çift bazlı `TTL_i ↔ RS232_i` ile global `1→10` fan-out farklı gereksinimlerdir | Eşleme tipi `UNKNOWN`; RS-232 çıkışları paralellenmez | Yalnız MIRRORED_5 seçilirse `Q-C07-MAP` gerekir | Mantık tarafı fan-out, ayrı fiziksel RS-232 sürücü çıkışları ve elektriksel yük `V-MAP-01/V-RS232-01` ile kanıtlanır. |
| IC-08 | 5× RS-232 TX + 1 RX / REQ-004 | Beş TX, bir RX; `C-RS-01`, `C-RS-02`, `C-RS-03`, `C-RS-04`, `C-RS-05`, `C-RS-06`; 5T/3R yalnız fiziksel kanal sınıfıdır | GPIO, transceiver, charge-pump, power-off ve DTE/DCE `UNKNOWN` | `BLOCKED` — CF-09 akış anlamı | 5T/3R fiziksel sayımı mantıksal eşlemeyi çözmez; Q-C13–15 ve `V-RS232-01/V-PWR-01/V-EMC-01` kapanmadan seçim yok. |
| IC-09 | HD44780 8×2 LCD / PROP-02 | Arayüz modu açık | 4-bit=6 ve 8-bit=10 GPIO yalnız `ASSUMPTION`; atama yok | Ürün öğesi alımı `PASS` | Mantık seviyesi/backlight/arayüz G2 `BLOCKED` — Q-C05. |
| IC-10 | 2 PCB butonu / PROP-02 | Fiziksel giriş → MCU | Pull, debounce ve GPIO `UNKNOWN` | Sayı alımı `PASS` | Elektriksel sözleşme G2 `UNKNOWN`. |
| IC-11 | CAN/TWAI DNP / PROP-02 | Denetleyici TX/RX ↔ harici transceiver | İki GPIO yalnız bütçe senaryosu; atama yok | Kapsam `PASS` | Transceiver/terminasyon/fit varyantı G2 `UNKNOWN`. |
| IC-12 | Gelecek TFT konnektörü DNP / PROP-02 | İlk revizyonda DNP | Sinyal seti, pin sayısı, gerilim ve atama `UNKNOWN` | Kapsam `PASS` | Q-C11 teknik alanları G2 `UNKNOWN`; sahte GPIO rezervasyonu yapılmadı. |
| IC-13 | Wi-Fi / PROP-02 | WROOM-1-N8 entegre radyo | Harici GPIO ayrılmaz; anten keepout ölçüsü `UNKNOWN` | İşlev kapsamı `PASS` | HDG/yerleşim kanıtı G3 `NOT_RUN`. |
| IC-14 | Pad JTAG | Harici debug ↔ MCU | GPIO39=MTCK, 40=MTDO, 41=MTDI, 42=MTMS; `CONDITIONAL` | Kaynak alımı `PASS` | D-JTAG G2 `UNKNOWN`; CF-09 çözülmeden pin dondurma `BLOCKED`. |
| IC-15 | EN/reset | Supervisor/reset → CHIP_PU | EN boşta bırakılmaz; kaynakta verilen RC önerisi korunur; exact-part `UNKNOWN` | Güvenlik ilkesi alımı `PASS` | G2 zamanlama/part `UNKNOWN`; G3 açılış testi `NOT_RUN`. |
| IC-16 | CF-09 iç host veri yolu | SPI/I²C/paralel/helper-MCU sınıfları; `MIR-EXT-1`, `IND-EXT-1`, `MIR-HELP-1`, `IND-HELP-1` | Bus, CS/adres, IRQ, saat, güç/kontrol ve GPIO `UNKNOWN` | G1’de seçim yapılmaz | G2 `BLOCKED`; `V-BUS-01` ve `V-FAIL-01` `NOT_RUN`; exact-part yok. |
| IC-17 | CF-09 seri zamanlama sözleşmesi | Tüm mantıksal kanallar | Baud, data bits, parity, stop bits, eşzamanlılık, burst/duty, jitter/gecikme `UNKNOWN` | G1’de yalnız dal anlamı kapanır | Q-C12 `UNKNOWN`; `V-FW-01` `BLOCKED`, `V-CONC-01` `NOT_RUN`. |
| IC-18 | CF-09 güvenli durum sözleşmesi | Boot/reset/brownout/idle/disable, power-off/back-power ve fault recovery | İzinli seviye, süre ve toparlanma eşiği `UNKNOWN` | Güvenlik ihtiyacı kaydedildi | Q-C15 `UNKNOWN`; `V-BOOT-01`, `V-PWR-01`, `V-FAIL-01` `NOT_RUN`. |

## 3. Mimari blokaj ve güvenli tabanlar

- CF-09 yalnız mantıksal soruyla G1’i bloke eder: beş akış iki fiziksel biçimde mi aynalanacak, yoksa on bağımsız TX akışı mı vardır? Matrisin seçili dalı `UNDECIDED`; `V-REQ-01` ve `V-MAP-01` kapanmadan seri topoloji, pin eşleme veya exact-part seçilmez.
- `MIRRORED_5` iki ayrı yoruma ayrılır: beş çift bazlı `TTL_i ↔ RS232_i` eşlemesi ve tek kaynağın global `1→10` fan-out’u. Kaynak bunlardan birini seçmiyor.
- `INDEPENDENT_10` için yalnız ESP32-S3 iç çevre birimleri yolu `IND-INT-1` kanal aritmetiğinde `BLOCKED` kalır. UART0 korunursa iki donanım UART kalır; RMT-as-UART yayımlanmış/desteklenen ESP-IDF özelliği değildir ve `V-RMT-01 NOT_RUN` durumundadır.
- Harici UART köprüsü, helper MCU/FPGA ve 5T/3R RS-232 aileleri yalnız araştırma adaylarıdır. Bunlar müşteri akış anlamını, host veri yolu seçimini veya exact-part kararını vermez.
- 5T/3R sınıfı beş fiziksel RS-232 TX + bir RX’i sayısal olarak karşılayabilir; mantıksal eşleme yine `BLOCKED` kalır. RS-232 sürücü çıkışları paralellenmez.
- GPIO19/20 USB D± ve GPIO43/44 UART0 için ayrılmıştır.
- GPIO45, N8 açılışında yüksek çekilmez; GPIO3 yüksek empedans bırakılmaz.
- GPIO39–42 kalıcı JTAG veya TX olarak atanmadı.
- USB_PHY_SEL eFuse için geçici taban “yakılmaz”dır; nihai G2 dondurması ve G3 işlev kanıtı yoktur.

## 4. Tek doğrulama matrisi

| Kanıt / test | Kapı | İnsan rolü | Eşik | Durum |
|---|---|---|---|---|
| PKG-A/B hash, lisans, provenans, tekrar/çelişki | G1-JOB01/REP01 | Evidence Owner + bağımsız doğrulayıcı; adlar `TBD` | Kullanılan tüm kaynak baytları ve karar izi kayıtlı | `BLOCKED` |
| V-REQ-01 — MIRRORED_5 / INDEPENDENT_10 anlamı | G1 | Customer / Product Owner; ad `TBD` | Tek dal + izlenebilir yetkili müşteri kanıtı | `BLOCKED` |
| V-MAP-01 — TTL_i ↔ RS232_i eşleme | G1/G2 | Requirements + HW; adlar `TBD` | Her fiziksel çıkışın mantıksal kaynağı tekil ve açık | `BLOCKED` |
| V-FW-01 — UART / bridge kaynak tahsisi | G2 | FW Owner; ad `TBD` | Tüm zorunlu akışlar atanmış; çakışma yok | `BLOCKED` |
| V-BUS-01 — SPI/I²C kapasitesi | G2/G3 | FW + HW; adlar `TBD` | Kaynaklı yük ve gecikme bütçesi müşteri sınırlarını karşılar | `NOT_RUN` |
| V-RMT-01 — RMT-as-UART prototipi | G2/G3 | FW + Test; adlar `TBD` | Kaynaklı framing/hata/yük eşikleri sağlanır | `NOT_RUN` |
| V-CONC-01 — eşzamanlı TX yükü | G3/G5 | FW + Test; adlar `TBD` | Kayıp, underrun ve gecikme sınırları sağlanır | `NOT_RUN` |
| V-BOOT-01 — boot/reset/brownout TX davranışı | G3/G5 | HW + Test; adlar `TBD` | Konnektörde tanımlanan yasak darbe oluşmaz | `NOT_RUN` |
| V-RS232-01 — RS-232 hat yükü ve kablo | G3/G5 | HW + Test; adlar `TBD` | Kaynaklı seviye/kenar/hata eşiği sağlanır | `NOT_RUN` |
| V-PWR-01 — power-off/hot-plug/back-power | G3/G5 | HW + Test; adlar `TBD` | Kaynaklı akım/gerilim sınırı sağlanır | `NOT_RUN` |
| V-EMC-01 — konnektör ESD/EFT/surge | G5 | Compliance Owner; ad `TBD` | Yetkili standart, seviye ve test noktası sağlanır | `NOT_RUN` |
| V-FAIL-01 — bridge/helper fault recovery | G3/G5 | FW + Test; adlar `TBD` | Kaynaklı timeout/veri kaybı/toparlanma sınırı sağlanır | `NOT_RUN` |
| Rail budget / diğer exact-part / pin freeze | G2 | Design Owner; ad `TBD` | Kaynaklı sayısal eşikler ve izlenebilir kararlar | `NOT_RUN` |
| Şematik, ERC, model provenansı, simülasyon, pre-layout | G3 | Verification Owner; ad `TBD` | Açıklanmamış hata/uyarı yok; modeller kaynaklı | `NOT_RUN` |
| G5 uygulanabilirlik kararı | G5 | Gate Owner; ad `TBD` | JOB-01 için açık kapsam kararı | `UNKNOWN` |

## 5. Sonraki güvenli adım

Adlandırılmış insan Gate/Evidence/Verification sahiplerini kaydet; PKG-A/B gerçek kaynak kanıtını kapat; CF09 Girdi Formunda Q-C07 dalını ve yalnız MIRRORED_5 seçilirse Q-C07-MAP eşlemesini yetkili kanıtla kapat. Q-C12–15, G2/G3/G5 devir girdileri olarak sahipli `UNKNOWN` kalabilir ve G1 PASS'i tek başına bloke etmez. G1 engelleri kapanmadan pin dondurma, exact-part, şematik veya PCB başlangıcı kaydedilmez.

