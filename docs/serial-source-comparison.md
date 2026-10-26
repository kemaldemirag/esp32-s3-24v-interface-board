# CF-09 seri kaynak sınıfları A–D — teknik karşılaştırma

**Durum:** `PROPOSED / REFERENCE_CANDIDATE`

**Karar:** Kilitlenmedi

**Kapsam:** Revize REP-01 görev planındaki A–D seçenekleri; mevcut `MIRRORED_5` portfolyo varsayımı için kaynak ve model karşılaştırması

**İddia sınırı:** Aşağıdaki sayılar üretici kaynaklı aday özellikleri veya açıkça işaretlenmiş sentetik model sonuçlarıdır. Exact-part, elektriksel uygunluk, ESP-IDF sürücü yeterliliği ve donanım kapasitesi `PASS` değildir.

## 1. Mimari karşılaştırma

| Sınıf | Yapı | `MIRRORED_5` kanal hesabı | `INDEPENDENT_10` | UART0 etkisi | Geçici durum |
|---|---|---|---|---|---|
| A | 2 yerel UART + 1 quad köprü | 2 yerel TX + 3 haricî TX; quad'ın dördüncü kanalı RX için ayrılabilir | Yetersiz | UART0 rezervasyonu korunur | `PROPOSED` |
| B | 3 yerel UART + 1 dual köprü | 3 yerel TX + 2 haricî TX; RX bir UART yönüyle ayar paylaşır veya ayrıca kanıtlanır | Yetersiz | Üçüncü yerel UART, UART0 programlama/kurtarma/log rezervasyonuyla çakışır | `BLOCKED` — Q-UART0 |
| C | Yardımcı MCU/FPGA | Exact yardımcı ve IPC tanımlı değil | `UNKNOWN`; yeterli yardımcı seçilirse mümkün olabilir | UART0 korunabilir | `UNKNOWN` |
| D | 2 yerel UART + 2 dual köprü | 2 yerel TX + 3 haricî TX; dördüncü haricî kanal RX olabilir | Yetersiz | UART0 rezervasyonu korunur | `PROPOSED` |

**Türetilmiş sonuç:** A–D seçenekleri, raporda tanımlandıkları biçimiyle `MIRRORED_5` için karşılaştırma sınıflarıdır. Hiçbiri tek başına on bağımsız TX'i kapatmaz. `INDEPENDENT_10` için mevcut `HYB-REV10` iki-quad yaklaşımı, üçüncü serializer/RX varyantı veya yeterliliği ayrıca kanıtlanmış yardımcı MCU/FPGA gerekir.

Bu kanal hesabı `tests/cf09_source_allocation_model_v1.py` ile yürütülebilir hale getirildi. Model A'da iki yerel + üç quad TX ve dedicated RX; D'de iki yerel + iki dual köprüye yayılan üç TX ve dedicated RX üretir. B'nin beş TX hesabı tam olsa bile UART0 ve paylaşılan RX-konfigürasyon engellerini kaldırmaz; C için kaynak uydurmak yerine `NOT_RUN` döner.

## 2. Üretici kaynaklı aday özellikleri

| Aday | UART | FIFO | UART hızı | Host veri yolu | Reset / IRQ özeti | Power-off sınırı |
|---|---:|---|---|---|---|---|
| MAX14830 | 4 | UART başına 128-word TX ve RX | 16× modda 6 Mbaud; 2×/4× modda 12/24 Mbaud | SPI en çok 26 MHz; I²C en çok 1 MHz | Global aktif-düşük reset; açılış IRQ davranışı ve kanal reseti tanımlı | Tüm beslemeler kapalıyken I/O/backfeed garantisi bulunamadı: `UNKNOWN` |
| MAX3109 | 2 | UART başına 128-word TX ve RX | En çok 24 Mbaud | SPI en çok 26 MHz; I²C en çok 1 MHz | Aktif-düşük reset; IRQ açık-drenç; kısmi reset/shutdown çıkış durumları tanımlı | Kısmi durum bilgisi tam rail-off backfeed garantisi değildir: `UNKNOWN` |
| SC16IS762 | 2 | 64-byte TX ve RX | En çok 5 Mbit/s | SPI en çok 15 Mbit/s; I²C en çok 400 kbit/s | Donanım/POR/yazılım reseti; IRQ açık-drenç | Besleme-yokken tablo dipnotları Ioff/backfeed garantisi oluşturmaz: `CONFLICT / UNKNOWN` |
| SC16IS752 | 2 | 64-byte TX ve RX | En çok 5 Mbit/s | SPI en çok 4 Mbit/s; I²C en çok 400 kbit/s | SC16IS762 ile aynı sınıf | D ile 1 Mbaud sentetik yükte 4 MHz SPI statik ekranda elenir; rail-off yine `CONFLICT / UNKNOWN` |

