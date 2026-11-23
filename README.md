# Nama proyek
### Smart Rain-Aware Drying Rack Berbasis ESP32 dengan Sensor Hujan (FC-37), Sensor Kelembapan (DHT22), Motor Servo (SG90), dan Kipas DC

Tema proyek
  Internet of Things (IoT) untuk Smart Home, khususnya otomasi perlindungan barang rumah tangga dari cuaca.

Deskripsi singkat fungsi proyek
  Smart Rain-Aware Drying Rack adalah miniatur jemuran pintar yang melindungi jemuran dari hujan secara otomatis. Sensor hujan FC-37 mendeteksi air yang mengenai papan sensor, lalu ESP32 memproses sinyalnya dan memerintahkan motor servo SG90 menggerakkan penutup atau rak ke posisi terlindung. Selama jemuran berada pada posisi terlindung, kipas dapat dinyalakan berdasarkan pembacaan DHT22 atau perintah pengguna dari aplikasi agar kelembapan tidak tertahan pada kain. Saat sensor tidak lagi mendeteksi air, servo mengembalikan mekanisme ke posisi terbuka. Melalui Wi-Fi bawaan ESP32, sistem juga dapat mengirim notifikasi dan mencatat log kejadian hujan kepada pengguna. 
