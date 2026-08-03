# Giám Sát Máy Chủ Proxmox VE Bằng Arduino Uno & Màn Hình TFT 3.5" ILI9486

Dự án này giúp hiển thị thời gian thực các thông số hoạt động của máy chủ cá nhân chạy ảo hóa Proxmox VE lên màn hình cảm ứng TFT LCD 3.5 inch sử dụng chip điều khiển **ILI9486** (loại shield 8-bit parallel cắm trực tiếp lên Arduino Uno).

---

## 🚀 Tính năng nổi bật

- **Giao diện Modern Dark Mode:** Thiết kế tinh gọn, độ tương phản cao giúp dễ dàng quan sát từ xa.
- **Cảm ứng chuyển trang (Multi-page):** Chạm bất kỳ đâu trên màn hình chính để chuyển đổi giữa 3 trang thông tin:
  - **Trang 1: Tải hệ thống (Overview)** - Tỉ lệ sử dụng CPU (thanh bar), RAM (thanh bar), Nhiệt độ CPU hiện tại và Uptime của Node.
  - **Trang 2: Lưu trữ & Nhiệt độ Ổ cứng (Storage & Disk Temps)** - Hiển thị 1 ổ đĩa SSD hệ thống (Dung lượng, % Sử dụng, Nhiệt độ °C) và danh sách tối đa 6 ổ đĩa HDD (Tên ổ, Dung lượng, % Sử dụng, Nhiệt độ °C riêng từng ổ).
  - **Trang 3: Quản lý Máy ảo & Dịch vụ (Workloads & Health)** - Phân loại chi tiết chính xác và gọn gàng số lượng đang chạy (Active) / đang tắt (Offline) cho 3 nhóm: Máy ảo **QEMU VMs**, Container **LXC Containers**, và Container dịch vụ **Docker Services**, kèm tổng hợp tổng số Workload và trạng thái Node/Agent.
- **Tối ưu hiển thị (Không nhấp nháy):** Áp dụng phương pháp nạp nền chữ tĩnh và vẽ ghi đè vùng giá trị thay đổi, tránh tình trạng màn hình bị giật/chớp nháy khi cập nhật dữ liệu.
- **Cảnh báo mất kết nối:** Nếu quá 10 giây không nhận được gói tin Serial từ Server, đèn trạng thái trên góc phải sẽ chuyển sang màu **ĐỎ** và hiển thị trạng thái `OFFLINE` / `NO SIGNAL`.
- **Sử dụng bộ nhớ tối ưu:** Sử dụng mảng ký tự tĩnh (`char array`) và Macro `F()` giúp tiết kiệm tối đa bộ nhớ RAM khi chạy trên Arduino Uno (chỉ có 2KB SRAM).

---

## 🔌 Sơ đồ lắp đặt phần cứng

1. **Lắp màn hình:** Cắm shield TFT LCD 3.5" trực tiếp chồng lên board Arduino Uno. Các chân LCD sẽ khớp hoàn toàn với chân cắm của Arduino Uno.
2. **Kết nối máy chủ:** Cắm cáp USB nối từ cổng USB của Arduino Uno vào một trong các cổng USB trên máy chủ Proxmox VE (hoặc máy tính test).

---

## 🛠️ Hướng dẫn cấu hình phần mềm

### 1. Nạp chương trình lên Arduino Uno

Dự án hiện đã hỗ trợ đầy đủ **PlatformIO** (khuyên dùng) và **Arduino IDE**.

#### Cách 1: Nạp qua PlatformIO (Khuyên dùng)
1. Mở thư mục dự án `Server_Monitor` bằng **VS Code** có cài extension PlatformIO.
2. PlatformIO sẽ tự động tải các thư viện cần thiết (`Adafruit GFX`, `MCUFRIEND_kbv`, `TouchScreen`, `Adafruit BusIO`).
3. Nhấn nút **Build** (hoặc chạy lệnh `pio run`) để biên dịch.
4. Nhấn nút **Upload** (hoặc chạy lệnh `pio run --target upload`) để nạp code vào Arduino.

