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

    private val routeCallback = object : AudioDeviceCallback() {
        override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>?) {
            mainHandler.post { if (running && !hasWiredOutput()) stopAudio("Tai nghe đã ngắt kết nối") }
        }
    }
    private val refresh = object : Runnable {
        override fun run() {
            if (running) {
                val s = NativeAudio.snapshot()
                pitchValue.text = if (s[3] > 0) "%.1f Hz".format(Locale.US, s[0]) else "—"
                targetValue.text = if (s[3] > 0) "%.1f Hz".format(Locale.US, s[1]) else "—"
                correctionValue.text = if (s[3] > 0) "%+.0f cents".format(Locale.US, s[2]) else "—"
                val algoMs = "%.1f".format(Locale.US, s[9])
                val cbMaxMs = "%.2f".format(Locale.US, s[10])
                val cbAvgMs = "%.2f".format(Locale.US, s[11])
                telemetry.text = "${s[4].toInt()} Hz  ·  thiếu ${s[5].toLong()} frames  ·  DSP ${s[6].toLong()} frames  ·  xrun ${s[8].toLong()}  ·  trễ ~$algoMs ms  ·  cb $cbMaxMs/$cbAvgMs ms"
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

    private fun hasWiredOutput(): Boolean = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).any {
        it.type in setOf(AudioDeviceInfo.TYPE_WIRED_HEADSET, AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
            AudioDeviceInfo.TYPE_USB_HEADSET, AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_ACCESSORY)
    }

    private fun tryStart() {
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
        if (requestCode == 101 && grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) tryStart()
        else if (requestCode == 101) status.text = "Cần cấp quyền microphone để hát trực tiếp"
    }

    override fun onStop() {
        stopAudio("Đã tạm dừng khi ứng dụng không hiển thị")
        super.onStop()
    }
    override fun onDestroy() {
        if (callbacksRegistered) audioManager.unregisterAudioDeviceCallback(routeCallback)
        super.onDestroy()
    }
}
