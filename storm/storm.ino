#include <Adafruit_NeoPixel.h>

// ======================================================
//                        CONFIG
// ======================================================

#define LED_PIN    4
#define NUM_LEDS   30
#define BUTTON_PIN 12

// Encoder 1
#define outputA1   32
#define outputB1   14

// Encoder 2  (adjust pins to match your wiring)
#define outputA2   13
#define outputB2   12

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ======================================================
//                    TEST / SCALE
// ======================================================

float brightnessScale = 1.0f;

// ======================================================
//                    GLOBAL OUTPUT STATE
// ======================================================

bool lightsOff = false;

// ======================================================
//                    STORM CONFIG
// Tuned for ~30 swipes raising intensity one step each
// ======================================================

const uint8_t  MAX_INTENSITY_LEVEL             = 30;
const uint8_t  INTENSITY_STEP_SIZE             = 1;

const uint8_t  BASE_MIN_BRIGHTNESS             = 0;
const uint8_t  BASE_MAX_BRIGHTNESS             = 5;    // subtle at level 0
const uint8_t  BRIGHTNESS_PER_LEVEL            = 8;    // 5 + 30*8 = 245 at max
const uint8_t  ABS_MAX_BRIGHTNESS              = 255;

const unsigned long BASE_MIN_INTERVAL          = 30;   // fastest crackle update (ms)
const unsigned long BASE_MAX_INTERVAL          = 800;
const int      INTERVAL_DECREASE_PER_LEVEL     = 20;   // 800 - 30*20 = 200 at max

const uint8_t  BASE_CALM_CHANCE                = 92;   // 8% crackle at level 0
const uint8_t  CALM_CHANCE_REDUCTION_PER_LEVEL = 3;    // 92-90 = 2% calm at max → 98% crackle

// ---------------- CLOUD (storm) ----------------

const uint8_t CLOUD_R = 15;
const uint8_t CLOUD_G = 0;
const uint8_t CLOUD_B = 45;

// ---------------- FLASH ----------------
// Short, 1-2 pulse bursts

const uint8_t  FLASH_MIN_PULSES  = 1;
const uint8_t  FLASH_MAX_PULSES  = 2;

const unsigned long FLASH_MIN_ON   = 100;
const unsigned long FLASH_MAX_ON   = 200;
const unsigned long FLASH_MIN_OFF  = 100;
const unsigned long FLASH_MAX_OFF  = 200;

// ======================================================
//               AUTO-FLASH CONFIG
// Spontaneous flashes that increase with storm intensity
// ======================================================

const unsigned long AUTO_FLASH_CHECK_MS   = 1500;  // roll the dice every 1.5 s
const uint8_t       AUTO_FLASH_MAX_CHANCE = 55;    // max ~55% chance per roll at full intensity

// ======================================================
//                    SUNSET CONFIG
// ======================================================

const uint8_t  SUNSET_MAX_BRIGHTNESS  = 120;

// Encoder stepping
const int          ENCODER_FULL_STEPS     = 60;
const int          ENCODER_SINGLE_STEP    = 1;
const int          ENCODER_DUAL_STEP      = 3;
const unsigned long DUAL_ENCODER_WINDOW_MS = 150;

// Colour flow: how far the hue map shifts per encoder step
// (palette units; 5 = one full palette rotation)
const float SUNSET_HUE_PER_STEP = 0.06f;

// How many palette units span the full LED strip
// 5.0 = one full palette gradient across all 30 LEDs
const float SUNSET_LED_SPREAD = 5.0f;

// Warm sunset palette (pink → orange → coral → magenta → amber)
const uint8_t SUNSET_PALETTE_COUNT   = 5;
const uint8_t SUNSET_R[5] = { 255, 230, 255, 200, 255 };
const uint8_t SUNSET_G[5] = {  80,  90, 120,  60, 150 };
const uint8_t SUNSET_B[5] = {  20,  50,  80, 120,  70 };

// ======================================================
//                    PULSE CONFIG
// ======================================================

const float        PULSE_DEPTH             = 0.38f;   // ±38% – clearly visible
const unsigned long STORM_PULSE_PERIOD_MS  = 1000;    // fast breathing in storm
const unsigned long SUNSET_PULSE_PERIOD_MS = 3500;    // slow, calm breathing in sunset
const unsigned long ENCODER_IDLE_MS        = 400;     // ms before sunset pulse resumes

// ======================================================
//                  LIGHTNING EFFECT
// ======================================================

class LightningEffect {

  public:
    LightningEffect(Adafruit_NeoPixel &s) : strip(s) {}

