# CF-09 seri haberleşme mimarisi — teknik portfolyo tabanı

**Sürüm:** v1.2  
**Tarih:** 18.10.2026  
**Kapsam:** ESP32-S3-WROOM-1-N8 üzerinde 5 adet 5 V TTL TX, 5 adet RS-232 TX ve 1 adet RX için ölçeklenebilir seri mimari  
**Durum:** `PROVISIONAL / ASSUMPTION-DRIVEN` — PCB pin-freeze veya exact-part seçimi değildir  
**Yönetişim tabanı:** 18.10.2026 tarihli WF01/G1 dosyaları değiştirilmeden korunmuştur.

## 1. Teknik sonuç

Geçici tercih `HYB-REV10` mimarisidir:

- ESP32-S3 `UART0`, GPIO43/44 üzerinde programlama, kurtarma ve log için ayrılmış kalır.
- `UART1` ve `UART2`, iki yerel serileştirici kaynağı sağlar.
- Haricî **quad-UART köprü A**, dört ek kanal sağlar.
- Haricî **quad-UART köprü B**, `INDEPENDENT_10` montajında dört ek TX kanalı daha sağlar; `MIRRORED_5` montajında DNP bırakılabilir.
- `INDEPENDENT_10` altında tek RX, bir TX kanalının RX yönünü ve aynı baud/framing ayarını paylaşabiliyorsa iki köprü yeterlidir. RX bağımsız UART konfigürasyonu istiyorsa üçüncü bridge/ayrı serializer gerekir.
- Host bağlantısı için geçici tercih SPI'dır. I²C, ancak gerçek trafik ve exact-part deneyi kapasiteyi doğrularsa yeniden değerlendirilebilir.
- TTL ve RS-232 çıkışları boot/reset/brownout boyunca donanımsal varsayılanla kapalı tutulur. GPIO Hi-Z güvenli idle kabul edilmez.
- RMT veya yazılım UART üretim kapasitesine yazılmaz; yalnız karşılaştırma prototipi olarak kullanılır.

Bu seçim, beş eşlenik akışla düşük BOM'lu başlangıca ve aynı konektör sözleşmesini koruyarak on ayrı akışa geri dönüşlü yükseltmeye izin verir. Bu bir müşteri gereksinimi değil, aşağıdaki açık varsayımlarla kurulmuş portfolyo mimarisidir.

## 2. Kanıtlanan sınırlar ve varsayımlar

### 2.1 Kaynakla doğrulananlar

- `REQ-004-C`, 5 bağımsız 5 V TTL TX, 5 bağımsız RS-232 TX ve 1 RX ister; “bağımsız” ifadesinin beş eşlenik akış mı yoksa on ayrı akış mı olduğu açık değildir.
- ESP32-S3'te üç donanım UART vardır. UART0 korunduğunda ürün trafiği için iki donanım UART kalır.
- USB D−/D+ GPIO19/20, UART0 GPIO43/44 ayrılmıştır.
- GPIO45, N8 açılışında yükseğe çekilmez; GPIO3 yüksek empedans bırakılmaz.
- GPIO39–42, pad JTAG ile seri kaynaklar arasında koşulludur.
- RMT, dört TX ve dört RX kanallı genel amaçlı dalga çevre birimidir; resmî ESP-IDF'de UART yerine geçen desteklenmiş bir sürücü olarak sunulmaz. Özel encoder ve yük doğrulaması gerekir.
- RS-232 sürücü çıkışları paralellenmez. Aynalama mantık seviyesinde yapılır; her RS-232 konnektörü ayrı sürücü çıkışı alır.

### 2.2 Portfolyo varsayımları