#### Cách 2: Nạp qua Arduino IDE
1. Mở file [ServerMonitorTFT.ino](arduino/ServerMonitorTFT/ServerMonitorTFT.ino).
2. Cài đặt các thư viện qua Library Manager: `Adafruit GFX Library`, `MCUFRIEND_kbv`, `TouchScreen`, `Adafruit BusIO`.
3. Chọn Board **Arduino Uno** và đúng cổng COM/Serial Port.
4. Nhấn **Upload**.

---

### 2. Chạy thử nghiệm giả lập (Local Mock Test)

Trước khi cấu hình trên server Proxmox, bạn có thể kiểm tra xem màn hình và cảm ứng hoạt động tốt hay không bằng cách cắm Arduino Uno vào máy tính cá nhân (Windows/macOS) và chạy script giả lập dữ liệu:

1. Đảm bảo đã cài đặt thư viện điều khiển Serial cho Python:
   ```bash
   pip install pyserial
   ```
2. Chạy file giả lập [mock_agent.py](host_agent/mock_agent.py):
   ```bash
   python host_agent/mock_agent.py
   ```
   *Script sẽ tự động tìm cổng kết nối của Arduino và bắt đầu gửi các thông số thay đổi liên tục. Chấm đỏ góc phải sẽ chuyển sang màu xanh lá và màn hình sẽ bắt đầu hiển thị đồ thị và số liệu.*

---

### 3. Cấu hình trên máy chủ Proxmox VE (Production)

Khi màn hình đã chạy thử nghiệm thành công, bạn tiến hành triển khai lên server Proxmox VE:

1. **Kết nối Arduino:** Cắm Arduino Uno vào cổng USB của server Proxmox VE.
2. **Cài đặt thư viện trên Proxmox:** Đăng nhập vào shell của Proxmox (qua giao diện Web hoặc SSH) và chạy lệnh cài đặt thư viện Serial:
   ```bash
   apt-get update && apt-get install -y python3-serial
   ```
   *(Cách này an toàn và không gây xung đột gói hệ thống Debian của Proxmox).*
3. **Sao chép File Agent:** Chép file script [monitor_agent.py](host_agent/monitor_agent.py) lên máy chủ (ví dụ lưu tại thư mục `/root/monitor/monitor_agent.py`).
4. **Phân quyền thực thi:**
   ```bash
   chmod +x /root/monitor/monitor_agent.py
   ```
5. **Chạy thử nghiệm:**
   ```bash
   python3 /root/monitor/monitor_agent.py
   ```
   *Bạn sẽ thấy các thông số thật của server Proxmox như tải CPU, RAM, nhiệt độ, và số lượng máy ảo VM/CT được vẽ lên màn hình.*

---

## ⚙️ Cấu hình Tự động chạy khi cắm cổng USB (Auto-Run on USB Plug-in)

Để chương trình tự động kích hoạt **ngay lập tức khi bạn cắm Arduino vào cổng USB** của máy chủ Proxmox, và tự động tắt (không chạy ngầm vô ích) khi rút ra, hãy thực hiện cấu hình kết hợp **udev rules** và **systemd service** như sau:

### Bước 1: Thiết lập udev rule (Nhận diện thiết bị cắm vào)
1. Tạo file cấu hình udev mới trên máy chủ:
   ```bash
   nano /etc/udev/rules.d/99-arduino-monitor.rules
   ```
