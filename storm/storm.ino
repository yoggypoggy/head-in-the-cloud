#include <Adafruit_NeoPixel.h>

// ======================================================
//                        CONFIG
// ======================================================

#define LED_PIN  4
#define NUM_LEDS 30

// Encoder 1
#define outputA1 13
#define outputB1 12

// Encoder 2  
#define outputA2 27
#define outputB2 33

// Distance sensor
#define TRIG_PIN 15
#define ECHO_PIN 32

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

float brightnessScale = 1.0f;
bool  lightsOff       = false;

// ======================================================
//                    STORM CONFIG
// ~30 swipes raise intensity one step each.
// Ramp is delayed: first ~10 swipes are subtle, intensity
// builds in the second half. Max is firm but not strobing.
// ======================================================

const uint8_t  MAX_INTENSITY_LEVEL             = 30;
const uint8_t  INTENSITY_STEP_SIZE             = 1;

const uint8_t  BASE_MIN_BRIGHTNESS             = 0;
const uint8_t  BASE_MAX_BRIGHTNESS             = 20;
const uint8_t  BRIGHTNESS_PER_LEVEL            = 6;   // 5+30×7 = 215 at max
const uint8_t  ABS_MAX_BRIGHTNESS              = 255;

const unsigned long BASE_MIN_INTERVAL          = 80;
const unsigned long BASE_MAX_INTERVAL          = 800;
const int      INTERVAL_DECREASE_PER_LEVEL     = 15;  // 800-30×20 = 200ms at max

const uint8_t  BASE_CALM_CHANCE                = 95;  // 5% crackle at level 0
const uint8_t  CALM_CHANCE_REDUCTION_PER_LEVEL = 2;   // 35% calm at max → 65% crackle

// Cloud colour (storm)
const uint8_t  CLOUD_R = 60;
const uint8_t  CLOUD_G = 20;
const uint8_t  CLOUD_B = 120;

// ======================================================
//                    FLASH CONFIG
// ======================================================

const uint8_t  FLASH_MIN_PULSES    = 1;
const uint8_t  FLASH_MAX_PULSES    = 2;

const unsigned long FLASH_MIN_ON   = 150;
const unsigned long FLASH_MAX_ON   = 300;
const unsigned long FLASH_MIN_OFF  = 150;
const unsigned long FLASH_MAX_OFF  = 300;

// Auto-flash: spontaneous flashes grow quadratically with intensity
const unsigned long AUTO_FLASH_CHECK_MS    = 1500;
const uint8_t       AUTO_FLASH_MAX_CHANCE  = 55;  // % at full intensity

// ======================================================
//                    SUNSET CONFIG
// ======================================================

const uint8_t  SUNSET_MAX_BRIGHTNESS  = 200;
const int          ENCODER_FULL_STEPS      = 160;
const int          ENCODER_SINGLE_STEP     = 0;
const int          ENCODER_DUAL_STEP       = 1;
const unsigned long DUAL_ENCODER_WINDOW_MS = 150;

// How far the hue map shifts per encoder step (drives colour flow)
const float SUNSET_HUE_PER_STEP = 0.02f;
// How many palette units span the full LED strip
const float SUNSET_LED_SPREAD   = 5.0f;

// Warm sunset palette (pink → orange → coral → magenta → amber)
const uint8_t SUNSET_PALETTE_COUNT   = 5;
const uint8_t SUNSET_R[5] = { 255, 230, 255, 200, 140 };
const uint8_t SUNSET_G[5] = {  80,  90, 120,  60, 210 };
const uint8_t SUNSET_B[5] = {  20,  50,  80, 120, 200 };

// ======================================================
//                    PULSE CONFIG
// ======================================================

const float        PULSE_DEPTH              = 0.38f;
const unsigned long STORM_PULSE_PERIOD_MS   = 1800;
const unsigned long SUNSET_PULSE_PERIOD_MS  = 3500;
const unsigned long ENCODER_IDLE_MS         = 400;

// ======================================================
//                    PROXIMITY CONFIG
// ======================================================
const unsigned long DISTANCE_CHECK_INTERVAL_MS = 1000;
const int           DISTANCE_TRIGGER_CM        = 20;
const uint8_t       PROXIMITY_ALERT_BRIGHTNESS = 180;

// ======================================================
//                    GLITCH CONFIG
// ======================================================

