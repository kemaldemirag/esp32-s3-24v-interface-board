# WF01-D1 Açık Kararlar ve Bağımsız İnceleme Soruları

**Ürün:** ESP32-S3 Interface Architecture Decision Workbench  
**Aşama:** WF01-D1  
**Sürüm:** 1.0.0-draft  
**Tarih:** 2026-10-17

## 1. D1'de kabul edilen tasarım kararları

| Karar | Kabul edilen yön | Sınır |
|---|---|---|
| `AD-D1-001` | Müşteri ve mühendislik değerleri `KNOWN`, `UNKNOWN`, `NOT_APPLICABLE` veya `CONFLICT` durumuyla taşınır | Bilinmeyen sayı, akım, eşik, pin, zaman veya parça için varsayılan üretilmez |
| `AD-D1-002` | İlk registry tam olarak görev emrindeki sekiz kuralı içerir | Yeni kural ancak sürümlü incelemeyle eklenir |
| `AD-D1-003` | Kaynaktaki 31 fiziksel satır, Q-C04'ün iki dalı birleştirilerek kayıpsız 30 mantıksal senaryo yapılır | Kaynak satırlarının izi `source_scenario_ids` alanında korunur |
| `AD-D1-004` | MVP: `SCN-USB-003`, `SCN-USB-002`, `SCN-SER-003`, `SCN-TFT-005` | Diğer 26 senaryo backlog'dur; reddedilmiş sayılmaz |
| `AD-D1-005` | PASS yalnız kural veya açıkça tanımlı senaryo koşulu kapsamındadır | PASS, G1, kart veya ürün onayı değildir |
| `AD-D1-006` | Koşullar kapalı, bildirimsel `wf01-expression-v1` ile ifade edilir | Registry içinden keyfi kod çalıştırılmaz |
| `AD-D1-007` | REP-01 arayüz/GPIO/USB/seri/TFT kararlarını; REP-02 rail bütçesi, derating, kararlılık, sıralama ve termal analizi sahiplenir | REP-01 içinde genel güç motoru yeniden geliştirilmez |
| `AD-D1-008` | D2 ancak D1 insan incelemesi ve açık onayı sonrasında koşullu olarak başlayabilir | Bu teslim D2'yi otomatik başlatmaz |

## 2. Açık karar kaydı

