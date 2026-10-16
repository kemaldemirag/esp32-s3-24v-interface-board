# WF01 Açık Kararlar ve Kapanış Kaydı

**Sürüm:** v1.0-draft  
**Tarih:** 2026-10-16  
**WF01 sonucu:** Dört taslak üretildi; G1 `BLOCKED` kaldı.  
**Durum sözlüğü:** `PASS`, `BLOCKED`, `UNKNOWN`, `NOT_RUN`

Bu kayıt karar vermek için değil, karar sahiplerini ve eksik kanıtı görünür kılmak için oluşturuldu. Kaynakta olmayan pin, akım, eşik ve parça numarası eklenmedi.

## 1. Müşteri soruları

| ID | Soru | Sahip önerisi | Etki | Durum |
|---|---|---|---|---|
| Q-C01 | 24 V girişin sürekli min/maks çalışma aralığı nedir? | CUSTOMER | REQ-001, UVLO ve exact-part | `UNKNOWN` |
| Q-C02 | Merkezi load-dump bastırması ve geçerli darbe/OEM profili nedir? | CUSTOMER | REQ-001 koruma mimarisi | `UNKNOWN` |
| Q-C03 | Ters polarite ve jump-start gerilimi/süresi nedir? | CUSTOMER | REQ-001 koruma ve exact-part | `UNKNOWN` |
| Q-C04 | USB-A cihazının en yüksek akımı nedir? | CUSTOMER | VBUS limiti ve 5 V ray bütçesi | `UNKNOWN` |
| Q-C05 | LCD mantık gerilimi ve arka ışık akımı nedir? | CUSTOMER | Seviye dönüşümü, güç ve GPIO | `UNKNOWN` |
| Q-C06 | Ortam sıcaklığı ve kutu içi soğutma koşulu nedir? | CUSTOMER | Derating/termal | `UNKNOWN` |
| Q-C07 | 5 TTL TX ve 5 RS-232 TX on ayrı akış mı, aynı beş akışın iki fiziksel biçimi mi; frozen GPIO belgesi var mı? | CUSTOMER | CF-09, serial topoloji ve GPIO | `BLOCKED` |
| Q-C08 | RS-232 baud/kablo uzunluğu ve TTL yük tipi nedir? | CUSTOMER | Sürücü/transceiver seçimi | `UNKNOWN` |
| Q-C09 | ESD/EFT hedef seviyesi nedir? | CUSTOMER | Konnektör koruması ve test eşiği | `UNKNOWN` |
| Q-C10 | Hedef üretici ve montaj sınıfı nedir? | CUSTOMER / PROJECT | Stackup, DFM ve maliyet | `UNKNOWN` |
| **Q-C11** | **Gelecek TFT konnektörü hangi arayüz/sinyal setini ve kaç pini gerektiriyor?** | CUSTOMER | TFT hariç tutulan GPIO bütçesi | `UNKNOWN` |

Q-C01–Q-C03 araştırma/karşılaştırmayı durdurmaz. Bunlar yalnız REQ-001 için exact-part seçimini ve PASS kararını bloke eder. A07 başlama koşulu, `WF01 REQ-001 taslağı mevcut` olarak karşılanmıştır.

## 2. Mimari kararlar ve çatışmalar

