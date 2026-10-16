# WF01 Arayüz Sözleşmesi Taslağı

**Durum:** DRAFT — G1 `BLOCKED`  
**Tarih:** 2026-10-16  
**Aktif WF01 kaydı:** Bu dosya  
**Önceki scaffold taslağı:** `docs/arayuz-sozlesmesi-taslagi.md` (`OPEN / SOURCE_PENDING`; değiştirilmedi)

Bu kayıt, doğrulanmış gereksinimleri ve eksikleri birbirinden ayırır. `UNKNOWN` bir değer atanmamış olduğunu; `BLOCKED` ise karar/uygulamanın eksik kanıt veya girdiye bağlı olduğunu gösterir. Buradaki hiçbir `UNKNOWN` alan için pin, akım, eşik ya da parça numarası varsayılmamıştır.

## Sözleşme tablosu

| ID | Arayüz / REQ | Rol ve yön | Gerilim / güç alanı | Güç kapalı davranış | Pin / kaynak | Kanıt ve sınıf | Doğrulama yöntemi | Durum / engel |
|---|---|---|---|---|---|---|---|---|
| IC-01 | 24 V araç girişi / REQ-001 | Araç beslemesi → kart | 24 V nominal; min/max, transient, jump-start ve ters gerilim profili `UNKNOWN` | Koruma davranışı profil ve exact-part olmadan `UNKNOWN` | Konnektör ve net adı `UNKNOWN` | `SRC-L01`; `CLAUDE-06 REQ-001-C`; `SOURCE_EXPLICIT` | Q-C01–Q-C03 yanıtı; masaüstü güç ve transient test planı | `BLOCKED`; exact-part ve PASS kararı verilemez. WF01 REQ-001 taslağıyla araştırma/karşılaştırma yapılabilir. |
| IC-02 | 5 V dahili ray / REQ-002 | 24→5 V switching kademe; yükler tasarımda tanımlanacak | 5 V nominal; tolerans ve akım bütçesi `UNKNOWN` | Geri besleme ve deşarj `UNKNOWN` | Net/pin `UNKNOWN` | `SRC-L01`; `CLAUDE-06 REQ-002-C/-I`; `SOURCE_EXPLICIT` + `INTERNAL_RULE` | Worst-case rail bütçesi ve ölçüm | Topoloji `PASS`; sayısal bütçe `BLOCKED` (Q-C04–Q-C06). |
| IC-03 | 3,3 V MCU rayı / REQ-002 | 5→3,3 V switching kademe → ESP32-S3-WROOM-1-N8 | Modül beslemesi 3,0–3,6 V; dış kaynak ≥0,5 A | Deşarj/geri besleme `UNKNOWN` | WROOM-1 pin 2 (3V3), GND pinleri | `CLAUDE-11 C11-007`; `SOURCE_EXPLICIT` | Gerilim toleransı, açılış ve yük bütçesi | Kaynak şartı `PASS`; uygulama/rail bütçesi `BLOCKED`. |
| IC-04 | USB-A host D−/D+ / REQ-003 | Host veri yolu, çift yönlü | Dahili USB PHY; GPIO’lar 5 V toleranslı değildir | Kablo/ESD parçası olmadan `UNKNOWN` | GPIO19=D−, GPIO20=D+; ikisi `RESERVED` | `CLAUDE-03 C03-004/C03-007`; `CLAUDE-11 C11-012/C11-013`; `SOURCE_EXPLICIT` | Enumeration/FAT32 işlev testi; sinyal ve ESD incelemesi | Pin ayırma `PASS`; tek dahili PHY nedeniyle aynı anda tek denetleyici. GPIO19/20 boot glitch notu korunur. |
| IC-05 | USB VBUS / REQ-003 | Kart → USB cihazı; kart kaynaklı ve akım sınırlı | 5 V nominal; limit ve kabul eşiği `UNKNOWN` | Anahtar/off-state/backfeed `UNKNOWN` | EN/FAULT pinleri ve exact-part `UNKNOWN` | `CLAUDE-06 REQ-003-I`; `ENGINEERING_RULE` | Akım limiti, kısa devre ve backfeed testi | `BLOCKED`; Q-C04. Referans tasarım değeri JOB-01 gereksinimi sayılmaz. |
| IC-06 | Programlama/log UART0 / REQ-003-I2 | MCU ↔ programlayıcı/log | 3,3 V mantık alanı | `UNKNOWN` | GPIO43=U0TXD, GPIO44=U0RXD; `RESERVED` | `CLAUDE-11 C11-014/C11-015`; `SOURCE_EXPLICIT` | Flash/log erişim testi USB host etkinken | Ayırma `PASS`; eFuse stratejisi D-USB-EFUSE altında `OPEN`. |
| IC-07 | 5× TTL TX / REQ-004 | Kart → harici; beş çıkış | Harici arayüz 5 V TTL; MCU tarafı 3,3 V, seviye dönüştürme gerekir | Buffer OE, backfeed ve yük davranışı `UNKNOWN` | GPIO ataması `UNKNOWN`; glitchsiz adaylar bütçe tablosunda koşullu | `SRC-L01`; `CLAUDE-03 C03-020/C03-022–025`; `SOURCE_EXPLICIT`, destek `DERIVED`/`ENGINEERING_RULE` | Boot sırasında çıkış seviyesi; yük ve baud testi | `BLOCKED` — CF-09, Q-C07, frozen GPIO belgesi ve exact-part yok. |
| IC-08 | 5× RS-232 TX + 1 RX / REQ-004 | Beş kart→harici TX, bir harici→kart RX | Hat gerilimleri ve transceiver/charge-pump exact-part `UNKNOWN` | Backfeed ve power-off `UNKNOWN` | GPIO ataması `UNKNOWN` | `SRC-L01`; `CLAUDE-03 C03-020`; `SOURCE_EXPLICIT` | Kanal sayısı, baud/kablo/yük ve power-off testi | `BLOCKED` — CF-09; Q-C07/Q-C08. |
| IC-09 | HD44780 uyumlu 8×2 LCD / PROP-02 | Yön ve R/W kullanımı arayüz moduna bağlı, `UNKNOWN` | Mantık seviyesi ve arka ışık akımı `UNKNOWN` | `UNKNOWN` | 4-bit=6, 8-bit=10 GPIO yalnız `ASSUMPTION` senaryosudur; atama yok | `SRC-L01`; `CLAUDE-06 PROP-02`; `SOURCE_EXPLICIT`, senaryo `ASSUMPTION` | Müşteri arayüzü onayı; işlev ve açılış testi | `BLOCKED` — Q-C05. |
| IC-10 | 2 PCB butonu / PROP-02 | Fiziksel giriş → MCU (yön `DERIVED`) | Mantık alanı, pull ve debounce `UNKNOWN` | `UNKNOWN` | GPIO ataması `UNKNOWN` | `SRC-L01`; `SOURCE_EXPLICIT`, yön `DERIVED` | Pull/debounce ve boot etkisi testi | Sayı `PASS`; elektriksel sözleşme `UNKNOWN`. |
| IC-11 | CAN/TWAI DNP / PROP-02 | Denetleyici TX/RX ↔ harici transceiver; DNP | Gerilim, transceiver ve terminasyon `UNKNOWN` | `UNKNOWN` | İki GPIO bütçe senaryosu; atama yok | `SRC-L01`; `CLAUDE-03 C03-015/C03-016`; `SOURCE_EXPLICIT`, ayrım `DERIVED` | DNP/fit varyant ve ağ testi kararı | Kapsam `PASS`; uygulama `UNKNOWN`. |
| IC-12 | Gelecek TFT konnektörü DNP / PROP-02 | Sinyal seti ve yön `UNKNOWN` | Gerilim/power-off `UNKNOWN` | `UNKNOWN` | Pin sayısı ve atama `UNKNOWN` | `SRC-L01`; `CLAUDE-11` GPIO bütçe notları; `SOURCE_EXPLICIT` | Q-C11 müşteri yanıtı | `BLOCKED`; pin bütçesi TFT hariç değerlendirilir. |
| IC-13 | Wi-Fi / PROP-02 | WROOM-1-N8 entegre radyo | 3,3 V MCU rayı | Kart kapalıyken harici arayüz yok | Harici GPIO ayırması yok; anten keepout boyutu `UNKNOWN` | `SRC-L01`; `CLAUDE-11 C11-011`; `SOURCE_EXPLICIT` | HDG/anten yerleşim incelemesi; RF işlev testi | İşlev kapsamı `PASS`; keepout ölçüsü `UNKNOWN`. |
| IC-14 | Pad JTAG | Harici debug ↔ MCU | 3,3 V mantık alanı; ayrıntılar `UNKNOWN` | `UNKNOWN` | GPIO39=MTCK, 40=MTDO, 41=MTDI, 42=MTMS; `CONDITIONAL` | `CLAUDE-11 C11-014`; `SOURCE_EXPLICIT` | Debug erişimi ve pin bütçesi kararı | `OPEN`; D-JTAG kararı 28↔24 genel GPIO bütçesini etkiler. |
| IC-15 | EN/reset | Supervisor/reset kaynağı → CHIP_PU | 3,3 V alanı; RC önerisi 10 kΩ/1 µF, nihai değer zamanlamaya göre ayarlanır | EN boşta bırakılmaz | WROOM-1 pin 3 (EN) | `CLAUDE-11 C11-009`; `SOURCE_EXPLICIT` | Reset süreleri ve açılış testi | İlke `PASS`; supervisor exact-part ve nihai zamanlama `UNKNOWN`. |

