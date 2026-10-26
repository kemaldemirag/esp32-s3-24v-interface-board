# CF-09 seri port envanteri

**Durum:** `PROVISIONAL / PORTFOLIO_MODEL_ONLY`

**Gereksinim:** `REQ-004-C`

**Amaç:** On bir fiziksel portun kimliğini ve henüz kaynaklanmamış değer alanlarını tek yerde görünür tutmak.

**İddia sınırı:** Bu envanter exact parça seçimi, pin dondurma, elektriksel uygunluk veya donanım doğrulaması değildir.

## Port iskeleti

`UNKNOWN` alanlar müşteri veya exact-part kanıtı olmadan doldurulmaz. Geçici yönlendirme modeli ayrı, makine-okunur `engineering/cf09-serial-port-inventory-v1.0.json` dosyasındadır ve yalnız mevcut CF-09 modelinin tutarlılığını temsil eder.

| Port ID | Yön | REQ bağlantısı | Elektriksel değerler | Baud / framing / eşzamanlılık | Konnektör / pinout | Exact sürücü / alıcı | İlgili açık soru |
|---|---|---|---|---|---|---|---|
| TTL0 | TX | REQ-004-C | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | Q-C07, Q-C07-MAP, Q-C12, Q-C14, Q-C15 |
| TTL1 | TX | REQ-004-C | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | Q-C07, Q-C07-MAP, Q-C12, Q-C14, Q-C15 |
| TTL2 | TX | REQ-004-C | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | Q-C07, Q-C07-MAP, Q-C12, Q-C14, Q-C15 |
| TTL3 | TX | REQ-004-C | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | Q-C07, Q-C07-MAP, Q-C12, Q-C14, Q-C15 |
| TTL4 | TX | REQ-004-C | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | Q-C07, Q-C07-MAP, Q-C12, Q-C14, Q-C15 |
| RS2320 | TX | REQ-004-C | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | Q-C07, Q-C07-MAP, Q-C12, Q-C13, Q-C15 |
| RS2321 | TX | REQ-004-C | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | Q-C07, Q-C07-MAP, Q-C12, Q-C13, Q-C15 |
| RS2322 | TX | REQ-004-C | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | Q-C07, Q-C07-MAP, Q-C12, Q-C13, Q-C15 |
| RS2323 | TX | REQ-004-C | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | Q-C07, Q-C07-MAP, Q-C12, Q-C13, Q-C15 |
| RS2324 | TX | REQ-004-C | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | Q-C07, Q-C07-MAP, Q-C12, Q-C13, Q-C15 |
| RX0 | RX | REQ-004-C | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | `UNKNOWN` | Q-C12, Q-C13, Q-C15 |

## Korunan teknik sınırlar

- USB D−/D+ için GPIO19/20 ve programlama, kurtarma ve log için UART0 GPIO43/44 bu envanterin port kaynağı değildir.
- GPIO45 N8 açılışında yükseğe çekilmez; GPIO3 güvenli durum olarak yüksek empedansta bırakılmaz.
- GPIO39–42 pad JTAG kararı verilmeden son tahsise alınmaz.
- RMT üretim kapasite kredisi `0` kalır; yalnız ayrı prototip kanıtıyla yeniden değerlendirilebilir.
- RS-232 sürücü çıkışları paralellenmez. `MIRRORED_5` seçilirse aynalama sürücülerin mantık tarafında yapılır.
- `MIRRORED_5`, `INDEPENDENT_10` veya hibrit eşleme müşteri gereksinimi olarak kapanmış değildir; Q-C07 ve Q-C07-MAP açık kalır.

## Kapanma ölçütü

Bir satırdaki değer ancak yetkili müşteri girdisi veya exact üretici dokümanı; belge revizyonu, locator ve kanıt sınıfıyla birlikte kaydedildiğinde `UNKNOWN` durumundan çıkar. Fiziksel portun boot, aktif, reset, brownout ve power-off davranışı bench kanıtı olmadan `PASS` olamaz.