| ID | Karar / çatışma | Kanıt | Öneri | Nihai durum |
|---|---|---|---|---|
| CF-09 | On TX çıkışının mantıksal topolojisi ve frozen GPIO ataması | `CLAUDE-03 C03-020`; `CLAUDE-06 REQ-004-C`; `CONFLICT` | Q-C07 ve frozen GPIO kaydını bekle; seçenekleri yalnız karşılaştır | `BLOCKED` |
| D-JTAG | GPIO39–42 pad JTAG için ayrılsın mı? | `CLAUDE-11 C11-014`; `SOURCE_EXPLICIT` | Proje/debug sahibi karar versin; ayırmak genel GPIO bütçesini 28'den 24'e indirir | `UNKNOWN` |
| D-USB-EFUSE | USB_PHY_SEL eFuse yakılsın mı? | `C11-012 SOURCE_EXPLICIT`; `C11-015 DERIVED` | **Yakma:** hosta çalışma zamanında geçiş eFuse gerektirmez; boot USB-Serial-JTAG yolunu korur | `UNKNOWN` — öneri nihai karar değildir |
| D-EFUSE-IO35-37 | N8'de GPIO35–37 varsayılan eFuse işlevi doğrulandı mı? | `C11-003/C11-004`; `DERIVED` | Resmî eFuse/default kaydını doğrulamadan atama yapma | `UNKNOWN` |
| D-ANTENNA | WROOM-1 anten keepout ölçüsü nedir? | `C11-011`; `UNKNOWN` | Donanım tasarım kılavuzunu yerel kanıt setine al | `UNKNOWN` |
| D-PROP-IDS | PROP-01…04 için resmî REQ kimlikleri atanacak mı? | `CLAUDE-06 PROP-01…04` | Proje sahibi onayıyla kimliklendir | `UNKNOWN` |

GPIO aritmetik kontrolü: CLAUDE-11a'da 36 sayısal GPIO vardır. USB için 2, UART0 için 2 ve dört strapping pini ayrıldığında 28 genel GPIO kalır; GPIO39–42 JTAG'a ayrılırsa 24 kalır. TFT hariç 4-bit LCD senaryosundaki talep 25'tir; JTAG ayrılırsa **1 pin eksik** olur. LCD 4-bit=6 hesabı bir `ASSUMPTION`, TFT ihtiyacı `UNKNOWN` ve bu sayım CF-09'u kapatmaz.

## 3. Kaynak ve kapsam kararları

| ID | Açık karar | Durum |
|---|---|---|
| Q-S01 | Okunamayan ilan görselleri JOB-03/04/05 metinlerini içeriyor mu? | `UNKNOWN` |
| Q-S02 | JOB-01…JOB-05 ilan URL'leri kayıtlı mı? | `UNKNOWN` |
| Q-S03 | PKG-C için manifest üretilecek mi, yoksa kapsamdan çıkarılacak mı? | `UNKNOWN` |
| Q-S04 | G1 JOB-01 için ayrı mı, beş iş için tek kapı mı? | `UNKNOWN` |
| S-CANON | Aşağıdaki tek yerel, manifestle eşleşen CLAUDE-01…10 klasörü ana kayıt seçilecek mi? | `UNKNOWN`; yalnız öneri var |

## 4. Salt okunur kaynak envanteri

**Kayıt kökü (tam yerel yol):**  
`C:\Users\jagua\OneDrive\Masaüstü\Model Bağımsız Çalışmaları\15.10.2026\001-Kayıt Notları ve Dokümantasyon\002-Proje Dokümantasyonu\001-Power-Embedded-Interface-Portfolio\06-Claude-Arastirma`

Çalışma alanı taramasında CLAUDE-01…10 için birer yerel kopya bulundu; yinelenen yol veya yinelenen hash bulunmadı. CLAUDE-01…10 boyut ve SHA-256 değerleri `CLAUDE-OUTPUT-MANIFEST.json` ile birebir eşleşti. CLAUDE-11/11a aynı seri klasöründedir ancak daha eski manifestte kayıtlı değildir.

