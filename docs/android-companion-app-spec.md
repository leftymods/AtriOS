# AtriOS Android Companion App Specification & Agent Guide

> **Target Audience**: AI Agent / Android Developer building the mobile setup & companion application for **AtriStation** & **Yandex Station Max** running **AtriOS**.
> **Project Scope**: Standalone Android application (Kotlin + Jetpack Compose) for initial Bluetooth onboarding, Wi-Fi configuration, hardware diagnostic telemetry, and device management.

---

## 1. System Overview & Architecture

When AtriStation boots into initial setup mode (or when `atri-onboard` is invoked), the device exposes a hybrid Bluetooth interface:
1. **BLE Advertising (Discovery Layer)**: Broadcasts a dedicated 16-bit Service UUID (`0xFE33`) with name `AtriStation-Setup-XXXX`. The device operates in "stealth" mode (hidden from standard OS Bluetooth scans) so that only the official companion application discovers it.
2. **Classic Bluetooth RFCOMM / SPP (Transport Layer)**: Standard Serial Port Profile (SPP, UUID `00001101-0000-1000-8000-00805F9B34FB`) on RFCOMM channel `1`. Once paired, the app opens a bidirectional stream socket.
3. **JSON Framing Protocol (Application Layer)**: Line-delimited JSON strings (`\n`) for bidirectional command-response messaging.

```
+-----------------------------------------------------------+
|                    Android Smartphone                     |
|  [Jetpack Compose UI] <--> [Repository] <--> [BtManager]  |
+-----------------------------------------------------------+
                              |
                 BLE Scan (UUID 0xFE33)
                              |
               RFCOMM SPP Socket (Channel 1)
           Newline-Delimited JSON ("\n")
                              |
                              v
+-----------------------------------------------------------+
|                   AtriStation / AtriOS                    |
|   atri-onboard (Pure C Daemon / BlueZ RFCOMM Server)       |
|      |               |                 |            |     |
|   [Network]      [Audio DSP]       [LED Ring]    [Matrix] |
|   (nmcli)        (PipeWire/ALSA)   (atri-led)  (Gowin FP) |
+-----------------------------------------------------------+
```

---

## 2. Bluetooth Identification & Discovery

### 2.1 BLE Advertising Parameters
- **Service UUID (16-bit)**: `0xFE33`
- **Full 128-bit UUID**: `0000FE33-0000-1000-8000-00805F9B34FB`
- **Device Broadcast Name Pattern**: `AtriStation-Setup-XXXX`
  - `XXXX` represents the last 4 hexadecimal characters of the station's primary MAC address (e.g., `AtriStation-Setup-A1B2`).

### 2.2 Classic Bluetooth RFCOMM Service
- **Profile**: Serial Port Profile (SPP)
- **Standard UUID**: `00001101-0000-1000-8000-00805F9B34FB`
- **RFCOMM Channel**: `1`
- **Pairing Mode**: Just-Works / Simple Pairing (`bluetoothctl pairable on`).

---

## 3. Communication Protocol Specification

All communication occurs over the established RFCOMM socket. 
- **Framing**: UTF-8 encoded text terminated by a newline character `\n`.
- **Payload Format**: Strictly JSON objects.
- **Request Format**: `{"cmd": "<command_name>", ...parameters...}\n`
- **Response Format**: `{"status": "ok" | "error", "type": "<response_type>", ...data...}\n`

### 3.1 Initial Connection Handshake
Upon successful RFCOMM socket connection, the station immediately emits a welcome banner:
```json
{
  "status": "ready",
  "device": "AtriStation",
  "version": "AtriOS 2.1",
  "channel": "bluetooth"
}
```
*Visual feedback on station*: LED matrix displays `PAIRED`; LED ring rotates in cyan (`cyan_spin`).

---

### 3.2 Command Reference

#### 1. Scan Wi-Fi Networks
Instructs the station to scan for surrounding 2.4 GHz and 5 GHz wireless networks.
- **Request**:
  ```json
  {"cmd": "scan"}
  ```
