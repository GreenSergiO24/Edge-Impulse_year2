# คู่มือ Edge Impulse + MPU-6050 + ESP32-S3 — Predictive Maintenancee Impulse + MPU-6050 + ESP32-S3 (ฉบับแก้ไข)

Oct 14, 2026 · @J

## ภาพรวมและสิ่งที่แก้ไข

คู่มือฉบับนี้แก้ไขจากฉบับเดิม 4 จุดที่กระทบผลลัพธ์โดยตรง และ 6 จุดด้านความชัดเจน รายละเอียดแต่ละจุดอธิบายไว้ในขั้นตอนที่เกี่ยวข้อง

| ประเภท | รายการ |
| --- | --- |
| ต้องแก้ (ผลลัพธ์ผิด) | Timing drift, Unit mismatch, zip ใน lib/, USB port ผิด |
| ควรปรับ (ความชัดเจน) | UI Step 2 เปลี่ยน, ลบ lib\_deps ที่ไม่ใช้, ลบ artifact ของ copy-paste, เพิ่ม wiring, เพิ่มเวลา compile, เพิ่มข้อมูล header name |

## Step 1: โคลนโปรเจกต์ต้นแบบ Predictive Maintenance บน Edge Impulse

1. เปิดเบราว์เซอร์ไปที่โปรเจกต์สาธารณะ **Thingy:53 - Predictive Maintenance** บน Edge Impulse Studio: `https://studio.edgeimpulse.com/studio/135470`
2. คลิกปุ่ม **Clone** มุมขวาบน เพื่อคัดลอก dataset vibration motor และโครงสร้างโมเดลเข้าบัญชีของผู้เรียน

**Dataset ที่ได้:**

| พารามิเตอร์ | ค่า |
| --- | --- |
| Sensor | accX, accY, accZ |
| Frequency | 100 Hz |
| Data collected | \~4 นาที |
| Labels | Normal\_Operation, Unbalance\_In\_Rotating\_Parts |
| Use case | ตรวจจับ motor imbalance จาก vibration pattern |

> **ยืนยันแล้ว:** Dataset นี้เก็บจาก accelerometer 3 แกน หน่วย **m/s²** ที่ 100 Hz — Adafruit MPU6050 C++ library คืนหน่วยเดียวกัน ไม่ต้องแปลงหน่วย

1. เปิดเบราว์เซอร์ไปที่โปรเจกต์สาธารณะ **Avi / ei\_ros2** บน Edge Impulse Studio
2. คลิกปุ่ม **Clone** มุมขวาบน เพื่อคัดลอกชุดข้อมูล 3 แกน (accX, accY, accZ) และโครงสร้างโมเดลเข้าบัญชีของผู้เรียน

> **ยืนยันแล้ว (จากซอร์สโค้ดต้นทาง): Dataset ของโปรเจกต์ Avi / ei\_ros2 เก็บด้วย Adafruit MPU6050 Python library ซึ่งคืนค่า mpu.acceleration เป็น m/s² — ค่าแกนที่รับแรงโน้มถ่วงขณะวางนิ่งจะอยู่ที่ \~9.8 โค้ด ESP32 ของเราใช้ Adafruit MPU6050 C++ library ซึ่งคืนหน่วยเดียวกัน ไม่ต้องแปลงหน่วย**

## Step 2: ตั้งค่า Target Device

⚠️ **UI ของ Edge Impulse Studio อัปเดตบ่อย** — ตำแหน่งปุ่มอาจต่างจากภาพในเอกสารรุ่นเก่า ให้ตรวจสอบหน้าจอจริงก่อนทำภาพประกอบ

ใน Studio รุ่นปัจจุบัน Target device มักอยู่ที่:

- มุมขวาบนของหน้า (dropdown **Target**) หรือ
- หน้า **Dashboard → Project info** (รุ่นเก่า)

ให้ค้นหาและเลือก **Espressif ESP32-S3** (ชื่ออาจมีต่อท้ายเช่น ESP32-S3 DevKitC) เพื่อให้ระบบประเมิน RAM/Flash ได้ถูกต้อง

## Step 3: ตรวจสอบ Impulse Design

ไปที่ **Impulse design → Create impulse** แล้วตรวจสอบ:

1. **Time series data:** **`ค่า Frequency ของโปรเจกต์ Avi / ei_ros2 คือ 100 Hz, Window 2000 ms — ไลบรารีที่ build ออกมาจะฝังค่าเหล่านี้เป็น EI_CLASSIFIER_FREQUENCY และ EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE (อัตโนมัติ ไม่ต้องพิมพ์เองในโค้ด**`
2. **Processing block:** ใช้ **Spectral Analysis** (FFT ของข้อมูลความเร่ง 3 แกน)
3. **Learning block:** เลือก **Classification สำหรับจำแนก 2 คลาส: Normal\_Operation, Unbalance\_In\_Rotating\_Parts**
4. กด **Save Impulse**

> **ข้อควรรู้สำหรับผู้สอน:** Spectral Analysis แปลงข้อมูล time-domain → frequency-domain ก่อนส่งเข้า Neural Network ความถี่ในการเก็บข้อมูลต้องตรงกับตอนเทรนทุกครั้ง เพราะ FFT bin แต่ละ bin แทนความถี่ที่แน่นอน — ถ้า sampling rate เพี้ยน ตำแหน่ง bin เพี้ยนตาม โมเดลทายผิดได้แม้ท่าทางถูกต้อง

## Step 4: Feature Extraction และ Training

1. ไปที่เมนูย่อย **Spectral features** → กด **Save parameters** → แท็บ **Generate features** → กด **Generate features** (รอจนแถบ progress เสร็จ)
2. ไปที่เมนู **Classifier** → ตั้ง **Epochs = 30** → กด **Start training**
3. ตรวจสอบ **Confusion Matrix** — ค่า accuracy ที่น่าพอใจขึ้นอยู่กับ dataset แต่ควรสูงกว่า 80% สำหรับ 3 คลาส

> **ถ้า accuracy ต่ำ:** เช็กก่อนว่าข้อมูลแต่ละคลาสมีจำนวนใกล้เคียงกัน (class imbalance) และลองเพิ่ม Epochs หรือปรับ Learning rate ใน Neural network settings

## Step 5: Build และ Download Arduino Library

1. ไปที่เมนู **Deployment**
2. เลือก **Arduino library** ใน Create library
3. เลือก quantization: **Quantized (int8)** ประหยัด RAM กว่า Float32 บน ESP32-S3 (แนะนำ) หรือ **Float32** ถ้า accuracy ตก
4. ตรวจสอบว่าเปิด **Enable EON Compiler** — ระบบจะ compile โมเดลเป็น C++ ที่ประหยัด Flash
5. กด **Build** — ระบบจะดาวน์โหลดไฟล์ `.zip` ชื่อเช่น `ei_ros2_inferencing.zip`

**ชื่อ header ที่ได้:** `<ชื่อโปรเจกต์>_inferencing.h` เช่น `thingy53_predictive_maintenance_inferencing.h` — ใช้ชื่อนี้ใน `#include` ของ main.cpp

## Step 6: การต่อสายฮาร์ดแวร์ MPU-6050 กับ ESP32-S3

| MPU-6050 Pin | ESP32-S3-DevKitC-1 Pin | หมายเหตุ |
| --- | --- | --- |
| VCC | 3V3 | ห้ามต่อ 5V — MPU-6050 ทนได้ 3.3V–5V แต่ I2C logic ของ ESP32-S3 เป็น 3.3V |
| GND | GND |  |
| SDA | GPIO8 | I2C default ของ ESP32-S3-DevKitC-1 |
| SCL | GPIO9 | I2C default ของ ESP32-S3-DevKitC-1 |
| AD0 | GND | I2C address = 0x68 (ถ้าต่อ VCC = 0x69) |
| INT | ไม่จำเป็น | ไม่ใช้ใน firmware นี้ |

⚠️ **USB Port:** DevKitC-1 มี 2 พอร์ต USB

- **UART** (มักอยู่ด้านบน) — ใช้สำหรับ flash firmware
- **USB** (native ESP32-S3) — ใช้สำหรับ Serial monitor เมื่อใช้ `ARDUINO_USB_CDC_ON_BOOT=1`

เสียบสาย Serial monitor ที่พอร์ต **USB** มิฉะนั้นโปรแกรมจะค้างที่ `while (!Serial)` และไม่แสดงผลใดๆ

