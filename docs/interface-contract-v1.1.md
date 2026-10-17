# WF01 Arayüz Sözleşmesi — v1.1

**Tarih:** 2026-10-18  
**Durum:** Onaylı kapı sınırı; `G1-JOB01/REP01 BLOCKED`, `G1-PORTFOLIO BLOCKED`  
**Önceki sürüm:** `docs/interface-contract.md` (değiştirilmedi)  
**Onay dayanağı:** `docs/WF01-G1-Karar-Paketi-v1.3.docx`; kullanıcı, insan inceleyenlerin sınır önerisini onayladığını bildirdi. Gerçek kişi adları `TBD`; Codex onaylayan değildir.

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
| IC-07 | 5× TTL TX / REQ-004 | Kart → harici; 5 V TTL, MCU 3,3 V | GPIO ve exact-part `UNKNOWN`; OE/güvenli açılış gerekir | `BLOCKED` — CF-09 akış anlamı | Baud, framing, eşzamanlılık, GPIO ve dönüştürücü G2 `BLOCKED`; boot kanıtı G3 `NOT_RUN`. |
| IC-08 | 5× RS-232 TX + 1 RX / REQ-004 | Beş TX, bir RX; hat sürücüsü gerekli | GPIO, transceiver, charge-pump, power-off `UNKNOWN` | `BLOCKED` — CF-09 akış anlamı | Q-C07/Q-C08 ve exact-part G2 `BLOCKED`; fiziksel test `NOT_RUN`. |
| IC-09 | HD44780 8×2 LCD / PROP-02 | Arayüz modu açık | 4-bit=6 ve 8-bit=10 GPIO yalnız `ASSUMPTION`; atama yok | Ürün öğesi alımı `PASS` | Mantık seviyesi/backlight/arayüz G2 `BLOCKED` — Q-C05. |
| IC-10 | 2 PCB butonu / PROP-02 | Fiziksel giriş → MCU | Pull, debounce ve GPIO `UNKNOWN` | Sayı alımı `PASS` | Elektriksel sözleşme G2 `UNKNOWN`. |
| IC-11 | CAN/TWAI DNP / PROP-02 | Denetleyici TX/RX ↔ harici transceiver | İki GPIO yalnız bütçe senaryosu; atama yok | Kapsam `PASS` | Transceiver/terminasyon/fit varyantı G2 `UNKNOWN`. |
| IC-12 | Gelecek TFT konnektörü DNP / PROP-02 | İlk revizyonda DNP | Sinyal seti, pin sayısı, gerilim ve atama `UNKNOWN` | Kapsam `PASS` | Q-C11 teknik alanları G2 `UNKNOWN`; sahte GPIO rezervasyonu yapılmadı. |
| IC-13 | Wi-Fi / PROP-02 | WROOM-1-N8 entegre radyo | Harici GPIO ayrılmaz; anten keepout ölçüsü `UNKNOWN` | İşlev kapsamı `PASS` | HDG/yerleşim kanıtı G3 `NOT_RUN`. |
| IC-14 | Pad JTAG | Harici debug ↔ MCU | GPIO39=MTCK, 40=MTDO, 41=MTDI, 42=MTMS; `CONDITIONAL` | Kaynak alımı `PASS` | D-JTAG G2 `UNKNOWN`; CF-09 çözülmeden pin dondurma `BLOCKED`. |
| IC-15 | EN/reset | Supervisor/reset → CHIP_PU | EN boşta bırakılmaz; kaynakta verilen RC önerisi korunur; exact-part `UNKNOWN` | Güvenlik ilkesi alımı `PASS` | G2 zamanlama/part `UNKNOWN`; G3 açılış testi `NOT_RUN`. |

## 3. Mimari blokaj ve güvenli tabanlar

- CF-09 yalnız mantıksal soruyla G1’i bloke eder: beş akış iki fiziksel biçimde mi aynalanacak, yoksa on bağımsız TX akışı mı vardır? Cevap gelmeden seri topoloji, pin eşleme, UART/RMT ya da exact-part seçilmez.
- GPIO19/20 USB D± ve GPIO43/44 UART0 için ayrılmıştır.
- GPIO45, N8 açılışında yüksek çekilmez; GPIO3 yüksek empedans bırakılmaz.
- GPIO39–42 kalıcı JTAG veya TX olarak atanmadı.
- USB_PHY_SEL eFuse için geçici taban “yakılmaz”dır; nihai G2 dondurması ve G3 işlev kanıtı yoktur.

## 4. Tek doğrulama matrisi

| Kanıt / test | Kapı | İnsan rolü | Eşik | Durum |
|---|---|---|---|---|
| PKG-A/B hash, lisans, provenans, tekrar/çelişki | G1-JOB01/REP01 | Evidence Owner + bağımsız doğrulayıcı; adlar `TBD` | Kullanılan tüm kaynak baytları ve karar izi kayıtlı | `BLOCKED` |
| CF-09 mantıksal akış cevabı | G1-JOB01/REP01 | Customer + Gate Owner; adlar `TBD` | 5 aynalı veya 10 bağımsız açıkça seçilmiş | `BLOCKED` |
| Rail budget / exact-part / zamanlama / pin freeze | G2 | Design Owner; ad `TBD` | Kaynaklı sayısal eşikler ve izlenebilir kararlar | `NOT_RUN` |
| Şematik, ERC, model provenansı, simülasyon, pre-layout | G3 | Verification Owner; ad `TBD` | Açıklanmamış hata/uyarı yok; modeller kaynaklı | `NOT_RUN` |
| Bench planı uygulanabilirliği | G5 | Test/Gate Owner; adlar `TBD` | Önce JOB-01 kapsam kararı, sonra test–sahip–eşik kaydı | `UNKNOWN` |
| Fiziksel bench yürütmesi | G5 | Test Owner; ad `TBD` | Uygulanabilir bulunan testlerin ölçüm kanıtı | `NOT_RUN` |

## 5. Sonraki güvenli adım

Adlandırılmış insan Gate/Evidence/Verification sahiplerini kaydet; PKG-A/B gerçek kaynak kanıtını kapat ve Q-C07’nin CF-09 mantıksal cevabını al. Bunlar tamamlanmadan G1 PASS, pin dondurma, exact-part, şematik veya PCB başlangıcı kaydedilmez.