| Dosya (tam yol = kayıt kökü + dosya) | Bayt | SHA-256 | Kopya |
|---|---:|---|---:|
| `CLAUDE-01-Power-Architecture-Research.md` | 11.444 | `9867BA94211BDF507CD7BEFFFCE451E6879347923F79FF15836F4DD8B6D74928` | 1 |
| `CLAUDE-02-Power-Sequencing-Research.md` | 4.538 | `9B23EEEC778956D23605FA03DCFA5D65627B068C0E239E19A1552A28E0AD2EA1` | 1 |
| `CLAUDE-03-Interface-Architecture-Research.md` | 5.886 | `18D707572926679871CE4255083345FB67FD0D0233EFAE929AE453E808F1A479` | 1 |
| `CLAUDE-04-Pre-Layout-Verification.md` | 4.171 | `7CB7FD953099B5FEC6D05255F15CE5A3976C31B007AA2A0344B6BD5EBCAF2673` | 1 |
| `CLAUDE-05-Automotive-Power-Research.md` | 2.594 | `74B0DBE74999F74AAEC4A4952EBFB5276806697242982717A31E45B65CD2C2F0` | 1 |
| `CLAUDE-06-Requirement-Evidence-Matrix.csv` | 8.879 | `32F182FEAB89A09E5A7D2AE8F422053ABF79EA645DB781C1EAE146586637C4CE` | 1 |
| `CLAUDE-07-Open-Engineering-Questions.md` | 4.948 | `F91FD4F39B32A2E8D6E8C865BF5E07BA7368320E3BF9EDEFA882B33ACF6761D8` | 1 |
| `CLAUDE-08-Source-Register.csv` | 9.615 | `323C43DA698677D123A9B43F21569F233D591333A44A250EFBA6D6244124DAD2` | 1 |
| `CLAUDE-09-Codex-Research-Handoff.md` | 2.947 | `974878AFF812CBFF6652201818458D381A72CF9CA7CDD21AD3F526EC9E152A2F` | 1 |
| `CLAUDE-10-G1-Readiness-Assessment.md` | 6.609 | `FBE92157550A09B03A0E4BCFAA34271E85F2CCB3FD16AE828DFFF0DC8242E4C4` | 1 |
| `CLAUDE-11-RnD-WROOM1-N8-USB-GPIO.md` | 6.679 | `D9D85EF5FFD6402BC227F016E3E917D4DEC6E5005995CD9EEF52A72051051F1B` | 1 |
| `CLAUDE-11a-WROOM1-N8-Pin-Evidence.csv` | 5.799 | `CF4B739F8663F4696CC788CB1B6C51177B2B02F058DFFE118DB042CCA3358CBE` | 1 |

**Ana kayıt önerisi — karar değildir:** Yukarıdaki seri klasörü, tek yerel kopya olması ve CLAUDE-01…10'un manifestle byte/hash düzeyinde eşleşmesi nedeniyle ana kayıt adayıdır. CLAUDE-11/11a için manifest güncellemesi ayrıca karara bağlanmalıdır. Uzak/Drive kopyaları bu görevde incelenmedi.

## 5. Salt okunur repo ve Git envanteri

**Repo kökü:**  
`C:\Users\jagua\OneDrive\Masaüstü\Model Bağımsız Çalışmaları\15.10.2026\002-Proje Çıktıları\esp32-s3-24v-interface-board`

- Hedef klasör mevcut: `PASS`.
- Başlangıç yapısı: 10 dosya / 10 dizin / 3.352 bayt; yalnız scaffold ve README kayıtları.
- Hedef klasör ve üst zincirinde `.git` yok; `git status` uygulanabilir değil: `NOT_RUN`.
- Git init/commit/push yapılmadı.
- Başlangıçta şematik, PCB, BOM, exact-part, üretim çıktısı, ERC/DRC/simülasyon/bench sonucu yoktu.
- `docs/arayuz-sozlesmesi-taslagi.md` semantik olarak yeni sözleşmeyle örtüşür; değiştirilmedi ve önceki scaffold olarak referanslandı.
- Başlangıç taramasında `docx-preview.d.ts`, `node_modules`, `cache/.cache`, `__pycache__`, `.pytest_cache`, `.mypy_cache`, `.ruff_cache`, `.venv` veya `.tox` bulunmadı: `PASS`.

