# HC1703L iç yapı (tersine mühendislik notları)

[English](INTERNALS.md)

HC1703L Plus'ın dayandığı notlar. Hepsi tek kamerada, sha256 değeri `5e2dc1a15734e214` ile başlayan
`p2pcam` sürümünde bulundu. Adresler bu ikilideki sanal adreslerdir (dosya ofseti = VA − 0x10000).
"Doğrulandı" çalışan kamerada ölçüldü demektir; geri kalanı statik analizdir.

## Platform

- Augentix HC1703 ailesi SoC, Cortex-A7, 64 MB RAM, 8 MB SPI NOR, Linux 3.18.31, uClibc, BusyBox 1.33.
- Çekirdekte netfilter (iptables) ve `AF_PACKET` yok. Bu yüzden bulut engeli rotalarla yapılır.
- Sensör SC1346, **1280×720**. Stok yazılım 720p çalıştırıp 1920×1080'e büyütür (`sensorFake=CHAZHI`,
  `support_chazhi` ile zorlanır). Doğrulandı: gerçek 720p piksel başına daha keskin.
- `p2pcam` (sembolsüz ARM Thumb, ET_EXEC, taban 0x10000) içinde Augentix MPI (ISP, kodlayıcı, OSD,
  hareket algılama), Tange bulut SDK'sı ve CS2 PPCS statik olarak gömülü.

## Açılış

1. Stok `start.sh` SD kartı `/mnt`'ye bağlar, `/mnt/debug_cmd.sh` varsa çalıştırır (root, WiFi'den ve
   `p2pcam`'den önce, `/bak` o anda yazılabilir).
2. `/bak/sensor.sh` ISP/sensör dosyalarını yükler. Temel hack buraya, `/mnt/debug_cmd.sh` yokken
   `/home/minihack.sh`'yi çalıştıran bir satır ekler.
3. `start.sh` `/mnt`'yi ayırır, `/bak/p2pcam.sqfs`'yi `/p2pcam`'e bağlar ve `p2pcam`'i başlatır; o da
   kartı `/tmp/mnt/mmc01/0` altına yeniden bağlar.
4. 30 sn'lik donanım watchdog'unu yalnız `p2pcam` besler. `p2pcam` çıkarsa `start.sh` `reboot -f` yapar.

Plus, 1. adımda `/bak/p2pcam.sqfs` ve `/bak/sensor.sh` üzerine RAM kopyaları bağlar; flash'a hiç yazılmaz.

## p2pcam yamaları (`patcher/patch_p2pcam.py`)

