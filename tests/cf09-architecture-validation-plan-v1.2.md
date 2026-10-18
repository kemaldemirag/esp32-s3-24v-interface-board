# CF-09 mimari doğrulama planı

**Sürüm:** v1.2  
**Tarih:** 18.10.2026  
**Hedef:** `HYB-REV10` mimarisini model, mantık-seviyesi prototip, veri yolu yükü, boot/idle, hata toparlanması ve fiziksel katman açısından doğrulamak  
**Durum:** model testleri `PASS`; donanım testleri `NOT_RUN`  
**Kapsam dışı:** yönetim/gate onayı, PCB üretim serbest bırakma ve müşteri kabul testi

## 1. Test edilen mimari

```text
S0 = UART1 TX
S1 = UART2 TX
S2…S5 = quad-UART bridge A
S6…S9 = quad-UART bridge B

MIRRORED_5:
  TTL_i   ← S_i
  RS232_i ← S_i
  bridge B DNP olabilir

INDEPENDENT_10:
  TTL_i   ← S_i
  RS232_i ← S_(i+5)
  bridge A+B popüle
```

UART0/GPIO43–44 programlama/log için; GPIO19/20 USB için ayrılmış kalır. Tek RX için ilk aday bridge A üzerindeki bir TX kanalının RX tarafıdır; bu iki-bridge düzeni RX'in aynı kanalın baud/framing ayarını paylaşabildiği `ASSUMPTION`'ına bağlıdır. Bağımsız RX konfigürasyonu dokuzuncu haricî UART kanalı ve quad sınıfında üçüncü bridge/ayrı serializer duyarlılığı gerektirir. Exact bridge, TTL translator, RS-232 transceiver, konektör ve pin-freeze `UNKNOWN` durumundadır.

## 2. Test ilkeleri

| ID | Sınıf | Kural |
|---|---|---|
| IR-CF09-01 | `INTERNAL_RULE` | Sistem sessiz byte kaybı, tekrar veya sıra değişimi üretmez. Şüpheli/kayıp aralık açıkça raporlanır. |
| IR-CF09-02 | `INTERNAL_RULE` | “Bridge'e yazıldı” ve “fiziksel TX tamamlandı” durumları ayrı izlenir. |
| IR-CF09-03 | `INTERNAL_RULE` | Boot/reset/brownout sırasında fiziksel çıkışlar güvenli-disable'da kalır; GPIO Hi-Z güvenli idle sayılmaz. |
| IR-CF09-04 | `INTERNAL_RULE` | Bir RS-232 sürücü girişine iki serileştirici kaynağının aynı anda bağlanması montajla engellenir. |
| A-CF09-T01 | `ASSUMPTION` | İlk sentetik çerçeve 8N1'dir; 9.600, 115.200 ve 1.000.000 baud yalnız duyarlılık noktalarıdır, müşteri gereksinimi değildir. |
| A-CF09-T02 | `ASSUMPTION` | Trafik vektörü kanal ID, artan sequence, timestamp, payload ve CRC içerir. |
| A-CF09-T03 | `ASSUMPTION` | `0x55/0xAA`, `0x00/0xFF`, PRBS ve farklı burst uzunlukları elektriksel/zamansal stres vektörleridir. |
| A-CF09-T04 | `ASSUMPTION` | Beş MIRRORED mantıksal kanal aynı anda aktif olabilir. INDEPENDENT testinde on kanal eşzamanlı denenir. |
| A-CF09-T05 | `ASSUMPTION` | İki-bridge INDEPENDENT düzeninde tek RX, bir TX kanalının baud/framing konfigürasyonunu paylaşabilir; aksi durumda üçüncü bridge/ayrı RX serializer denenir. |

Müşteri tarafından gecikme, jitter, kablo veya hata oranı eşiği verilmediği için bu niceliklerde uydurma `PASS` eşiği kullanılmaz. Ham dağılım, en kötü gözlem ve test koşulu raporlanır; üretim eşiği Q-C12…Q-C15 ve exact-part kaynaklarıyla doldurulur.

