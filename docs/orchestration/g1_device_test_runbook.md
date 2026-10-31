# G1 Device-Test Runbook — Samsung thật (owner-facing, 2026-10-31)

Kế hoạch gốc: `docs/orchestration/g1_measurement_plan.md` (REV 2 theo GPT round-2). Runbook này là
các bước cụ thể trên máy. **Tất cả con số phải ghi lại thật — không đoán.**

## 0. Chuẩn bị (10 phút)
1. Cài APK debug: `adb install -r app/build/outputs/apk/debug/app-debug.apk`
   (hoặc copy APK vào máy + cài trực tiếp; cho phép "cài từ nguồn không xác định").
2. **Rút tai nghe** — diagnostic loopback (Class 1) yêu cầu đường loa→mic; tai nghe cắm vào
   sẽ làm INVALID_MEASUREMENT (fail-closed theo thiết kế).
3. Phòng yên tĩnh, giữ máy để mặt bàn (loa cách mic vài chục cm).

## 1. Class 1 — Diagnostic acoustic loopback (loa → mic)
1. Mở app → cấp quyền micro nếu hỏi.
2. **Giữ lâu hàng chỉ số** (dòng số liệu) — app bắt đầu đo loopback N=100 chu kỳ
   (NCC ≥ 0.6; fail-closed nếu kém). Chờ đến khi xong (UI hiển thị kết quả).
3. Ghi lại: median/p95 round-trip (ms), số chu kỳ hợp lệ / NCC peak.
   Kết quả này là **đường loa→mic** — KHÔNG phải latency mic→DSP→tai nghe.
4. Nếu hiện INVALID_MEASUREMENT — ghi lý do (routing/amplitude/timeout) rồi thử lại 1 lần.

## 2. Thông tin stream (getStreamInfo)
Trên UI panel, ghi lại các trường: sample rate, buffer capacity, frames per burst,
sharing mode, performance mode. **Ghi đúng giá trị hiển thị** — đây là cơ sở so sánh
Shared vs Exclusive sau này.

## 3. Phiên hát 10 phút (stability + thermal)
1. Cắm tai nghe, hát (hoặc để nhạc đang phát) trong **10 phút liên tục** ở chế độ live.
2. Ghi lại: xruns hiển thị (0 / giá trị / "unsupported"), `missFramesPerSec`,
   p99 histogram nếu hiển thị.
3. Quan sát nhiệt máy + app còn chạy mượt không; nếu xruns tăng về cuối phiên → ghi.
4. Nghe thật: delay có "nghe được" không khi hát (bản năng singer: chậm thấy rõ không).

## 4. Kiểm tra runtime 16 KB
App chạy được = native .so load thành công (CI đã kiểm 16 KB PT_LOAD). Ghi: app mở
và engine khởi động không lỗi (nếu crash khi mở → ghi logcat: `adb logcat -d | tail -50`).

## 5. Class 2 — fixture (chỉ khi có)
Fixture loopback hiệu chuẩn **chưa có** → app sẽ báo MEASUREMENT_UNAVAILABLE (fail-closed).
Bỏ qua bước này lần đầu; gate release (p95 ≤ 50 ms S-series) cần fixture — lên kế hoạch sau.

## 6. Báo lại cho Hermes (dán vào chat)
- Median/p95 Class 1 (ms), chu kỳ hợp lệ/NCC.
- getStreamInfo: sr / bufCap / framesPerBurst / sharing / perfMode.
- Phiên 10 phút: xruns + missFramesPerSec + nhiệt.
- Nghe thật: đánh giá delay (nghe được/không).
- Lỗi nào (nếu có) + logcat.

## Dừng/không làm
- Không tự hạ threshold, không gắn nhãn Class 1 = "latency tổng".
- Không gắn kết quả device lên CI/gate mà không báo Hermes xác nhận trước.