## USB dahili PHY ve eFuse sınırı

- USB OTG/TinyUSB başlatıldığında dahili PHY çalışma zamanında host tarafına geçirilebilir; bu yol için USB_PHY_SEL eFuse yakılması gerekmez (`CLAUDE-11 C11-012`, `SOURCE_EXPLICIT`).
- OTG ve USB-Serial-JTAG aynı dahili PHY’yi paylaşır ve eşzamanlı kullanılamaz (`C11-013`). Bu nedenle programlama/log için GPIO43/44 UART0 ayrılmıştır (`C11-014`).
- **Taslak öneri:** USB_PHY_SEL eFuse yakılmasın; böylece boot USB-Serial-JTAG yolu korunur (`C11-015`, `DERIVED`). Bu bir nihai karar değildir; `open-decisions.md` içindeki D-USB-EFUSE `OPEN` kalır.

## Mimari blokaj

On TX çıkışının mantıksal topolojisi CF-09 nedeniyle `BLOCKED` kalır. GPIO sayımı, glitchsiz aday listesi veya olası yazılım/dış genişletici seçenekleri bu çatışmayı kapatmaz. Q-C07 ve müşterinin frozen GPIO kaydı gelmeden pin eşleme, UART/RMT seçimi veya exact-part kararı yapılmaz.

## Kanıt sınıfları

Bu taslak yalnız şu CLAUDE-06 uyumlu sınıfları kullanır: `SOURCE_EXPLICIT`, `DERIVED`, `ENGINEERING_RULE`, `SECONDARY_SOURCE`, `HANDOFF_ONLY`, `INTERNAL_RULE`, `ASSUMPTION`, `UNKNOWN`, `CONFLICT`. Birincil sınıf ile destekleyen sınıf karıştırılmamıştır.