## 3. Model doğrulaması — çalıştırıldı

Model, yerel `engineering/gpio-budget-v1.2.csv` kaynağını okuyup aşağıdaki değişmezleri denetledi.

| Test | Yöntem | Beklenen | Sonuç |
|---|---|---|---|
| M-CF09-01 | GPIO kaynağını ayrıştır; 41 veri satırı ve 36 dışarı açık GPIO say. | Kaynak bütünlüğü korunur. | `PASS` |
| M-CF09-02 | GPIO19/20, 43/44, 3, 45 ve 39–42 sınıflarını kaynakla karşılaştır. | USB/UART0/boot/JTAG sınıfları bire bir eşleşir. | `PASS` |
| M-CF09-03 | `HYBRID_5_PLUS_K`, `K=0…5` kanal ve bridge aritmetiğini üret. | Mantıksal TX monoton 5→10; iki yerel TX ile bridge sayısı 1→2. | `PASS` |
| M-CF09-04 | `INDEPENDENT_10` için `2 yerel + 8 haricî` kanal denetimi. | İki quad-UART sınıfı bridge gerekir. | `PASS` |
| M-CF09-05 | Pad JTAG ayrılmış/ayrılmamış GPIO sweep'i. | JTAG'li en iyi durumda tam sığar; kontrol/RX ayrıştırmasına göre en kötü durumda 3 pin açığı verir. | `PASS` |
| M-CF09-06 | Sentetik bus duyarlılığı. | 8 haricî kanal × 115.200 vektörü I²C 1 Mbit/s planlama limitini aşar, SPI 4 Mbit/s altında kalır. | `PASS` |
| M-CF09-07 | RMT üretim kapasite kredisi. | `0`; yalnız karşılaştırma testi. | `PASS` |
| M-CF09-08 | `INDEPENDENT_10_DEDICATED_RX` duyarlılığı. | 8 haricî TX + bağımsız 1 RX = 9 haricî UART kanalı; quad sınıfında 3 bridge, CF-09 GPIO 12…16. | `PASS` |
| M-CF09-09 | `INDEPENDENT_10_ALL_EXTERNAL` duyarlılığı. | 10 haricî TX; `3×quad` veya `2×quad+1×dual`; CF-09 GPIO 10…15. | `PASS` |

### 3.1 Yeniden üretilebilir model denklemleri

```text
uart_byte_rate = external_tx × baud × duty / frame_bits
required_bus_bps = uart_byte_rate × host_bits_per_uart_byte × guard
utilization = required_bus_bps / reference_bus_bps
full_fifo_time = fifo_entries × frame_bits / baud
```

Kullanılan model girdileri `ASSUMPTION / MODEL_ONLY`: 10 frame bit, `%100` duty, UART byte başına 16 host bit, `1,25` guard, `%70` planlama doluluğu, 64/128 FIFO karakter girdisi duyarlılık noktaları. Sayısal bus tablosu yalnız TX yükünü içerir; RX/status/IRQ/retry/fault trafiği ölçülmeden kapasite `PASS` verilmez. Bunlar aday cihazların resmî `64-byte` veya `128-word` FIFO terimlerinin yerine geçen parça özellikleri değil, ortak servis-ufku modelidir.

Kontrol vektörleri:

| Mod | Haricî TX | Baud | Gerekli bus | Referans bus | Kullanım | Model sonucu |
|---|---:|---:|---:|---:|---:|---|
| `MIRRORED_5` | 3 | 115.200 | 691.200 bit/s | 1 Mbit/s | %69,12 | planlama eleğini geçer, kapasite `PASS` değildir |
| `INDEPENDENT_10` | 8 | 115.200 | 1.843.200 bit/s | 1 Mbit/s | %184,32 | elenir |
| `INDEPENDENT_10` | 8 | 115.200 | 1.843.200 bit/s | 4 Mbit/s | %46,08 | bench adayı |
| `INDEPENDENT_10` | 8 | 1.000.000 | 16.000.000 bit/s | 15 Mbit/s | %106,67 | elenir |
| `INDEPENDENT_10` | 8 | 1.000.000 | 16.000.000 bit/s | 26 Mbit/s | %61,54 | yalnız destekleyen exact-part için bench adayı |
| `INDEPENDENT_10_ALL_EXTERNAL` | 10 | 115.200 | 2.304.000 bit/s | 4 Mbit/s | %57,60 | bench adayı; RX/status/fault yükü hariç |
| `INDEPENDENT_10_ALL_EXTERNAL` | 10 | 1.000.000 | 20.000.000 bit/s | 26 Mbit/s | %76,92 | planlama eleğinde elenir |