| ID | Varsayım | Etki |
|---|---|---|
| A-CF09-01 | Varsayılan işlev `MIRRORED_5`: beş ayrı mantıksal akış, her `TTL_i`/`RS232_i` çifti aynı payload, baud ve framing'i taşır. | Beş serileştirici kaynağı yeterlidir. |
| A-CF09-02 | Global `1→10` fan-out yapılmaz. | Tek yayın kanalı, müşteri kaynağı olmadan mimari kabul edilmez. |
| A-CF09-03 | Beş mantıksal akış eşzamanlı olabilir. | Zaman çoklamalı mux güvenli ana çözüm değildir. |
| A-CF09-04 | UART0 ürün trafiğine verilmez. | Yerel TX kapasitesi iki kanaldır. |
| A-CF09-05 | Donanımsal RTS/CTS yoktur ve yalnız bir RX vardır. | Aksi bilgi bridge/GPIO bütçesini değiştirir. |
| A-CF09-06 | İki-bridge `INDEPENDENT_10` tabanı için tek RX, bridge A üzerindeki bir TX kanalının RX yönünü ve o kanalın baud/framing ayarını paylaşabilir. | Bu varsayım yanlışsa sekiz haricî TX'e ek dokuzuncu UART kanalı gerekir; quad sınıfında üçüncü bridge/ayrı serializer duyarlılığına geçilir. |
| A-CF09-07 | TTL ve RS-232 bankalarının ayrı güvenli-enable hattı vardır. | İki OE GPIO'su planlanır; exact polarite ve pull ağı bilinmiyor. |
| A-CF09-08 | TFT DNP kalır; TFT pin talebi bütçeye dahil değildir. | TFT eklenirse bütçe yeniden çalıştırılır. |
| A-CF09-09 | RMT üretim kapasite kredisi `0`'dır. | RMT yalnız Ar-Ge A/B testi olabilir. |
| A-CF09-10 | Mod seçimi çalışma sırasında değil, DNP/0 Ω montaj bağlantılarıyla yapılır. | İki kaynağın aynı düğümü sürmesi fiziksel olarak engellenir. |

## 3. Mantıksal ve fiziksel kanal modeli

### 3.1 Serileştirici kaynakları

| Kaynak | Geçici kaynak | `MIRRORED_5` | `INDEPENDENT_10` |
|---|---|---|---|
| `S0` | UART1 TX | mantıksal kanal 0 | TX kanal 0 |
| `S1` | UART2 TX | mantıksal kanal 1 | TX kanal 1 |
| `S2…S5` | quad-UART bridge A | `S2…S4`: mantıksal 2…4; kullanılmayan kanalın RX yönü tek RX adayı | TX kanal 2…5; `S5.RX`, yalnız S5 ile aynı baud/framing kabul edilirse tek RX adayı |
| `S6…S9` | quad-UART bridge B | DNP | TX kanal 6…9 |
| UART0 | GPIO43/44 | programlama/log | programlama/log |

Bir bridge kanalının TX ve RX yönlerinin eşzamanlı çalışması kanıtlanmış sayılmamıştır; `A-CF09-06` bir portfolyo varsayımıdır. Varsayım yalnız tek RX'in `S5.RX` yönünü kullanması ve `S5.TX` ile aynı baud/framing ayarını paylaşması koşulunda geçerlidir. Exact-part veri sayfası ve bench bunu doğrulayacaktır. RX bağımsız konfigürasyon istiyorsa üçüncü bridge/ayrı RX serializer varyantına geçilir; bu varyant aşağıdaki bütçede ayrıca gösterilir.

### 3.2 Konektör tarafı kaynak seçimi

| Mod | TTL çıkışları | RS-232 çıkışları | Fiziksel TX | Mantıksal TX |
|---|---|---|---:|---:|
| `MIRRORED_5` | `TTL_i ← S_i` | `RS232_i ← S_i` | 10 | 5 |
| `INDEPENDENT_10` | `TTL_i ← S_i` | `RS232_i ← S_(i+5)` | 10 | 10 |

Her RS-232 sürücü girişinde `S_i` ile `S_(i+5)` arasında karşılıklı dışlayan montaj seçimi kullanılır. İki kaynağı aynı anda bağlayan popülasyon mümkün olmamalıdır. TTL ve RS-232 fiziksel çıkışlarının her biri kendi sürücü çıkışına sahiptir; fiziksel sürücü çıkışları birbirine bağlanmaz.

## 4. Mimari karşılaştırma

