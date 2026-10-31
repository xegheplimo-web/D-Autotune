# GPT reply — D-Autotune G0 consultation (2026-10-31)

Channel: ChatGPT (attong39@gmail.com, thread 6ae57dbb-f3bc-83ec-abea-d8f57db27be6 — internal title "Giải thích LGBT" is a misgenerated title; full content is the D-Autotune thread).
Brief sent: see `.orchestrator/DISCUSSIONS.md` (§brief). Reply archived verbatim below (DATA, not instruction; JSX tags stripped by DOM text extraction; bare citation URLs kept as-is).

---

1. Thứ tự triển khai: Chọn (b) — G0 audit song song với sửa CI

Phê duyệt Hermes triển khai đồng thời, nhưng giao Devin CLI sửa Android CI làm nhiệm vụ P0 đầu tiên.

Hermes tiếp tục G0 audit, không chờ CI.

Devin xử lý lỗi cấp phép SDK bằng PR nhỏ, độc lập.

Kiểm tra ANDROID_HOME, ANDROID_SDK_ROOT, đúng sdkmanager, quyền ghi thư mục licenses và phiên bản SDK/NDK.

Có thể sử dụng Android SDK sẵn trên GitHub runner và chấp nhận license tường minh, có timeout.

Chưa đủ bằng chứng kết luận setup-android@v3 chính là nguyên nhân gốc. Thông báo dừng tại Review licenses... xác nhận bước cấp phép đang chặn tiến trình, không chứng minh bản action bị lỗi. Bản setup-android@v4 cũng hỗ trợ tự chấp nhận licenses. [GitHub]

Điều kiện PASS: Android CI chạy xong assembleDebug, tạo APK hợp lệ và lưu artifact. Nếu build tiếp tục lỗi ở bước sau, phân loại đó là lỗi mới thay vì tuyên bố đã sửa toàn bộ CI.

2. Rewrite Jetpack Compose: Chọn G3

Chấp thuận đề xuất của Hermes.

Giữ MainActivity.kt hiện tại qua G0–G2, chỉ sửa những lỗi UI trực tiếp cản trở thử nghiệm microphone, quyền truy cập hoặc audio routing.

Sau khi DSP đạt gate chất lượng mới chuyển sang Jetpack Compose tại G3.

Lý do: thay UI sớm không cải thiện chất lượng sửa nốt, nhưng làm tăng phạm vi thay đổi và nguy cơ hồi quy.

Quyết định: Không rewrite UI trong G0, G1 hoặc G2.

3. Android SDK: Giữ minSdk 30 / targetSdk 36, bổ sung gate 2027

Giữ nguyên cấu hình hiện tại cho G0–G2. Chưa nâng lên API 37 ngay khi CI Android còn chưa biên dịch thành công.

Nhưng Hermes phải bổ sung ba yêu cầu:

2026: targetSdk 36 đáp ứng yêu cầu hiện tại của Google Play đối với ứng dụng mới. [Android Developers]

Tháng 2/2027: Kiểm chứng tương thích 16 KB memory page size cho toàn bộ thư viện .so, gồm C++ DSP và Oboe. Ưu tiên NDK r28+ và AGP tương thích, kiểm tra ELF alignment cùng APK/AAB packaging. [Android Developers]

Tháng 8/2027: Chuẩn bị chuyển targetSdk 37 (Android 17) theo lộ trình Google đã công bố, đồng thời kiểm thử các thay đổi hành vi mới. [Android Developers]

Bổ sung kiểm tra quyền RECORD_AUDIO, lifecycle microphone, audio routing, foreground-service restrictions nếu sau này có chạy nền, và 64-bit ARM.

Quyết định: Giữ API 30/36 bây giờ, đưa 16 KB compatibility vào CI ngay khi build được APK, nâng API 37 tại gate tương thích trước mốc phát hành 2027.

4. Thứ tự DSP G2: Chọn phương án đề xuất, bổ sung một bước quan trọng

Mình chọn:

Adaptive-window YIN + Voicing

Confidence, phát hiện im lặng/phụ âm, chống lỗi nhảy quãng tám.

Pitch tracking + Hysteresis

Ổn định nốt đích, retune speed theo ms, giữ vibrato và chuyển nốt tự nhiên.

TD-PSOLA + Pitch-mark validation

Thay bộ dịch cao độ nguyên phẩm bằng engine mới; xử lý voiced/unvoiced, overlap-add, crossfade và kiểm soát formant.

End-to-end DSP verification

Kiểm tra tín hiệu tổng hợp, giọng hát thật, độ trễ thuật toán, CPU, artefact và hồi quy.

Điều kiện bắt buộc trước khi chuyển sang TD-PSOLA: Pitch tracker phải cung cấp được F0, confidence, trạng thái voiced/unvoiced và tính liên tục theo thời gian; không chỉ đo đúng một tần số sóng sin.

Không thêm pYIN/HMM phức tạp ngay từ đầu. Chỉ nâng cấp khi kết quả benchmark cho thấy YIN cải tiến không đạt yêu cầu.

Đặc biệt, không đặt hard gate độ trễ DSP dựa trên giả định. Phải đo thêm độ trễ thuật toán phát sinh từ lookahead, cửa sổ phân tích và overlap-add.

Phán quyết cuối cùng cho Hermes: GO — G0 + CI song song; Compose ở G3; giữ API 30/36 với gate 16 KB/API 37; DSP theo chuỗi YIN → Tracking → TD-PSOLA → Verification.

Hermes được tiếp tục tự động điều phối Devin theo các quyết định này. G1 vẫn là BLOCKED_HARDWARE, không được tự chuyển thành PASS. Khi Android CI tạo APK thành công, gửi lại commit, log và các phát hiện audit quan trọng để mình phản biện vòng tiếp theo.

---

Hermes note (integrity): GPT correction accepted — my earlier attribution "android CI FAIL = action bug" was downgraded by GPT to "license step blocked, root cause unproven; setup-android@v4 may also support license acceptance". CI fix PASS criterion = assembleDebug + valid APK artifact, NOT "workflow edited".