Bu vektörler bir bridge'in gerçek SPI verimi değildir. Bir model satırı yalnız seçilen exact-part resmî olarak ilgili host hızını destekliyorsa uygulanır.

## 4. Gerekli prototip düzeneği

İlk donanım adımı için:

- ESP32-S3-WROOM-1-N8 tabanlı geliştirme kartı,
- resmî veri sayfası ve sürücüsü bulunan bir quad-UART evaluation board,
- ikinci aşama için aynı bridge sınıfından ikinci board,
- RX'in bağımsız baud/framing gereksinimi için üçüncü bridge veya ayrı RX serializer deneyi,
- tamamen haricî karşılaştırma için `3×quad` veya `2×quad+1×dual` eşdeğer kanal düzeneği,
- mantık analizörü ve yeterli kanallı UART alıcı/fikstürü,
- osiloskop,
- TTL translator/buffer ve RS-232 driver candidate evaluation/devre düzeneği,
- programlanabilir besleme veya kontrollü power-cycle/reset/brownout düzeneği,
- SPI hattında kesme/CS/clock ve bridge reseti enjekte edebilen test bağlantıları.

Exact parça seçilmeden doğrudan PCB layout veya gerilim eşiği yazılmaz.

## 5. Test firmware sözleşmesi

Her test frame'i en az şu mantıksal alanları taşır:

```text
mode | logical_channel | sequence | sender_timestamp | payload_kind | payload | crc
```

Firmware sayaçları:

- enqueue edilen, bridge'e kabul edilen ve TX-complete görülen byte/frame,
- kanal bazında queue high-water mark,
- SPI transaction sayısı/süresi ve retry,
- IRQ servis gecikmesi,
- FIFO overrun/underrun,
- bus timeout, bridge reset/re-init ve watchdog,
- alıcıda kayıp, tekrar, sıra hatası ve CRC hatası,
- boot/reset boyunca enable state geçişleri.

Ölçüm firmware'i ürün protokolü değildir. Test protokolü kaldırıldığında aynı hata sayaçları ürün telemetrisinde korunmalıdır.

## 6. Test matrisi

### 6.1 Scheduler ve kanal eşleme

| ID | Kurulum ve uyarım | Ölçüm | Geçme kuralı | Başlangıç durumu |
|---|---|---|---|---|
| SW-CF09-01 | UART1/2 + bridge A; beş mantıksal kanalı `MIRRORED_5` eşlemesiyle eşzamanlı sür. | Her TTL/RS-232 çiftinde sequence/CRC, başlangıç farkı ve gap dağılımı. | Her çift aynı payload/sıra iletir; sessiz kayıp/tekrar yok. Skew ham dağılımı raporlanır; müşteri eşiği `UNKNOWN`. | `NOT_RUN` |
| SW-CF09-02 | Bridge B ekle; on mantıksal kanalı `INDEPENDENT_10` eşlemesiyle sür. | Kanal izolasyonu, sequence/CRC, queue high-water. | Her fiziksel TX yalnız atanmış `S0…S9` kaynağını iletir; kayıp/tekrar/sıra hatası yok. | `NOT_RUN` |
| SW-CF09-03 | MIRRORED ve INDEPENDENT DNP/0 Ω popülasyonlarını continuity ile denetle. | Kaynak-seçim netleri. | Aynı sürücü girişine iki kaynak bağlanamaz. | `NOT_RUN` |
| SW-CF09-04 | Bridge A üzerindeki aday kanalda eşzamanlı TX ve tek RX çalıştır. | RX framing/CRC, TX etkilenmesi, IRQ yükü. | Veri bütünlüğü korunur; exact-part full-duplex davranışı doğrulanır. | `NOT_RUN` |
| SW-CF09-05 | RX'i önce S5 ile aynı, sonra farklı baud/framing gereksinimiyle çalıştır; farklı ayar desteklenmiyorsa üçüncü bridge/ayrı RX serializer ekle. | Konfigürasyon bağımsızlığı, ek CS/IRQ/reset, bus ve GPIO etkisi. | İki-bridge varsayımının geçerli sınırı ölçülür; bağımsız RX gerekiyorsa dokuzuncu haricî kanal açıkça devreye alınır. | `NOT_RUN` |