| ID | Açık karar / soru | Mevcut durum | D1 etkisi | Kapatma kanıtı ve sahibi | Sonraki adım |
|---|---|---|---|---|---|
| `OD-D1-001` | Müşteri hedef MCU/modül varyantı ve silicon revision nedir? N8 kesin mi? | `UNKNOWN` | N8'e özel GPIO45 ve modül profili kuralları koşullu kalır | Onaylı müşteri konfigürasyonu; Sistem Mimarı / Müşteri | Configuration fixture'ına kanıt bağlı değer gir |
| `OD-D1-002` | Q-C11: TFT konnektörü gerekli mi; arayüzü, sinyal seti, touch ve PSRAM ihtiyacı nedir? | `BLOCKED` | `SCN-TFT-005` ve RULE-TFT-001 PASS olamaz; GPIO bütçesi kapanmaz | Panel/konnektör gereksinimi ve onaylı sinyal tablosu; Sistem Mimarı / Müşteri | Q-C11 karar kaydı üret; bilinmiyorsa `UNKNOWN` olarak koru |
| `OD-D1-003` | GPIO39-42 pad JTAG için ayrılacak mı, yoksa genel GPIO/TFT için kullanılacak mı? | `BLOCKED` | Pad JTAG rezervasyonu source-profile GPIO bütçesini 25/24 ihlaline taşıyabilir | Debug/bring-up politikası, üretim erişimi ve pin bütçesi; Firmware + Hardware Lead | Host sırasında canlı JTAG gereksinimini karara bağla |
| `OD-D1-004` | `USB_PHY_SEL` eFuse stratejisi nedir? | `OPEN`; araştırma önerisi `DO_NOT_BURN` | Boot rolü, kurtarma yolu ve geri döndürülemez üretim işlemi etkilenir | Silicon/date-code kapsamı, boot gereksinimi, kurtarma ve üretim onayı; Firmware + Manufacturing | Varsayılan üretme. C11-012/015 yönünde `DO_NOT_BURN` önerisini bağımsız incele; burn ancak açık onayla |
| `OD-D1-005` | CF-09: Beş TTL ve beş RS-232 çıkış on bağımsız akış mı, beş aynalı çift mi? | `BLOCKED` | RULE-SERIAL-001 talebi ve `SCN-SER-003` sonucu kapanmaz | Kanal-topoloji, baud ve concurrency gereksinimi; Sistem Mimarı / Müşteri | Topolojiyi ve bağımsız stream sayısını kanıtlı olarak belirle |
| `OD-D1-006` | On bağımsız TX gerekiyorsa haricî UART bridge, yardımcı MCU veya başka doğrulanmış serializer kapasitesi hangisidir? | `BLOCKED` | Araştırma düzeyi RMT/soft-UART kapasitesi PASS'e katılamaz | Exact architecture, kapasite, sürücü, stres testi ve kaynak kanıtı; Firmware + Hardware Lead | Seçenek prototipi ve stres testi için ayrı görev aç |
| `OD-D1-007` | Q-C04: USB VBUS seçili cihaz profili mi, genel FS host profili mi; akım ve inrush nedir? | `UNKNOWN` | REP-02 ray isteği ve USB güç sözleşmesi kapanmaz | Müşteri yük profili ve VBUS anahtarlama/koruma kararı | `SCN-VBUS-001` dalını ve ölçütleri belirle |
| `OD-D1-008` | `SCN-USB-002` 25/24 GPIO karşılaştırması D2'de typed scenario assertion mı olmalı, yoksa sonraki registry sürümünde genel `RULE-GPIO-004` mü eklenmeli? | `OPEN` | Mantığın rapor koduna gizlenmemesi gerekir; sekiz kural şartı korunur | D1 sözleşme incelemesi; Ürün Mimarı | D2 başlamadan bir yaklaşımı onayla; ilk registry'yi sessizce değiştirme |
| `OD-D1-009` | `wf01-expression-v1` için kesin operatör sözlüğü, adım/derinlik limitleri ve referans uygulama semantiği nedir? | `OPEN` | Farklı implementasyonların aynı girdiye farklı sonuç verme riski | Sürüm kontrollü DSL spesifikasyonu ve conformance fixture'ları; Software Lead | D2'nin ilk teknik tasarım incelemesinde dondur |
| `OD-D1-010` | JSON canonicalization RFC 8785 mi, eşdeğer özel profil mi kullanacak? | `OPEN` | Deterministik SHA-256 ve çapraz dil uyumluluğu etkilenir | Canonical byte örnekleri ve iki bağımsız implementasyon testi; Software Lead | RFC 8785'i tercih edilen yön olarak incele |
| `OD-D1-011` | `CONTENT_READ_HASH_UNVERIFIED` upstream kaynak, kural PASS'i için hangi koşullarda yeterli sayılabilir? | `BLOCKED` | CLAUDE-11'in yerel kaydı hashli olsa da upstream WROOM-1 içeriğinin hash zinciri doğrulanmış değil | İndirilebilir upstream dosya, SHA-256, sürüm ve locator; Evidence Owner | Kaynağı yerelde sabitle veya kural sonucunu BLOCKED tut |
| `OD-D1-012` | Evidence catalog ID/locator biçimi ve claim-level granularity nasıl standardize edilecek? | `OPEN` | Farklı kaynaklarda `C11-012`, satır ve dosya referansları karışabilir | Kimlik sözlüğü ve migration kuralı; Evidence Owner | D2 fixture'larından önce tek format seç |
| `OD-D1-013` | REP-02 request/response şemaları diğer repo tarafından kabul edildi mi? | `BLOCKED` | Canlı REP-02 entegrasyonu NO-GO; yalnız adapter/stub tasarlanabilir | REP-02 sahibi incelemesi, yerel PKG-A girdileri ve karşılıklı fixture testi | Ayrı REP-02 sözleşme görevi planla |
| `OD-D1-014` | REP-02 model provenansı, lisans durumu ve validity bounds için zorunlu minimum alanlar yeterli mi? | `UNKNOWN` | Fiziksel iddia ve tekrarlanabilirlik bloke olur | REP-02 model kayıt şeması ve örnek model envanteri | REP-02 tarafında bağımsız inceleme yap |
| `OD-D1-015` | Güç profili Q-C01-Q-C03, Q-C06 termal ortamı, Q-C08 kablo/yük, Q-C09 ESD alanı ve Q-C10 üretici DFM girdileri ne zaman sağlanacak? | `UNKNOWN` | Backlog senaryoları kapanmaz; G1 genel olarak BLOCKED kalır | Müşteri gereksinimleri ve gate-owner takvimi | Tek doğrulama matrisiyle sahip/eşik/tarih ata |
| `OD-D1-016` | Kural durumu ile senaryo durumunun `UNKNOWN` ve `BLOCKED` ayrımı yeterince nesnel mi? | `OPEN` | Motorlar arası tutarlılık ve rapor yorumu etkilenir | Durum karar tablosu ve adversarial fixture'lar; Product + QA | D2 conformance testlerinde sınır vakalarını dondur |
| `OD-D1-017` | Kural istisnaları ayrı, imzalı waiver nesnesi mi olmalı? | `OPEN` | Serbest metin istisnası deterministik değerlendirmeyi zayıflatabilir | Waiver şeması, sahip, süre, kanıt ve kapsam; Quality/Gate Owner | İlk MVP'de otomatik istisna kabul etme |
| `OD-D1-018` | G0/G1 kapı sahipleri, PASS ölçütleri, kanıtları ve test-kapı-sahip-eşik matrisi kim tarafından onaylanacak? | `BLOCKED` | Workbench tek başına G1 kararı üretemez | Proje yönetişim kaydı; Gate Owner | D2 öncesi sahiplik ve kapı arayüzünü belgeye bağla |