- **Response**:
  ```json
  {
    "status": "ok",
    "type": "scan_result",
    "networks": [
      {"ssid": "Keenetic-5G", "signal": 92, "sec": "WPA2"},
      {"ssid": "Home-Network", "signal": 74, "sec": "WPA2"},
      {"ssid": "Guest-Open", "signal": 45, "sec": "NONE"}
    ]
  }
  ```

#### 2. Connect to Wi-Fi & Set Device Name
Configures the station's wireless network and optionally sets its network hostname.
- **Request**:
  ```json
  {
    "cmd": "connect",
    "ssid": "Keenetic-5G",
    "password": "RouterPassword123",
    "name": "Living Room Station"
  }
  ```
- **Hardware Feedback during connection**:
  - LED ring illuminates in solid amber (`255, 180, 0`).
  - 25x16 LED matrix displays `WIFI...`.
- **Success Response**:
  ```json
  {
    "status": "ok",
    "type": "connect_result",
    "ip": "192.168.1.145"
  }
  ```
  - *Hardware Feedback*: LED matrix shows `ONLINE`, LED ring lights up in bright green (`0, 255, 60`), and the station plays a celebratory 3-tone audio chime across all speakers.
- **Failure Response**:
  ```json
  {
    "status": "error",
    "type": "connect_result",
    "error": "Connection failed (incorrect password or timeout)"
  }
  ```
  - *Hardware Feedback*: LED matrix shows `FAIL`, LED ring turns solid red (`255, 0, 0`).

#### 3. Real-Time Telemetry & Status
Polls current hardware health and state metrics.
- **Request**:
  ```json
  {"cmd": "telemetry"}
  ```
- **Response**:
  ```json
  {
    "status": "ok",
    "type": "telemetry",
    "platform": "AtriStation",
    "version": "AtriOS 2.1",
    "ip": "192.168.1.145",
    "wifi_ssid": "Keenetic-5G",
    "volume": 75,
    "als_lux": 160,
    "cpu_temp": 44,
    "uptime": 2340
  }
  ```
- **Fields Description**:
  - `ip`: Device IP address on local network.
  - `wifi_ssid`: Currently connected Wi-Fi network.
  - `volume`: Master sound volume percentage (0–100).
  - `als_lux`: Ambient Light Sensor reading in lux (LTR308ALS on I2C-3).
  - `cpu_temp`: S905X3 / S905X2 thermal sensor in degrees Celsius.
  - `uptime`: System uptime in seconds.

#### 4. Sound Verification Test
Plays an acoustic sweep tone across the dual SY6045S amplifiers (stereo tweeters + woofer).
- **Request**:
  ```json
  {"cmd": "sound_test"}
  ```
- **Response**:
  ```json
  {
    "status": "ok",
    "type": "sound_test"
  }
  ```

#### 5. Master Volume Control
- **Get Volume**:
  - Request: `{"cmd": "get_volume"}`
  - Response: `{"status": "ok", "type": "volume_result", "volume": 75}`
- **Set Volume**:
  - Request: `{"cmd": "set_volume", "val": 80}`
  - Response: `{"status": "ok", "type": "set_volume_result", "volume": 80}`

#### 6. RGB LED Ring Customization
Controls the 24-addressable RGB LEDs on the station top ring.
- **Request**:
  ```json
  {"cmd": "set_led", "r": 0, "g": 200, "b": 255}
  ```
- **Response**:
  ```json
  {
    "status": "ok",
    "type": "led_result",
    "r": 0,
    "g": 200,
    "b": 255
  }
  ```

#### 7. Front 25x16 Matrix Display Control
Renders text, icons, or clock on the front FPGA screen.
- **Request**:
  ```json
  {"cmd": "set_display", "mode": "text 'Hello'"}
  ```
- Valid `mode` strings:
  - `clock`: Displays real-time digital clock.
  - `text '<string>'`: Renders static/scrolling text on the 25x16 grid.
  - `off`: Clears the screen.
