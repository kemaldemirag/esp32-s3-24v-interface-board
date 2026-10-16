# Arayüz Sözleşmesi — Taslak Sınırlar

Durum: OPEN / SOURCE_PENDING. Bu belge pin-level tasarım veya onaylanmış elektriksel spesifikasyon değildir.

| Alan | Beklenen sınır | Şu anki durum | Kilitleme kanıtı |
|---|---|---|---|
| 24 V giriş | Koruma, min/nom/max, transient ve ters polarite | UNKNOWN | Onaylı araç/güç profili + kaynak datasheet |
| 5 V rayı | Kaynak topolojisi, tolerans, normal/tepe akım | UNKNOWN | Rail budget + exact-part hesabı |
| 3.3 V rayı | MCU ve arayüz yükleri, tolerans, sıralama | UNKNOWN | Rail budget + zaman matrisi |
| EN/PGOOD/RESET | Gerilim alanı, aktif seviye, min/max gecikme, fault action | UNKNOWN | Exact-part datasheet + timing review |
| USB-A host | Controller host modu, VBUS source, current limit, ESD | OPEN | Müşteri rol onayı + şema/bench planı |
| Native USB/device | USB-A host ile karıştırılmayan ayrı rol | OPEN | Port sözleşmesi |
| TTL | Mantık seviyesi, referans toprak, power-off davranışı | UNKNOWN | Harici cihaz spesifikasyonu |
| RS-232 | Transceiver, charge pump beslemesi, ESD ve power-off davranışı | UNKNOWN | Exact-part datasheet |
| CAN/TWAI | ESP32 controller + harici fiziksel transceiver | OPEN | Exact-part ve ağ profili |
| LIN | Uygun transceiver + doğrulanmış UART/peripheral yaklaşımı | OPEN | Exact-part ve protokol kararı |
| GNSS | Modül varyantı, güç, RF/antenna feed ve opsiyonlar | UNKNOWN | MAX-M10S varyant datasheet’i |

Çelişkili gerilim veya rol gereksinimi çözülmeden şematik başlatılmaz.