// Brightness of pure R / B glitch flashes (0–255)
const uint8_t GLITCH_BRIGHTNESS = 220;

// Number of encoder twists to complete the glitch stage
const uint8_t GLITCH_N_TWISTS = 8;

// Twist detection: cumulative absolute encoder ticks within a time window
const int          GLITCH_TWIST_TICKS      = 25;
const unsigned long GLITCH_TWIST_WINDOW_MS = 1800;

// How soon after the last tick the encoder counts as "actively twisting"
const unsigned long GLITCH_ACTIVE_TWIST_MS = 500;

// Heartbeat timing
const unsigned long GLITCH_BEAT_PULSE_MS         = 200;   // each pulse ON duration
const unsigned long GLITCH_BEAT_GAP_MS           = 200;  // gap between the two pulses
const unsigned long GLITCH_BEAT_PAUSE_MS         = 700;  // pause after ba-bump (was GLITCH_LIGHT_MAX_MS range)
const unsigned long GLITCH_BEAT_PAUSE_CHAOTIC_MS = 40;  // shorter pause in chaotic mode (was GLITCH_CHAOTIC range)

// Sunset-colour flash durations (during active twist AND post-nth-twist idle)
const unsigned long GLITCH_FLASH_MIN_MS = 200;
const unsigned long GLITCH_FLASH_MAX_MS = 400;

const unsigned long GLITCH_IDLE_SUNSET_START_MS = 2000;  // interval at 0 twists
const unsigned long GLITCH_IDLE_SUNSET_END_MS   = 400;   // interval at max twists

