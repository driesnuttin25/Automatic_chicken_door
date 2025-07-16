/*
  ───────────────────────────────────────────────────────────
  CHICKEN‑COOP DOOR CONTROLLER  |  ATmega328P Pro Mini 3 V3
  ───────────────────────────────────────────────────────────
  • Opens at  OPEN_HH : OPEN_MM   → runs until limit switch HIGH.
  • Closes at CLOSE_HH : CLOSE_MM → runs CLOSE_DEG degrees.
  • Power‑up behaviour
        · If current time is daytime  → ensure door is OPEN.
        · If current time is night    → ensure door is CLOSED.
  • Sleeps in PWR_DOWN between RTC alarms (≈ 0.2 µA + RTC 2 µA).
  • LED on D13 lights solid if any motion/RTC fault is detected.
*/

#include <Wire.h>
#include <RTClib.h>
#include <Servo.h>
#include <avr/sleep.h>
#include <avr/power.h>

/* ========== ONE‑TIME RTC‑STAMP ============================
   Set   SET_RTC_NOW = 1,  compile+upload ONCE to stamp
   the RTC with your PC‑clock time, then set it back to 0
   and upload again.                                          */
#define SET_RTC_NOW   0
/* ========================================================= */

RTC_DS3231 rtc;
Servo      door;

/* ───── USER‑TUNABLE CONSTANTS ───────────────────────────── */

/* Pin assignment */
const uint8_t SERVO_PIN  = 9;   // yellow wire
const uint8_t MOSFET_PIN = 3;   // HIGH = 5 V to servo
const uint8_t LIMIT_PIN  = 4;   // NO lug  (HIGH when lever pressed)
const uint8_t ALARM_PIN  = 2;   // DS3231 INT/SQW
const uint8_t LED_PIN    = 13;  // on‑board LED

/* Servo motion */
const uint8_t  FWD_CMD   = 0;       // 0 = cw opens, 180 = ccw opens
const uint16_t REV_MS    = 900;     // ms per one 360° revolution
const uint8_t  MAX_TURNS = 6;       // timeout if switch never hits
const uint16_t CLOSE_DEG = 2160;    // close travel (degrees)  ← 6 revs

/* Alarm schedule (24‑h)  */
const uint8_t OPEN_HH  = 8,  OPEN_MM  = 0;   // 08:00
const uint8_t CLOSE_HH = 22, CLOSE_MM = 0;   // 22:00

const uint16_t OBSERVE_MS = 3000;   // ms to keep MCU awake after actions
/* ────────────────────────────────────────────────────────── */

enum DoorState { OPEN, CLOSED };
DoorState gateState = CLOSED;

volatile bool alarmFlag = false;    // set in ISR

/* ----- helper macros / lambdas --------------------------- */
inline void powerServo(bool on)            { digitalWrite(MOSFET_PIN, on?HIGH:LOW); }
inline bool limitPressed()                 { return digitalRead(LIMIT_PIN) == HIGH; } // HIGH = pressed
void fault(const __FlashStringHelper* msg) { Serial.println(msg); digitalWrite(LED_PIN, HIGH); }
void wakeISR()                             { alarmFlag = true; }

/* =========================================================
   SETUP
   ========================================================= */
void setup()
{
  /* GPIO init */
  pinMode(MOSFET_PIN, OUTPUT); powerServo(false);
  pinMode(LIMIT_PIN,  INPUT_PULLUP);
  pinMode(LED_PIN,    OUTPUT); digitalWrite(LED_PIN, LOW);

  /* Servo neutral pulse so it stays still while powered */
  door.attach(SERVO_PIN);
  door.write(90);

  /* Serial + I²C */
  Serial.begin(9600);
  delay(10);               // let FTDI settle
  Wire.begin();

  /* ---------- RTC initialisation ---------- */
  if (!rtc.begin()) fault(F("RTC missing!"));
#if SET_RTC_NOW
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));      // stamp once
#endif
  Serial.print(F("RTC time → "));
  Serial.println(rtc.now().timestamp());

  rtc.disableAlarm(1);      // clear any stale alarms
  rtc.clearAlarm(1);
  rtc.writeSqwPinMode(DS3231_OFF);    // INT mode (no 1 Hz square wave)

  scheduleNextWake();       // Alarm‑1 programmed for next event

  /* INT0 wake‑up */
  pinMode(ALARM_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ALARM_PIN), wakeISR, FALLING);

  /* ---------- Power‑up door correction ---------- */
  DateTime now = rtc.now();
  uint16_t nowMin  = now.hour()*60 + now.minute();
  uint16_t openMin = OPEN_HH*60 + OPEN_MM;
  uint16_t closMin = CLOSE_HH*60 + CLOSE_MM;
  bool daytime = (nowMin >= openMin) && (nowMin < closMin);

  if (daytime) {                               // daytime → want OPEN
    if (!limitPressed()) {
      Serial.println(F("Daytime & door DOWN → lifting"));
      if (!openDoor()) fault(F("Startup open fault"));
    }
    gateState = OPEN;
  } else {                                     // night → want CLOSED
    if (limitPressed()) {
      Serial.println(F("Night start & door UP → closing"));
      if (!closeDoor()) fault(F("Startup close fault"));
    }
    gateState = CLOSED;
  }

  Serial.println(F("Setup complete – sleeping in 5 s…"));
  delay(OBSERVE_MS);
  goToSleep();
}