- **Response**:
  ```json
  {
    "status": "ok",
    "type": "display_result",
    "applied": "text 'Hello'"
  }
  ```

#### 8. Bluetooth Audio Sink (A2DP Streaming)
Enables pairing the station as a Bluetooth loudspeaker for direct smartphone audio streaming.
- **Enable**:
  - Request: `{"cmd": "bt_audio", "enabled": true}`
  - Response: `{"status": "ok", "type": "btaudio_result", "enabled": true}`
- **Disable**:
  - Request: `{"cmd": "bt_audio", "enabled": false}`
  - Response: `{"status": "ok", "type": "btaudio_result", "enabled": false}`

#### 9. System Language & Locale
- **Get Language**:
  - Request: `{"cmd": "get_lang"}`
  - Response: `{"status": "ok", "type": "lang_result", "lang": "ru_RU.UTF-8"}`
- **Set Language**:
  - Request: `{"cmd": "set_lang", "lang": "ru"}` (or `"en"`)
  - Response: `{"status": "ok", "type": "set_lang_result", "lang": "ru_RU.UTF-8"}`

#### 10. Timezone Configuration
- **Get Timezone**:
  - Request: `{"cmd": "get_tz"}`
  - Response: `{"status": "ok", "type": "tz_result", "timezone": "Europe/Moscow"}`
- **Set Timezone**:
  - Request: `{"cmd": "set_tz", "tz": "Europe/Moscow"}`
  - Response: `{"status": "ok", "type": "set_tz_result", "timezone": "Europe/Moscow"}`

#### 11. System Reboot & Power Management
- **Reboot**:
  - Request: `{"cmd": "reboot"}`
  - Response: `{"status": "ok", "message": "Rebooting system..."}`
- **Power Off**:
  - Request: `{"cmd": "poweroff"}`
  - Response: `{"status": "ok", "message": "Powering off..."}`
- **Exit Setup**:
  - Request: `{"cmd": "exit"}`
  - Response: `{"status": "ok", "message": "Goodbye"}`

---

## 4. Android Application Architecture & Implementation Guide

### 4.1 Recommended Tech Stack
- **Language**: Kotlin 2.0+
- **Min SDK**: API 26 (Android 8.0) | **Target SDK**: API 34+ (Android 14)
- **UI Framework**: Jetpack Compose + Material 3 (Dark & Light theme, Material You)
- **Asynchronous Architecture**: Kotlin Coroutines + `StateFlow` / `SharedFlow`
- **DI Framework**: Hilt / Koin
- **Serialization**: `kotlinx.serialization` (JSON)
- **Architecture Pattern**: Clean Architecture (UI -> ViewModel -> UseCases -> Repository -> BluetoothDataSource)

---

### 4.2 Manifest Permissions Setup (`AndroidManifest.xml`)

```xml
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <!-- Legacy Bluetooth permissions for Android 11 and lower -->
    <uses-permission android:name="android.permission.BLUETOOTH" android:maxSdkVersion="30" />
    <uses-permission android:name="android.permission.BLUETOOTH_ADMIN" android:maxSdkVersion="30" />
    <uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" android:maxSdkVersion="30" />
    <uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" android:maxSdkVersion="30" />

    <!-- Modern Bluetooth permissions for Android 12 (API 31)+ -->
    <!-- neverForLocation indicates BLE is used strictly for device communication, not location tracking -->
    <uses-permission
        android:name="android.permission.BLUETOOTH_SCAN"
        android:usesPermissionFlags="neverForLocation" />
    <uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />

    <!-- Internet permission for subsequent HTTP/Web dashboard access -->
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />

    <uses-feature android:name="android.hardware.bluetooth" android:required="true" />
    <uses-feature android:name="android.hardware.bluetooth_le" android:required="true" />
...
```

---

### 4.3 Core Bluetooth Manager (`AtriBluetoothManager.kt`)

