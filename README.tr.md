# HC1703L Plus

[English](README.md)

Ucuz Augentix **HC1703L** pan/tilt kamera için, [Jalecom'un hack'i](https://github.com/Jalecom/AJ_HC1703L_Teardown) üzerine kurulan,
gizliliği öne alan modern bir katman. Üreticinin `p2pcam` programı kapalı bir "sürücü" olarak yerinde
kalır: **kendi** kameranızdan aldığınız kopya üzerinden yamalanır, kullandığınız her şey SD karttan çalışır.

> **Durum:** Ekim 2026'dan beri yazarın kamerasında her gün kullanılıyor. **Tek** kamerada ve **tek**
> firmware sürümünde denendi. Başlamadan önce [Uyumluluk](#uyumluluk) ve [Kurtarma](#kurtarma) bölümlerini okuyun.

## Ne kazanırsınız

| | Stok | Plus |
|---|---|---|
| Çözünürlük | 1920×1080, 1280×720 sensörden büyütülmüş | gerçek 1280×720 |
| Kare hızı | gündüz 12,5, gece 8 | gündüz 25, gece 12,5 |
| Video bit hızı tavanı | 1 Mbps | 2,56 Mbps |
| Boş RAM | ~14 MB | ~22 MB |
| Bulut | Tange/iCam365 SDK, HTTPDNS, ters `teltun` tüneli | SDK hiç başlamaz, uç noktalar loopback'e, tünel komutu bozuk, tüm genel IP'ler reddedilir |
| p2pcam'in LAN'a açık servisleri | 80, 6670, 8001, 20202, 554 üzerinden CTP/HTTP | yalnız 554'te RTSP |

Ayrıca:

- **Kontrol paneli** (`:8080`): canlı önizleme, basılı tutunca dönen PTZ, beyaz ışık / IR,
  gündüz / gece / otomatik, keskinlik hazır ayarları, durum (yamalı mı stok mu, bulut engeli, WiFi, SD,
  RAM, CPU, son hareket), günlükler. Komutlar yalnız POST ile gider: sayfayı yenilemek ya da geri yüklenen
  sekme komutu tekrar çalıştırmaz. İngilizce ve Türkçe (başlıktaki EN/TR anahtarı; galeri
  panelin seçimini izler).
- **Kayıt galerisi** (`:8090`): hareket olayları kart olarak (fotoğraf + klip), gün görünümü, tarayıcıda
  oynatma, indirme, silme, birden çok olayı seçip birlikte silme, bir günü toplu silme, harici bir mail betiği
  için hareket maili anahtarı.
- **Hareket ayarları** panelde: hassasiyet, takip ve canlı önizleme üzerinde çizilen en çok 25 alarm
  bölgesi. Hassasiyet ve takip hemen, bölgeler yeniden başlatınca uygulanır. Her şey RAM'de kalır, ayarlar da
  flash'a dokunmaz.
- **Hareket fotoğrafları** (`mo.sh`): her hareket klibi için bir fotoğraf, klibe bağlı.
- **Düzeltmeler:** RTSP soket arabelleğinin yanlış sokete verilmesi, loş ışıkta kayıtların saniye
  atlaması, 554 portunun LAN'dan RTSP dışı komut kabul etmesi.
- **Araştırma:** hareket algılama, bölgeler ve kaydın `p2pcam` içinde nasıl çalıştığı
  ([İÇ YAPI](docs/IC-YAPI.md)).

## Nasıl çalışır

```
güç verilir
 └─ stok start.sh
     ├─ <SD>/debug_cmd.sh   (Plus yükleyicisi, kendini siler = tek seferlik)
     │    ├─ netguard.sh    bulut engeli (RAM kopyası)
     │    ├─ boot.sh        SD kartı bekler, sonra health.sh'yi çalıştırır
     │    ├─ clean/p2pcam.sqfs ve clean/sensor.sh'nin RAM kopyalarını /bak üzerine bağlar   (flash'a dokunmaz)
     │    └─ clean/md.conf'u devParam.dat ve hwcfg.ini'nin RAM kopyalarına uygular          (flash'a dokunmaz)
     ├─ sensor.sh → minihack.sh (temel hack: WiFi, telnet, FTP, httpd)
     └─ p2pcam (yamalı)
health.sh  T+0 plus.sh (panel, galeri, hareket servisi)
           T+5 dk  sağlıklıysa yükleyiciyi geri koyar; art arda 3 başarısız açılış → stokta kalır
           T+30 dk hâlâ sağlıklıysa başarısızlık sayaçlarını siler
```

Plus flash'a hiç yazmaz. Stok PTZ sürücüsü ise yazar: hareketten sonra ve her takip dönüşünde kamera konumunu
`/home/psp.dat` dosyasına kaydeder. SD kartı çıkarmak ya da içinden `debug_cmd.sh`'yi silmek, bir sonraki açılışta
sizi düz temel hack'e döndürür. Panelden yapılan yeniden başlatma başarısız açılış sayılmaz; art arda ayar
değiştirmek kamerayı stoğa düşürmez.

## Uyumluluk

- Yalnız sha256 değeri `5e2dc1a15734e214…` olan `p2pcam` sürümü (SC1346 sensör). `build.sh` başka
  sürümü reddeder: yama adresleri sürümden sürüme değişir ve yanlış yama kamerayı yeniden başlama
  döngüsüne sokabilir.
- Temel hack'in **kalıcı** sürümü gerekir (`HACKTYPE=P`, `/home/minihack.sh`): buluta gerek kalmadan
  WiFi, telnet, FTP ve BusyBox httpd ondan gelir.
- Derleme bilgisayarı: `python3` ve `squashfs-tools` (`unsquashfs`, `mksquashfs`, xz) olan Linux ya da macOS.

## Kurulum

1. Temel hack'in kalıcı sürümünü [Jalecom'un README'sindeki](https://github.com/Jalecom/AJ_HC1703L_Teardown) gibi kurun. Telnet, FTP ve
   `http://<kamera>:8080` çalışıyor mu kontrol edin.
2. Kameradan `/bak/p2pcam.sqfs` dosyasını bilgisayarınıza alın (FTP).
3. Derleyin: `patcher/build.sh p2pcam.sqfs out/`. Yamalı `p2pcam` md5 değeri
   `018f4c40708d3a2b3ba0a26343ab0f14` olmalı.
4. SD kartın köküne kopyalayın: `sdcard/clean/`, `sdcard/motion/`, `sdcard/mo.sh`.
   `out/p2pcam.sqfs`, `out/p2pcam.sqfs.md5` ve `out/p2pcam.md5` dosyalarını `clean/` içine koyun.
   **`debug_cmd.sh`'yi henüz kopyalamayın.**
5. Kamerayı açın, telnet ile: `sh /tmp/mnt/mmc01/0/clean/prepare.sh`
   Bu betik `sensor.sh` ve `sensor_day.ini` dosyalarını kameranızın kendi dosyalarından üretir;
   üreticiye ait hiçbir dosya dağıtılmaz.
6. Yükleyiciyi kurun: `cp /tmp/mnt/mmc01/0/clean/debug_cmd.sh /tmp/mnt/mmc01/0/; sync; reboot`
7. Yaklaşık bir dakika sonra paneldeki durum **Yamalı** göstermeli, RTSP akışı
   (`rtsp://<kamera>:554/0/av0`) 1280×720 olmalı.

## Kurtarma

- **Bir açılışlığına stoğa dönmek:** SD kart kökünden `debug_cmd.sh`'yi silin. Bir sonraki sağlıklı
  açılış onu geri koyar.
- **Plus'ı kapatmak:** `touch <SD>/clean/DISABLE` ve kökteki `debug_cmd.sh`'yi silin.
- **Kameraya ulaşılamıyor ya da sürekli yeniden başlıyor:** gücü kesin, SD kartı bilgisayara takın,
  kart kökündeki `debug_cmd.sh`'yi silin, geri takın, gücü verin. Neden olabilir: temel hack'in
  `/bak/sensor.sh` içine eklediği satır, `debug_cmd.sh` dururken `minihack.sh`'yi atlar; WiFi'yi açan da
  `minihack.sh`'dir. Yükleyici çalışamaz ya da kendini silemezse WiFi gelmez ve kamera döngüye girer
  (bir kez gözlendi; büyük olasılıkla `p2pcam`'in WiFi arayüzü kapalı kalınca yaptığı yeniden başlatma).
  Mac kartı salt okunur gösterirse SD adaptörünün yanındaki kilit anahtarına bakın.

## Güvenlik notları

- Kamerayı özel bir ağda tutun, asla internete açmayın.
- RTSP parolasızdır (stok davranış) ve ses de içerir.
- Telnet (parolasız) ve FTP (`root` / `cxlinux`) temel hack'ten gelir: yerel ağınızdaki herkes kamerayı
  yönetebilir.
- Panel ve galeri özel ağ için bilerek girişsizdir.
- Temel hack herkese aynı dropbear sunucu anahtarını dağıtır. SSH açarsanız kendi anahtarınızı üretin.

## Bilinen sınırlar

- Yalnız tek firmware sürümü. Diğer sürümler için yeni adresler gerekir.
- Tek kamerada denendi.
- Bölge değişikliği için yeniden başlatma gerekir. Çalışan `p2pcam`'e yazılan bölgeler okunmadı.
- Takip açıkken bütün hareket kaydedilir, ama kamera yalnız bölge içindeki hareket görüntünün sol ya da sağ
  çeyreğine ulaşınca döner. Takip için bütün kareyi seçili bırakın.
- Takip, görüntünün orta yarısındaki kişiyi izlemez; kameraya tam önden yaklaşan kişi izlenmez. Dikey hareket
  yalnız yatay dönüşle birlikte ve büyük adımlarla olur.
- Her 10. dönüşte ve bir dönüşten 30 dakika sonra kamera, referansını bulmak için yaklaşık 20 sn tarama yapar ve
  kayıtlı konuma gider. İlk seferde başka yere düşerse takibi kapatın, panel oklarıyla ayarlayın, takibi yeniden
  açın.
- HTTPS yok (BusyBox httpd), bu yüzden panel çift yönlü ses için tarayıcı mikrofonunu kullanamaz.

## Teşekkür ve lisans

- Bu projenin üzerine kurulduğu ve ihtiyaç duyduğu [temel hack ve söküm](https://github.com/Jalecom/AJ_HC1703L_Teardown) için
  [@Jalecom](https://github.com/Jalecom) ve orada adı geçen katkıcılar.
- Tarayıcıda oynatma için GPAC'ın mp4box.js 0.5.2 kütüphanesi (BSD 3-Clause) pakete dahildir.
- `sdcard/clean/bin/mdcfg`, `tools/mdcfg.c`'nin statik ARM derlemesidir (derleme komutu kaynakta).
- Bu depoda yalnız HC1703L Plus kodu vardır, MIT lisanslıdır ([LICENSE](LICENSE)). Temel hack pakete
  dahil değildir; kendi deposundan alınır.
- Augentix ya da Tange ile bağlantısı yoktur. Üreticiye ait ikili dağıtılmaz. Kullanım sorumluluğu
  size aittir.

## İsteğe bağlı: sağlık denetimini her açılışta çalıştırmak

Bu olmadan sağlık denetimi yalnız yükleyicinin kurulu olduğu açılışlarda çalışır. Stoğa düşülen
açılışlardan sonra da çalışması için `/home/minihack.sh` dosyasının sonuna şunu ekleyin
(flash yazımıdır, orijinalin kopyasını saklayın):

```sh
( n=0; until grep -q ' /tmp/mnt/mmc01/0 ' /proc/mounts || [ $n -ge 90 ]; do sleep 2; n=$((n+1)); done
  [ -f /tmp/mnt/mmc01/0/clean/health.sh ] && cp /tmp/mnt/mmc01/0/clean/health.sh /tmp/health.sh && sh /tmp/health.sh ) </dev/null >/dev/null 2>&1 &
```