    // --------------------------------------------------
    //  INIT / RESET
    // --------------------------------------------------

    void begin() { resetState(); }

    void resetState() {
      stormActive           = false;
      intensity             = 0;
      flashActive           = false;
      flashStateOn          = false;
      sunsetMode            = false;
      encoderPosition       = 0;
      sunsetHue             = 0.0f;
      enc1LastTurnMs        = 0;
      enc2LastTurnMs        = 0;
      lastEncoderActivityMs = 0;
      autoFlashLastCheck    = 0;

      for (uint16_t i = 0; i < NUM_LEDS; i++) {
        brightness[i] = 0;
        nextUpdate[i] = 0;
      }
    }

    // --------------------------------------------------
    //  MAIN UPDATE LOOP
    // --------------------------------------------------

    void update() {

      unsigned long now = millis();

      // ---- ALL LIGHTS OFF ----
      if (lightsOff) {
        for (uint16_t i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, 0);
        strip.show();
        return;
      }

      // ---- PRE-STORM: pulsing purple cloud ----
      if (!stormActive) {
        float pf    = getPulseFactor(now, STORM_PULSE_PERIOD_MS);
        uint32_t c  = applyPulse(baseStormCloudColor(), pf);
        renderSolidCloud(c);
        strip.show();
        return;
      }

      // Sunset transition progress: 0.0 (full storm) → 1.0 (full sunset)
      float progress = 0.0f;
      if (sunsetMode) {
        progress = constrain(encoderPosition, 0, ENCODER_FULL_STEPS)
                   / (float)ENCODER_FULL_STEPS;
      }

      // ---- AUTO-FLASH: spontaneous flashes scale quadratically with intensity ----
      if (!flashActive && !sunsetMode &&
          now - autoFlashLastCheck > AUTO_FLASH_CHECK_MS) {

        autoFlashLastCheck = now;
        // Quadratic ramp: rare at low intensity, common near max
        int chance = (int)(AUTO_FLASH_MAX_CHANCE
                           * (float)intensity * (float)intensity
                           / ((float)MAX_INTENSITY_LEVEL * MAX_INTENSITY_LEVEL));
        if (random(0, 100) < chance) startFlash();
      }

      // ---- FLASH ----
      if (flashActive) {
        if (now - flashTimer >= flashDuration) {
          flashTimer   = now;
          flashStateOn = !flashStateOn;

          if (flashStateOn) {
            flashDuration = random(FLASH_MIN_ON, FLASH_MAX_ON);
          } else {
            flashDuration = random(FLASH_MIN_OFF, FLASH_MAX_OFF);
            flashIndex++;
            if (flashIndex >= flashCount) flashActive = false;
          }
        }

        if (flashStateOn) renderFlash();
        else              renderDistributedCloud(progress, 1.0f);

        strip.show();
        return;
      }

      // ---- CRACKLE + DISTRIBUTED CLOUD ----

      // Determine whether the sunset pulse should apply
      bool  encoderIdle = sunsetMode &&
                          (now - lastEncoderActivityMs > ENCODER_IDLE_MS);
      float pulseFactor = encoderIdle
                          ? getPulseFactor(now, SUNSET_PULSE_PERIOD_MS)
                          : 1.0f;

      for (uint16_t i = 0; i < NUM_LEDS; i++) {

        if (now >= nextUpdate[i]) {

          // Base calm chance → suppress crackle as intensity and sunset grow
          int calmChance = BASE_CALM_CHANCE
                           - (intensity * CALM_CHANCE_REDUCTION_PER_LEVEL);
          calmChance = constrain(calmChance, 0, 100);
          calmChance = (int)(calmChance + progress * (100 - calmChance));

          bool crackleActive = (random(0, 100) > calmChance);
          uint8_t target = 0;

          if (crackleActive) {
            uint8_t maxB = (uint8_t)(getMaxBrightness() * (1.0f - progress));
            if (maxB < 1) maxB = 1;

            // Bimodal distribution: mostly dim flicker with occasional bright spikes
            // → looks like crackling electricity rather than uniform glow
            if (random(0, 10) < 3) {
              target = random((uint8_t)(maxB * 0.6f), maxB);       // bright spike
            } else {
              target = random(0, max(1, (int)(maxB * 0.45f)));     // dim crackle
            }
          }

          brightness[i] = target;
          nextUpdate[i] = now + random(BASE_MIN_INTERVAL, getInterval());
        }

        // Per-LED cloud colour, optionally pulsed
        uint32_t cloud = blendedCloudColorForLED(progress, i);
        if (encoderIdle) cloud = applyPulse(cloud, pulseFactor);

        // Add crackle white on top
        uint8_t b  = brightness[i];
        uint8_t cr = min(255, (int)((cloud >> 16) & 0xFF) + b);
        uint8_t cg = min(255, (int)((cloud >> 8)  & 0xFF) + b);
        uint8_t cb = min(255, (int)(cloud & 0xFF)          + b);

        strip.setPixelColor(i, applyScale(strip.Color(cr, cg, cb)));
      }

      strip.show();
    }