| Seçenek | Mantıksal/fiziksel yapı | ESP32-S3 ve haricî kaynaklar | Yazılım/yük | Boot, hata ve geri dönüş | Sonuç |
|---|---|---|---|---|---|
| `MIRRORED_5`, mantık tarafı fan-out | 5 mantıksal TX → 10 fiziksel TX; 1 RX | 2 yerel UART TX + bridge A'dan 3 TX; RX bridge A adayı | Beş kuyruk; eş çift için tek serileştirme. Haricî bus yalnız 3 TX'i taşır. | En düşük köprü/bus yükü. Bridge B footprint'i korunursa bağımsız moda dönebilir. | **Tercih edilen varsayılan montaj** |
| `MIRRORED_5`, iki serileştiriciye yazılım kopyası | 5 mantıksal akış, 10 serializer | 2 yerel + 8 haricî TX | On kuyruk/yazma; çift başlangıç skew'i ve INDEPENDENT_10 kadar bus yükü | Dinamik eşleme mümkün, ancak gereksiz yük ve yeni hata durumları yaratır. | Ana çözüm değil |
| `INDEPENDENT_10`, hibrit | 10 mantıksal = 10 fiziksel TX; 1 RX | 2 yerel UART TX + iki quad köprüde 8 TX | Yerel/haricî heterojen servis; ortak SPI planlayıcı | İki bridge ortak hata alanı; fakat iki bridge ile kanal sayısı tamamlanır. | **Tercih edilen kapasite üst sınırı** |
| `INDEPENDENT_10`, bağımsız RX | 10 mantıksal TX + bağımsız ayarlı 1 RX | 2 yerel UART TX + iki quad köprüde 8 TX + üçüncü bridge/ayrı RX serializer | RX trafiği, üçüncü CS/IRQ/reset ve ek sürücü | JTAG'li GPIO profiline sığmaz; bus ve fault alanı büyür. | **Duyarlılık / fallback** |
| `INDEPENDENT_10`, tamamen haricî | 10 haricî TX; 1 RX | `3×quad` veya `2×quad + 1×dual` | Homojen TX sürücüsü; en yüksek bus ve bridge sayısı | `3×quad` iki boş bridge kanalıyla bağımsız RX'i taşıyabilir. `2×quad+1×dual` TX slotlarını doldurur; RX bir TX konfigürasyonunu paylaşır veya boş UART1/2'nin RX yönüne gider. | İkincil fallback |
| 2 UART + 3 RMT | 5 mantıksal TX; 1 RX için ek kaynak | Yerel kaynaklar | Özel UART encoder, ISR/ping-pong bellek, parity/framing ve stres doğrulaması | Wi-Fi/USB kesme yükünde underrun/eski sembol riski ölçülmemiştir. | Yalnız laboratuvar A/B testi |
| Yalnız iç çevre birimleriyle `INDEPENDENT_10` | 10 TX gerekir | UART0 ayrılınca 2 UART + en çok 4 RMT TX = 6 | — | Kanal aritmetiğinde yetersiz | `BLOCKED` |
| Zaman çoklamalı UART/mux | 10 fiziksel çıkış, daha az serializer | UART + mux | Kanal değiştirme, guard-time ve yanlış alıcı riski | Eşzamanlılık varsayımında elenir | `BLOCKED` (A-CF09-03) |
| Yardımcı MCU/FPGA | 5 veya 10 serileştirici | İkinci programlanabilir sistem | Framed IPC, ikinci firmware, watchdog ve güncelleme | Ana MCU gerçek zaman yükünü ayırır; sistem karmaşıklığını yükseltir | Birincil işlevsel fallback |

## 5. Veri yolu kapasite modeli

Exact baud, framing, burst, bridge protokolü ve FIFO bilinmediği için aşağıdaki değerler gereksinim değil, **sentetik duyarlılık vektörleridir**.

Kullanılan muhafazakâr planlama denklemi:

```text
uart_byte_rate = external_tx × baud × duty / frame_bits
required_bus_bps = uart_byte_rate × host_bits_per_uart_byte × guard
planning_utilization = required_bus_bps / reference_bus_bps
```

Model varsayımları: `8N1 → 10 frame bit`, `%100 duty`, host üzerinde UART byte başına `16 bit`, `1,25` guard ve en çok `%70` planlama doluluğu. “Host byte başına 16 bit” seçimi bir bridge özelliği değildir; register/adres ve işlem maliyetini yukarıdan sınırlamak için kullanılan portfolyo varsayımıdır. Tablo yalnız TX yükünü modeller; tek RX, status, IRQ, retry ve fault trafiği ayrıca eklenmeden kapasite `PASS` verilemez.

