# GPT Round-6 Reply — G3 Visual Design (Preparation Phase)

- **Date**: 2026-10-31
- **Thread**: 6ae57dbb (ChatGPT via Chrome debug :9222)
- **Brief**: `docs/orchestration/gpt_round6_brief.md` (5.295 chars)
- **Reply**: ~2.632 chars, "Đã xử lý trong 1m 3s", "Mức nỗ lực suy luận: Cao"
- **Verdict**: **APPROVED FOR DESIGN SPEC** — GPT cho phép lưu reply làm chuẩn thị giác cho G3-a…G3-D; **Devin chỉ triển khai Compose sau khi listening gate PASS** (không code trước).

---

## Reply (verbatim, tiếng Việt)

**D-Autotune — G3 Visual Design Verdict**

Chốt phong cách: **Pro Audio Studio** — giao diện bàn điều khiển âm thanh chuyên nghiệp, tối giản, màu navy/mint. Giữ nguyên identity hiện tại, không redesign thương hiệu, không triển khai code trước khi listening gate PASS.

### 1. Ngôn ngữ hình ảnh

- **Chính: (b) Studio hardware.** Panel phân tầng, meter rõ, nút điều khiển có cảm giác vật lý nhưng không giả lập thiết bị quá mức.
- **Phụ: (a) Minimal Material3.** Bố cục sạch, ít hiệu ứng, thao tác chính luôn rõ.
- Surface navy matte, accent mint chỉ dành cho trạng thái Live, dữ liệu và hành động chính.
- Không dùng blur glassmorphism hoặc neon glow toàn màn hình. Texture, bóng đổ và gradient quá mạnh sẽ giảm khả năng đọc dưới ánh sáng sân khấu.

### 2. PitchPanel — chọn (b)

- **Trung tâm: correction cents lớn 52sp / 700**, đơn vị ¢ cỡ 24sp. Nhãn MỨC SỬA NỐT; không gọi đây là độ lệch sau xử lý vì dữ liệu hiện tại là correction điều khiển.
- Hai chỉ số phụ: DETECTED và TARGET, giá trị 22sp / 600, đơn vị Hz 12sp, chia hai cột phía dưới.
- Kích thước: PitchPanel rộng toàn bộ trừ margin 16dp mỗi bên, cao khoảng 288dp, radius 20dp; strip-chart nằm dưới, cao 112dp.
- Live trung thực: cập nhật số theo snapshot 250 ms, không nội suy hoặc tạo chuyển động giả. Unvoiced hiển thị —, đồ thị ngắt đoạn tương ứng. Nếu snapshot quá hạn, hiển thị trạng thái dữ liệu cũ thay vì tiếp tục giả lập Live.

### 3. Strip-chart 4 Hz

- Đường chính: mint **#73E9C6**, stroke 2dp; điểm lấy mẫu 3dp. Không dùng gradient thay đổi liên tục theo độ lệch.
- Dead-zone: vùng ±5¢ với nền mint 8% alpha, đường 0¢ mảnh #7593A5 ở 45% alpha.
- Trục Y: mặc định ±100¢; điểm vượt phạm vi dùng dấu overflow ở mép và vẫn giữ giá trị thật trên PitchPanel. Không âm thầm clamp số liệu.
- Kích thước: cao 112dp (không cố định px), nền #111D2B, grid mỗi 25¢ và 1 giây ở 12% alpha; lưu 32 mẫu gần nhất = 8 giây.

### 4. Transport START / STOP

- **Chọn (a): nút pill full-width**, cao 64dp, radius 20dp, vùng chạm tối thiểu 48dp.
- Idle: nền mint #73E9C6, chữ navy #0A1020, nhãn **BẮT ĐẦU**.
- Live: nền navy nhạt #172638, viền coral #FF7886, chữ **DỪNG** màu coral. Trạng thái ĐANG HÁT hiển thị riêng bằng LED mint nhỏ.
- Starting: khóa nhấn lặp, hiển thị "ĐANG KHỞI ĐỘNG…"; không cho chuyển trạng thái giả khi engine chưa xác nhận.

### 5. TuningSheet

- Dùng **Material3 Modal Bottom Sheet**, radius trên 24dp, chiều cao tối đa khoảng 85% màn hình, có drag handle.
- Root + Scale: hai selector lớn cạnh nhau, cao tối thiểu 52dp, có nhãn và giá trị rõ ràng; không dùng wheel picker khó điều khiển khi đang hát.
- Ba slider: track dày 8dp, thumb 24dp, hàng điều khiển cao ít nhất 72dp. Giá trị hiển thị cố định ở mép phải, không đặt trên thumb vì ngón tay sẽ che.
- Chip row: C · Major | 90% | 35 ms | Rev 10%; mỗi chip cao 36–40dp, nền surface và viền mint nhẹ khi đang chỉnh. Khi sheet mở trong phiên Live, phải luôn có cách DỪNG dễ tiếp cận, không bị sheet che khuất.