    // --------------------------------------------------
    //  STORM CONTROL
    // --------------------------------------------------

    void activateStorm() {
      if (!stormActive) {
        stormActive = true;
        unsigned long now = millis();
        for (uint16_t i = 0; i < NUM_LEDS; i++) {
          brightness[i] = 0;
          nextUpdate[i] = now + random(BASE_MIN_INTERVAL, BASE_MAX_INTERVAL);
        }
      }
    }

    void increaseIntensity() {
      if (intensity < MAX_INTENSITY_LEVEL) intensity += INTENSITY_STEP_SIZE;
    }

    // Public flash trigger (used by "swipe").
    // Also ramps up intensity by one level.
    // Ignored once sunset mode is active.
    void triggerFlash() {
      if (sunsetMode) return;
      activateStorm();
      increaseIntensity();   // each swipe makes the storm one step more intense
      startFlash();
    }

    void enableSunsetMode() {
      sunsetMode            = true;
      encoderPosition       = 0;
      sunsetHue             = 0.0f;
      lastEncoderActivityMs = 0;
      enc1LastTurnMs        = 0;
      enc2LastTurnMs        = 0;
      activateStorm();
    }

    // --------------------------------------------------
    //  DUAL ROTARY ENCODER
    //  Direction is ignored — always progressive.
    //  Both encoders turning simultaneously = larger step.
    // --------------------------------------------------

    void notifyEncoderTurn(int encoderNum) {
      if (!sunsetMode) return;

      unsigned long now     = millis();
      lastEncoderActivityMs = now;

      if (encoderNum == 1) enc1LastTurnMs = now;
      else                 enc2LastTurnMs = now;

      unsigned long otherLastTurn = (encoderNum == 1) ? enc2LastTurnMs
                                                       : enc1LastTurnMs;
      bool bothActive = (now - otherLastTurn) < DUAL_ENCODER_WINDOW_MS;

      advanceSunset(bothActive ? ENCODER_DUAL_STEP : ENCODER_SINGLE_STEP);
    }

    // --------------------------------------------------
    //  SERIAL COMMANDS
    // --------------------------------------------------

    void handleSerialCommand(String cmd) {
      cmd.trim();
      cmd.toLowerCase();

      if      (cmd == "swipe")           triggerFlash();
      else if (cmd == "sunset")          enableSunsetMode();
      else if (cmd == "reset")           { brightnessScale = 1.0f; lightsOff = false; resetState(); }
      else if (cmd == "test on")         brightnessScale = 0.5f;
      else if (cmd == "test off")        brightnessScale = 1.0f;
      else if (cmd.startsWith("scale ")) brightnessScale = constrain(cmd.substring(6).toFloat(), 0.0f, 1.0f);
      else if (cmd == "off")             lightsOff = true;
      else if (cmd == "on")              lightsOff = false;
    }

  // ====================================================
  private:
  // ====================================================

    Adafruit_NeoPixel &strip;

    uint8_t       brightness[NUM_LEDS];
    unsigned long nextUpdate[NUM_LEDS];

    bool    stormActive = false;
    uint8_t intensity   = 0;

    bool          flashActive   = false;
    bool          flashStateOn  = false;
    uint8_t       flashCount    = 0;
    uint8_t       flashIndex    = 0;
    unsigned long flashTimer    = 0;
    unsigned long flashDuration = 0;

    // Sunset state
    bool          sunsetMode            = false;
    int           encoderPosition       = 0;     // 0 … ENCODER_FULL_STEPS (drives progress)
    float         sunsetHue             = 0.0f;  // continuously advancing colour offset

    // Dual encoder timing
    unsigned long enc1LastTurnMs        = 0;
    unsigned long enc2LastTurnMs        = 0;
    unsigned long lastEncoderActivityMs = 0;

    // Auto-flash timing
    unsigned long autoFlashLastCheck    = 0;

    // --------------------------------------------------
    //  SUNSET ADVANCE  (always additive — no reversal)
    // --------------------------------------------------