### 6.2 Bus, FIFO ve sistem yükü

| ID | Kurulum ve uyarım | Ölçüm | Geçme kuralı | Başlangıç durumu |
|---|---|---|---|---|
| BUS-CF09-01 | Candidate'ın desteklediği baud/bus noktalarında 1→5→10 kanal sweep'i. | Gerçek bus kullanım oranı, transaction süresi, FIFO seviyesi, CPU yükü. | Sessiz veri hatası yok; candidate'ın resmî sınırları aşılmaz; ölçülen margin raporlanır. | `NOT_RUN` |
| BUS-CF09-02 | 1, 4, 16, 64 ve candidate FIFO/burst sınırına uygun burst sweep'i. | Transaction/byte, latency ve queue high-water. | En güvenli burst çalışma bölgesi ölçümle seçilir; taşma varsa sayaç ve recovery görünür. | `NOT_RUN` |
| BUS-CF09-03 | USB Host, Wi-Fi ve UART0 log aynı anda aktifken `0x55/AA`, `0x00/FF` ve PRBS yükü. | Kayıp/tekrar/CRC, IRQ gecikmesi, CPU ve queue. | IR-CF09-01 sağlanır; performans dağılımı kaydedilir. | `NOT_RUN` |
| BUS-CF09-04 | BLE gerekiyorsa UART DMA/UHCI ile Bluetooth HCI paylaşımını ayrı konfigürasyonlarda dene. | DMA kullanılabilirliği, CPU ve veri bütünlüğü. | Paylaşım çakışması yok veya DMA'sız güvenli profil belgelenir. | `NOT_RUN` |
| BUS-CF09-05 | SPI hızını candidate'ın izin verdiği aralıkta düşür/yükselt. | Hata ve throughput eğrisi. | Kullanılabilir çalışma bölgesi ölçülür; model değeri gerçek kapasite sanılmaz. | `NOT_RUN` |

### 6.3 Boot, reset, idle ve power sırası

| ID | Kurulum ve uyarım | Ölçüm | Geçme kuralı | Başlangıç durumu |
|---|---|---|---|---|
| BOOT-CF09-01 | MCU ve bridge'i birlikte, MCU önce ve bridge önce besle. | TTL/RS-232 enable, TX dalga şekli ve alıcıda geçerli frame görülmesi. | Firmware hazır olana kadar sürücüler disabled; istenmeyen geçerli frame yok. Gerilim seviyeleri candidate datasheet ile karşılaştırılır. | `NOT_RUN` |
| BOOT-CF09-02 | EN reseti, watchdog reseti, software reset ve tekrarlı power-cycle. | OE, idle, ilk frame bütünlüğü. | Güvenli-disable → konfigürasyon → idle → enable sırası her çevrimde korunur. | `NOT_RUN` |
| BOOT-CF09-03 | Kontrollü brownout/gerilim rampası. | Output enable ve back-power akımı/davranışı. | Candidate abs-max ve power-off şartları ihlal edilmez; istenmeyen frame yok. Numeric sınır exact-part kaynağından alınır. | `NOT_RUN` |
| BOOT-CF09-04 | RS-232 auto-shutdown varsa uzun idle sonrası ilk frame. | Wake gecikmesi ve ilk karakter. | İlk frame kaybı/bozulması yok veya üst katman preamble/retry gereği açıkça belirlenir. | `NOT_RUN` |