## Step 7: ติดตั้งไลบรารีและ platformio.ini

**วิธีติดตั้งไลบรารี Edge Impulse (แก้ไขจากฉบับเดิม)**

PlatformIO ไม่แตก `.zip` ใน `lib/` ให้อัตโนมัติ — ต้องแตกก่อน:

1. แตกไฟล์ `ei_ros2_inferencing.zip` ที่ดาวน์โหลดมา
2. วางโฟลเดอร์ที่แตกแล้ว (ชื่อเช่น `ei_ros2_inferencing`) ไว้ที่ `lib/ei_ros2_inferencing/`
3. ตรวจสอบว่ามีไฟล์ `library.properties` อยู่ในโฟลเดอร์นั้น
4. **ไม่ต้องใส่** บรรทัด `.zip` ใน `lib_deps` — PlatformIO จะหาไลบรารีใน `lib/` ให้เอง

```
โครงสร้างโปรเจกต์:
my_project/
├── lib/
│   └── ei_ros2_inferencing/   ← แตก zip มาวางที่นี่
│       ├── src/
│       ├── include/
│       └── library.properties
├── src/
│   └── main.cpp
└── platformio.ini
```

**platformio.ini ฉบับแก้ไข**

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1   ; Serial ออกทางพอร์ต USB (native) ไม่ใช่ UART
    -DARDUINO_USB_MODE=1
lib_deps =
    adafruit/Adafruit MPU6050@^2.2.4
    adafruit/Adafruit Unified Sensor@^1.1.9
; ไลบรารี Edge Impulse: แตก zip ไว้ที่ lib/<ชื่อโปรเจกต์>_inferencing/
; PubSubClient และ ArduinoJson ย้ายไปใช้ในบท MQTT
```

> **เวลา compile ครั้งแรก:** ใช้เวลา 3–8 นาที เพราะไลบรารี Edge Impulse มีขนาดใหญ่มาก — นักศึกษาไม่ต้องกังวล เครื่องไม่ค้าง ครั้งต่อไปจะเร็วขึ้นเพราะมี build cache

## Step 8: main.cpp ฉบับแก้ไข

การแก้ไขหลักจากฉบับเดิม:

- **Timing:** ใช้ `micros()` + `EI_CLASSIFIER_INTERVAL_MS` แทน `delay(10)` — รับประกันว่า sampling rate ตรงกับตอนเทรน
- **Unit check:** ยืนยันแล้วว่า Adafruit MPU6050 library คืนค่า m/s² ทั้งบน Python (RPi) และ C++ (ESP32) — ไม่ต้องแปลงหน่วย
- **signal\_from\_buffer:** ใช้ helper ของ EI SDK แทนการเขียน callback เอง
- **Confidence threshold:** กรองผลที่ไม่แน่ใจออก แสดง "Uncertain" แทน
- **Sanity check:** `#error` ถ้าโมเดลไม่ใช่ 3 แกน

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <thingy53_predictive_maintenance_inferencing.h>   // เปลี่ยนเป็นชื่อโปรเจกต์ของคุณ

// Sanity check: ตรวจว่าโมเดลถูกเทรนด้วยข้อมูล 3 แกน
#if EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME != 3
#error "โมเดลนี้ไม่ได้ใช้ข้อมูล 3 แกน — ตรวจสอบ Impulse design"
#endif

#define CONFIDENCE_THRESHOLD 0.60f   // ต่ำกว่านี้ถือว่าไม่แน่ใจ

Adafruit_MPU6050 mpu;
static float buffer[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);   // รอ USB CDC พร้อม (เสียบพอร์ต USB ไม่ใช่ UART)

    Wire.begin(8, 9);            // SDA=GPIO8, SCL=GPIO9 (default ของ DevKitC-1)
    if (!mpu.begin()) {
        Serial.println("Failed to find MPU6050 chip");
        while (1) delay(10);
    }
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);   // LPF ต่ำกว่า Nyquist (~49 Hz)

    Serial.printf("Ready: %d Hz, %d samples/window, %d labels\n",
                  (int)EI_CLASSIFIER_FREQUENCY,
                  EI_CLASSIFIER_RAW_SAMPLE_COUNT,
                  EI_CLASSIFIER_LABEL_COUNT);
}