2. Dán nội dung cấu hình từ file [99-arduino-monitor.rules](host_agent/99-arduino-monitor.rules) vào:
   ```udev
   # Arduino Uno (Original)
   SUBSYSTEM=="tty", ATTRS{idVendor}=="2341", ATTRS{idProduct}=="0043", SYMLINK+="arduino_monitor", TAG+="systemd"
   # CH340 USB-to-Serial (Các bản clone Arduino)
   SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", SYMLINK+="arduino_monitor", TAG+="systemd"
   # CP210x USB-to-Serial
   SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", SYMLINK+="arduino_monitor", TAG+="systemd"
   # FTDI FT232 USB-to-Serial (Các bản clone Arduino Nano)
   SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", SYMLINK+="arduino_monitor", TAG+="systemd"
   ```
   *(Bạn có thể sao chép trực tiếp file cấu hình có sẵn từ thư mục dự án vào `/etc/udev/rules.d/`)*.
3. Tải lại udev rules để áp dụng thay đổi:
   ```bash
   udevadm control --reload-rules && udevadm trigger
   ```

### Bước 2: Cấu hình systemd Service (Liên kết với cổng USB)
1. Tạo file dịch vụ mới:
   ```bash
   nano /etc/systemd/system/pve-monitor.service
   ```
2. Dán nội dung cấu hình từ file [pve-monitor.service](host_agent/pve-monitor.service) vào:
   ```ini
   [Unit]
   Description=Proxmox TFT Monitor Data Agent
   BindsTo=dev-arduino_monitor.device
   After=dev-arduino_monitor.device

   [Service]
   Type=simple
   ExecStart=/usr/bin/python3 /root/monitor/monitor_agent.py
   Restart=always
   RestartSec=5
   User=root

   [Install]
   WantedBy=dev-arduino_monitor.device
   ```
   *(Đảm bảo đường dẫn trong `ExecStart` trỏ đúng tới vị trí của file `monitor_agent.py` trên server)*.
3. Nạp lại systemd daemon và kích hoạt service:
   ```bash
   systemctl daemon-reload
   systemctl enable pve-monitor.service
   ```
   *Lưu ý: Không cần chạy `systemctl start` thủ công. Ngay khi bạn cắm Arduino vào cổng USB, hệ thống sẽ tự động kích hoạt service này. Khi rút Arduino ra, service sẽ tự động dừng lại.*

### Bước 3: Kiểm tra hoạt động
1. Cắm Arduino vào máy chủ Proxmox.
2. Kiểm tra xem file symlink `/dev/arduino_monitor` đã được tạo tự động chưa:
   ```bash
   ls -l /dev/arduino_monitor
   ```
3. Kiểm tra trạng thái của service để đảm bảo chương trình đang chạy:
   ```bash
   systemctl status pve-monitor.service
   ```

---

## 🔍 Giải quyết các sự cố thường gặp (Troubleshooting)

1. **Màn hình hiển thị toàn màu trắng:**
   - Đảm bảo bạn đã cắm khít tất cả các chân shield màn hình vào Arduino Uno.
   - Một số màn hình dùng IC driver biến thể khác, hãy xem log nạp trong Serial Monitor xem `tft.readID()` trả về mã ID nào. Nếu ID trả về là `0x9486` mà màn vẫn trắng, thử ép ID trong hàm `setup()` thành `tft.begin(0x9486)`.

2. **Cảm ứng chạm không nhận hoặc bị lệch tọa độ:**
   - Trong file [ServerMonitorTFT.ino](arduino/ServerMonitorTFT/ServerMonitorTFT.ino), tìm phần khai báo biến hiệu chuẩn: `TS_LEFT`, `TS_RT`, `TS_TOP`, `TS_BOT`.
   - Nếu chạm bên phải mà màn hình hiểu bên trái, hãy tráo đổi giá trị của `TS_LEFT` và `TS_RT`. Nếu chạm trên dưới bị ngược, tráo đổi `TS_TOP` và `TS_BOT`.

3. **Lỗi phân quyền Serial trên Proxmox:**
   - Nếu chạy script Python báo lỗi `Permission denied` đối với cổng `/dev/ttyACM0` hoặc `/dev/ttyUSB0`, hãy cấp quyền truy cập cổng serial cho user (hoặc chạy script dưới quyền `root`).