/* =========================================================
   LOOP (runs once after every RTC wake)
   ========================================================= */
void loop()
{
  if (!alarmFlag) return;          // ignore stray wakes
  alarmFlag = false;

  DateTime now = rtc.now();
  Serial.print(F("Wake @ "));
  Serial.println(now.timestamp(DateTime::TIMESTAMP_TIME));

  bool ok = true;
  if (now.hour() == OPEN_HH && now.minute() == OPEN_MM) {
    ok = openDoor();  gateState = OPEN;
  }
  else if (now.hour() == CLOSE_HH && now.minute() == CLOSE_MM) {
    ok = closeDoor(); gateState = CLOSED;
  }

  if (!ok) fault(F("Motion fault"));

  scheduleNextWake();
  Serial.println(F("Sleeping in 5 s…"));
  delay(OBSERVE_MS);
  goToSleep();
}

/* =========================================================
   openDoor()  – drive full speed until switch HIGH
   ========================================================= */
bool openDoor()
{
  Serial.println(F("→ Opening"));
  if (limitPressed()) { Serial.println(F("Already open")); return true; }

  powerServo(true); delay(200);
  door.write(FWD_CMD);

  uint32_t t0 = millis();
  uint32_t timeout = (uint32_t)MAX_TURNS * REV_MS;

  while (!limitPressed() && millis() - t0 < timeout);   // busy‑wait

  door.write(90); delay(200); powerServo(false);

  bool ok = limitPressed();
  Serial.println(ok ? F("✓ Limit hit") : F("✗ Timeout"));
  return ok;
}

/* =========================================================
   closeDoor() – drive fixed CLOSE_DEG angle
   ========================================================= */
bool closeDoor()
{
  Serial.println(F("→ Closing"));
  uint32_t runMs = (uint32_t)CLOSE_DEG * REV_MS / 360UL;

  powerServo(true); delay(200);
  door.write(180 - FWD_CMD);      // reverse direction
  delay(runMs);
  door.write(90); delay(200); powerServo(false);

  bool ok = !limitPressed();      // switch should now be released
  Serial.println(ok ? F("✓ Closed") : F("✗ Switch still pressed"));
  return ok;
}

/* =========================================================
   scheduleNextWake() – load Alarm‑1 with next OPEN/CLOSE
   ========================================================= */
void scheduleNextWake()
{
  DateTime now = rtc.now(), next;

  auto today = [&](uint8_t h,uint8_t m){
      return DateTime(now.year(),now.month(),now.day(),h,m,0); };

  if (now.unixtime() < today(OPEN_HH, OPEN_MM).unixtime())
       next = today(OPEN_HH, OPEN_MM);
  else if (now.unixtime() < today(CLOSE_HH, CLOSE_MM).unixtime())
       next = today(CLOSE_HH, CLOSE_MM);
  else { DateTime t = now + TimeSpan(1,0,0,0);
         next = DateTime(t.year(),t.month(),t.day(),OPEN_HH,OPEN_MM,0); }

  rtc.clearAlarm(1);
  rtc.setAlarm1(next, DS3231_A1_Date);   // match ss mm hh DD

  Serial.print(F("Next alarm → "));
  Serial.println(next.timestamp(DateTime::TIMESTAMP_TIME));
}

/* =========================================================
   goToSleep() – enter PWR_DOWN until INT0 falls
   ========================================================= */
void goToSleep()
{
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_bod_disable();
  sleep_cpu();                       // MCU sleeps here
  /* execution resumes immediately after wake – loop() runs */
}