// Gap between idle sunset flashes once glitch stage is complete
const unsigned long GLITCH_IDLE_SUNSET_MIN_MS = 500;
const unsigned long GLITCH_IDLE_SUNSET_MAX_MS = 1000;

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
      stormActive        = false;
      intensity          = 0;
      flashActive        = false;
      flashStateOn       = false;
      autoFlashLastCheck = 0;

      proximityAlert = false;

      glitchMode            = false;
      glitchComplete        = false;
      glitchTwistsDone      = 0;
      glitchFlashSunset     = false;
      glitchFlashTimer      = 0;
      glitchIdleFlashActive = false;
      glitchIdleFlashTimer  = 0;

      enc1TickAccum    = 0;
      enc2TickAccum    = 0;
      enc1WindowStart  = 0;
      enc2WindowStart  = 0;
      enc1WindowActive = false;
      enc2WindowActive = false;
      lastAnyTickMs    = 0;

      encoderPosition       = 0;
      sunsetHue             = 0.0f;
      enc1LastTurnMs        = 0;
      enc2LastTurnMs        = 0;
      lastEncoderActivityMs = 0;

      for (uint16_t i = 0; i < NUM_LEDS; i++) {
        brightness[i]   = 0;
        nextUpdate[i]   = 0;
        glitchBeatColour = 1;
        glitchBeatPhase  = 0;
        glitchBeatTimer  = 0;
      }
    }

    // --------------------------------------------------
    //  MAIN UPDATE LOOP
    // --------------------------------------------------

    void update() {
      unsigned long now = millis();

      if (lightsOff) {
        for (uint16_t i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, 0);
        strip.show();
        return;
      }

      // GLITCH / SUNSET — takes full priority once entered
      if (glitchMode) {
        renderGlitchMode(now);
        return;
      }

      // PRE-STORM: pulsing purple cloud
      if (!stormActive) {
        float pf   = getPulseFactor(now, STORM_PULSE_PERIOD_MS);
        uint32_t c = applyPulse(baseStormCloudColor(), pf);
        renderSolidCloud(c);
        strip.show();
        return;
      }

      // STORM MODE ----

      // Auto-flash: probability ramps quadratically with intensity
      if (!flashActive && now - autoFlashLastCheck > AUTO_FLASH_CHECK_MS) {
        autoFlashLastCheck = now;
        int chance = (int)(AUTO_FLASH_MAX_CHANCE
                          * (float)intensity * (float)intensity
                          / ((float)MAX_INTENSITY_LEVEL * MAX_INTENSITY_LEVEL));
        if (random(0, 100) < chance) startFlash();
      }
      

      // Flash
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
        else              renderSolidCloud(baseStormCloudColor());
        strip.show();
        return;
      }

      if (proximityAlert) {
        uint32_t c = strip.Color(PROXIMITY_ALERT_BRIGHTNESS, 0, 0);
        for (uint16_t i = 0; i < NUM_LEDS; i++)
      strip.setPixelColor(i, applyScale(c));
      strip.show();
      return;
    }

      // Crackle
      uint32_t cloud = baseStormCloudColor();
      for (uint16_t i = 0; i < NUM_LEDS; i++) {
        if (now >= nextUpdate[i]) {
          int calmChance = BASE_CALM_CHANCE
                           - (intensity * CALM_CHANCE_REDUCTION_PER_LEVEL);
          calmChance = constrain(calmChance, 0, 100);

          bool crackleActive = (random(0, 100) > calmChance);
          uint8_t target = 0;
          if (crackleActive) {
            uint8_t maxB = getMaxBrightness();
            if (maxB < 1) maxB = 1;
            // Bimodal: mostly dim with occasional bright spikes → looks like
            // crackling electricity rather than uniform glow
            if (random(0, 10) < 3)
              target = random((uint8_t)(maxB * 0.5f), maxB);
            else
              target = random(0, max(1, (int)(maxB * 0.35f)));
          }
          brightness[i] = target;
          nextUpdate[i] = now + random(BASE_MIN_INTERVAL, getInterval());
        }

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

    void setProximityAlert(bool alert) {
      proximityAlert = alert;
    }

    // Each swipe increases intensity by one step.
    // Commented-out code: On hitting MAX_INTENSITY_LEVEL, glitch mode begins automatically.
    void triggerFlash() {
      if (glitchMode || proximityAlert) return;
      activateStorm();
      if (intensity < MAX_INTENSITY_LEVEL) {
        intensity += INTENSITY_STEP_SIZE;
        // if (intensity >= MAX_INTENSITY_LEVEL) {
        //   enterGlitchMode();
        //   return; // skip flash – glitch takes over immediately
        // }
      }
      startFlash();
    }

    // --------------------------------------------------
    //  GLITCH MODE
    // --------------------------------------------------

    void enterGlitchMode() {
      if (glitchMode) return;

      glitchMode            = true;
      glitchComplete        = false;
      glitchTwistsDone      = 0;
      glitchFlashSunset     = false;
      glitchFlashTimer      = 0;
      glitchIdleFlashActive = false;
      glitchIdleFlashTimer  = 0;

      enc1TickAccum    = 0;
      enc2TickAccum    = 0;
      enc1WindowActive = false;
      enc2WindowActive = false;
      lastAnyTickMs    = 0;

      unsigned long now = millis();
      for (uint16_t i = 0; i < NUM_LEDS; i++) {
        glitchBeatColour = 1;
        glitchBeatPhase  = 0;
        glitchBeatTimer  = now + GLITCH_BEAT_PULSE_MS;
      }
    }

    // Complete the glitch stage: n twists done, awaiting encoder for sunset transition.
    void completeGlitchStage() {
      // updateAndRenderGlitchLEDs(millis(), true)
      // strip.show()
      // delay(3000);
      glitchComplete        = true;
      encoderPosition       = 0;
      sunsetHue             = 0.0f;
      lastEncoderActivityMs = 0;
      enc1LastTurnMs        = 0;
      enc2LastTurnMs        = 0;
      glitchFlashSunset     = false;
      glitchIdleFlashActive = false;
      glitchIdleFlashTimer  = millis()
                              + random(GLITCH_IDLE_SUNSET_MIN_MS, GLITCH_IDLE_SUNSET_MAX_MS + 1);
    }

    // --------------------------------------------------
    //  ENCODER INPUT
    //
    //  In glitch stage:  accumulates ticks for twist detection.
    //  Post-glitch:      drives the sunset transition (same dual
    //                    encoder mechanism as before).
    // --------------------------------------------------

    void notifyEncoderTurn(int encoderNum) {
      unsigned long now = millis();

      // ---- POST-GLITCH: sunset transition ----
      if (glitchComplete) {
        lastEncoderActivityMs = now;
        if (encoderNum == 1) enc1LastTurnMs = now;
        else                 enc2LastTurnMs = now;

        unsigned long other = (encoderNum == 1) ? enc2LastTurnMs : enc1LastTurnMs;
        bool both = (now - other) < DUAL_ENCODER_WINDOW_MS;
        advanceSunset(both ? ENCODER_DUAL_STEP : ENCODER_SINGLE_STEP);
        return;
      }

      // ---- GLITCH STAGE: twist detection ----
      if (!glitchMode) return;

      lastAnyTickMs = now;

      // Per-encoder cumulative tick accumulation
      int           &accum     = (encoderNum == 1) ? enc1TickAccum    : enc2TickAccum;
      unsigned long &winStart  = (encoderNum == 1) ? enc1WindowStart  : enc2WindowStart;
      bool          &winActive = (encoderNum == 1) ? enc1WindowActive : enc2WindowActive;

      if (!winActive) {
        winActive = true;
        winStart  = now;
        accum     = 0;
      }
      accum++;  // cumulative absolute ticks (direction ignored)

      if (accum >= GLITCH_TWIST_TICKS) {
        Serial.println("twist");
        glitchTwistsDone++;
        Serial.println(glitchTwistsDone);
        accum     = 0;
        winActive = false;

        if (glitchTwistsDone >= GLITCH_N_TWISTS) {
          completeGlitchStage();
        }
      }
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
      else if (cmd == "glitch") {
        // Testing: force entry into glitch mode
        if (!glitchMode) {
          stormActive = true;
          intensity   = MAX_INTENSITY_LEVEL;
          enterGlitchMode();
        }
      }
      else if (cmd == "sunset") {
        // Testing: jump to post-glitch state (n twists done, no encoder yet)
        stormActive = true;
        intensity   = MAX_INTENSITY_LEVEL;
        if (!glitchMode)    enterGlitchMode();
        if (!glitchComplete) {
          glitchTwistsDone = GLITCH_N_TWISTS;
          completeGlitchStage();
        }
      }
      else if (cmd == "reset") {
        brightnessScale = 1.0f;
        lightsOff       = false;
        resetState();
      }
      else if (cmd == "test on")         brightnessScale = 0.5f;
      else if (cmd == "test off")        brightnessScale = 1.0f;
      else if (cmd.startsWith("scale ")) brightnessScale = constrain(cmd.substring(6).toFloat(), 0.0f, 1.0f);
      else if (cmd == "off")             lightsOff = true;
      else if (cmd == "on")              lightsOff = false;
    }

    bool isStormMode() {
       return stormActive && !glitchMode;
      }
    bool isGlitchMode() {
      return glitchMode;
    }

  // ====================================================
  private:
  // ====================================================

    Adafruit_NeoPixel &strip;

    // Storm crackle state
    uint8_t       brightness[NUM_LEDS];
    unsigned long nextUpdate[NUM_LEDS];

    bool    stormActive        = false;
    uint8_t intensity          = 0;

    bool          flashActive   = false;
    bool          flashStateOn  = false;
    uint8_t       flashCount    = 0;
    uint8_t       flashIndex    = 0;
    unsigned long flashTimer    = 0;
    unsigned long flashDuration = 0;

    unsigned long autoFlashLastCheck = 0;

    bool proximityAlert = false;

    // Glitch mode state
    bool          glitchMode            = false;
    bool          glitchComplete        = false;
    uint8_t       glitchTwistsDone      = 0;
    uint8_t       glitchColor[NUM_LEDS];
    unsigned long glitchExpiry[NUM_LEDS];
    bool          glitchFlashSunset     = false;  // currently showing sunset colour
    unsigned long glitchFlashTimer      = 0;
    bool          glitchIdleFlashActive = false;
    unsigned long glitchIdleFlashTimer  = 0;

    uint8_t       glitchBeatColour = 1;
    uint8_t       glitchBeatPhase  = 0;
    unsigned long glitchBeatTimer  = 0;

    uint8_t glitchSunsetFlashIdx = 0;

    // Twist detection
    int           enc1TickAccum    = 0;
    int           enc2TickAccum    = 0;
    unsigned long enc1WindowStart  = 0;
    unsigned long enc2WindowStart  = 0;
    bool          enc1WindowActive = false;
    bool          enc2WindowActive = false;
    unsigned long lastAnyTickMs    = 0;

    // Sunset transition (post-glitch encoder)
    int           encoderPosition       = 0;
    float         sunsetHue             = 0.0f;
    unsigned long enc1LastTurnMs        = 0;
    unsigned long enc2LastTurnMs        = 0;
    unsigned long lastEncoderActivityMs = 0;

    // --------------------------------------------------
    //  GLITCH MODE RENDERING
    // --------------------------------------------------

    void renderGlitchMode(unsigned long now) {

      // Expire stale twist-detection windows
      if (enc1WindowActive && now - enc1WindowStart > GLITCH_TWIST_WINDOW_MS) {
        enc1WindowActive = false;
        enc1TickAccum    = 0;
      }
      if (enc2WindowActive && now - enc2WindowStart > GLITCH_TWIST_WINDOW_MS) {
        enc2WindowActive = false;
        enc2TickAccum    = 0;
      }

      // ---- GLITCH COMPLETE: encoder-driven sunset transition ----
      if (glitchComplete) {
        float progress = constrain(encoderPosition, 0, ENCODER_FULL_STEPS)
                         / (float)ENCODER_FULL_STEPS;

        // Full sunset
        if (progress >= 1.0f) {
          bool  idle = (now - lastEncoderActivityMs > ENCODER_IDLE_MS);
          float pf   = idle ? getPulseFactor(now, SUNSET_PULSE_PERIOD_MS) : 1.0f;
          renderDistributedCloud(1.0f, pf);
          strip.show();
          return;
        }

        // Blending: glitch colours fade out, sunset gradient fades in
        if (progress > 0.0f) {
          renderGlitchSunsetBlend(progress, now);
          strip.show();
          return;
        }

        // Awaiting first encoder turn: glitch with occasional idle sunset flashes
        if (now >= glitchIdleFlashTimer) {
          if (!glitchIdleFlashActive) {
            glitchIdleFlashActive = true;
            glitchSunsetFlashIdx  = random(0, SUNSET_PALETTE_COUNT);
            glitchIdleFlashTimer  = now + random(GLITCH_FLASH_MIN_MS,
                                                 GLITCH_FLASH_MAX_MS + 1);
          } else {
            glitchIdleFlashActive = false;
            float t = min(1.0f, glitchTwistsDone / (float)GLITCH_N_TWISTS);
            unsigned long interval = (unsigned long)(GLITCH_IDLE_SUNSET_START_MS
                                     + t * (long)(GLITCH_IDLE_SUNSET_END_MS - GLITCH_IDLE_SUNSET_START_MS));
            glitchIdleFlashTimer = now + interval;
          }
        }

        if (glitchIdleFlashActive)
          renderSunsetFlashColour();
        else
          updateAndRenderGlitchLEDs(now, true);  // always chaotic once complete

        strip.show();
        return;
      }

      // ---- GLITCH IN PROGRESS ----

      // Chaotic mode kicks in after (n-1) twists
      bool chaotic       = (glitchTwistsDone >= (GLITCH_N_TWISTS - 1));
      bool activeTwisting = (now - lastAnyTickMs < GLITCH_ACTIVE_TWIST_MS)
                             && (lastAnyTickMs > 0);

      if (activeTwisting) {
        // Alternate between sunset colour and glitch at ~200-400ms
if (now >= glitchFlashTimer) {
  glitchFlashSunset = !glitchFlashSunset;
  glitchFlashTimer  = now + random(GLITCH_FLASH_MIN_MS, GLITCH_FLASH_MAX_MS + 1);
  if (glitchFlashSunset) {glitchSunsetFlashIdx = random(0, SUNSET_PALETTE_COUNT);}
}
        if (glitchFlashSunset) {
          renderSunsetFlashColour();
          strip.show();
          return;
        }
      } else {
        glitchFlashSunset = false;  // reset cleanly for next twist
      }

      updateAndRenderGlitchLEDs(now, chaotic);
      strip.show();
    }

    // Update expired LED states and render the glitch pattern
    void updateAndRenderGlitchLEDs(unsigned long now, bool chaotic) {
      if (now >= glitchBeatTimer) {
        glitchBeatPhase = (glitchBeatPhase + 1) % 4;

        // After completing a full ba-bump (phase wraps to 0), switch colour  
      if (glitchBeatPhase == 0)
      glitchBeatColour = (glitchBeatColour == 1) ? 3 : 1;

      unsigned long pauseMs = chaotic ? GLITCH_BEAT_PAUSE_CHAOTIC_MS
                                    : GLITCH_BEAT_PAUSE_MS;
      switch (glitchBeatPhase) {
        case 0: glitchBeatTimer = now + GLITCH_BEAT_PULSE_MS; break;
        case 1: glitchBeatTimer = now + GLITCH_BEAT_GAP_MS;   break;
        case 2: glitchBeatTimer = now + GLITCH_BEAT_PULSE_MS; break;
        case 3: glitchBeatTimer = now + pauseMs;               break;
    }
  }

  // Phases 0 and 2 = colour ON, phases 1 and 3 = OFF
  uint32_t c = 0;
  if (glitchBeatPhase == 0 || glitchBeatPhase == 2) {
    c = (glitchBeatColour == 1)
        ? strip.Color(GLITCH_BRIGHTNESS, 0, 0)
        : strip.Color(0, 0, GLITCH_BRIGHTNESS);
  }

  for (uint16_t i = 0; i < NUM_LEDS; i++)
    strip.setPixelColor(i, applyScale(c));
}

void renderSunsetFlashColour() {
  float scale = SUNSET_MAX_BRIGHTNESS / 255.0f;
  int idx = random(0, SUNSET_PALETTE_COUNT);
  uint32_t c = strip.Color(
    (uint8_t)(SUNSET_R[glitchSunsetFlashIdx] * scale),
    (uint8_t)(SUNSET_G[glitchSunsetFlashIdx] * scale),
    (uint8_t)(SUNSET_B[glitchSunsetFlashIdx] * scale)
  );
  for (uint16_t i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, applyScale(c));
}

    // All LEDs show the fixed orangey-pink sunset flash colour
void renderGlitchSunsetBlend(float progress, unsigned long now) {
  // Glitch component: use current heartbeat phase and colour
  uint32_t gc = 0;
  if (glitchBeatPhase == 0 || glitchBeatPhase == 2) {
    gc = (glitchBeatColour == 1)
         ? strip.Color(GLITCH_BRIGHTNESS, 0, 0)
         : strip.Color(0, 0, GLITCH_BRIGHTNESS);
  }

  uint8_t gr = (uint8_t)(((gc >> 16) & 0xFF) * (1.0f - progress));
  uint8_t gg = (uint8_t)(((gc >> 8)  & 0xFF) * (1.0f - progress));
  uint8_t gb = (uint8_t)((gc & 0xFF)          * (1.0f - progress));

  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    // Sunset component (fades in as distributed gradient)
    float phase = sunsetHue
                  + (float)i * SUNSET_LED_SPREAD / (float)NUM_LEDS;
    uint32_t sun = samplePalette(phase, progress);
    uint8_t  sr  = (sun >> 16) & 0xFF;
    uint8_t  sg  = (sun >> 8)  & 0xFF;
    uint8_t  sb  = sun & 0xFF;

    strip.setPixelColor(i, applyScale(strip.Color(
      min(255, (int)gr + sr),
      min(255, (int)gg + sg),
      min(255, (int)gb + sb)
    )));
  }
}

    // --------------------------------------------------
    //  SUNSET ADVANCE (post-glitch encoder)
    // --------------------------------------------------

    void advanceSunset(int step) {
      sunsetHue += (float)step * SUNSET_HUE_PER_STEP;
      if (encoderPosition < ENCODER_FULL_STEPS)
        encoderPosition = min(ENCODER_FULL_STEPS, encoderPosition + step);
      // After ENCODER_FULL_STEPS: only hue advances → continuous colour cycling
    }

    // --------------------------------------------------
    //  FLASH (private, no mode guard)
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
    // Result is scaled to SUNSET_MAX_BRIGHTNESS via progress.
    uint32_t samplePalette(float phase, float progress) {
      phase = fmod(phase, (float)SUNSET_PALETTE_COUNT);
      if (phase < 0.0f) phase += (float)SUNSET_PALETTE_COUNT;

      int   idx0 = (int)phase;
      int   idx1 = (idx0 + 1) % SUNSET_PALETTE_COUNT;
      float t    = phase - (float)idx0;

      float scale = progress * SUNSET_MAX_BRIGHTNESS / 255.0f;

      return strip.Color(
        (uint8_t)((SUNSET_R[idx0] * (1.0f - t) + SUNSET_R[idx1] * t) * scale),
        (uint8_t)((SUNSET_G[idx0] * (1.0f - t) + SUNSET_G[idx1] * t) * scale),
        (uint8_t)((SUNSET_B[idx0] * (1.0f - t) + SUNSET_B[idx1] * t) * scale)
      );
    }

    // Per-LED colour: storm cloud fades out, sunset gradient fades in
    uint32_t blendedCloudColorForLED(float progress, uint16_t ledIndex) {
      uint32_t sc = baseStormCloudColor();
      uint8_t  sr = (uint8_t)(((sc >> 16) & 0xFF) * (1.0f - progress));
      uint8_t  sg = (uint8_t)(((sc >> 8)  & 0xFF) * (1.0f - progress));
      uint8_t  sb = (uint8_t)((sc & 0xFF)          * (1.0f - progress));

      float phase = sunsetHue
                    + (float)ledIndex * SUNSET_LED_SPREAD / (float)NUM_LEDS;
      uint32_t sun = samplePalette(phase, progress);
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
      for (uint16_t i = 0; i < NUM_LEDS; i++)
        strip.setPixelColor(i, applyScale(color));
    }

    // Render distributed sunset gradient, optionally with a pulse factor
    void renderDistributedCloud(float progress, float pulseFactor) {
      for (uint16_t i = 0; i < NUM_LEDS; i++) {
        uint32_t c = blendedCloudColorForLED(progress, i);
        if (pulseFactor != 1.0f) c = applyPulse(c, pulseFactor);
        strip.setPixelColor(i, applyScale(c));
      }
    }

    void renderFlash() {
      for (uint16_t i = 0; i < NUM_LEDS; i++)
        strip.setPixelColor(i, applyScale(strip.Color(255, 255, 255)));
    }

    uint32_t applyScale(uint32_t c) {
      return strip.Color(
        (uint8_t)(((c >> 16) & 0xFF) * brightnessScale),
        (uint8_t)(((c >> 8)  & 0xFF) * brightnessScale),
        (uint8_t)((c & 0xFF)          * brightnessScale)
      );
    }

    // --------------------------------------------------
    //  CRACKLE TIMING / BRIGHTNESS
    // --------------------------------------------------

    uint8_t getMaxBrightness() {
      return (uint8_t)constrain(BASE_MAX_BRIGHTNESS + intensity * BRIGHTNESS_PER_LEVEL,
                                0, ABS_MAX_BRIGHTNESS);
    }

    unsigned long getInterval() {
      int val = BASE_MAX_INTERVAL - intensity * INTERVAL_DECREASE_PER_LEVEL;
      return (val < (int)BASE_MIN_INTERVAL) ? BASE_MIN_INTERVAL : (unsigned long)val;
    }
};