### Değişmeyen başlangıç dosyaları

| Repo içi yol | Bayt | SHA-256 |
|---|---:|---|
| `README.md` | 569 | `A2890425574F8BEAE77B4CCEA7A536F24BC577CA0D2A570B1CD06BFEBDA6209D` |
| `docs/arayuz-sozlesmesi-taslagi.md` | 1.548 | `5D69E2EF9F626DF1C5043AAF2124577BCE735590291031C6173E3F1D1351183A` |
| `docs/block-diagrams.md` | 142 | `ECA9527282FBF83A6302F528299CA7995AE0F00513924D0251FF0FF52919B6EB` |
| `engineering/calculations/part-selection.md` | 195 | `6444E981E919FF27EAF1E702AAECF8713BEFA943FF718E17E47822E2993D9B46` |
| `engineering/power-tree/README.md` | 178 | `110E497C8596E2F54857BD15075AE19C9B6DF1915713B8AB332BB24CB02B9772` |
| `evidence/README.md` | 187 | `13706D04DE29C5754139A830D9C931B57BB59AFBBD3B3271B6C2A4F0A5FC9680` |
| `hardware/bom/README.md` | 107 | `CA78D9B46693282483709B140AE54BC1EC5222F77BB5B1AEA22512503D1F90F1` |
| `hardware/kicad/README.md` | 113 | `EDC119EC05640B224A6A782B158F7AA5094420C4717E2DE98C88C056156BAA92` |
| `simulation/README.md` | 188 | `C3C42272481E91C9D005A17AB3EBB77569C0071386C75F11DF93B36A7E423B2D` |
| `tests/README.md` | 125 | `655D7F9F4E0530CE83135E1EFD2BE88C0D16B9987ED4BFAE7BD8546EB2C23D68` |

## 6. WF01 kapanış kaydı

Eylem dökümü v1.0 §8'de ayrı “plan boşluğu” olarak duran başlıklar WF01 kapanış kaydına taşınmıştır.

| Kapanış alanı | Mevcut kayıt | Durum | Kapanış ölçütü / gereken kanıt |
|---|---|---|---|
| Kapı sahipleri | G0–G6 kapıları tanımlı, sahip alanları yok | `UNKNOWN` | Her kapı için tek sorumlu ve onaylayan kaydedilir |
| G0 PASS ölçütü | İzinli kök; doğrudan repo topolojisi; yönetim ayrımı; araç envanteri | `PASS` | `kapilar-G0-G6.md` PASS kaydı vardır; kanıt dosyası/approver bağı ayrıca yazılmalıdır |
| G5 gerekli mi? | G5 tanımlı: REP-02 testleri, REP-03 varyantı, bench planı | `UNKNOWN` | G5'in JOB-01 release/portföy kapsamına uygulanıp uygulanmayacağı karar kaydıyla belirtilir |
| Takvim / efor | Başlangıç-bitiş, tahmin/gerçek efor alanları yok | `UNKNOWN` | Aksiyon bazında owner, tarih, tahmini ve gerçek efor eklenir |
| Tek doğrulama matrisi | Test–kapı–sahip–eşik tek kayıtta değil | `UNKNOWN` | Aşağıdaki matris eşikleri ve sahipleri doldurularak aktif doğrulama kaydı olur |

### Tek doğrulama matrisi taslağı