### 6.4 Hata enjeksiyonu ve recovery

| ID | Enjeksiyon | Ölçüm | Geçme kuralı | Başlangıç durumu |
|---|---|---|---|---|
| FAIL-CF09-01 | SPI işlemi sırasında CS'yi kes veya clock'u durdur. | Timeout, reset/re-init, sequence aralığı. | Kilitlenme yok; şüpheli aralık raporlanır; sessiz devam yok. | `NOT_RUN` |
| FAIL-CF09-02 | IRQ'yu maskele/geciktir; FIFO taşmasını zorla. | Overrun sayacı, recovery ve diğer kanallar. | Overrun görünür; kayıp aralığı raporlanır; diğer kanalların sonucu ölçülür. | `NOT_RUN` |
| FAIL-CF09-03 | Yalnız bridge A veya B'yi resetle/cevapsız bırak. | Yerel UART ve diğer bridge sürekliliği, yeniden keşif. | Arıza alanı tanımlı kalır; sistem tüm kanallarda sessiz veri kaybına girmez. | `NOT_RUN` |
| FAIL-CF09-04 | Aynalı çiftin TTL veya RS-232 fiziksel tarafını ayır/kısa devreyi güvenli fikstürle simüle et. | Eş taraf ve fault bildirimi. | Diğer sürücü zarar görmez; sistem iki tarafı durdurur veya `DEGRADED` bildirir. | `NOT_RUN` |
| FAIL-CF09-05 | Kuyruk taşması ve scheduler gecikmesi oluştur. | Drop policy ve sequence raporu. | Drop sessiz değildir; seçilen backpressure/drop politikası deterministiktir. | `NOT_RUN` |

### 6.5 Fiziksel katman

| ID | Kurulum ve uyarım | Ölçüm | Geçme kuralı | Başlangıç durumu |
|---|---|---|---|---|
| ELEC-CF09-01 | TTL çıkışı candidate translator/buffer, tanımlı yük ve kabloyla sür. | VOH/VOL, rise/fall, overshoot, akım, idle ve power-off. | Candidate ve müşteri kaynaklı limitler karşılanır; limitler gelene kadar yalnız ham sonuç, durum `UNKNOWN`. | `NOT_RUN` |
| ELEC-CF09-02 | Her RS-232 konnektörünü ayrı DOUT ile test et. | Sürücü seviyesi, slew, yük/kablo etkisi, idle ve shutdown. | Candidate datasheet ve müşteri kablo/yük şartları karşılanır; kaynak gelmeden numeric PASS verilmez. | `NOT_RUN` |
| ELEC-CF09-03 | Tek RX yolunu gerçek elektrik standardı ve DTE/DCE yönüyle test et. | Seviye, framing, noise ve power-off. | Q-C13 kapandıktan sonra kaynak limitlerine göre değerlendirilir. | `NOT_RUN` |
| ELEC-CF09-04 | Kaynak seçimi, bridge ve sürücülerde power-off/back-power deneyi. | Besleme rayları ve pin akımları. | Exact-part abs-max/power-off şartları ihlal edilmez. | `NOT_RUN` |

### 6.6 RMT karşılaştırması

| ID | Kurulum ve uyarım | Ölçüm | Geçme kuralı | Başlangıç durumu |
|---|---|---|---|---|
| RMT-CF09-01 | `2 UART + 3 RMT` ile MIRRORED_5; aynı test frame'leri. | Jitter, underrun, CPU/IRAM, ilk byte ve hata görünürlüğü. | Yalnız ölçüm; üretim seçimi otomatik değildir. | `NOT_RUN` |
| RMT-CF09-02 | Wi-Fi/USB/log stresi, cache-disabled bölümler ve parity/framing varyantları. | Eski sembol tekrarı, gap, framing ve scheduler yükü. | Her hata görünür. Herhangi bir sessiz tekrar/underrun RMT'yi üretim adaylığından çıkarır. | `NOT_RUN` |