| # | Yer | Ne |
|---|---|---|
| 1 | `0x1a794` | `support_chazhi=1` zorlaması kaldırıldı → gerçek 720p |
| 2 | dizgeler | HTTPDNS 203.107.1.x, log yükleme, 4G hız testi, `ep.tange365.com`, `fidistr*`, AGPS → loopback |
| 3 | `0x32a263` | `teltun` bulut komutu (ters telnet tüneli) hiç eşleşmeyecek şekilde yeniden adlandırıldı |
| 4 | dizgeler | `telnetd`'yi başlatan ya da öldüren bulut / 6670 yolları → `true` |
| 5 | `0x5f840`… | tüm kare hızı noktaları 12,5 → 25, cihaz fps 13 → 26, kodlayıcı `frm_rate_o` 12 → 25, GOP 25 → 50 |
| 6 | `0x601a2` | 720p ana akış VBR tavanı 1024 → 2048 kbps (`patch_rc.py` 2560'a çıkarır) |
| 7, 8 | `0x5f83e`, `0x5f8ca`, `0x5f748` | 25 FPS yalnız gündüz; gece / beyaz ışık 8 → 12,5 |
| 9 | `0x16be4` | Tange bulut SDK'sı hiç başlamaz (`main` kendi `pause()` döngüsüne düşer) |
| 10 | `0x583ac` | kimliksiz LAN firmware alıcısı (`do_update`) kapalı |
| 11 | `0x113a84` + 32 çağrı | tüm `bind()` çağrıları `bind_lo` üzerinden: üretici servisleri 127.0.0.1'e bağlanır; RTSP 554, DHCP 68 ve RTP/UDP gönderici hariç |
| 12 | `0x55a4a` | `SO_SNDBUF` RTSP istemci soketi yerine CTP soketine veriliyordu |
| 13 | `0x113c00` + 2 çağrı | `req_gate`: 554, CTP/HTTP ile aynı dağıtıcıyı paylaşır; RTSP dışı istek yalnız loopback'ten kabul edilir |
| 14 | `0x5ef24` | `get_venc_stream` içindeki kare başına 300 KiB `memset` kaldırıldı (CPU) |
| 15 | `0x19bae` | `support_ymv` varsayılanı 1 → 0: dikey takip |

`bind_lo` ve `req_gate`, 9. yamadan sonra ölü kod olan Tange komut işleyicisi alanında durur. Yerel
üretici API'si (`http://127.0.0.1:8001/...`; `httpclt`, panel ve `mo.sh` kullanır) çalışmaya devam eder.

## Bit hızı denetimi (`patcher/patch_rc.py`, C2 seti)

`T = max_bit_rate × 1024 × faktör / 100`; kare hızı yalnız kare başına payı böler. C2, gündüz / gece /
beyaz ışık `g_rc_param` tablolarında faktörü 100, `m2s` 28, doku 400000/21, gündüz `g_rgl_qp` I/P tavanını
4/6 → 2/2 ve üst sınırı 2560 kbps yapar. `g_vbr_qp_table`'dan yalnız 5. indis okunur.

## ISP dosyaları

- `/bak/sensors/sensor_day_<sensör>.ini.lzma` her açılışta `/tmp/sensor_day.ini`'ye açılır. `p2pcam` onu
  gündüz/gece geçişlerinde yeniden yükler; bir değişiklik RAM'de `ircut?mode=night` → `ircut?mode=day` →
  `ircut?mode=auto` ile denenebilir (IR filtresi tıklar, görüntü bir an siyah-beyaz olur).
- `[AE] frame_rate` ve `slow_frame_rate` **aynı olmalı**. 25 / 12,5 ile loş ışıkta AE 12,5 FPS'e düşer;
  kayıtlarda (ve RTSP'de) her ~4 sn'de bir saniye atlanır, başka bir saniye iki saniye sürer. Doğrulandı;
  25 / 25 düzeltir.
- `[SHP_V2] strength` ayrıntıyı ve greni aynı oranda ölçekler (140'a karşı 170: ikisi de yaklaşık −%20,
  doğrulandı). 140 stok değerdir.
- `vb.ko param_vb_size_mb`: stok 27 MB; gerçek 720p'de 18 MB yeter (`/dev/vbs` tepe kullanımı 13,7 MB),
  yaklaşık 11 MB RAM boşalır.

## RTSP

- Port 554, yollar `/0/av0` (ana, H.265 + G.711 A-law ses) ve `/0/av1` (alt). Kimlik doğrulama yok
  (`no_rtsp_auth`).
- Yavaş WiFi'de ilk karelerin bozulması taşıma katmanından: `sendmsg` kısmi yazımları denetlenmiyor ve
  `SO_SNDTIMEO` 50 ms. SD karttaki klipler temiz. Tüm yamalarla yapılan altı açılışlık ilk bağlantı testinde
  bozuk kare görülmedi (12. yama tek başına denenmedi).
- `http://127.0.0.1:8001/snapshot` yamalı sürümde taze 640×360 JPEG döndürür (art arda üç anlık
  görüntüyle doğrulandı).

## Ayar dosyaları (`/home`, flash)

`/home/devParam.dat` (1004 bayt) ve `/home/extraParam.dat` (5120 bayt):

```
0x00 u32 magic 0x20150910
0x04 u32 checksum   veri bölümündeki little-endian u16 sözcüklerin toplamı (+ tek kalan bayt)
0x08 u32 uzunluk    veri uzunluğu (992 / 5108)
0x0c ...            veri; p2pcam dosyanın tamamını okur, alan ofsetleri dosya ofsetidir
```

Magic, uzunluk ya da checksum bozuksa `p2pcam` dosyayı siler ve `*_bak`'tan geri yükler. Her kayıttan sonra
dosyayı `*_bak`'a kopyalar. `devParam.dat` WiFi SSID'sini (+0x100) ve parolasını (+0x12c) da düz metin tutar.

## Hareket algılama ve alarm bölgeleri

| Ayar | Yer | Not |
|---|---|---|
| Alarm bölgeleri | `devParam.dat` 0x314 | i32 sayı (en çok 25) + 25 × {u8 x, y, w, h}, birim görüntünün onda biri (10×10 ızgara). Olay iş parçacığı başlarken bir kez okunur (`checkMdAreparams`, `0x16dc8`). Canlı doğrulandı |
| Bölge desteği | `support_mdzone` | Bu kamerada çalışırken 1 (`0x3a4ca8`, süreç belleğinden okundu); oysa ne `hwcfg.ini` ne de bir `defalut.config` bunu ayarlıyor. Neyin ayarladığı bilinmiyor |
| Gerçek eşik | `hwcfg.ini` içinde `[config] motion_level`, 0/1/2 | açılışta bir kez `0x617bc` uygular: gereken hareketli blok 15 / 4 / 1 (`md_setting_low/middle/high` ile değişir). Yoksa 1. Canlı doğrulandı: `motion_level = 2` eşiği 1 yapar |
| Uygulamaya gösterilen seviye | `devParam.dat` 0xe8 = 1/2/3 | yalnız üretici uygulamasına geri bildirilir |
| Hareket takibi | `devParam.dat` 0x2d8 | açıkken kayıt bölge denetimini atlar; ayrıntı için aşağıda Takip |
| Olay anahtarları | `devParam.dat` 0x388 hareket, 0x38c kişi, 0x390 ses | 2 = kapalı (statik analiz) |
| Savunma, bölge durumu | `devParam.dat` 0x3d4, 0x3d5 | |
| Uygulama olay bayrakları | `extraParam.dat` 0x1340–0x134d | `setMotEventStatus` 0x1346 (2 = kayıt tetiklenmez), `setMotionSensity` 0x134b, `setAreaSensity` 0x1349, … |

`FUN_00018fb8` anahtarları `/home/defalut.config` ya da `/bak/defalut.config` içindeki `[defalut]` bölümünden
okur, ama bu kamerada iki dosya da yok; yerleşik varsayılanlar geçerli. `hwcfg.ini` (`[config]`) varsa
`/home/hwcfg.ini`'den, yoksa `/bak/hwcfg.ini`'den okunur. `start.sh`, `/home/hwcfg.ini` bulursa bir sonraki
açılışta `/bak`'a taşır; bu bir flash yazımıdır, Plus bind mount ile bundan kaçınır.

Klip nasıl başlar: olay iş parçacığı (`0x340e4`) hareket olaylarını alır. `support_mdzone` açıkken bölgeyle
kesişmeyen olay atılır (`0x33a24`), takip açıksa bu denetim atlanır. Geçen olay `FUN_00033218(1)`'i çağırır, o da
30 sn'lik olay kaydını başlatır (`FUN_001d5a9e(0,2,0x1e)`). Bölge sayısı −1 bütün kare demektir.

Tek bölgeyle yürüyüş testi: bölge dışında dakikalarca hareket eden kişi klip üretmedi, bölgeye girince saniyeler
içinde klip başladı (iki kez). Girmeden önce `0x33a24`'e hiç olay ulaşmamıştı (ilk çağrı bayrağı `0x3a86a4` set
kaldı); yani dışarıdaki hareket yukarıdaki koddan daha önce süzülüyor olabilir. Tam yeri bilinmiyor.

Çalışırken bunları yalnız Tange komut işleyicisi değiştirir (`TCI_CMD_SETMOTIONDETECT`,
`SET_MOTION_TRACKER_REQ`, `SET_MDAREA_STATE`, `SET_DEFENCE_REQ`) ve o burada kapalıdır. Bu yüzden Plus ayarları
açılışta, `p2pcam` başlamadan uygular: `debug_cmd.sh`, `clean/bin/mdcfg` (kaynak `tools/mdcfg.c`) ile
`clean/md.conf`'tan `devParam.dat`'ın RAM kopyasını üretip `/home/devParam.dat` üzerine bağlar; `/bak/hwcfg.ini`
için de aynısını yapar. Flash'a dokunulmaz (`/home` ve `/bak` ham bind ile md5 karşılaştırmasıyla doğrulandı).

`mdcfg live`, hassasiyeti (`0x3f1b0c`) ve takibi (devParam RAM kopyası + 0x2d8) `/proc/<pid>/mem` üzerinden çalışan
`p2pcam`'e de yazar; yalnız RAM. Önce salt okunur veridaki bir dizgiyi ve devParam sihirli sayısını denetler, panel
de çalışan ikiliyi `clean/p2pcam.md5` ile karşılaştırır. Canlı yazmadan sonra takibin çalıştığı görüldü. Bölgeler
yalnız açılışta geçerlidir: bölgeler `0x3a8514`'e, sayı `0x39ddf8`'e yazılıp `0x3a86a4` bayrağı kurulduğunda hareket
klip başlattı, kamera hareketi bile bayrağı temizlemedi; yani `0x33a24`'e ulaşılmadı.

Çalışan değerler hiçbir şey değiştirmeden okunabilir:
`dd if=/proc/<pid>/mem bs=4 skip=$((0x3a4ca8/4)) count=1`. Bu sürümdeki adresler: `0x3a4ca8` `support_mdzone`,
`0x3a4bf4` `motion_level`, `0x3f1b0c` eşik, `0x39ddf8` bölge sayısı, `0x3a8514` bölgeler.

Hata ayıklama anahtarları: `touch /tmp/md_info`, `/tmp/md_region`, `/tmp/track_info`, `/tmp/od_info` ya da
`/tmp/hd_info`, `p2pcam`'in hareket, bölge, takip ve kişi algılama ayrıntısını yazdırmasını sağlar
(henüz denenmedi).

### Takip

`/dev/ptz` tamsayı dizisiyle üç ioctl alır: 0x81 aralık (510 × 160), 0x82 konum, 0x83 konuma git. Statik diye
belirtilmeyen sayılar kamerada ölçüldü.

- **Ölü bölge.** `FUN_00060e68` (statik), nesnenin merkezi görüntünün orta yarısındayken dx ve dy'yi sıfırlar.
  Ölçülen bütün adımlar bu sınırda ya da dışındaydı; 21 adım değeri tam olarak genişliğin %25'idir.
- **Bölgeler.** Takip yalnız bölgeyle kesişen olayla başlar (`0x33a24`, statik). Bölge sağdayken dokuz adımın hepsi
  sağa gitti; bütün karede iki yöne de gitti; %20–50 bölgesinde kamera hiç kıpırdamadı. Kayıt bunu beklemez: takip
  açıkken bölge dışındaki hareket için de klip başladı.
- **Dikey.** `support_ymv` (varsayılan 1, bu kamerada olmayan `defalut.config`'den okunur) `FUN_00033978` içinde
  dy'yi sıfırlar. 15. yama varsayılanı 0 yapar. Dikey hareket, ölü bölge yüzünden yalnız yatay dönüşle birlikte olur
  ve `FUN_000399e4` adımını 7 ile çarpar.
- **Dönüş.** Son adımdan on saniye sonra `FUN_0003990c`, takip başlarken okunan konuma döner.
- **Yeniden hizalama.** Her 10. dönüşte (`ptzAdjustCount`) ve bir dönüşten 30 dakika sonra bunun yerine ioctl 0x8a
  çağrılır: kamera tarama yapar ve `/home/psp.dat` içindeki konuma gider. Argüman `support_ymv != 0`'dır; 1 yalnız
  yatayı, 0 iki ekseni tarar (ölçüldü, yaklaşık 20 sn).
- **`/home/psp.dat`** 20 bayttır: `PSPT`, ardından i32 olarak x, y, z ve x + y − 176. Sürücü dosyayı hareketten sonra
  ve her takip dönüşünde yeniden yazar; yani takip flash yazımını artırır. Açılışta motorlar oynatılmadan okunur.
  Bu yüzden kaymış bir değer ilk yeniden hizalamaya kadar görünmez: bizim kamerada bu, görüşü kapıdan 57 adım
  uzağa çevirdi ve yeniden başlatma geri getirmedi. Takip kapalıyken panel oklarıyla yapılan hareket dosyayı
  günceller; takip sırasında kayıt kapalıdır (ioctl 0x89).
- Takip sırasında yeniden başlatma, kamerayı bir sonraki hizalamaya kadar kayıtlı konumdan uzakta bırakır (statik).

Kayıtlı konumlar SD karttaki `clean/ptz.conf` dosyasındadır. Dişlilerde yaklaşık 5 adım boşluk var; bu yüzden
`tools/ptzpos.c` son 12 adımı hep aynı yönden atar. Dört yönden de 320 pikselde 1 piksel içinde aynı noktaya geldi.
Yukarıdaki yeniden hizalamayla aynı konuma gelmek yaklaşık 4 dikey adım fark edebilir, çünkü o tarama diğer yönden
gelir.

Seçili görüntü modu devParam + 0xfa baytındadır (0 otomatik, 1 gündüz, 2 gece). Panel bunu `mdcfg image` ile okur;
çalışan mod tek başına Otomatik seçimini gösteremez.

## Gece yeniden başlatma

`FUN_00042030` 6 saniyede bir çalışır (ölçüldü: 32 262 saniyede 5363 tur). Şu koşulların hepsi tutarsa kamerayı
yeniden başlatır:

- `0x3a8fbc` sayacı `0x3390`'ı geçmiştir (13 200 tur, yaklaşık 22 saat).
- `0x3a4bf8` sıfır değildir. Bu, hwcfg'deki `auto_reboot` alanıdır (+0x24c, varsayılan 1). Onu yalnız bu denetim
  okur; donanım test kipi 0 yapar.
- Yerel saat 2, 3 ya da 4'tür ve zaman `0x3a55a8`'den 0 ile 600 saniye sonrasındadır. Bu değer açılışta bir kez
  rastgele seçilir (`rand() % 7200`, 02:00'den sonraki saniye).
- Art arda beş denetimde (30 sn) süren bir olay yoktur (`0x3a84f0`).

Sonra `/home/silent_reboot` dosyasını oluşturup `reboot -nf` çalıştırır. Sonraki açılışta `start.sh` bu dosyayı
görünce PTZ öz denetimini atlar, bu yüzden görüş yerinden oynamaz. `0x39ded5` baytı `'0'` kalmalıdır: başka bir
değer kamerayı 11 tur sonra yeniden başlatır.

Koşullar ve `0x39ded5` kuralı ayrıştırılmış koddan çıkarıldı (statik). 6 saniyelik tur, `auto_reboot` = 1 ve seçilen
saat çalışan süreçten okundu.

`mdcfg reboot <pid> [0|1]`, `0x3a4bf8`'i okur ve yazar, `<açık> <02:00'den sonraki saniye>` yazdırır. Kameramızın
açılış kaydında 3–8 Kasım arasında her gece bir yeniden başlatma var (02:07 ile 03:56 arası). 17:28'deki bir
açılıştan sonraki gece, kamera yalnız 10 saattir açık olduğu için atlandı. Kapatmanın etkisi bellekte doğrulandı,
henüz bütün bir gece boyunca denenmedi.

## Bulut uç noktaları

`netguard.sh`, `/home/etc/resolv.conf` dosyasını (`/etc/resolv.conf` buraya bağlı, flash'ta) salt okunur bir RAM
kopyasıyla örter. `p2pcam` açılışta bu dosyayı yeniden yazar; bundan önce DNS en çok 5 sn router'ı gösteriyor ve
dosya her açılışta flash'ta yeniden yazılıyordu.

İkilide görülenler: Alibaba HTTPDNS `203.107.1.1/.33/.34/.35`, `139.224.31.65` (log yükleme),
`47.107.191.43` (4G hız testi), `ep.tange365.com`, `fidistr.tange365.com`, `www.goke-agps.com`.
Bazıları doğrudan IP olduğu için `hosts` ile engelleme yetmez. Yazarın kamerasında aktif test genel
adreslere %100 kayıp gösterdi; aynı anda router kuralı da vardı, kameranın rotaları tek başına ölçülmedi.