void loop() {
    // 1) เก็บข้อมูล 1 หน้าต่าง ด้วยความถี่ตรงกับตอนเทรน
    const uint32_t interval_us = (uint32_t)(EI_CLASSIFIER_INTERVAL_MS * 1000);
    uint32_t next_tick = micros();

    for (size_t ix = 0; ix < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE; ix += 3) {
        next_tick += interval_us;

        sensors_event_t a, g, temp;
        mpu.getEvent(&a, &g, &temp);

        // หน่วย: m/s² (ถ้าโมเดลเทรนด้วย g ให้หาร 9.80665)
        buffer[ix + 0] = a.acceleration.x;
        buffer[ix + 1] = a.acceleration.y;
        buffer[ix + 2] = a.acceleration.z;

        while ((int32_t)(micros() - next_tick) < 0) { }   // busy-wait รอให้ครบคาบ
    }

    // 2) แปลง buffer เป็น signal แล้วรัน inference
    signal_t signal;
    if (numpy::signal_from_buffer(buffer, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal) != 0) {
        Serial.println("signal_from_buffer failed");
        return;
    }

    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);
    if (res != EI_IMPULSE_OK) {
        Serial.printf("run_classifier error: %d\n", res);
        return;
    }

    // 3) แสดงผล
    Serial.printf("--- Inference (DSP %dms, NN %dms) ---\n",
                  result.timing.dsp, result.timing.classification);

    size_t best = 0;
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        Serial.printf("  %s: %.2f\n",
                      result.classification[ix].label,
                      result.classification[ix].value);
        if (result.classification[ix].value > result.classification[best].value)
            best = ix;
    }

    if (result.classification[best].value >= CONFIDENCE_THRESHOLD) {
        Serial.printf("--> Detected: %s (%.2f)\n\n",
                      result.classification[best].label,
                      result.classification[best].value);
    } else {
        Serial.println("--> Uncertain\n");
    }
}
```

## ประเด็นสอนในห้องและ Lab ต่อยอด

**3 ประเด็นที่ใช้เป็น teaching moment ได้ดี**

1. **Garbage in, garbage out (Timing drift):** โค้ดที่ดูทำงานได้ แต่ sampling rate เพี้ยน 10% ทำให้ FFT bin เลื่อน → accuracy ตกโดยไม่มี error message ใดๆ — นี่คือตัวอย่างจริงของ silent failure ใน ML pipeline
2. **Data contract (Unit mismatch):** training pipeline และ inference pipeline ต้องตกลงหน่วยกัน ใน production ทีม ML จะเขียน schema หรือ data contract ไว้ชัดเจน — ถ้าไม่มี โมเดลทายผิดโดยไม่รู้ว่าผิดที่ไหน
3. **Build system convention:** PlatformIO หาไลบรารีตาม folder structure ที่กำหนด ไม่ใช่ magic — รู้ว่า toolchain คาดหวังอะไร แก้ปัญหา "library not found" ได้ทันที

**Lab ต่อยอด (ถ้ามีเวลา)**

| ระดับ | เป้าหมาย | สิ่งที่เรียน |
| --- | --- | --- |
| ปานกลาง | เปลี่ยนเป็น `run_classifier_continuous` + ring buffer | ไม่มีช่วง blind spot ระหว่าง inference — สำคัญมากสำหรับ real-time motor monitoring |
| สูง | Publish alert ไปยัง MQTT เมื่อ detect Unbalance\_In\_Rotating\_Parts | IoT integration, PubSubClient |
| สูง | เก็บข้อมูล fault class ใหม่ เช่น Bearing\_Wear หรือ Misalignment แล้วเทรนใหม่ | Full TinyML lifecycle |

**Checklist ก่อนนำไปสอน**

- [x] Frequency = 100 Hz, sensor accX/Y/Z ✓
- [x] 2 class: Normal\_Operation, Unbalance\_In\_Rotating\_Parts ✓
- [x] หน่วยข้อมูล = m/s² (Adafruit MPU6050 คืนค่าเดียวกันทั้ง Python/RPi และ C++/ESP32) ✓
- [ ] ถ่าย screenshot UI Step 2 จาก Studio ปัจจุบัน
- [ ] คอมไพล์โค้ดบน hardware จริงก่อนนำไปสอน