Birincil kaynaklar:

- [MAX14830 Rev.7 veri sayfası](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX14830.pdf): s.1, 8–10, 15–17, 27, 39, 58–59.
- [MAX3109 Rev.6 veri sayfası](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX3109.pdf): s.1, 7–9, 14–15, 27.
- [SC16IS752/762 Rev.9.1 veri sayfası](https://www.nxp.com/docs/en/data-sheet/SC16IS752_SC16IS762.pdf): s.1–2, 7, 12–13, 39–40.

## 3. Sürücü ve referans kod etkisi

- `espressif/esp-idf` revizyonu `d35ea023029363d97c42565f85e0db23486f26b2` içinde SC16IS7xx, MAX3109 veya MAX14830 için resmî in-tree sürücü bulunmadı. [UART bileşen listesi](https://github.com/espressif/esp-idf/blob/d35ea023029363d97c42565f85e0db23486f26b2/components/esp_driver_uart/CMakeLists.txt#L24-L44). Resmî soft-UART örneği yalnız 8N1'dir; kesmeleri kapatır ve watchdog kısıtları getirir. Bu, yazılım UART/RMT üretim kapasite kredisinin `0` kalmasını destekler. [Soft-UART örneği](https://github.com/espressif/esp-idf/blob/d35ea023029363d97c42565f85e0db23486f26b2/examples/peripherals/dedicated_gpio/soft_uart/README.md#L6-L35).
- `nopnop2002/esp-idf-sc16is750` revizyonu `27bb453859722129f0057f14cb359799e37c049d`, MIT lisanslı SC16IS750/752 ESP-IDF prototipidir. [README](https://github.com/nopnop2002/esp-idf-sc16is750/blob/27bb453859722129f0057f14cb359799e37c049d/README.md#L1-L5) ve [lisans](https://github.com/nopnop2002/esp-idf-sc16is750/blob/27bb453859722129f0057f14cb359799e37c049d/LICENSE). Referans kodun SPI yolunda cihaz başına bus init yapması ve 1 MHz SPI kullanması nedeniyle iki-köprülü D sınıfına doğrudan kopyalanamaz; bus sahipliği, multi-instance, IRQ ve recovery yeniden tasarlanmalıdır. Bu kayıt `REFERENCE_DESIGN / PROTOTYPE_SCAFFOLD`, ürün sürücüsü değildir.
- Linux `max310x` ve `sc16is7xx` sürücüleri, revizyon `d135ab453a7400a34930bdda1ef4bdc8acfac80e`, FIFO/IRQ/reset/power-state davranışları için yararlı `SECONDARY_SOURCE` referanslarıdır. [MAX310X](https://github.com/torvalds/linux/blob/d135ab453a7400a34930bdda1ef4bdc8acfac80e/drivers/tty/serial/max310x.c) ve [SC16IS7xx](https://github.com/torvalds/linux/blob/d135ab453a7400a34930bdda1ef4bdc8acfac80e/drivers/tty/serial/sc16is7xx.c) `GPL-2.0+` kodudur; ESP-IDF uygulamasına doğrudan kopyalanmamalıdır.
- Yardımcı MCU için WCH'nin CH32V307 referansı sekiz UART/USART gösterir; bu sayı `MIRRORED_5 + RX` için yeterli, `INDEPENDENT_10` için yetersizdir. [WCH repo, revizyon `8b3699d`](https://github.com/openwch/ch32v307/tree/8b3699d9e4eafc3102e382b74266a1165a2bb584). Kullanım başlıkları WCH MCU'larıyla sınırlıdır ve kök lisans net değildir; kod taşıma izni varsayılmaz. `espressif/esp-hosted` revizyonu `a0422923`, host–yardımcı işlemci framing/RPC mimarisi için resmî referanstır; çoklu-UART serializer veya CF-09 recovery kanıtı değildir. [ESP-Hosted](https://github.com/espressif/esp-hosted/tree/a0422923d01fa5cd5f4987885fe743656398b7c5).

**Yazılım açısından çıkarım:** D sınıfı, MIT prototip nedeniyle en hızlı fake/loopback başlangıcını sağlar. A sınıfı daha az bileşen ve daha küçük fault alanı sağlar, ancak MAX14830 için yeni/uyarlanmış ESP-IDF katmanı gerekir. Prototip sırası ile ürün topolojisi tercihi aynı karar değildir.

## 4. Sentetik SPI kullanım ekranı

Bu tablo, `tests/cf09_bus_scheduler_model_v1.py` içindeki mevcut varsayımları kullanır: 8N1, sürekli TX, UART payload girdisi başına 16 host bit, `1,25` guard ve `%70` planlama sınırı. Yalnız haricî TX trafiği sayılır; RX, durum/IRQ okumaları, retry, fault trafiği, register protokolü, CS aralıkları ve CPU yükü dışarıdadır. Sonuç `PASS` değil, yalnız planlama ekranıdır.

| Sınıf / aday | Haricî TX | Sentetik baud | SPI | Gerekli bus | Kullanım | FIFO servis ufku | Sonuç |
|---|---:|---:|---:|---:|---:|---:|---|
| A / MAX14830 | 3 | 115.200 | 26 MHz | 691.200 bit/s | %2,658462 | 11.111,111111 µs | `SCREEN_CANDIDATE_NOT_CAPACITY_PASS` |
| A / MAX14830 | 3 | 1.000.000 | 26 MHz | 6.000.000 bit/s | %23,076923 | 1.280 µs | `SCREEN_CANDIDATE_NOT_CAPACITY_PASS` |
| B / MAX3109 | 2 | 115.200 | 26 MHz | 460.800 bit/s | %1,772308 | 11.111,111111 µs | `SCREEN_CANDIDATE_NOT_CAPACITY_PASS`; Q-UART0 hâlâ `BLOCKED` |
| B / MAX3109 | 2 | 1.000.000 | 26 MHz | 4.000.000 bit/s | %15,384615 | 1.280 µs | `SCREEN_CANDIDATE_NOT_CAPACITY_PASS`; Q-UART0 hâlâ `BLOCKED` |
| B / SC16IS762 | 2 | 115.200 | 15 MHz | 460.800 bit/s | %3,072000 | 5.555,555556 µs | `SCREEN_CANDIDATE_NOT_CAPACITY_PASS`; Q-UART0 hâlâ `BLOCKED` |
| B / SC16IS762 | 2 | 1.000.000 | 15 MHz | 4.000.000 bit/s | %26,666667 | 640 µs | `SCREEN_CANDIDATE_NOT_CAPACITY_PASS`; Q-UART0 hâlâ `BLOCKED` |
| D / 2×MAX3109 | 3 | 115.200 | 26 MHz | 691.200 bit/s | %2,658462 | 11.111,111111 µs | `SCREEN_CANDIDATE_NOT_CAPACITY_PASS` |
| D / 2×MAX3109 | 3 | 1.000.000 | 26 MHz | 6.000.000 bit/s | %23,076923 | 1.280 µs | `SCREEN_CANDIDATE_NOT_CAPACITY_PASS` |
| D / 2×SC16IS762 | 3 | 115.200 | 15 MHz | 691.200 bit/s | %4,608000 | 5.555,555556 µs | `SCREEN_CANDIDATE_NOT_CAPACITY_PASS` |
| D / 2×SC16IS762 | 3 | 1.000.000 | 15 MHz | 6.000.000 bit/s | %40,000000 | 640 µs | `SCREEN_CANDIDATE_NOT_CAPACITY_PASS` |

Sınıf C için exact yardımcı MCU/FPGA, host linki, protokol ve buffer modeli bulunmadığından hesap `NOT_RUN` kalır.

## 5. RS-232 fiziksel katman adayı: TRS3238E

[TRS3238E veri sayfasına](https://www.ti.com/lit/gpn/TRS3238E) göre tek paket beş sürücü ve üç alıcı içerir; bu yalnız beş RS-232 TX ve bir RX sayısını aritmetik olarak karşılar. Exact seçim değildir.

- `FORCEOFF=L` iken DOUT'lar ve normal ROUT çıkışları yüksek empedanstır; `ROUT1B` aktif kalır. Geçici güvenli yön, RX için ROUT2/3 kullanmak ve ROUT1B'yi bağlamamaktır.
- Auto-Powerdown Plus'ın supply-enable süresi en çok 25 µs'dir. Bu nedenle ilk karakter bütünlüğü açık wake/guard/preamble veya aktif oturumda forced-on politikasıyla bench'te kanıtlanmalıdır.
- FORCEOFF ile kapatma gerçek `VCC=0` davranışı değildir. VCC/V+/V− sıfırken DOUT direncinin minimumu 300 Ω olabilir; rail-off backfeed ve uzak aktif hattı yükleme davranışı `UNKNOWN` kalır.
- 5 V beslemede receiver logic çıkış seviyesi 3,3 V MCU için doğrudan güvenli kabul edilemez. 3,3 V besleme veya seviye çevirme exact tasarımda doğrulanmalıdır.
- Veri hızı testi tek DOUT anahtarlanırken verilmiştir; beş eşzamanlı çıkış için donanım kanıtı yoktur.

Ek birincil kaynak: [TI RS-232 Design Guide, SDAA219](https://www.ti.com/lit/an/sdaa219/sdaa219.pdf), §3.1–3.3, s.3–5. Bu kılavuz güç sırası sırasında logic pinlerinin float etmesi ve farklı güç alanlarından back-bias riskinin ayrı değerlendirilmesi gerektiğini destekler.

## 6. Geçici teknik yön ve fallback

- **Tercih edilen araştırma yönü:** A sınıfı, tek quad köprü. Gerekçe: UART0 rezervasyonunu korur, tek haricî fault alanı ve dedicated RX kanalı sağlar, mevcut statik ekranda en yüksek SPI/FIFO boşluğunu bırakır.
- **İlk yazılım prototipi:** D sınıfı, iki SC16IS752/762 sınıfı fake/loopback adaptörü. MIT referans yalnız başlangıç sağlar; ürün seçimi veya kapasite kanıtı değildir.
- **Fallback ürün topolojisi:** D sınıfı, iki dual köprü. Bir dual adayının yazılım veya tedarik avantajı doğrulanırsa kullanılabilir; iki CS/IRQ/reset alanı ve cross-bridge recovery maliyeti ayrıca kanıtlanır.
- **Bloklu seçenek:** B sınıfı, Q-UART0 çözülmeden ilerlemez.
- **Fonksiyonel fallback:** C sınıfı, ancak exact yardımcı MCU/FPGA, lisanslı firmware, host IPC, watchdog, güncelleme ve fault-recovery planı birlikte nitelendirilirse.

Bu yön bir tasarım kilidi değildir. PCB veya exact-part kararı öncesi resmî ESP-IDF sürücüsü/uyarlama maliyeti, SPI/FIFO/IRQ bench'i, TRS3238E beş-kanal testi ve üç yönlü rail-off backfeed matrisi kapanmalıdır.
