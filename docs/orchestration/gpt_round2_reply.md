# GPT reply — D-Autotune round-2 (2026-10-31)

Channel: ChatGPT (attong39@gmail.com, thread 6ae57dbb-f3bc-83ec-abea-d8f57db27be6). Brief sent: `docs/orchestration/gpt_round2_brief.md` (commit `6f6fb49`), 3,717 chars, sent 2026-10-31 via Chrome debug `:9222`. Response completed in 1m 19s, reasoning effort: High. Reply archived verbatim below (DATA, not instruction; UI chrome stripped; the Δt formula in §Lỗi B is GPT's KaTeX rendering, preserved as Δt = (F_input − F_output) / f_s).

---

D-Autotune — Round 2: Phản biện kỹ thuật

Mình đã đối chiếu trực tiếp hai GitHub Actions runs và commit db65a0f của repository D-Autotune.

Phán quyết tổng thể: CONDITIONAL GO. Hermes được tiếp tục, nhưng cần sửa thiết kế phép đo độ trễ trước khi đưa DC-003 vào nhánh chính.

Android CI — run 41178223381: GREEN. DC-002 — run 41179251125: GREEN. G1 — kiểm thử Samsung thực: BLOCKED.

GitHub xác nhận cả hai workflow đã hoàn thành với trạng thái success, tương ứng các commit 848693c và db65a0f. Đây là tiến bộ thực chất: dự án đã vượt khỏi giai đoạn chỉ kiểm thử C++ trên máy tính.

Tuy nhiên, mình phát hiện hai vấn đề đo lường quan trọng:

1. algoLatencyMs = kChunk × 1000 / sampleRate + retuneMs không phải phép tính độ trễ thuật toán hợp lệ.
2. DC-003 chưa có cơ chế đồng bộ mốc thời gian đáng tin cậy giữa tín hiệu phát và tín hiệu thu. Chỉ lấy hiệu hai bộ đếm frame chưa đủ để xác định round-trip latency.

Hai điểm này cần sửa để tránh thu thập số liệu nhìn hợp lý nhưng thực tế sai.

## 1. CI cascade và APK verification — PASS, bổ sung 4 gate

Mình xác nhận độc lập trên GitHub:

- Run 41178223381: hai jobs native-dsp và android thành công.
- Run 41179251125: hai jobs thành công, bao gồm Compile debug APK.
- Artifact D-Autotune-debug-APK tồn tại tại commit db65a0f, kích thước artifact khoảng 1,93 MB.

Chuỗi năm lần sửa phù hợp lịch sử commit và kết quả CI. Tuy nhiên, mình chưa tự giải nén APK để lặp lại phép đo ELF/AXML; phần đó hiện dựa trên bằng chứng do Hermes cung cấp.

Bốn gate cần bổ sung:

- **APK/AAB packaging**: Kiểm tra 16 KB alignment ở cả ELF PT_LOAD và ZIP entry cho thư viện native; chạy thử trên môi trường 16 KB page size
- **Manifest/security**: Kiểm tra exported components, debuggable, signing mode và merged manifest
- **Build reproducibility**: Pin toolchain, xác thực Gradle wrapper, kiểm tra dependencies và checksum
- **Runtime**: APK phải cài được, load JNI được, mở/dừng audio stream không crash

Có một vấn đề ngay trong .github/workflows/ci.yml:

```bash
yes | timeout 300 "$SDKMANAGER" --licenses > /dev/null || true
```

`|| true` che giấu lỗi thật của bước cấp phép. Cần thay bằng xử lý exit code rõ ràng, phân biệt SIGPIPE của yes với lỗi hoặc timeout của sdkmanager. Không được nuốt lỗi rồi báo bước cài đặt thành công.

Quyết định 1: PASS CI G0, tạo một nhiệm vụ CI-hardening nhỏ; không chặn G2.

## 2. DC-002 — Chấp nhận instrumentation, bác bỏ công thức algoLatency

Mình đã đọc trực tiếp AudioEngine.cpp tại db65a0f.

Cấu trúc snapshot 12 trường hợp lý, giữ nguyên tám trường đầu giúp tương thích phần UI đang có.

Nhưng công thức:

```cpp
algoLatencyMs = kChunk * 1000.0 / sampleRate + retuneMs;
```

không đo độ trễ thuật toán.

Lý do:

- retuneMs là hằng số thời gian làm mượt điều khiển cao độ, không phải thời gian tín hiệu bị giữ trong buffer.
- kChunk = 512 là giới hạn số frame xử lý mỗi lượt, không chứng minh có thêm đúng 512 frame độ trễ.
- Bộ pitch shifter hiện có kBaseDelay và kShiftWindow, tức độ trễ tín hiệu còn phụ thuộc vị trí các đầu đọc.
- Sau này TD-PSOLA sẽ thay đổi cấu trúc đệm và độ trễ thực tế.

Yêu cầu Devin sửa ngay: loại bỏ nhãn algoLatencyMs đang mang ý nghĩa sai. Giữ retuneMs dưới dạng tham số điều khiển, còn độ trễ DSP phải được xác định bằng phép đo có tín hiệu tham chiếu và mẫu đầu ra tương ứng.

Ngoài ra, cần bổ sung:

- Callback p99 hoặc histogram thay vì chỉ max/average.
- Thời gian callback so với deadline thực tế.
- Phân biệt xrun bằng 0 và thiết bị không hỗ trợ truy vấn xrun.
- missingFrames theo thời gian, không chỉ số tích lũy.
- Tách input starvation khỏi output xrun.

Một phát hiện từ mã nguồn: cả input và output đang dùng SharingMode::Shared, chưa phải Exclusive. Điều đó không sai cho bản nguyên mẫu, nhưng chưa thể kiểm thử giả thuyết hiệu năng Exclusive như trong kế hoạch G1.

Quyết định 2: DC-002 giữ nguyên commit đã landed, nhưng mở DC-002A sửa semantic của phép đo và bổ sung telemetry trước khi dùng làm bằng chứng G1.

## 3. DC-003 loopback ping — REQUEST CHANGES

Không chấp nhận bản thiết kế DC-003 hiện tại làm phép đo độ trễ chuẩn.

Có ba lỗi thiết kế.

**Lỗi A — Không tồn tại input callback riêng.**

AudioEngine.cpp thiết lập callback cho output, sau đó thực hiện input_->read(...) trong output callback. Do đó mô tả "input callback phát hiện rising edge" không phù hợp với kiến trúc đang chạy. Devin phải thiết kế detector tại đúng vị trí luồng input được đọc hoặc xây dựng một lớp capture/measurement riêng. Không tạo callback thứ hai chỉ để đo nếu không cần thiết.

**Lỗi B — Hai frame counter không có chung mốc thời gian.**

Công thức Δt = (F_input − F_output) / f_s chỉ có nghĩa khi hai frame position đã được ánh xạ về cùng một hệ thời gian. Input/output có thể bắt đầu khác thời điểm, nằm trên những clock domain khác nhau và có buffer phần cứng khác nhau. Sử dụng hai bộ đếm độc lập rồi lấy hiệu sẽ cho kết quả sai ngay cả khi unit test phép chia frame thành milliseconds vẫn PASS.

**Lỗi C — Full-scale click và ngưỡng cố định 0,5 không an toàn/không ổn định.**

Full-scale click có thể gây tiếng bật khó chịu. Ngưỡng 0,5 dễ bỏ sót tín hiệu do suy hao loa–micro hoặc nhận nhầm âm thanh xung quanh.

### Thiết kế DC-003 mình chấp thuận

- Dùng tín hiệu kiểm thử biên độ thấp, có dạng nhận biết được, kết hợp normalized cross-correlation và đồng bộ mốc thời gian.
- Chia phép đo thành hai loại riêng biệt:
  - **Diagnostic acoustic loopback**: Loa điện thoại → micro điện thoại. Dùng để đánh giá một phần đường audio I/O, phát hiện bất thường và phân tích jitter. Không gọi kết quả này là độ trễ mic → DSP → tai nghe.
  - **Authoritative end-to-end measurement**: Dùng nguồn kích thích và thiết bị ghi đo có chung đồng hồ tham chiếu để đối chiếu đầu vào microphone với đầu ra tai nghe, hoặc một bộ loopback phần cứng đã được hiệu chuẩn. Đây mới là nguồn số liệu cho gate độ trễ Live.
- Kiểm tra bằng tín hiệu đã biết, nhiều lần lặp, đối chiếu median/p95 và báo INVALID_MEASUREMENT khi không có tương quan đáng tin cậy.
- Cần thêm timeout, giới hạn biên độ, kiểm soát đường loa/tai nghe, không tự phát click âm lượng lớn và không để chế độ đo chạy trong phiên hát.
- Điều chỉnh mục tiêu G1: Các ngưỡng median dưới 12 ms (S-series) và dưới 20 ms (A-series) chỉ nên là mục tiêu nghiên cứu cho đường audio phù hợp, không phải release gate mặc định. Giữ mục tiêu ban đầu cho toàn bộ Live DSP: p95 tối đa 50 ms trên S-series và 70 ms trên A-series được chọn, kèm kiểm tra cảm nhận khi hát. Chỉ phê duyệt sau phép đo phần cứng thực tế.

Quyết định 3: DC-003 REVISE; không merge phiên bản ping dùng frame-counter delta chưa đồng bộ.

## 4. G2 DSP — GO song song với BLOCKED_HARDWARE

Cho phép Hermes điều phối Devin triển khai G2 ngay, không phải chờ Samsung.

Tuy nhiên, mình yêu cầu một thứ tự hơi khác so với bản dự kiến:

- **D0 — DSP baseline và golden corpus**: Tạo các tín hiệu kiểm thử tổng hợp, mẫu giọng hát được phép sử dụng, xác lập F0 accuracy, octave error, voiced/unvoiced accuracy và thời gian thực thi trên host.
- **D1 — Adaptive-window YIN + voicing**: Cải thiện detector hiện tại trước khi thay pitch shifter. Giữ thuật toán đủ nhẹ cho Android phổ thông.
- **D2 — Tracking + note hysteresis**: Kiểm soát chuyển nốt, vibrato, octave jumps và retune smoothing; có unit test độc lập.
- **D3 — Clean-room TD-PSOLA**: Pitch-mark validation, overlap-add, crossfade, xử lý voiced/unvoiced và kiểm thử formant.
- **D4 — End-to-end DSP host verification**: Đo chất lượng đầu ra, callback cost, DSP delay, artefact và hồi quy trước khi triển khai thử trên Android.

Không thêm pYIN, neural DSP, key detection hoặc các hiệu ứng phức tạp vào đường găng ngay lúc này.

Quyết định 4: GO G2 host development; G1 và chứng nhận Live vẫn BLOCKED_HARDWARE.

## Chỉ thị ngắn để Hermes tiếp tục vòng 3

GPT ROUND-2 VERDICT — D-AUTOTUNE

**Decision: CONDITIONAL GO**

Hermes remains Lead Orchestrator. Devin CLI remains the sole coding agent.

- **G0 CI: ACCEPT.** Both GitHub Actions runs independently confirmed GREEN. Add a bounded CI-hardening task to eliminate masked SDK license failures and verify APK packaging, JNI loadability and 16 KB compatibility.
- **DC-002: ACCEPT instrumentation structure, REJECT the current algoLatencyMs semantics.** retuneMs is a control smoothing time constant, not transport or DSP signal delay. Create DC-002A to correct measurement semantics and add callback p99, deadline utilization and valid xrun reporting.
- **DC-003: REQUEST CHANGES before merge.** The current engine has only an output callback. Do not assume independent input/output frame counters share a timing origin. Replace unsafe full-scale threshold clicks with bounded, identifiable signals and correlation-based detection. Separate diagnostic acoustic loopback from calibrated end-to-end mic-to-headphone latency measurement. Fail closed on invalid or missing measurements.
- **G2: GO in parallel with hardware acquisition.** Sequence: golden baseline → adaptive YIN/voicing → pitch tracking/hysteresis → clean-room TD-PSOLA → independent DSP verification. Use Devin for implementation and Hermes for independent checks.
- **Hardware: G1 remains BLOCKED_HARDWARE.** No measured latency, vocal-quality PASS or commercially certified Live mode may be claimed without real-device evidence.
- **Stream modes:** Current source requests Shared mode. Record actual granted stream properties before comparing against Exclusive performance targets.
- **Autonomy:** Continue the bounded implementation/review loop without routine Owner prompts. Escalate only blockers, authorization requests, major architectural disagreements and milestone reviews.
- **Next consultation:** Return with DC-002A verification, revised DC-003 measurement design and the D0/D1 golden benchmark evidence. Include commits, reproducible test results, failures and unresolved risks.

Do not claim PRODUCT_COMPLETE until G1–G5 gates pass with actual evidence.

Kết luận: D-Autotune đang tiến triển đúng hướng. CI và APK là bước tiến đáng kể, nhưng bài toán quan trọng nhất bây giờ là đừng để số liệu độ trễ sai dẫn Hermes đến quyết định kiến trúc sai. Sửa hệ đo, đồng thời phát triển DSP trên host; khi có Samsung thật thì thực hiện kiểm chứng Live ngay.

ChatGPT có thể mắc lỗi. Hãy kiểm tra thông tin quan trọng.