```kotlin
package tech.leftymods.atrios.bluetooth

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.ParcelUuid
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.json.Json
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStream
import java.util.UUID

class AtriBluetoothManager(private val context: Context) {

    companion object {
        val BLE_SERVICE_UUID: UUID = UUID.fromString("0000FE33-0000-1000-8000-00805F9B34FB")
        val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
    }

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager.adapter
    private val bleScanner: BluetoothLeScanner? get() = bluetoothAdapter?.bluetoothLeScanner

    private var socket: BluetoothSocket? = null
    private var outputStream: OutputStream? = null
    private var reader: BufferedReader? = null

    private val _incomingMessages = MutableSharedFlow<String>(extraBufferCapacity = 64)
    val incomingMessages: SharedFlow<String> = _incomingMessages.asSharedFlow()

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val ioScope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    sealed class ConnectionState {
        object Disconnected : ConnectionState()
        object Scanning : ConnectionState()
        data class Connecting(val deviceName: String) : ConnectionState()
        data class Connected(val deviceName: String, val address: String) : ConnectionState()
        data class Error(val message: String) : ConnectionState()
    }

    @SuppressLint("MissingPermission")
    fun startDiscovery(onDeviceFound: (BluetoothDevice, Int) -> Unit) {
        val scanner = bleScanner ?: return
        _connectionState.value = ConnectionState.Scanning

        val scanFilter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(BLE_SERVICE_UUID))
            .build()

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val device = result.device
                val name = result.scanRecord?.deviceName ?: device.name ?: ""
                if (name.startsWith("AtriStation") || result.scanRecord?.serviceUuids?.contains(ParcelUuid(BLE_SERVICE_UUID)) == true) {
                    onDeviceFound(device, result.rssi)
                }
            }
        }

        scanner.startScan(listOf(scanFilter), settings, callback)
    }

    @SuppressLint("MissingPermission")
    suspend fun connect(device: BluetoothDevice): Boolean = withContext(Dispatchers.IO) {
        try {
            _connectionState.value = ConnectionState.Connecting(device.name ?: "AtriStation")
            bluetoothAdapter?.cancelDiscovery()

            val rfcommSocket = device.createRfcommSocketToServiceRecord(SPP_UUID)
            rfcommSocket.connect()

            socket = rfcommSocket
            outputStream = rfcommSocket.outputStream
            reader = BufferedReader(InputStreamReader(rfcommSocket.inputStream, Charsets.UTF_8))

            _connectionState.value = ConnectionState.Connected(device.name ?: "AtriStation", device.address)
            startListening()
            true
        } catch (e: Exception) {
            disconnect()
            _connectionState.value = ConnectionState.Error("Connection failed: ${e.localizedMessage}")
            false
        }
    }

    private fun startListening() {
        ioScope.launch {
            try {
                while (isActive && socket?.isConnected == true) {
                    val line = reader?.readLine() ?: break
                    if (line.isNotBlank()) {
                        _incomingMessages.emit(line)
                    }
                }
            } catch (e: Exception) {
                // Connection lost
            } finally {
                disconnect()
            }
        }
    }

    suspend fun sendCommand(jsonString: String) = withContext(Dispatchers.IO) {
        try {
            val payload = if (jsonString.endsWith("\n")) jsonString else "$jsonString\n"
            outputStream?.write(payload.toByteArray(Charsets.UTF_8))
            outputStream?.flush()
        } catch (e: Exception) {
            _connectionState.value = ConnectionState.Error("Failed to send command: ${e.message}")
        }
    }

    fun disconnect() {
        try {
            reader?.close()
            outputStream?.close()
            socket?.close()
        } catch (_: Exception) {}
        socket = null
        outputStream = null
        reader = null
        _connectionState.value = ConnectionState.Disconnected
    }
}
```

---

### 4.4 Data Transfer Objects (`AtriProtocolModels.kt`)