| Mod | Haricî TX | Sentetik baud | Gerekli bus | I²C 1 Mbit/s | SPI 4 Mbit/s | SPI 15 Mbit/s | SPI 26 Mbit/s |
|---|---:|---:|---:|---:|---:|---:|---:|
| `MIRRORED_5` fan-out | 3 | 115.200 | 691.200 bit/s | %69,12 — sınırda | %17,28 | %4,61 | %2,66 |
| `INDEPENDENT_10` hibrit | 8 | 115.200 | 1.843.200 bit/s | %184,32 — elenir | %46,08 | %12,29 | %7,09 |
| `INDEPENDENT_10` hibrit | 8 | 1.000.000 | 16.000.000 bit/s | %1.600 — elenir | %400 — elenir | %106,67 — elenir | %61,54 |
| `INDEPENDENT_10` tamamen haricî | 10 | 115.200 | 2.304.000 bit/s | %230,40 — elenir | %57,60 | %15,36 | %8,86 |
| `INDEPENDENT_10` tamamen haricî | 10 | 1.000.000 | 20.000.000 bit/s | %2.000 — elenir | %500 — elenir | %133,33 — elenir | %76,92 — elenir |

Bu tablo yalnız planlama eleğidir. Bir satırın `%70` altında kalması mimari `PASS` değildir. Gerçek bridge register protokolü, burst sınırı, status/IRQ okumaları, RX, retry, CS aralıkları ve sürücü gecikmesi ölçülmeden kapasite kabul edilmez.

**Yön:** SPI sınıfı, 8 haricî kanallı 115.200 sentetik vektörde boşluk bırakırken I²C 1 Mbit/s bırakmamaktadır. Bu nedenle host bus geçici tercihi SPI'dır. Hız, seçilecek bridge'in resmî sınırını aşamaz.

### 5.1 FIFO servis ufku

Sadece duyarlılık için:

```text
full_fifo_time = fifo_entries × frame_bits / baud
```

64 FIFO karakter girdili sentetik model için tam boşalma süresi 115.200 baud'da `5,556 ms`, 1 Mbaud'da `0,640 ms` olur. Bunlar exact-part özelliği veya kabul eşiği değildir; IRQ ve scheduler testinin ölçeklenmesi için vektördür. Aday tablosundaki `64-byte` ve `128-word` ifadeleri ise üreticilerin resmî terminolojisidir.

## 6. GPIO ve çevrebirim bütçesi

CLAUDE-11'in TFT-hariç tahmininde seri dışı talep 14 GPIO'dur; bunun bir tanesi eski, genel “çıkış OE” kalemidir. Bu model o tek OE'yi çıkarıp TTL ve RS-232 için iki ayrı güvenli-enable hattını CF-09 bütçesinde sayar. Böylece seri dışı sabit taban `13 GPIO` olur.

CF-09 model bileşenleri:

- 2 yerel TX GPIO,
- SPI çekirdeği: SCLK, MOSI, MISO → 3 GPIO,
- bridge başına 1 CS,
- paylaşımlı veya bridge-başına IRQ,
- paylaşımlı veya bridge-başına reset/enable,
- 2 güvenli-output-enable bankası,
- RX bridge üzerinde ise 0, doğrudan MCU'ya giderse 1 GPIO.

| Mod | Bridge | CF-09 GPIO min…max | Toplam GPIO min…max | JTAG yok, 28 pin artık | JTAG ayrılmış, 24 pin artık |
|---|---:|---:|---:|---:|---:|
| `MIRRORED_5` | 1 | 10…11 | 23…24 | 5…4 | 1…0 |
| `HYBRID_5_PLUS_1` | 1 | 10…11 | 23…24 | 5…4 | 1…0 |
| `HYBRID_5_PLUS_2…5` / `INDEPENDENT_10` | 2 | 11…14 | 24…27 | 4…1 | 0…−3 |
| `INDEPENDENT_10_DEDICATED_RX` | 3 | 12…16 | 25…29 | 3…−1 | −1…−5 |
| `INDEPENDENT_10_ALL_EXTERNAL` | 3 | 10…15 | 23…28 | 5…0 | 1…−4 |

Sonuçlar:

- Pad JTAG ayrılmış `INDEPENDENT_10`, yalnız RX bridge üzerinde ve IRQ/reset paylaşımlı en iyi durumda tam sığar; başka düzenlerde 1…3 GPIO eksik kalır.
- RX'in bağımsız baud/framing konfigürasyonu için üçüncü bridge gerekirse JTAG'li profil en iyi durumda bile 1 GPIO eksiktir; JTAG bırakılmış profil de kontrol hatları ayrıştırıldığında 1 GPIO eksik verebilir.
- Tamamen haricî üç-bridge seçenek, JTAG bırakılmış profilde 0…5 GPIO boşluk bırakır. JTAG ayrılmış profilde yalnız paylaşımlı kontrol/en iyi durumda sığar; izole IRQ/reset ve doğrudan MCU RX ile 4 GPIO açığına çıkabilir.
- JTAG bırakılırsa model, TFT hariç 1…4 GPIO boşluk gösterir.
- TFT, ek flow-control, ikinci RX, ayrı fault hatları veya CAN'ın gerçekten popüle edilmesi bütçeyi yeniden açar.
- Bu sayılar pin-freeze değildir. Exact bridge/driver kontrol pinleri bilinmediği için min…max aralığı korunur.

### 6.1 Pin sınıfları

| Sınıf | Pinler | Teknik kullanım |
|---|---|---|
| Ayrılmış | GPIO19/20 | USB D−/D+; CF-09'a verilmez. |
| Ayrılmış | GPIO43/44 | UART0 TX/RX; programlama/log yolu. |
| Boot kısıtlı | GPIO45 | N8'de açılışta yükseğe çekilmez; TX/OE adayı değildir. |
| Boot kısıtlı | GPIO3 | Hi-Z bırakılmaz; TX/OE adayı değildir. |
| Koşullu eFuse | GPIO35–37 | Varsayılan eFuse işlevi doğrulanana kadar final tahsis yapılmaz. |
| Koşullu JTAG | GPIO39–42 | JTAG kararı verilmeden final tahsis yapılmaz. |
| Yerel TX aday havuzu | GPIO21/38/47/48 | Final pin değil; kaynak tabanındaki koşulsuz/glitch görünmeyen adaylar. |

## 7. RMT ve yazılım yükü

RMT'nin dört TX kanalı vardır; UART0 korunduğunda `2 UART + 4 RMT = 6` teorik TX kaynağı elde edilir. Bu, `INDEPENDENT_10` için zaten yetersizdir. `MIRRORED_5` için `2 UART + 3 RMT` aritmetik olarak mümkün görünse de aşağıdakiler kanıtlanmamıştır:

- istenen parity/data/stop kombinasyonları,
- kesme gecikmesi altında sürekli aktarım,
- Wi-Fi/BLE, USB Host ve UART0 log eşzamanlıyken jitter/underrun,
- ilk byte ve reset sonrası idle davranışı,
- CPU, IRAM ve bellek yükü,
- hata bildirim ve recovery semantiği.

Bu nedenle RMT kapasite kredisi `0`; yalnız aynı test vektörleriyle A/B laboratuvar karşılaştırmasına alınır. Espressif'in RMT-as-UART GitHub kaydı standart bir sürücü değil, açık özellik talebidir.

## 8. Boot, idle ve fault-recovery sözleşmesi

### 8.1 Boot/idle

- TTL ve RS-232 fiziksel sürücüler, MCU/bridge konfigürasyonu tamamlanana kadar donanım pull'u ile disabled kalır.
- OE'nin aktif seviyesi ve pull yönü exact driver veri sayfasından seçilir; burada değer veya polarite varsayılmaz.
- UART idle/mark seviyesi GPIO Hi-Z ile temsil edilmez.
- RS-232 sürücüler etkinleştirilmeden önce serileştirici idle durumuna alınır; enable sırası bench'te osiloskopla doğrulanır.
- Auto-shutdown kullanılan bir RS-232 sınıfında wake ve ilk karakter bütünlüğü ayrıca test edilir.
- GPIO3/45 enable kaynağı yapılmaz. GPIO35–37 ve 39–42 koşullu kalır.

### 8.2 Hata toparlanması

