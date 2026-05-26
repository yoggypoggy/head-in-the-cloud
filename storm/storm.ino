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
// ======================================================

const uint8_t MAX_INTENSITY_LEVEL             = 30;
const uint8_t INTENSITY_STEP_SIZE             = 1;

const uint8_t BASE_MIN_BRIGHTNESS             = 0;
const uint8_t BASE_MAX_BRIGHTNESS             = 10;
const uint8_t BRIGHTNESS_PER_LEVEL            = 25;
const uint8_t ABS_MAX_BRIGHTNESS              = 255;

const unsigned long BASE_MIN_INTERVAL         = 80;
const unsigned long BASE_MAX_INTERVAL         = 700;
const int     INTERVAL_DECREASE_PER_LEVEL     = 70;

const uint8_t BASE_CALM_CHANCE                = 90;
const uint8_t CALM_CHANCE_REDUCTION_PER_LEVEL = 18;

// ---------------- CLOUD (storm) ----------------

const uint8_t CLOUD_R = 15;
const uint8_t CLOUD_G = 0;
const uint8_t CLOUD_B = 45;

// ---------------- FLASH ----------------

const uint8_t FLASH_MIN_PULSES     = 2;
const uint8_t FLASH_MAX_PULSES     = 5;

const unsigned long FLASH_MIN_ON   = 60;
const unsigned long FLASH_MAX_ON   = 220;
const unsigned long FLASH_MIN_OFF  = 80;
const unsigned long FLASH_MAX_OFF  = 350;

// ======================================================
//                    SUNSET CONFIG
// ======================================================

// Maximum brightness the sunset glow can reach (0-255)
const uint8_t SUNSET_MAX_BRIGHTNESS = 120;

// Total encoder advance clicks to go from full storm → full sunset
const int ENCODER_FULL_STEPS = 60;

// Step applied when only one encoder is turned
const int ENCODER_SINGLE_STEP = 1;

// Step applied when both encoders are turned within the window
const int ENCODER_DUAL_STEP   = 3;

// Time window (ms) within which both encoders count as "simultaneous"
const unsigned long DUAL_ENCODER_WINDOW_MS = 150;

// Sunset/dusk colour palette – warm pinks and oranges (0-255 scale)
const uint8_t SUNSET_PALETTE_COUNT = 5;
const uint8_t SUNSET_R[SUNSET_PALETTE_COUNT] = { 255, 230, 255, 200, 255 };
const uint8_t SUNSET_G[SUNSET_PALETTE_COUNT] = {  80,  90, 120,  60, 150 };
const uint8_t SUNSET_B[SUNSET_PALETTE_COUNT] = {  20,  50,  80, 120,  70 };

// ======================================================
//                    PULSE CONFIG
// ======================================================

// Amplitude of the brightness breathing effect (fraction of base, e.g. 0.18 = ±18%)
const float PULSE_DEPTH = 0.18f;

// How long one full breath takes in each context
const unsigned long STORM_PULSE_PERIOD_MS  = 1100;   // fast – stormy
const unsigned long SUNSET_PULSE_PERIOD_MS = 3600;   // slow – calm

// How long after the last encoder turn before the sunset pulse resumes
const unsigned long ENCODER_IDLE_MS = 400;

// ======================================================
//                  LIGHTNING EFFECT
// ======================================================

class LightningEffect {

  public:
    LightningEffect(Adafruit_NeoPixel &s) : strip(s) {}

    // --------------------------------------------------
    //  INIT / RESET
    // --------------------------------------------------

    void begin() {
      resetState();
    }

