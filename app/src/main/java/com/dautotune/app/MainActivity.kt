package com.dautotune.app

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.ArrayAdapter
import android.widget.TextView
import android.widget.Toast
import java.util.Locale

/** Foreground-only audio monitor. Never runs microphone capture in the background. */
class MainActivity : Activity() {
    private val mainHandler = Handler(Looper.getMainLooper())
    private val audioManager by lazy { getSystemService(Context.AUDIO_SERVICE) as AudioManager }
    private lateinit var status: TextView
    private lateinit var pitchValue: TextView
    private lateinit var targetValue: TextView
    private lateinit var correctionValue: TextView
    private lateinit var telemetry: TextView
    private lateinit var startButton: Button
    private lateinit var root: Spinner
    private lateinit var scale: Spinner
    private var amount = 0.90f
    private var speedMs = 35f
    private var reverb = 0f
    private var running = false
    private var callbacksRegistered = false
    private var streamLine = ""
    // DC-003 REV2 debug diagnostic (measurement class 1) state.
    @Volatile private var measuring = false
    private var measThread: Thread? = null

    private val routeCallback = object : AudioDeviceCallback() {
        override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>?) {
            mainHandler.post { if (running && !hasWiredOutput()) stopAudio("Tai nghe đã ngắt kết nối") }
        }
        override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>?) {
            // A device appearing mid-diagnostic invalidates the acoustic path.
            mainHandler.post {
                if (measuring) cancelLoopbackDiagnostic(
                    "INVALID_MEASUREMENT · thiết bị audio thay đổi giữa chừng")
            }
        }
    }
    private val refresh = object : Runnable {
        override fun run() {
            if (running) {
                val s = NativeAudio.snapshot()
                pitchValue.text = if (s[3] > 0) "%.1f Hz".format(Locale.US, s[0]) else "—"
                targetValue.text = if (s[3] > 0) "%.1f Hz".format(Locale.US, s[1]) else "—"
                correctionValue.text = if (s[3] > 0) "%+.0f cents".format(Locale.US, s[2]) else "—"
                val retuneMs = "%.0f".format(Locale.US, s[9])
                val cbMaxMs = "%.2f".format(Locale.US, s[10])
                val cbAvgMs = "%.2f".format(Locale.US, s[11])
                val cbP99Ms = "%.2f".format(Locale.US, s[12])
                val xrunText = if (s[13] > 0.5) "xruns=${s[8].toLong()} (valid)" else "xruns unsupported"
                val missRate = "%.1f".format(Locale.US, s[14])
                val shortRate = "%.1f".format(Locale.US, s[15])
                telemetry.text = "${s[4].toInt()} Hz  ·  thiếu ${s[5].toLong()} fr ($missRate/s)  ·  DSP ${s[6].toLong()} fr  ·  $xrunText  ·  input-short $shortRate/s  ·  retune $retuneMs ms (control)  ·  cb $cbMaxMs/$cbAvgMs/$cbP99Ms ms\n$streamLine"
                if (s[7] < 0.5) stopAudio("Audio engine đã dừng")
                else mainHandler.postDelayed(this, 250)
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Color.rgb(10, 16, 32)
        window.navigationBarColor = Color.rgb(10, 16, 32)
        buildUi()
        audioManager.registerAudioDeviceCallback(routeCallback, mainHandler)
        callbacksRegistered = true
        configure()
        status.text = "Sẵn sàng · cắm tai nghe có dây/USB"
    }

    private fun dp(value: Int): Int = (resources.displayMetrics.density * value + 0.5f).toInt()
    private fun text(value: String, sizeSp: Float, color: Int, bold: Boolean = false): TextView = TextView(this).apply {
        text = value; textSize = sizeSp; setTextColor(color)
        if (bold) typeface = Typeface.create("sans-serif", Typeface.BOLD)
        includeFontPadding = false
    }
    private val white get() = Color.rgb(244, 248, 255)
    private val muted get() = Color.rgb(162, 180, 198)
    private val mint get() = Color.rgb(115, 233, 198)

    private fun card(): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(18), dp(20), dp(18), dp(20))
        background = getDrawable(com.dautotune.app.R.drawable.card)
        val params = LinearLayout.LayoutParams(-1, -2)
        params.bottomMargin = dp(14)
        layoutParams = params
    }
    private fun LinearLayout.addSpace(height: Int) { addView(View(this@MainActivity), LinearLayout.LayoutParams(1, dp(height))) }
    private fun LinearLayout.addLabel(label: String) { addView(text(label, 13f, muted, true)); addSpace(10) }

    private fun buildUi() {
        val scroll = ScrollView(this).apply { setFillViewport(true); clipToPadding = false; fitsSystemWindows = true }
        val panel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(30), dp(20), dp(36))
            setBackgroundColor(Color.rgb(10, 16, 32))
        }
        scroll.addView(panel)
        panel.addView(text("D / AUTOTUNE", 30f, white, true))
        panel.addSpace(6)
        panel.addView(text("LIVE VOCAL PROCESSOR  ·  ALPHA 0.1", 11f, mint, true))
        panel.addSpace(26)

        val monitor = card()
        monitor.addView(text("LIVE MONITOR", 12f, mint, true))
        monitor.addSpace(10)
        status = text("Khởi tạo…", 14f, white)
        monitor.addView(status)
        monitor.addSpace(18)
        val metrics = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        fun metric(name: String): TextView {
            val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            box.addView(text(name, 11f, muted, true))
            val value = text("—", 19f, white, true)
            box.addSpace(8); box.addView(value)
            metrics.addView(box, LinearLayout.LayoutParams(0, -2, 1f))
            return value
        }
        pitchValue = metric("GIỌNG HÁT")
        targetValue = metric("NỐT CHUẨN")
        correctionValue = metric("SỬA CAO ĐỘ")
        // DC-003 REV2 debug entry: long-press arms/cancels the diagnostic
        // acoustic loopback measurement. NOT part of the singing UI flow.
        metrics.setOnLongClickListener {
            if (measuring) cancelLoopbackDiagnostic("Đã hủy đo chẩn đoán")
            else startLoopbackDiagnostic()
            true
        }
        monitor.addView(metrics)
        monitor.addSpace(16)
        telemetry = text("Chưa kết nối audio", 11f, muted)
        monitor.addView(telemetry)
        panel.addView(monitor)

        val tune = card()
        tune.addLabel("TÔNG NHẠC (KEY)")
        root = Spinner(this)
        root.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item,
            listOf("C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"))
        tune.addView(root)
        tune.addSpace(18)
        tune.addLabel("THANG ÂM (SCALE)")
        scale = Spinner(this)
        scale.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item,
            listOf("Major · Trưởng", "Minor · Thứ", "Chromatic · 12 nốt"))
        tune.addView(scale)
        tune.addSpace(18)
        addSlider(tune, "ĐỘ MẠNH SỬA NỐT", 90, 0, 100, "%", { amount = it / 100f })
        addSlider(tune, "TỐC ĐỘ SỬA NỐT", 35, 5, 150, " ms", { speedMs = it.toFloat() })
        addSlider(tune, "REVERB", 0, 0, 25, "%", { reverb = it / 100f })
        panel.addView(tune)
        root.onItemSelectedListener = simpleSelection { configure() }
        scale.onItemSelectedListener = simpleSelection { configure() }

        startButton = Button(this).apply {
            text = "BẬT LIVE AUTOTUNE"
            textSize = 15f
            isAllCaps = false
            setTextColor(Color.rgb(8, 22, 26))
            background = getDrawable(R.drawable.primary_button)
            setOnClickListener { if (running) stopAudio("Đã tắt") else tryStart() }
        }
        panel.addView(startButton, LinearLayout.LayoutParams(-1, dp(60)))
        panel.addSpace(14)
        panel.addView(text("An toàn: chỉ hoạt động khi app mở · chỉ cho tai nghe có dây/USB · bắt đầu với âm lượng thấp. Không dùng loa ngoài.", 12f, muted))
        panel.addSpace(6)
        panel.addView(text("Debug: giữ lâu hàng chỉ số để đo loopback chẩn đoán loa→mic (rút tai nghe trước).", 11f, muted))
        setContentView(scroll)
    }

    private fun simpleSelection(action: () -> Unit) = object : android.widget.AdapterView.OnItemSelectedListener {
        override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) = action()
        override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
    }
    private fun addSlider(panel: LinearLayout, title: String, initial: Int, min: Int, max: Int, suffix: String, update: (Int) -> Unit) {
        val line = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        line.addView(text(title, 12f, muted, true), LinearLayout.LayoutParams(0, -2, 1f))
        val value = text("$initial$suffix", 12f, mint, true)
        line.addView(value)
        panel.addView(line)
        panel.addSpace(8)
        val slider = SeekBar(this).apply {
            this.max = max - min
            progress = initial - min
            progressTintList = android.content.res.ColorStateList.valueOf(mint)
            thumbTintList = android.content.res.ColorStateList.valueOf(mint)
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar?, progress: Int, fromUser: Boolean) {
                    val current = progress + min
                    value.text = "$current$suffix"
                    update(current); configure()
                }
                override fun onStartTrackingTouch(s: SeekBar?) = Unit
                override fun onStopTrackingTouch(s: SeekBar?) = Unit
            })
        }
        panel.addView(slider)
        panel.addSpace(14)
    }

    private fun configure() {
        if (!::root.isInitialized || !::scale.isInitialized) return
        NativeAudio.configure(root.selectedItemPosition, scale.selectedItemPosition, amount, speedMs, reverb)
    }

    private fun formatStreamInfo(si: IntArray): String {
        if (si.size < 10 || si[0] <= 0) return "stream info n/a"
        fun sharing(m: Int) = if (m == 1) "Exclusive" else "Shared"
        fun perf(m: Int) = when (m) { 0 -> "LowLat"; 1 -> "PowerSave"; else -> "other" }
        return "out ${si[0]} Hz ${si[1]}cap/${si[2]}burst ${sharing(si[3])}·${perf(si[4])}  ·  in ${si[5]} Hz ${si[6]}cap/${si[7]}burst ${sharing(si[8])}·${perf(si[9])}"
    }

    private fun hasWiredOutput(): Boolean = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).any {
        it.type in setOf(AudioDeviceInfo.TYPE_WIRED_HEADSET, AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
            AudioDeviceInfo.TYPE_USB_HEADSET, AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_ACCESSORY)
    }

    // Anything that reroutes speaker/mic away from the phone's built-in pair
    // invalidates the acoustic speaker->mic diagnostic path.
    private fun hasExternalAudioOutput(): Boolean = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).any {
        it.type in setOf(AudioDeviceInfo.TYPE_WIRED_HEADSET, AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
            AudioDeviceInfo.TYPE_USB_HEADSET, AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_ACCESSORY,
            AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
            AudioDeviceInfo.TYPE_BLE_HEADSET, AudioDeviceInfo.TYPE_BLE_SPEAKER)
    }

    // DC-003 REV2 measurement class 1 - diagnostic acoustic loopback only.
    // Result is labeled diagnostic; it is never a mic->DSP->headphone latency.
    private fun startLoopbackDiagnostic() {
        if (running) {
            status.text = "Không đo trong phiên hát · dừng Live trước"
            return
        }
        if (measuring) return
        if (hasExternalAudioOutput()) {
            status.text = "INVALID_MEASUREMENT · đang có tai nghe/thiết bị ngoài · đường loa→mic không hợp lệ, rút ra rồi đo"
            return
        }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 102)
            return
        }
        if (!NativeAudio.start()) {
            status.text = "Không khởi động được audio: ${NativeAudio.lastError()}"
            return
        }
        // 100 cycles, chirp peak cap 0.25 FS, NCC validity threshold 0.6,
        // 250 ms search window (all clamped again inside the engine).
        val rc = NativeAudio.armLoopback(100, 0.25f, 0.6f, 250)
        if (rc != 0) {
            NativeAudio.stop()
            status.text = "INVALID_MEASUREMENT · armLoopback lỗi $rc"
            return
        }
        measuring = true
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        status.text = "ĐANG ĐO loopback loa→mic · chẩn đoán I/O, KHÔNG phải độ trễ mic→DSP→tai nghe · giữ lâu hàng chỉ số để hủy"
        // Poll on a worker thread: pollLoopback() runs the per-cycle NCC scan,
        // which is intentionally kept OFF the audio callback and off the UI.
        val deadlineMs = SystemClock.uptimeMillis() + 240_000  // global stop
        measThread = Thread {
            var state = 0
            while (measuring && SystemClock.uptimeMillis() < deadlineMs) {
                state = NativeAudio.pollLoopback()
                if (state == 4 || state == 0) break
                Thread.sleep(25)
            }
            val summary = NativeAudio.loopbackSummary()
            val cycles = NativeAudio.loopbackCycles()
            NativeAudio.stop()
            mainHandler.post { finishLoopbackDiagnostic(summary, cycles, state) }
        }
        measThread?.start()
    }

    private fun cancelLoopbackDiagnostic(message: String) {
        measuring = false
        NativeAudio.stopLoopback()
        measThread?.join(400)
        measThread = null
        NativeAudio.stop()
        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        status.text = message
    }

    private fun finishLoopbackDiagnostic(summary: DoubleArray, cycles: FloatArray, lastState: Int) {
        if (!measuring) return  // cancelled; cancelLoopbackDiagnostic owns status
        measuring = false
        measThread = null
        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (summary.size < 8 || lastState != 4) {
            status.text = "INVALID_MEASUREMENT · đo không hoàn tất (state=$lastState) · thử lại"
            return
        }
        val done = summary[3].toInt()
        val valid = summary[2].toInt()
        if (valid <= 0 || done <= 0) {
            status.text = "INVALID_MEASUREMENT · không có tương quan đáng tin (0/$done chu kỳ hợp lệ) · kiểm tra loa/mic/độ ồn"
            return
        }
        var minNcc = Float.MAX_VALUE
        var i = 0
        while (i + 2 < cycles.size) {
            if ((cycles[i + 2].toInt() and 1) != 0 && cycles[i + 1] < minNcc) minNcc = cycles[i + 1]
            i += 3
        }
        val median = "%.1f".format(Locale.US, summary[0])
        val p95 = "%.1f".format(Locale.US, summary[1])
        val min = "%.1f".format(Locale.US, summary[6])
        val max = "%.1f".format(Locale.US, summary[7])
        val nccText = if (minNcc == Float.MAX_VALUE) "n/a" else "%.2f".format(Locale.US, minNcc)
        status.text = "CHẨN ĐOÁN loa→mic: median $median ms · p95 $p95 ms · min/max $min/$max · $valid/$done chu kỳ hợp lệ (ncc đỉnh thấp nhất $nccText)"
        telemetry.text = "Chẩn đoán đường I/O · KHÔNG phải độ trễ mic→DSP→tai nghe. " +
            "E2E chuẩn: ${if (NativeAudio.authoritativeLatencyMs().isNaN()) "MEASUREMENT_UNAVAILABLE (cần fixture phần cứng)" else "?"} · " +
            "không dùng làm bằng chứng gate G1."
    }

    private fun tryStart() {
        if (measuring) {
            status.text = "Đang đo chẩn đoán · hủy đo trước (giữ lâu hàng chỉ số)"
            return
        }
        if (!hasWiredOutput()) {
            status.text = "Cần cắm tai nghe có dây hoặc USB để tránh hú tiếng"
            Toast.makeText(this, "Cắm tai nghe có dây/USB rồi thử lại", Toast.LENGTH_LONG).show()
            return
        }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 101)
            return
        }
        configure()
        running = NativeAudio.start()
        if (running) {
            streamLine = formatStreamInfo(NativeAudio.getStreamInfo())
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            startButton.text = "DỪNG LIVE AUTOTUNE"
            status.text = "ĐANG XỬ LÝ · bắt đầu với âm lượng thấp"
            mainHandler.post(refresh)
        } else {
            status.text = "Không khởi động được: ${NativeAudio.lastError()}"
        }
    }

    private fun stopAudio(message: String) {
        if (running) NativeAudio.stop()
        running = false
        mainHandler.removeCallbacks(refresh)
        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        startButton.text = "BẬT LIVE AUTOTUNE"
        status.text = message
    }

    @Deprecated("Framework permission callback is used to avoid extra AndroidX dependencies")
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 102) {
            if (grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) startLoopbackDiagnostic()
            else status.text = "Cần cấp quyền microphone để đo loopback"
        } else if (requestCode == 101 && grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) tryStart()
        else if (requestCode == 101) status.text = "Cần cấp quyền microphone để hát trực tiếp"
    }

    override fun onStop() {
        if (measuring) cancelLoopbackDiagnostic("Đã dừng đo khi ứng dụng không hiển thị")
        stopAudio("Đã tạm dừng khi ứng dụng không hiển thị")
        super.onStop()
    }
    override fun onDestroy() {
        if (callbacksRegistered) audioManager.unregisterAudioDeviceCallback(routeCallback)
        super.onDestroy()
    }
}