| Test / kanıt | Kapı | Sahip | Eşik / kabul | Durum |
|---|---|---|---|---|
| Kök ve repo hiyerarşisi | G0 | `UNKNOWN` | İzinli kök, üç doğrudan repo, yönetim ayrımı, araç envanteri | `PASS` |
| Paket hash/lisans/provenans ve tekrar/çelişki denetimi | G1 | `UNKNOWN` | Üç paket ve beş iş için eksiksiz, karşılaştırılmış kayıt | `BLOCKED` |
| Müşteri gereksinim parametreleri | G1 | CUSTOMER / proje sahibi `UNKNOWN` | Q-C01–Q-C11'in ilgili olanları yanıtlanmış; çatışmalar kapanmış | `BLOCKED` |
| Rail budget / exact-part / timing | G2 | `UNKNOWN` | Sayısal eşikler kaynakla tanımlı; hesaplar ve kararlar izlenebilir | `NOT_RUN` |
| USB-A host, UART0 ve USB PHY stratejisi | G2/G3 | `UNKNOWN` | İşlev testi ve eFuse karar kaydı; sayısal USB eşikleri `UNKNOWN` | `NOT_RUN` |
| Boot güvenliği ve serial çıkışlar | G3 | `UNKNOWN` | GPIO45 boot'ta yüksek çekilmez; GPIO3 yüksek empedans bırakılmaz; CF-09 kapalı | `BLOCKED` |
| ERC / model provenanslı benzetim / pre-layout review | G3 | `UNKNOWN` | Açıklanmamış hata/uyarı yok; diğer eşikler tasarım planında tanımlı | `NOT_RUN` |
| DRC ve üretim çıktıları | G4 | `UNKNOWN` | Açıklanmamış DRC hatası/uyarısı yok; Gerber/NC drill/CPL/BOM/STEP mevcut | `NOT_RUN` |
| REP-02/REP-03/bench planı uygulanabilirliği | G5 | `UNKNOWN` | Önce G5 kapsam kararı; sonra ilgili test/varyant/bench kanıtı | `UNKNOWN` |
| Yeniden üretim/lisans/checksum/dürüst iddia matrisi | G6 | `UNKNOWN` | Teslim seti tekrar üretilebilir ve kanıtlı | `NOT_RUN` |

## 7. Durum özeti ve G1 etkisi

| Kalem | Durum | G1 etkisi |
|---|---|---|
| Zorunlu CLAUDE-11/11a girdileri yerelde mevcut | `PASS` | N8/USB/GPIO kanıtı kullanılabildi |
| Dört WF01 taslağının oluşturulması | `PASS` | CF-08 sınıflandırma ve izlenebilirlik taslak düzeyinde ilerledi |
| GPIO19/20 USB ve GPIO43/44 UART0 ayırması | `PASS` | USB host/debug mimarisini güçlendirdi |
| GPIO45/GPIO3 boot kısıtlarının kayda alınması | `PASS` | Boot riskleri görünür oldu; test henüz yok |
| On TX topolojisi | `BLOCKED` | CF-09 G1 engeli olmaya devam ediyor |
| PKG-A/B hash karşılaştırması; PKG-C manifest/kapsam; lisans/provenans | `BLOCKED` | G1 PASS verilemez |
| Exact-part, rail bütçesi ve müşteri parametreleri | `BLOCKED` | Q-C01–Q-C08/Q-C11 kapatılmadan PASS verilemez |
| Git durumu | `NOT_RUN` | Klasör Git deposu değil; G1 kapı kararı değildir |
| ERC/DRC/simülasyon/bench | `NOT_RUN` | Tasarım/ölçüm kanıtı yok |
| G5'in JOB-01 için gerekliliği | `UNKNOWN` | Release/portföy planı kapanmadı |

**G1 kararı:** `BLOCKED`. WF01 taslakları G1'i ilerletir fakat otomatik olarak PASS yapmaz.

## 8. Sonraki güvenli adım

Tek ve güvenli sonraki adım: WF01 taslaklarını proje sahibiyle gözden geçirip önce Q-C07/CF-09, Q-C11, D-JTAG ve D-USB-EFUSE karar sahiplerini atamak; ardından Q-C01–Q-C06/Q-C08 yanıtlarını ve PKG-A/B/C hash-lisans-provenans kanıtını toplamak. Bu kapanmadan pin ataması, exact-part, şematik veya PCB başlatılmaz.
