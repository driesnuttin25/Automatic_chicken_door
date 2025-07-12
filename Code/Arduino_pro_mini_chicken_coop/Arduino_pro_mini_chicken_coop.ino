/*
   Deep-sleep demo  –  Pro Mini 3 V3 / 8 MHz + DS3231
   Wake every 30 s via INT/SQW → rotate servo 720° → sleep.

   Connections
   ───────────────────────────────────────────────────────────
   DS3231  SDA → A4     SCL → A5
           VCC → VCC    GND → GND
           INT/SQW → D2            (open-drain, active-LOW)

   Servo   SIG → D9                (>500 mA 5 V rail!)
           V+  → 5 V
           GND → GND

   MOSFET  gate → D3   (HIGH = servo powered)   ◄─ moved off D2
           drain→ servo V+   source→ GND
*/

#include <Wire.h>
#include <RTClib.h>
#include <Servo.h>
#include <avr/sleep.h>
#include <avr/power.h>

RTC_DS3231 rtc;
Servo door;

/* ── user tuning ─────────────────────────────── */
const uint8_t  SERVO_PIN   = 9;    // yellow lead
const uint8_t  MOSFET_PIN  = 3;    // HIGH = 5 V to servo
const uint16_t TURN_MS     = 900;  // ms for one 360 °
const uint8_t  FWD_CMD     = 0;    // 0=cw, 180=ccw (flip if reversed)
const uint32_t PERIOD_SEC  = 30;   // wake every 30 s
/* ────────────────────────────────────────────── */

volatile bool alarmFlag = false;
bool          toggleDir = false;   // alternate direction each wake

void wakeISR() { alarmFlag = true; }

void setup() {
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW);        // servo off

  door.attach(SERVO_PIN);
  door.write(90);                       // neutral pulse

  Serial.begin(9600);
  delay(20);                            // let USB/FTDI settle
  Serial.println(F("RTC-driven deep-sleep demo (30 s cycle)"));

  Wire.begin();
  if (!rtc.begin()) {
    Serial.println(F("!! DS3231 not found – check SDA/SCL !!"));
    while (true);
  }

  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power → stamping compile-time"));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  /* ---- INT/SQW set to interrupt mode ---- */
  rtc.disableAlarm(1);                  // disable any old alarms
  rtc.disableAlarm(2);
  rtc.clearAlarm(1);
  rtc.writeSqwPinMode(DS3231_OFF);      // INT mode (no square wave)

  setNextAlarm(PERIOD_SEC);             // first alarm = now + 30 s

  pinMode(2, INPUT_PULLUP);             // INT/SQW (D2 / INT0)
  attachInterrupt(digitalPinToInterrupt(2), wakeISR, FALLING);

  Serial.println(F("Entering first sleep …"));
  goToSleep();
}

void loop() {
  if (!alarmFlag) return;               // should not run otherwise
  alarmFlag = false;                    // clear local flag

  DateTime now = rtc.now();
  Serial.print(F("Wake @ "));
  Serial.println(now.timestamp(DateTime::TIMESTAMP_TIME));

  /* -------- servo action -------- */
  digitalWrite(MOSFET_PIN, HIGH);       // power ON
  delay(200);

  uint8_t cmd = toggleDir ? (180 - FWD_CMD) : FWD_CMD;
  toggleDir = !toggleDir;

  door.write(cmd);
  delay(2 * TURN_MS);                   // 720 °
  door.write(90);

  delay(200);
  digitalWrite(MOSFET_PIN, LOW);        // power OFF
  Serial.println(F("Servo done – scheduling next alarm"));

  /* -------- schedule next wake -------- */
  setNextAlarm(PERIOD_SEC);

  goToSleep();                          // back to Z-mode
}

/* ------------------------------------------------------------ */
void setNextAlarm(uint32_t deltaSec)
/*  Load Alarm 1 with now + deltaSec, exact hh:mm:ss:date match  */
{
  DateTime next = rtc.now() + TimeSpan(deltaSec);
  rtc.clearAlarm(1);    // clear any pending flag
  rtc.setAlarm1(next, DS3231_A1_Date);  // match ss mm hh DD
  /* Alarm is armed automatically by setAlarm1() in RTClib */
}

/* ------------------------------------------------------------ */
void goToSleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_bod_disable();   // drop brown-out detector → <100 nA
  sleep_cpu();           // Zzz…

  /* wakes on INT0 → execution resumes in loop() */
}