- Host sürücüsü “bridge'e kabul edildi” byte ile “fiziksel TX tamamlandı” byte'ı ayırır.
- Kanal çerçeveleri testte kanal ID, artan sequence, timestamp ve CRC taşır.
- FIFO overrun/underrun, IRQ kaybı, SPI timeout, bridge reseti ve bus yeniden başlatma sayaçlanır.
- Tek bridge resetinde diğer bridge ve iki yerel UART'ın davranışı gözlenir; sessiz veri kaybına izin verilmez.
- Şüpheli veya kayıp sequence aralığı üst katmana raporlanır; otomatik retry yalnız hedef protokolün idempotency bilgisi varsa açılır.
- Aynalı çiftin yalnız bir fiziksel tarafı arızalanırsa geçici politika iki çıkışı durdurmak veya açıkça `DEGRADED` bildirmektir; sessiz tek-taraf çalışma kabul edilmez.
- Watchdog/brownout sonrası tüm fiziksel çıkışlar önce güvenli-disable'a döner, sonra bridge/UART state temizlenerek kontrollü yeniden etkinleştirilir.

## 9. Haricî köprü aday sınıfları

Buradaki aileler kapasite ve prototip sınıfını kanıtlamak içindir; exact-part seçimi değildir.

| Aday sınıfı | Resmî kaynakta görülen özellik | Bu mimaride kullanım |
|---|---|---|
| NXP SC16IS752/762 | Çift UART, 64-byte FIFO; varyanta göre I²C/SPI ve farklı SPI üst sınırları | Dual veya karşılaştırma adayı; iki quad yaklaşımının yerine doğrudan seçilmiş değildir. |
| Analog Devices MAX14830 | Quad UART, kanal başına 128-word FIFO; SPI/I²C host | Quad bridge sınıfının uygulanabilirlik referansı. Exact elektrik ve tedarik seçimi açık. |
| Analog Devices MAX3109 | Dual UART, 128-word FIFO; SPI/I²C host | Dual fallback sınıfı. |

Exact bridge için şu kontroller zorunludur: besleme/IO seviyeleri, power-off/back-power, host bus gerçek throughput, FIFO semantiği, IRQ/reset davranışı, UART framing desteği, endüstriyel sıcaklık, yaşam döngüsü ve tedarik, paket/yerleşim ve sürücü lisansı.

## 10. Geri dönüş planı

1. **Varsayılan:** Bridge A popüle, B DNP; `MIRRORED_5` mantık fan-out.
2. **Kapasite yükseltme:** Bridge B ve kaynak-seçim bağlantıları popüle; `INDEPENDENT_10`.
3. **Bağımsız RX:** RX'in S5 ile baud/framing paylaşması kabul edilmiyorsa üçüncü bridge/ayrı RX serializer ekle ve GPIO/bus testini yeniden çalıştır.
4. **GPIO/heterojenlik sorunu:** Yerel TX UART'larını bırakıp `3×quad` veya `2×quad+1×dual` ile tüm TX kanallarını haricî bridge üzerinden homojenleştir. Bağımsız RX bridge fabric'inde kalacaksa `3×quad`; yerel RX kabul edilirse boş UART1/2 kullanılabilir.
5. **SPI/FIFO/IRQ veya recovery testi başarısız:** Aynı `S0…S9` mantıksal sözleşmesini taşıyan yardımcı MCU/FPGA serializer'a geç.
6. **RMT:** Yalnız karşılaştırma prototipinde; bridge çözümünün üretim fallback'i değildir.

## 11. Teknik durum ve gerçek engeller

| Kalem | Durum | Açıklama |
|---|---|---|
| Parametrik kanal/GPIO/bus modeli | `PASS` | Değişmezler ve kaynak GPIO sınıfları yerelde çalıştırıldı. |
| `HYB-REV10` topolojik uygulanabilirlik | `PROVISIONAL / ASSUMPTION` | İki bridge, RX bir TX kanalıyla UART konfigürasyonu paylaşırsa kanal aritmetiğini kapatır; exact-part ve bench yok. |
| `INDEPENDENT_10_DEDICATED_RX` | `PROVISIONAL / CONDITIONAL` | Dokuzuncu haricî UART kanalı nedeniyle üçüncü bridge/ayrı serializer ve yeni GPIO/bus bütçesi gerekir. |
| Yalnız iç kaynakla `INDEPENDENT_10` | `BLOCKED` | UART0 ayrılmış halde en çok 2 UART + 4 RMT TX, on kanala yetmiyor. |
| Haricî bridge SPI/FIFO/IRQ deneyi | `NOT_RUN` | Donanım ve exact candidate henüz seçilmedi. |
| RMT-as-UART A/B prototipi | `NOT_RUN` | Üretim kredisi yok. |
| Exact bridge, level translator, RS-232 driver ve pin-freeze | `UNKNOWN` | Elektriksel ve tedarik doğrulaması gerekir. |