## 7. Karar ve fallback tetikleyicileri

| Bulgular | Teknik yön |
|---|---|
| Bridge A ile MIRRORED_5 tüm zorunlu testleri geçer; bridge B ile bağımsız yük de geçer. | `HYB-REV10` devam; exact-part ve pin-freeze aşamasına geç. |
| Yerel UART pinlerinin boot/GPIO düzeni veya heterojen scheduler kabul edilemez. | Üç haricî bridge ile tamamen haricî, homojen SPI-UART topolojisini dene. |
| SPI/FIFO/IRQ veya bridge recovery testleri güvenilir değil. | `S0…S9` sözleşmesini koruyan yardımcı MCU/FPGA serializer prototipine geç. |
| Müşteri yalnız MIRRORED_5'i doğrular ve bağımsız yükseltme gerekmiyorsa. | Bridge B DNP; footprint'in tutulması maliyet/alan doğrulamasına bağlı. |
| Müşteri on ayrı akış, flow-control veya ek RX ister. | Bridge ve GPIO bütçesini yeni sözleşmeyle yeniden çalıştır; mevcut aralık geçersizleşebilir. |
| Tek RX, herhangi bir TX ile baud/framing paylaşamıyor. | `INDEPENDENT_10_DEDICATED_RX`: üçüncü bridge/ayrı RX serializer; GPIO 12…16 ve RX bus trafiğiyle modeli yeniden çalıştır. Tamamen haricî `2×quad+1×dual` kullanılırsa boş UART1/2 RX yönü de adaydır. |
| RMT testinde sessiz hata, yük hassasiyeti veya framing açığı görülür. | RMT üretim adaylığından çıkar; bridge tabanı korunur. |

## 8. PCB öncesi zorunlu teknik kapanış

Aşağıdaki maddeler yönetim imzası değil, geri dönüşü pahalı donanım adımından önce teknik kanıttır:

- Q-C12…Q-C15 için gerçek baud/framing/eşzamanlılık, RX standardı ve TX ile konfigürasyon paylaşımı, TTL/RS-232 elektrik ve boot/fault davranışı,
- en az bir exact quad-UART bridge üzerinde BUS-CF09-01…05 ve FAIL-CF09-01…03,
- TTL ve RS-232 exact driver adaylarında BOOT-CF09-01…04 ve ELEC-CF09-01…04,
- source-select ağında iki kaynağın aynı düğümü süremediğinin şema/continuity kanıtı,
- GPIO35–37 eFuse varsayılanı, JTAG kararı ve final pin eşlemesi,
- TFT/flow-control/ek RX kapsamının netleşmesi,
- güç, termal ve EMC bütçesinde seçilen bridge/driver etkisi.

## 9. Çalıştırma sırası

1. Exact quad-UART evaluation board adayını resmî kaynakla seç.
2. `SW-CF09-01`, `BUS-CF09-01…03` ve `BOOT-CF09-01…02` ile tek bridge/MIRRORED_5 prototipi.
3. Bridge B ekleyip `SW-CF09-02`, tam bus sweep ve fault injection.
4. `SW-CF09-04…05` ile RX'in aynı/ayrı UART konfigürasyonu varyantını çalıştır; gerekirse üçüncü bridge/ayrı RX serializer ekle.
5. TTL/RS-232 fiziksel driver adaylarını ekleyip boot, power-off ve kablo/yük testleri.
6. Yalnız karşılaştırma değeri varsa RMT A/B testi.
7. Sonuçlara göre hibrit, tamamen haricî veya yardımcı MCU/FPGA fallback'ini seç.

İlk somut adım: bir quad-UART evaluation board ile `UART1 + UART2 + bridge A` scheduler prototipini çalıştırmak ve ilk beş kanal için sequence/CRC ile gerçek SPI transaction izini toplamaktır.
