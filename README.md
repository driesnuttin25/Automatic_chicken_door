# 🐔 Automatic Chicken‑Coop Door

> Low‑power Arduino Pro Mini + DS3231 + continuous‑rotation servo

![chicken_coop](https://github.com/user-attachments/assets/5ef46eaf-d0f0-4d41-93c5-d69a680d93e1)

A micro‑power controller that opens your chicken‑coop door every morning, closes it every evening, and then sleeps at ≈ 2 µA so a single 18650 (or four AA cells) can run it for **months**.

---

## ✨ Key features

|                            |                                                                                                              |
| -------------------------- | ------------------------------------------------------------------------------------------------------------ |
| **Real‑time scheduling**   | Opens at `OPEN_HH:OPEN_MM`, closes at `CLOSE_HH:CLOSE_MM` (defaults: 08 : 00 / 22 : 00).                     |
| **Limit‑switch precision** | Door stops **instantly** when the microswitch hits on the way **up**.                                        |
| **Fixed‑angle closing**    | Door lowers a calibrated number of degrees (6 revs by default).                                              |
| **Safe start‑up**          | *Daytime power‑up* → makes sure door is **open**. <br>*Night‑time power‑up* → makes sure door is **closed**. |
| **Ultra‑low power**        | MCU in `POWER_DOWN` (< 0.5 µA) + DS3231 (\~2 µA).                                                            |
| **One‑line RTC stamping**  | Flash once → real time stored; flip the flag and re‑flash to lock it in.                                     |
| **Fault LED**              | Any missed switch or RTC error lights D13 solid.                                                             |

---

## 🛠️ Bill of materials

| Qty | Part                                              | Example link / note                             |
| :-: | ------------------------------------------------- | ----------------------------------------------- |
|   1 | **Arduino Pro Mini 3.3 V / 8 MHz**                | SparkFun DEV‑11114 or clone                     |
|   1 | **DS3231 “Tiny” breakout**                        | Wingoneer / ZS‑042                              |
|   1 | **Continuous‑rotation servo**                     | e.g. MG996R, SG90‑CR                            |
|   1 | **Logic‑level N‑MOSFET**                          | AO3400 / IRLZ44N, plus fly‑wheel diode optional |
|   1 | **10T85 / KW12‑3 microswitch**                    | limit detect                                    |
|   1 | CR2032 + holder                                   | RTC backup                                      |
|  ‑  | Dupont leads, 5 V > 500 mA supply, misc. hardware |                                                 |

---

## 🔌 Wiring summary

| DS3231    | Pro Mini  | Note             |
| --------- | --------- | ---------------- |
| SDA       | A4        | I²C data         |
| SCL       | A5        | I²C clock        |
| INT/SQW   | D2        | open‑drain alarm |
| VCC / GND | VCC / GND | 3 V3 OK          |

| Other signals    | Pro Mini                |                             |
| ---------------- | ----------------------- | --------------------------- |
| Servo SIG        | **D9**                  | driven by `Servo` lib       |
| Servo V+         | 5 V rail → MOSFET drain | separate 5 V ≥ 500 mA       |
| MOSFET gate      | **D3**                  | HIGH = servo powered        |
| Limit‑switch NO  | **D4**                  | HIGH when pressed (door up) |
| Limit‑switch COM | GND                     | —                           |

*(NC lug is unused in this wiring.)*

---

## ⚙️ Firmware parameters (top of `*.ino`)

| Constant              | Purpose                            | Default        |
| --------------------- | ---------------------------------- | -------------- |
| `OPEN_HH / OPEN_MM`   | Morning alarm                      | **8**, 0       |
| `CLOSE_HH / CLOSE_MM` | Evening alarm                      | **22**, 0      |
| `REV_MS`              | ms → 360 ° at full speed           | 900            |
| `CLOSE_DEG`           | Downward travel                    | 2160 (= 6 rev) |
| `FWD_CMD`             | Direction that lifts               | 0 (CW)         |
| `OBSERVE_MS`          | How long to stay awake after moves | 5000 ms        |

---

## 🖥️ Building & flashing

```bash
# clone
git clone https://github.com/<you>/automatic‑chicken‑door.git
cd automatic‑chicken‑door/Firmware

# open the .ino in Arduino IDE
# ▸ Board: “Arduino Pro or Pro Mini (3.3 V, 8 MHz)”
# ▸ Processor: “ATmega328P”
```

**1  Stamp the DS3231 clock once**

```cpp
#define SET_RTC_NOW  1   // <— flip to '1'
```

Compile ▸ Upload

Serial Monitor shows:

```
RTC time → 2025‑07‑16T15:42:07
```

**2  Lock the clock**

```cpp
#define SET_RTC_NOW  0   // back to '0'
```

Upload again – from now on the firmware will never overwrite RTC time.

---

### 🔧 Calibration tips

* **REV\_MS** – power the servo from 5 V, run at full speed, time a full rev with a stopwatch.
* **CLOSE\_DEG** – desired\_revs × 360. (e.g. 5.5 rev → 1980°).
* If the door lifts the wrong way, set `FWD_CMD = 180` and halve `CLOSE_DEG` if needed.

---

### 💤 Power consumption

| State             | Current (Pro Mini 3 V3) |
| ----------------- | ----------------------- |
| Sleep (PWR\_DOWN) | 0.2 µA                  |
| DS3231 running    | +2 µA                   |
| **Total idle**    | ≈ 2.2 µA                |
| Moving servo      | 300–700 mA peak (< 2 s) |

An 2000 mAh 18650 cell → > 2 years idle runtime (moves not counted).

---

### 🩺 Troubleshooting

| Symptom                         | Likely cause                 | Quick fix              |
| ------------------------------- | ---------------------------- | ---------------------- |
| LED D13 lights immediately      | RTC absent / SDA/SCL swapped | re‑wire                |
| Door never stops when lifting   | Limit‑switch wiring wrong    | NO lug → D4, COM → GND |
| Opens OK but never closes fully | Increase `CLOSE_DEG`         |                        |
| Closes the wrong direction      | Flip `FWD_CMD` (0 ↔ 180)     |                        |
