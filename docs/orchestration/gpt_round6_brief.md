# D-Autotune — Round 6: UI visual design (G3, preparation phase)

Bối cảnh: app Android realtime pitch-correction (mic → DSP → tai nghe, ca sĩ hát live). DSP chain đã host-verified (D0–D8 landed); blind listening pack v2 sẵn sàng (3 người nghe); thiết bị Samsung thật sắp test (G1). **G3 Compose vẫn gate trên listening PASS** — hôm nay chỉ xin ý kiến THIẾT KẾ HÌNH ẢNH, không code.

## Kiến trúc UI đã chốt (docs/design/G3_UI_CONTRACT.md, bạn đã phê duyệt hướng preparation ở round-4)
- Jetpack Compose + Material3, **dark theme only** (v1), 1 Activity + 1 ViewModel, engine không đổi.
- IA: LiveScreen (root) = SafetyBanner trên cùng → PitchPanel (detected Hz · target Hz · correction cents + strip-chart) → transport START/STOP lớn → chip row mở TuningSheet (bottom sheet, chỉnh được khi đang live) → DiagnosticsScreen (rare).
- State machine: Idle → Starting → Live; diagnostic loopback chỉ chạy khi Idle; tai nghe ngắt → về Idle + thông báo; screen luôn sáng khi Live.
- **Trung thực dữ liệu**: snapshot poll 250 ms (4 Hz) — không fabricate motion; khi unvoiced hiển thị "—" (không đóng băng giá trị cũ); strip-chart vẽ từ chính các mẫu 250 ms đó.
- Kiểm soát telemetry trung thực (nhãn đã chốt): số loa→mic là "CHẨN ĐOÁN loa→mic", retuneMs gắn "(control)", không bao giờ ghi là độ trễ tổng.

## Identity hiện tại (giữ, không redesign thương hiệu)
- Nền navy `rgb(10,16,32)`, accent mint `rgb(115,233,198)`, text trắng `rgb(244,248,255)`, muted `rgb(162,180,198)`.
- Tiêu đề "D / AUTOTUNE", subtitle "LIVE VOCAL PROCESSOR · ALPHA 0.1". UI tiếng Việt mặc định (+ `values-en` sau).
- 3 metric boxes (detected/target/correction, số ~19sp), telemetry line 11sp, 2 picker + 3 slider (ĐỘ MẠNH SỬA NỐT 0–100%, TỐC ĐỘ 5–150 ms, REVERB 0–25%), nút START lớn 60 dp.

## Câu hỏi (trả lời theo số, ngắn và cụ thể)

1. **Ngôn ngữ hình ảnh "đẹp hiện đại" cho app nhạc live**: với identity navy/mint đã chốt, bạn đề xuất hướng nào giữa các lựa chọn: (a) flat/minimal Material3 thuần, (b) mixing-desk / studio hardware (panel có texture nhẹ, nút kiểu phần cứng, LED meters), (c) neon glow trên nền tối (accent mint phát sáng nhẹ), (d) glassmorphism (panel mờ nhạt blur). Chọn 1 hướng chính + 1 hướng phụ, nêu lý do ngắn theo ngữ cảnh: ca sĩ nhìn trong lúc hát, ánh sáng phòng/ánh sáng sân khấu, không gây mỏi mắt khi dùng lâu. Cảnh báo rủi ro mỗi hướng (gلاف mờ = GPU cost, neon = nhạt nhòa trên OLED cũ…).

2. **PitchPanel là "trái tim" màn hình** — ca sĩ cần nhìn thấy trong 1 cái liếc khi đang hát: bố cục nào tốt? (a) 3 số lớn ngang hàng như hiện tại, (b) SỐ LỚN correction cents ở giữa + 2 số phụ, (c) đồng hồ/needle trung tâm + strip-chart bên dưới. Lưu ý trung thực: chỉ 250 ms cadence — làm sao cho cảm giác "live" mà không giả tạo? Đề xuất kích thước/wải nén cụ thể (sp/dp) cho 1 màn hình 6.5".

3. **Strip-chart (4 Hz canvas)**: màu gì cho đường correction cents (mint? gradient theo |cents|?), vùng dead-zone ±5¢ tô kiểu nào, grid/dự phòng nền thế nào cho dễ đọc trên navy? Đề xuất chiều cao px + phạm vi Y (±100¢?).

4. **Transport START/STOP**: kiểu dáng nào — (a) nút pill full-width mint, (b) nút tròn lớn giữa màn hình, (c) nút nhỏ + tap-to-toggle? Màu khi Live (đang hát) so với Idle — dùng mint liên tục hay đổi màu (đỏ để STOP)? Text tiếng Việt: "BẮT ĐẦU"/"DỪNG"?

5. **TuningSheet (bottom sheet)**: 2 picker (root/scale) + 3 slider — trình bày kiểu nào cho hiện đại mà vẫn dễ chạm khi đang live (ngón tay che nửa dưới)? Vòng chip hiện giá trị (key · scale · 90% · 35 ms · reverb%) — kiểu nào? Slider có nên có track dày + thumb to + giá trị hiện trực tiếp trên thumb?

6. **DiagnosticsScreen**: màn hình hiếm khi mở — trình bày như bảng kỹ thuật (mono font, số thẳng hàng) hay như dashboard card? Cách nào tránh ngộ nhận "đây là phần chính của app"? Nhãn "CHẨN ĐOÁN loa→mic" nên đặt/hiển thị thế nào để không bao giờ bị đọc là "độ trễ tổng"?

7. **Typography + tokens**: hệ chữ Material3 dark với identity navy/mint — đề xuất 1 bảng tokens cụ thể (display/headline/body/label + màu role mapping navy→surface, mint→primary, muted→onSurfaceVariant). Tiêu đề "D / AUTOTUNE" giữ ở đâu (to hơn/giảm để nhường chỗ cho pitch panel)?

8. **Anti-goals**: những gì bạn thấy NÊN CẤM trong v1 để app không rơi vào kiểu "generic Material app" hoặc "gimmick AMOLED" — liệt kê 5 mục cụ thể.

Trả lời ngắn gọn theo số, mỗi mục 2–5 gạch đầu dòng. Nếu cần giá trị cụ thể (sp/dp/hex/alpha) thì đưa số thật — Devin sẽ đọc reply này làm spec thị giác cho G3-a…G3-D sau khi listening PASS.