    void advanceSunset(int step) {
      // Hue always advances (drives colour flow across LEDs)
      sunsetHue += (float)step * SUNSET_HUE_PER_STEP;

      // Progress only advances until full (drives crackle suppression)
      if (encoderPosition < ENCODER_FULL_STEPS) {
        encoderPosition = min(ENCODER_FULL_STEPS, encoderPosition + step);
      }
      // Beyond ENCODER_FULL_STEPS: only hue advances → continuous colour cycling
    }

    // --------------------------------------------------
    //  PRIVATE FLASH TRIGGER (no sunsetMode guard)
    // --------------------------------------------------

    void startFlash() {
      flashActive   = true;
      flashCount    = random(FLASH_MIN_PULSES, FLASH_MAX_PULSES + 1);
      flashIndex    = 0;
      flashStateOn  = true;
      flashTimer    = millis();
      flashDuration = random(FLASH_MIN_ON, FLASH_MAX_ON);
    }

    // --------------------------------------------------
    //  PULSE HELPERS
    // --------------------------------------------------

    float getPulseFactor(unsigned long now, unsigned long periodMs) {
      float phase = (2.0f * PI * (float)(now % periodMs)) / (float)periodMs;
      return 1.0f + PULSE_DEPTH * sin(phase);
    }

    uint32_t applyPulse(uint32_t c, float factor) {
      uint8_t r = (uint8_t)constrain((int)(((c >> 16) & 0xFF) * factor), 0, 255);
      uint8_t g = (uint8_t)constrain((int)(((c >> 8)  & 0xFF) * factor), 0, 255);
      uint8_t b = (uint8_t)constrain((int)((c & 0xFF)          * factor), 0, 255);
      return strip.Color(r, g, b);
    }

    // --------------------------------------------------
    //  COLOUR HELPERS
    // --------------------------------------------------

    uint32_t baseStormCloudColor() {
      float t   = intensity / (float)MAX_INTENSITY_LEVEL;
      float dim = pow(1.0f - t, 1.5f);
      return strip.Color(
        (uint8_t)(CLOUD_R * dim),
        (uint8_t)(CLOUD_G * dim),
        (uint8_t)(CLOUD_B * dim)
      );
    }

    // Smoothly interpolate through the wrapped sunset palette at a float position.
    // Result is scaled by progress and SUNSET_MAX_BRIGHTNESS.
    uint32_t samplePalette(float phase, float progress) {
      // Wrap phase into [0, SUNSET_PALETTE_COUNT)
      phase = fmod(phase, (float)SUNSET_PALETTE_COUNT);
      if (phase < 0.0f) phase += (float)SUNSET_PALETTE_COUNT;

      int   idx0 = (int)phase;
      int   idx1 = (idx0 + 1) % SUNSET_PALETTE_COUNT;
      float t    = phase - (float)idx0;   // 0.0 – 1.0 fraction between entries

      float scale = progress * SUNSET_MAX_BRIGHTNESS / 255.0f;

      uint8_t r = (uint8_t)((SUNSET_R[idx0] * (1.0f - t) + SUNSET_R[idx1] * t) * scale);
      uint8_t g = (uint8_t)((SUNSET_G[idx0] * (1.0f - t) + SUNSET_G[idx1] * t) * scale);
      uint8_t b = (uint8_t)((SUNSET_B[idx0] * (1.0f - t) + SUNSET_B[idx1] * t) * scale);

      return strip.Color(r, g, b);
    }

    // Each LED sits at a different position in the colour cycle, creating
    // a flowing distributed gradient rather than a uniform colour.
    uint32_t blendedCloudColorForLED(float progress, uint16_t ledIndex) {
      // Storm cloud fades out
      uint32_t sc = baseStormCloudColor();
      uint8_t sr = (uint8_t)(((sc >> 16) & 0xFF) * (1.0f - progress));
      uint8_t sg = (uint8_t)(((sc >> 8)  & 0xFF) * (1.0f - progress));
      uint8_t sb = (uint8_t)((sc & 0xFF)          * (1.0f - progress));

      // Sunset glow fades in, each LED offset in the palette cycle
      float phase = sunsetHue
                    + (float)ledIndex * SUNSET_LED_SPREAD / (float)NUM_LEDS;
      uint32_t sun = samplePalette(phase, progress);
      uint8_t ur = (sun >> 16) & 0xFF;
      uint8_t ug = (sun >> 8)  & 0xFF;
      uint8_t ub = sun & 0xFF;

      return strip.Color(
        min(255, (int)sr + ur),
        min(255, (int)sg + ug),
        min(255, (int)sb + ub)
      );
    }