// ======================================================
//                      GLOBALS
// ======================================================

LightningEffect storm(strip);
String          serialBuffer = "";

int enc1AState, enc1ALastState;
int enc2AState, enc2ALastState;

unsigned long lastDistanceCheckMs  = 0;
bool          lastProximityAlert   = false;

// ======================================================
//                       SETUP
// ======================================================

void setup() {
  Serial.begin(9600);
  Serial.println("start");

  strip.begin();
  strip.show();

  pinMode(outputA1, INPUT);
  pinMode(outputB1, INPUT);
  pinMode(outputA2, INPUT);
  pinMode(outputB2, INPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  delay(50);
  enc1ALastState = digitalRead(outputA1);
  enc2ALastState = digitalRead(outputA2);

  storm.begin();
  randomSeed(analogRead(A0));
}

// ======================================================
//                       LOOP
// ======================================================

void loop() {

  storm.update();

  // ---- SERIAL ----
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
  if (storm.isGlitchMode()) {
  // ---- ENCODER 1 ----
  enc1AState = digitalRead(outputA1);
  if (enc1AState != enc1ALastState) storm.notifyEncoderTurn(1);
  enc1ALastState = enc1AState;

  // ---- ENCODER 2 ----
  enc2AState = digitalRead(outputA2);
  if (enc2AState != enc2ALastState) storm.notifyEncoderTurn(2);
  enc2ALastState = enc2AState;
  }

  if (storm.isStormMode() && millis() - lastDistanceCheckMs >= DISTANCE_CHECK_INTERVAL_MS) {
  lastDistanceCheckMs = millis();

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);  // 30ms timeout
  long dist = (duration == 0) ? 999 : duration * 0.034 / 2;
  Serial.println(dist);

  bool nowAlert = dist <= DISTANCE_TRIGGER_CM;

  if (nowAlert != lastProximityAlert) {
    Serial.println(nowAlert ? "close" : "far");
    lastProximityAlert = nowAlert;
  }

  storm.setProximityAlert(nowAlert);
}
}