## 3. G1 ve D2 durumu

- **G1:** `BLOCKED`. D1 sözleşmesi hazırlık sağlar; müşteri gereksinimleri, exact-part kararları, elektriksel/PCB doğrulamaları ve gate-owner kanıtları tamamlanmamıştır.
- **D2 REP-01 MVP:** D1 incelemesi ve `OD-D1-008`/`OD-D1-009` kararları sonrasında **koşullu GO önerisi**.
- **Canlı REP-02 entegrasyonu:** Repo scaffold-only, yerel PKG-A girdileri ve modeller yok; **NO-GO**.
- **Donanım veya uygunluk iddiası:** Test/simülasyon/bench kanıtı yok; **NO-GO**.

## 4. Claude bağımsız incelemesi için araştırma soruları

Bu sorular bağımsız incelemeye doğrudan aktarılabilir. Cevaplar yeni evidence kayıtları olarak eklenmeli; mevcut D1 kararları sessizce değiştirilmemelidir.

1. REP-01 ve REP-02 sorumluluk sınırında hâlâ güç bütçesi, derating, regülatör kararlılığı veya sıralama mantığı tekrarı var mı?
2. Önerilen REP-01 → REP-02 istek şeması rail budget ve sequencing için yeterli mi; gereksiz board-internal ayrıntı içeriyor mu?
3. `UNKNOWN`, `CONFLICT`, `BLOCKED` ve `NOT_RUN` ayrımı gizli varsayılanları tamamen önlüyor mu?
4. Gerilim, akım, zaman ve sıcaklık alanlarında değer, birim, tolerans ve kanıt bağı nasıl temsil edilmeli?
5. Sistem topolojisinden türetilmiş sıra ile veri sayfası zorunluluğunu ayırmak için `classification` alanı yeterli mi?
6. REP-02 yanıtındaki tekil PASS'in proje/G1 onayına yanlış yükseltilmesini önlemek için hangi ek koruma alanları gerekir?
7. Model provenansı, lisansı ve geçerlilik sınırları için zorunlu en küçük alan seti nedir?
8. Aynı girdilerin farklı dosya veya evidence sırasıyla verilmesi deterministik sonucu hangi köşe durumlarında etkileyebilir?
9. REP-02 çıktısındaki ölçüm/simülasyon metrikleri ile yorumlanan bulgular ayrı nesneler olmalı mı?
10. Backfeed, eksik PGOOD, gecikmiş rail, brownout ve reset senaryolarında hangi önkoşullar eksikse sonuç doğrudan BLOCKED olmalı?
11. CLAUDE-02 içindeki CF-03 benzeri kaynak-atıf riskleri sözleşmede nasıl otomatik denetlenebilir?
12. Yalnız transient grafiğinden loop stability veya hardware validation iddiasını engellemek için hangi negatif fixture ve claim-policy testi gerekir?
13. REP-02 bağımsız sürümlenirken geriye uyumluluk, schema migration ve hash eşleşmesi nasıl yönetilmeli?
14. D2'ye geçmeden önce hangi adversarial fixture'lar zorunlu tutulmalı?
15. GPIO3 için deterministik strap ifadesi, belirli bir seviyeyi uydurmadan güvenli ve test edilebilir biçimde yeterince tanımlı mı?
16. GPIO45 kuralının N8 dışındaki modül ve silicon profillerine genişletilmesi için hangi resmî kanıt gerekir?
17. Dahili PHY'den haricî PHY'ye geçiş istisnası, en az hangi pin, saat, güç, BOM, EMI ve yazılım girdilerini zorunlu kılmalı?
18. `SCN-USB-002` için genel GPIO-capacity kuralı eklemek, ilk sekiz kurallık MVP'yi gereksiz genişletir mi; typed scenario assertion daha güvenli mi?

## 5. Güvenli sonraki adım

D1 sözleşme dosyalarını insan incelemesine sun; önce `OD-D1-008`, `OD-D1-009`, `OD-D1-010` ve kanıt bütünlüğü kararı `OD-D1-011` üzerinde mutabakat sağla. Ardından yalnız REP-01'in dört MVP senaryosu için sentetik fixture'lı D2 uygulamasına ayrı ve açık bir görev emriyle geç. KiCad, eFuse, canlı REP-02 ve fiziksel doğrulama bu adımın dışında kalmalıdır.