    void resetState() {
      stormActive           = false;
      intensity             = 0;
      flashActive           = false;
      flashStateOn          = false;
      sunsetMode            = false;
      encoderPosition       = 0;
      sunsetColorIdx        = 0;
      enc1LastTurnMs        = 0;
      enc2LastTurnMs        = 0;
      lastEncoderActivityMs = 0;

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

      // ---- PRE-STORM: static purple cloud with fast pulse ----
      if (!stormActive) {
        uint32_t c = applyPulse(baseStormCloudColor(),
                                getPulseFactor(now, STORM_PULSE_PERIOD_MS));
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

      // ---- FLASH (only possible before sunset mode) ----
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
        else              renderSolidCloud(blendedCloudColor(progress));

        strip.show();
        return;
      }

      // ---- CRACKLE + CLOUD (blended toward sunset) ----

      // Compute base cloud colour once; apply sunset pulse when encoder is idle
      uint32_t cloudBase = blendedCloudColor(progress);

      bool encoderIdle = sunsetMode &&
                         (now - lastEncoderActivityMs > ENCODER_IDLE_MS);

      if (encoderIdle) {
        cloudBase = applyPulse(cloudBase, getPulseFactor(now, SUNSET_PULSE_PERIOD_MS));
      }

      for (uint16_t i = 0; i < NUM_LEDS; i++) {

        if (now >= nextUpdate[i]) {

          // Base calm chance for current storm intensity
          int calmChance = BASE_CALM_CHANCE
                           - (intensity * CALM_CHANCE_REDUCTION_PER_LEVEL);
          calmChance = constrain(calmChance, 0, 100);

          // Push calm chance toward 100 (no crackle) as sunset progresses
          calmChance = (int)(calmChance + progress * (100 - calmChance));

          bool crackleActive = (random(0, 100) > calmChance);

          uint8_t target = 0;
          if (crackleActive) {
            // Scale crackle brightness down with sunset progress
            uint8_t maxB = (uint8_t)(getMaxBrightness() * (1.0f - progress));
            if (maxB < 1) maxB = 1;
            target = random(BASE_MIN_BRIGHTNESS, maxB);
          }

          brightness[i] = target;
          nextUpdate[i] = now + random(BASE_MIN_INTERVAL, getInterval());
        }

        // Mix cloud colour with crackle white
        uint8_t b  = brightness[i];
        uint8_t cr = min(255, (int)((cloudBase >> 16) & 0xFF) + b);
        uint8_t cg = min(255, (int)((cloudBase >> 8)  & 0xFF) + b);
        uint8_t cb = min(255, (int)(cloudBase & 0xFF)          + b);

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

    // Trigger a multi-pulse lightning flash.
    // Ignored once sunset mode has been activated.
    void triggerFlash() {
      if (sunsetMode) return;

      activateStorm();

      flashActive   = true;
      flashCount    = random(FLASH_MIN_PULSES, FLASH_MAX_PULSES + 1);
      flashIndex    = 0;
      flashStateOn  = true;
      flashTimer    = millis();
      flashDuration = random(FLASH_MIN_ON, FLASH_MAX_ON);
    }

    // Enter sunset transition mode.
    void enableSunsetMode() {
      sunsetMode            = true;
      encoderPosition       = 0;
      sunsetColorIdx        = 0;
      lastEncoderActivityMs = 0;  // encoder starts idle → pulse is active immediately
      enc1LastTurnMs        = 0;
      enc2LastTurnMs        = 0;
      activateStorm();
    }

    // --------------------------------------------------
    //  DUAL ROTARY ENCODER
    //
    //  Call this from the main loop whenever either encoder
    //  detects a step (any direction – effect is always
    //  progressive; direction is ignored).
    //
    //  encoderNum: 1 or 2
    // --------------------------------------------------

    void notifyEncoderTurn(int encoderNum) {
      if (!sunsetMode) return;

      unsigned long now     = millis();
      lastEncoderActivityMs = now;

      // Record when this encoder last moved
      if (encoderNum == 1) enc1LastTurnMs = now;
      else                 enc2LastTurnMs = now;

      // Check whether the *other* encoder also turned recently
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

      if (cmd == "swipe") {
        triggerFlash();
      }
      else if (cmd == "sunset") {
        enableSunsetMode();
      }
      else if (cmd == "reset") {
        brightnessScale = 1.0f;
        lightsOff       = false;
        resetState();
      }
      else if (cmd == "test on") {
        brightnessScale = 0.5f;
      }
      else if (cmd == "test off") {
        brightnessScale = 1.0f;
      }
      else if (cmd.startsWith("scale ")) {
        brightnessScale =
          constrain(cmd.substring(6).toFloat(), 0.0f, 1.0f);
      }
      else if (cmd == "off") {
        lightsOff = true;
      }
      else if (cmd == "on") {
        lightsOff = false;
      }
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

    // Sunset transition state
    bool          sunsetMode            = false;
    int           encoderPosition       = 0;   // 0 … ENCODER_FULL_STEPS
    int           sunsetColorIdx        = 0;   // 0 … SUNSET_PALETTE_COUNT-1

    // Dual encoder timing
    unsigned long enc1LastTurnMs        = 0;
    unsigned long enc2LastTurnMs        = 0;
    unsigned long lastEncoderActivityMs = 0;

    // --------------------------------------------------
    //  SUNSET ADVANCE (always progressive, no reversal)
    // --------------------------------------------------

    void advanceSunset(int step) {
      if (encoderPosition < ENCODER_FULL_STEPS) {
        encoderPosition = min(ENCODER_FULL_STEPS, encoderPosition + step);
      } else {
        // Fully transitioned: each individual encoder turn cycles one colour
        sunsetColorIdx = (sunsetColorIdx + 1) % SUNSET_PALETTE_COUNT;
      }
    }

    // --------------------------------------------------
    //  PULSE HELPERS
    // --------------------------------------------------

    // Returns a multiplier that oscillates between (1-depth) and (1+depth)
    float getPulseFactor(unsigned long now, unsigned long periodMs) {
      float phase = (2.0f * PI * (float)(now % periodMs)) / (float)periodMs;
      return 1.0f + PULSE_DEPTH * sin(phase);
    }

    // Scale each channel of a colour by a float factor, clamping to 0-255
    uint32_t applyPulse(uint32_t c, float factor) {
      uint8_t r = (uint8_t)constrain((int)(((c >> 16) & 0xFF) * factor), 0, 255);
      uint8_t g = (uint8_t)constrain((int)(((c >> 8)  & 0xFF) * factor), 0, 255);
      uint8_t b = (uint8_t)constrain((int)((c & 0xFF)          * factor), 0, 255);
      return strip.Color(r, g, b);
    }

    // --------------------------------------------------
    //  COLOUR HELPERS
    // --------------------------------------------------

    // Storm cloud colour; dims as intensity rises
    uint32_t baseStormCloudColor() {
      float t   = intensity / (float)MAX_INTENSITY_LEVEL;
      float dim = pow(1.0f - t, 1.5f);
      return strip.Color(
        (uint8_t)(CLOUD_R * dim),
        (uint8_t)(CLOUD_G * dim),
        (uint8_t)(CLOUD_B * dim)
      );
    }

    // Sunset colour for the active palette entry, scaled to SUNSET_MAX_BRIGHTNESS,
    // and faded in proportionally to progress.
    uint32_t sunsetCloudColor(float progress) {
      float scale = progress * SUNSET_MAX_BRIGHTNESS / 255.0f;
      return strip.Color(
        (uint8_t)(SUNSET_R[sunsetColorIdx] * scale),
        (uint8_t)(SUNSET_G[sunsetColorIdx] * scale),
        (uint8_t)(SUNSET_B[sunsetColorIdx] * scale)
      );
    }

    // Cross-fade: storm cloud fades out, sunset glow fades in
    uint32_t blendedCloudColor(float progress) {
      uint32_t sc = baseStormCloudColor();

      uint8_t sr = (uint8_t)(((sc >> 16) & 0xFF) * (1.0f - progress));
      uint8_t sg = (uint8_t)(((sc >> 8)  & 0xFF) * (1.0f - progress));
      uint8_t sb = (uint8_t)((sc & 0xFF)          * (1.0f - progress));

      uint32_t sun = sunsetCloudColor(progress);
      uint8_t  ur  = (sun >> 16) & 0xFF;
      uint8_t  ug  = (sun >> 8)  & 0xFF;
      uint8_t  ub  = sun & 0xFF;

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

// Encoder 1 state
int enc1AState;
int enc1ALastState;

// Encoder 2 state
int enc2AState;
int enc2ALastState;

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

  // ---- ENCODER 1 ----
  // Direction is ignored; any movement advances the sunset transition.
  enc1AState = digitalRead(outputA1);
  if (enc1AState != enc1ALastState) {
    storm.notifyEncoderTurn(1);
    // Serial.println("Encoder1 step");
  }
  enc1ALastState = enc1AState;

  // ---- ENCODER 2 ----
  enc2AState = digitalRead(outputA2);
  if (enc2AState != enc2ALastState) {
    storm.notifyEncoderTurn(2);
    // Serial.println("Encoder2 step");
  }
  enc2ALastState = enc2AState;

  // ---- BUTTON ----
  handleButton();
}

// ======================================================
//                   BUTTON HANDLER
//
//  Short press removed — lightning is now triggered via
//  the "swipe" serial command.
//  Long press reserved for future use.
// ======================================================

void handleButton() {

  int buttonState = digitalRead(BUTTON_PIN);

  // PRESS DOWN
  if (lastButtonState == HIGH && buttonState == LOW) {
    buttonPressStart   = millis();
    buttonHeld         = true;
    longPressTriggered = false;
  }

  // LONG PRESS (held >= LONG_PRESS_TIME)
  if (buttonHeld &&
      !longPressTriggered &&
      buttonState == LOW &&
      millis() - buttonPressStart >= LONG_PRESS_TIME) {

    longPressTriggered = true;
    // Reserved — add behaviour here if needed
  }

  // RELEASE
  if (lastButtonState == LOW && buttonState == HIGH) {
    buttonHeld = false;
  }

  lastButtonState = buttonState;
}