```kotlin
package tech.leftymods.atrios.model

import kotlinx.serialization.Serializable

@Serializable
data class BaseRequest(
    val cmd: String
)

@Serializable
data class ConnectRequest(
    val cmd: String = "connect",
    val ssid: String,
    val password: String = "",
    val name: String = ""
)

@Serializable
data class SetVolumeRequest(
    val cmd: String = "set_volume",
    val val: Int
)

@Serializable
data class SetLedRequest(
    val cmd: String = "set_led",
    val r: Int,
    val g: Int,
    val b: Int
)

@Serializable
data class SetDisplayRequest(
    val cmd: String = "set_display",
    val mode: String
)

@Serializable
data class SetBtAudioRequest(
    val cmd: String = "bt_audio",
    val enabled: Boolean
)

@Serializable
data class SetLangRequest(
    val cmd: String = "set_lang",
    val lang: String
)

@Serializable
data class SetTzRequest(
    val cmd: String = "set_tz",
    val tz: String
)

/* Responses */

@Serializable
data class WifiNetwork(
    val ssid: String,
    val signal: Int,
    val sec: String = "WPA2"
)

@Serializable
data class ScanResultResponse(
    val status: String,
    val type: String,
    val networks: List<WifiNetwork> = emptyList()
)

@Serializable
data class ConnectResultResponse(
    val status: String,
    val type: String,
    val ip: String? = null,
    val error: String? = null
)

@Serializable
data class TelemetryResponse(
    val status: String,
    val type: String,
    val platform: String = "AtriStation",
    val version: String = "AtriOS 2.1",
    val ip: String = "",
    val wifi_ssid: String = "",
    val volume: Int = 0,
    val als_lux: Int = 0,
    val cpu_temp: Int = 0,
    val uptime: Long = 0L
)
```

---

### 4.5 Application User Journeys & UI Wireframes

#### Screen 1: Discovery Screen ("Find My Station")
- Animated radar/pulse effect.
- Scanning for BLE Service `0xFE33`.
- Displays card list of discovered stations:
  - Device Name: `AtriStation-Setup-A1B2`
  - Signal Strength Indicator (RSSI - dBm)
  - "Connect" button.

#### Screen 2: Wi-Fi Setup Wizard
- Automatically issues `{"cmd": "scan"}` upon connection.
- Shows list of available Wi-Fi networks sorted by signal strength.
- Tap network -> Password input modal dialog.
- Optional: "Device Name" field (e.g. "Гостиная" / "Living Room").
- "Connect Station" button -> issues `{"cmd": "connect", ...}`.
- Progress bar displaying connection status (`Connecting...`, verifying IP).

#### Screen 3: Setup Success & Control Dashboard
- Celebratory banner: "Station is Online!" with acquired IP address (`192.168.1.X`).
- Live Telemetry Card:
  - Wi-Fi SSID & IP badge
  - Temperature gauge (°C)
  - Ambient light sensor reading (Lux)
  - Volume slider (0–100%)
- Quick Actions:
  - **Speaker Test**: Button to trigger 3-tone acoustic sweep (`sound_test`).
  - **LED Ring Color Picker**: Interactive wheel to pick RGB colors for the top ring.
  - **Display Mode**: Toggle between digital clock, custom text greeting, or animations.
  - **Bluetooth Audio Mode**: Switch station into A2DP speaker sink.
  - **Web Dashboard**: Deep link to open the station's web interface in browser (`http://<ip>/`).

---

## 5. Summary Checklist for the Agent

When generating the Android project, implement:
1. `AndroidManifest.xml` with proper Android 12+ Bluetooth permissions (`BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT`, `neverForLocation`).
2. Kotlin Coroutines-based Bluetooth RFCOMM channel manager with automatic newline framing.
3. Clean MVI/MVVM ViewModel layer consuming `AtriBluetoothManager`.
4. Fallback handling: If station Wi-Fi fails, prompt user with the error string returned in `connect_result` and allow retrying password.
5. Unit tests validating JSON request formatting and response deserialization.