    // --------------------------------------------------
    //  RENDER HELPERS
    // --------------------------------------------------

    void renderSolidCloud(uint32_t color) {
      for (uint16_t i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, applyScale(color));
      }
    }

    // Render the distributed sunset cloud with an optional brightness multiplier
    // (used between flash pulses and in the crackle loop).
    void renderDistributedCloud(float progress, float pulseFactor) {
      for (uint16_t i = 0; i < NUM_LEDS; i++) {
        uint32_t c = blendedCloudColorForLED(progress, i);
        if (pulseFactor != 1.0f) c = applyPulse(c, pulseFactor);
        strip.setPixelColor(i, applyScale(c));
      }
    }

    void renderFlash() {
      for (uint16_t i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, applyScale(strip.Color(255, 255, 255)));
      }
    }

    uint32_t applyScale(uint32_t c) {
      uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * brightnessScale);
      uint8_t g = (uint8_t)(((c >> 8)  & 0xFF) * brightnessScale);
      uint8_t b = (uint8_t)((c & 0xFF)          * brightnessScale);
      return strip.Color(r, g, b);
    }

    // --------------------------------------------------
    //  CRACKLE TIMING / BRIGHTNESS
    // --------------------------------------------------

    uint8_t getMaxBrightness() {
      int val = BASE_MAX_BRIGHTNESS + (intensity * BRIGHTNESS_PER_LEVEL);
      return (uint8_t)constrain(val, 0, ABS_MAX_BRIGHTNESS);
    }

    unsigned long getInterval() {
      int val = BASE_MAX_INTERVAL - (intensity * INTERVAL_DECREASE_PER_LEVEL);
      return (val < (int)BASE_MIN_INTERVAL)
             ? BASE_MIN_INTERVAL
             : (unsigned long)val;
    }

    uint8_t smoothStep(uint8_t current, uint8_t target) {
      return (current + target) / 2;
    }
};

// ======================================================
//                      GLOBALS
// ======================================================

LightningEffect storm(strip);

String serialBuffer = "";

// Button state
int           lastButtonState    = HIGH;
bool          buttonHeld         = false;
bool          longPressTriggered = false;
unsigned long buttonPressStart   = 0;
const unsigned long LONG_PRESS_TIME = 2000;

// Encoder states
int enc1AState, enc1ALastState;
int enc2AState, enc2ALastState;

// ======================================================
//                       SETUP
// ======================================================

void setup() {
  Serial.begin(9600);
  Serial.println("start");

  strip.begin();
  strip.show();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(outputA1, INPUT);
  pinMode(outputB1, INPUT);
  pinMode(outputA2, INPUT);
  pinMode(outputB2, INPUT);

  delay(50);
  lastButtonState = digitalRead(BUTTON_PIN);
  enc1ALastState  = digitalRead(outputA1);
  enc2ALastState  = digitalRead(outputA2);

  storm.begin();
  randomSeed(analogRead(A0));
}

// ======================================================
//                       LOOP
// ======================================================

void loop() {

  // ---- LED UPDATE ----
  storm.update();

  // ---- SERIAL INPUT ----
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        storm.handleSerialCommand(serialBuffer);
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }

  // ---- ENCODER 1 (direction ignored – any movement advances sunset) ----
  enc1AState = digitalRead(outputA1);
  if (enc1AState != enc1ALastState) {
    storm.notifyEncoderTurn(1);
    Serial.println("Enc1 step");
  }
  enc1ALastState = enc1AState;

  // ---- ENCODER 2 ----
  enc2AState = digitalRead(outputA2);
  if (enc2AState != enc2ALastState) {
    storm.notifyEncoderTurn(2);
    Serial.println("Enc2 step");
  }
  enc2ALastState = enc2AState;

  // ---- BUTTON ----
  handleButton();
}

// ======================================================
//                  BUTTON HANDLER
//  Short press removed – lightning triggered via "swipe".
//  Long press reserved for future use.
// ======================================================

void handleButton() {

  int buttonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && buttonState == LOW) {
    buttonPressStart   = millis();
    buttonHeld         = true;
    longPressTriggered = false;
  }

  if (buttonHeld && !longPressTriggered && buttonState == LOW &&
      millis() - buttonPressStart >= LONG_PRESS_TIME) {
    longPressTriggered = true;
    // Reserved
  }

  if (lastButtonState == LOW && buttonState == HIGH) {
    buttonHeld = false;
  }

  lastButtonState = buttonState;
}