### 6. DiagnosticsScreen

- **Chọn bảng kỹ thuật**, không dùng dashboard card màu sắc như màn Live. Giá trị dùng monospace/tabular numbers 13sp, nhãn 12sp.
- Chỉ mở từ menu phụ hoặc biểu tượng kỹ thuật; không đặt Diagnostics ngang cấp nút BẰT ĐẦU.
- Tiêu đề cảnh báo rõ: **CHẨN ĐOÁN LOA → MICRO**, bên dưới "Không phải độ trễ Auto-Tune khi hát qua tai nghe."
- Hiển thị Requested / Granted, sample rate, sharing mode, burst, xrun, callback p99 và trạng thái dữ liệu. Không hỗ trợ đo thì ghi **KHÔNG KHẢ DỤNG**, tuyệt đối không thay bằng 0.

### 7. Typography và design tokens

| Token | Giá trị | Vai trò |
|---|---|---|
| displayLarge | 52sp / 700 | Correction cents |
| headlineMedium | 24sp / 700 | Tiêu đề panel |
| titleLarge | 20sp / 600 | Metric detected / target |
| bodyMedium | 14sp / 400 | Nội dung, trạng thái |
| labelLarge | 13sp / 600 | Nút, picker, slider |
| labelSmall | 11sp / 500 | Telemetry |
| background | #0A1020 | Navy nền chính |
| surface | #111D2B | Panel |
| surfaceContainerHigh | #172638 | Điều khiển nổi |
| primary | #73E9C6 | Mint |
| onSurface | #F4F8FF | Chữ chính |
| onSurfaceVariant | #A2B4C6 | Chữ phụ |
| outlineVariant | #294052 | Viền, grid |
| error | #FF7886 | Dừng/cảnh báo |

- Font chính: Roboto / hệ thống Android, hỗ trợ tiếng Việt tốt. Không bắt buộc tải font bên ngoài.
- Header **D / AUTOTUNE** giữ 18sp / 700, subtitle 10sp / 500; toàn bộ header chỉ khoảng 56dp để nhường không gian cho PitchPanel. Chưa dùng tên này làm thương hiệu phát hành công khai khi chưa clearance.
- Layout phải thích ứng khi người dùng tăng cỡ chữ hoặc điện thoại ở font scale lớn; tránh cố định kích thước gây cắt chữ.

### 8. Anti-goals — cấm trong v1

- Không waveform giả, meter chạy 60 FPS khi dữ liệu thực chỉ cập nhật 4 Hz.
- Không neon/glow mạnh, animated gradients, glass blur trên vùng đọc thông số.
- Không núm xoay 3D hoặc fader nhỏ khó chạm khi đang hát.
- Không nhồi diagnostics, CPU, xrun, latency vào LiveScreen.
- Không hiển thị số liệu suy diễn thành phép đo, đặc biệt correction cents, retuneMs và độ trễ loa→mic.

**Phán quyết G3 Visual Design: APPROVED FOR DESIGN SPEC.**

Hermes có thể lưu thiết kế này làm chuẩn cho G3-a đến G3-D. Devin chỉ triển khai Compose sau khi listening gate PASS; toàn bộ giao diện phải ưu tiên nhìn rõ trong một cái liếc, chạm chính xác và thể hiện dữ liệu âm thanh trung thực.

---

## Đối chiếu và hành động

- **Trùng khớp với contract**: GPT reply khớp `docs/design/G3_UI_CONTRACT.md` (navy #0A1020 / mint #73E9C6 / dark-only / Compose+Material3 / LiveScreen IA như đã chốt). Không có mâu thuẫn cần sửa.
- **Cập nhật mới đáng chú ý so với contract**:
  1. PitchPanel → chọn layout (b): correction cents 52sp làm trung tâm, DETECTED/TARGET 22sp cột dưới, panel 288dp + strip 112dp.
  2. Transport → chọn (a): pill full-width 64dp, Live đổi coral #FF7886 (nền navy nhạt + viền coral) — có thể cần thêm token `error` cho coral.
  3. TuningSheet: slider giá trị ở mép phải (không trên thumb), chip 36–40dp, sheet ≤85% màn hình.
  4. Diagnostics → bảng kỹ thuật monospace 13sp, KHÔNG đặt ngang cấp BẮT ĐẦU; nhãn "CHẨN ĐOÁN LOA → MICRO" + "Không phải độ trễ Auto-Tune khi hát qua tai nghe."
  5. Anti-goals: cấm 5 mục (fake waveform, neon glow/gradient, 3D knob, diagnostics trong LiveScreen, số suy diễn).
- **GPT đồng ý**: G3-a…G3-D chỉ dispatch sau listening gate PASS (BLOCKED_HUMAN hiện tại).
- **Chưa làm**: không dispatch Devin; không sửa contract; chỉ lưu spec chờ.