PCB üretiminden önce kapanması gereken gerçek teknik engeller:

- Q-C12: kanal bazlı baud/framing, en yüksek eşzamanlılık, burst/duty ve gecikme/jitter toleransı,
- Q-C13: RX elektrik standardı, kanal ilişkisi, TX ile baud/framing paylaşımı, flow-control, DTE/DCE ve pinout,
- Q-C14: “5 V TTL” giriş/çıkış seviyeleri, yük, kablo, toprak/shield ve izolasyon,
- Q-C15: boot/idle/disable ve fault-recovery davranışı,
- bridge bus/FIFO/IRQ throughput ve hata enjeksiyonu,
- source-select ağında çift sürmeyi fiziksel olarak önleme,
- GPIO35–37 eFuse varsayılanı ve pad JTAG kararı,
- exact bridge/translator/RS-232 transceiver elektrik, power-off ve sıcaklık doğrulaması,
- TFT popülasyonu ve pin talebi.

Bu engeller mimari modellemeyi veya dev-board prototipini durdurmaz; PCB pin-freeze ve üretim için durdurucudur.

## 12. Kaynaklar

### 12.1 Yerel kayıtlar

- `CLAUDE-06-Requirement-Evidence-Matrix.csv` — SHA-256 `32F182FEAB89A09E5A7D2AE8F422053ABF79EA645DB781C1EAE146586637C4CE`
- `CLAUDE-11-RnD-WROOM1-N8-USB-GPIO.md` — SHA-256 `D9D85EF5FFD6402BC227F016E3E917D4DEC6E5005995CD9EEF52A72051051F1B`
- `CLAUDE-11a-WROOM1-N8-Pin-Evidence.csv` — SHA-256 `CF4B739F8663F4696CC788CB1B6C51177B2B02F058DFFE118DB042CCA3358CBE`
- `engineering/gpio-budget-v1.2.csv` — SHA-256 `6E15AE82E2F70677E8F090F811F90644EAD8A9B892A97A440B940E58754C849B`
- `evidence/WF01-CF09-Mimari-Karar-Matrisi-v1.2.xlsx` — SHA-256 `CEC5FA0BE9CA5D80C36936E9C4D1CA964F1807C8CF80BEBBFCEE4018B9CDBA13`

### 12.2 Resmî ve birincil çevrimiçi kaynaklar

- [ESP32-S3 datasheet](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf)
- [ESP-IDF UART API — ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/uart.html)
- [ESP-IDF RMT API — ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/rmt.html)
- [ESP-IDF UHCI/UART DMA](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/uhci.html)
- [ESP32-S3 SoC capability tanımları](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32s3/include/soc/soc_caps.h)
- [Espressif RMT-as-UART özellik talebi #10128](https://github.com/espressif/esp-idf/issues/10128)
- [NXP SC16IS752/SC16IS762 datasheet](https://www.nxp.com/docs/en/data-sheet/SC16IS752_SC16IS762.pdf)
- [Analog Devices MAX14830](https://www.analog.com/en/products/max14830.html)
- [Analog Devices MAX3109](https://www.analog.com/en/products/max3109.html)

## 13. Sıradaki somut teknik adım

Bir quad-UART evaluation board seçilip `UART1 + UART2 + bridge A` ile `MIRRORED_5` mantık-seviyesi prototipi kurulacak. Ardından aynı scheduler'a bridge B eklenerek `INDEPENDENT_10` sentetik yükü ve TX ile aynı/ayrı UART konfigürasyonlu RX varyantı çalıştırılacak. Test, `tests/cf09-architecture-validation-plan-v1.2.md` içindeki sequence/CRC, bus trace, boot ve hata enjeksiyonu ölçümlerini kullanacaktır.
