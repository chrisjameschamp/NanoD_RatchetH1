#include "hmi_thread.h"
#include "com_thread.h"
#include "foc_thread.h"
#include "lcd_thread.h"
#include <Adafruit_TinyUSB.h>
#include "MIDI.h"
#include "audio/audio.h"
#include <SparkFun_STUSB4500.h>

using namespace ace_button;

Adafruit_USBD_MIDI usb_midi(1);

MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, midiu);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, midi2)

enum {
  RID_KEYBOARD = 1,
  RID_MOUSE = 2,
  RID_GAMEPAD = 3,
};


uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(RID_KEYBOARD) ),
  TUD_HID_REPORT_DESC_MOUSE   ( HID_REPORT_ID(RID_MOUSE) ),
  TUD_HID_REPORT_DESC_GAMEPAD( HID_REPORT_ID(RID_GAMEPAD) )
};

// USB HID object
Adafruit_USBD_HID usb_hid;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// Hmi thread controls LED via FastLed and buttons via AceButton

HmiThread::HmiThread(const uint8_t task_core ) : Thread("HMI", 4608, 1, task_core) {
    _q_config_in = xQueueCreate(2, sizeof( ledConfig ));
    _q_hmi_config_in = xQueueCreate(2, sizeof( hmiConfig ));
    _q_settings_in = xQueueCreate(2, sizeof( HmiDeviceSettings ));
    _q_keyevt_out = xQueueCreate(5, sizeof( KeyEvt ));
}

HmiThread::~HmiThread() {}



void midi_sysex_handler(byte* array, unsigned size) {
    hmi_thread.handleSysex(array, size);
};


// init_usb() must be called before the thread is started
void HmiThread::init_usb() {
  usb_midi.setStringDescriptor("Nano_D MIDI");
  midiu.setHandleSystemExclusive(midi_sysex_handler);
  usb_midi.begin();
  midiu.begin();

  usb_hid.setBootProtocol(HID_ITF_PROTOCOL_NONE);
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setStringDescriptor("Nano_D HID");
  usb_hid.begin();
};


// init must be called before the thread is started
void HmiThread::init(ledConfig& initial_led_config, hmiConfig& initial_hmi_config) {
    led_config = initial_led_config;
    hmi_config = initial_hmi_config;
    led_max_brightness =  DeviceSettings::getInstance().ledMaxBrightness;
    uint8_t b = min(led_max_brightness, led_config.led_brightness);
    FastLED.setBrightness(b);
    midi_sysex_id = DeviceSettings::getInstance().midi_sysex_id;
    midiUsbSettings = DeviceSettings::getInstance().midiUsb;
    midi2Settings = DeviceSettings::getInstance().midi2;
    Serial2.begin(31250, SERIAL_8N1, PIN_SERIAL2_RX, PIN_SERIAL2_TX);
    midi2.setHandleSystemExclusive(midi_sysex_handler);  
    midi2.begin();
    audioPlayer.audio_init();
};



void HmiThread::put_led_config(ledConfig& new_config) {
    xQueueSend(_q_config_in, &new_config, (TickType_t)0);
};


void HmiThread::put_hmi_config(hmiConfig& new_config){
    xQueueSend(_q_hmi_config_in, &new_config, (TickType_t)0);
};


void HmiThread::put_settings(HmiDeviceSettings& new_settings){
    xQueueSend(_q_settings_in, &new_settings, (TickType_t)0);
};


void HmiThread::handleConfig() {
    ledConfig newConfig;
    if (xQueueReceive(_q_config_in, &newConfig, (TickType_t)0)) {
        led_config = newConfig;
        uint8_t newBrightness = min(led_max_brightness, led_config.led_brightness);
        if (FastLED.getBrightness() != newBrightness) {
            FastLED.setBrightness(newBrightness);
        }
        updateKeyLeds();
    }
    hmiConfig newHmiConfig;
    if (xQueueReceive(_q_hmi_config_in, &newHmiConfig, (TickType_t)0)) {
        hmi_config = newHmiConfig;
        hmi_config_epoch++; // Signal profile change to reset stateful outputs
    }
};


void HmiThread::handleSettings() {
    HmiDeviceSettings newSettings;
    if (xQueueReceive(_q_settings_in, &newSettings, (TickType_t)0)) {
        midiUsbSettings = newSettings.midiUsb;
        midi2Settings = newSettings.midi2;
        midiu.setThruFilterMode(midiUsbSettings.thru? midi::Thru::Full : midi::Thru::Off);
        midiu.setInputChannel(midiUsbSettings.in? MIDI_CHANNEL_OMNI : MIDI_CHANNEL_OFF);
        midi2.setThruFilterMode(midi2Settings.thru? midi::Thru::Full : midi::Thru::Off);
        midi2.setInputChannel(midi2Settings.in? MIDI_CHANNEL_OMNI : MIDI_CHANNEL_OFF);
        led_max_brightness = newSettings.ledMaxBrightness;
        uint8_t newBrightness = min(newSettings.ledMaxBrightness, led_config.led_brightness);
        if (FastLED.getBrightness() != newBrightness) {
            FastLED.setBrightness(newBrightness);
            updateKeyLeds();
        }
        midi_sysex_id = newSettings.midi_sysex_id;
        Serial.println("Hmi settings updated from global settings");
    }
};


bool HmiThread::get_key_event(KeyEvt* keyEvt){
    return xQueueReceive(_q_keyevt_out, keyEvt, (TickType_t)0);
};




void HmiThread::run() {
    FastLED.addLeds<LED_CHIPSET, PIN_LED_A, RGB>(leds, NANO_LED_A_NUM);
    FastLED.addLeds<LED_CHIPSET, PIN_LED_B, LED_COL_ORDER>(ledsp, NANO_LED_B_NUM);
    FastLED.setBrightness( DEFAULT_LED_MAX_BRIGHTNESS );
    pinMode(PIN_BTN_A, INPUT_PULLUP);
    pinMode(PIN_BTN_B, INPUT_PULLUP);
    pinMode(PIN_BTN_C, INPUT_PULLUP);
    pinMode(PIN_BTN_D, INPUT_PULLUP);
    buttons[0] = new AceButton(new ButtonConfig(), PIN_BTN_A);
    buttons[1] = new AceButton(new ButtonConfig(), PIN_BTN_B);
    buttons[2] = new AceButton(new ButtonConfig(), PIN_BTN_C);
    buttons[3] = new AceButton(new ButtonConfig(), PIN_BTN_D);
    for (int i = 0; i < 4; i++) {
        button_handler[i] = HmiThreadButtonHandler(i);
        buttons[i]->getButtonConfig()->setIEventHandler(&button_handler[i]);
        buttons[i]->getButtonConfig()->setClickDelay(50);
        buttons[i]->getButtonConfig()->clearFeature(ButtonConfig::kFeatureDoubleClick);
    }
    int keys[4] = {0x1, 0x2, 0x4, 0x8};
    int leds[4][2] = {{3, 4}, {2, 5}, {1, 6}, {0, 7}};
    CRGB colors[4][2] = {
        {led_config.button_A_col_press, led_config.button_A_col_idle},
        {led_config.button_B_col_press, led_config.button_B_col_idle},
        {led_config.button_C_col_press, led_config.button_C_col_idle},
        {led_config.button_D_col_press, led_config.button_D_col_idle}
    };

    for (int i = 0; i < 4; i++) {
        CRGB color = (keyState & keys[i]) ? colors[i][0] : colors[i][1];
        ledsp[leds[i][0]] = color;
        ledsp[leds[i][1]] = color;
    }

    unsigned long total = 0;
    unsigned long updates = 0;
    unsigned long ts = micros();

    audioPlayer.play_audio(chime_wav, 80);
    while (1) {
        handleSettings();
        handleConfig();
        handleMidi();
        for (int i = 0; i < 4; i++)
            buttons[i]->check();
        updateValue();
        handleHid();       
        updateLeds();
        unsigned long currentMillis = millis();
        static unsigned long previousMillis = 0;
        if (currentMillis - previousMillis >= 16) {
            // Limit Leds to ~60fps
            FastLED.show();
            previousMillis = currentMillis;
        }
        #ifdef AUDIO_EN
        audioPlayer.audio_loop();
         #endif
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
};




HmiThreadButtonHandler::HmiThreadButtonHandler(uint8_t _index) : index(_index) {};



void HmiThreadButtonHandler::handleEvent(AceButton* button, uint8_t eventType, uint8_t buttonState) {
    uint8_t oldKeyState = hmi_thread.keyState;

    switch (eventType) {
        case AceButton::kEventPressed:
            hmi_thread.keyState |= (1<<index);
            for (int i=0; i<hmi_thread.hmi_config.keys[index].num_pressed_actions; i++) {
                hmi_thread.handleKeyAction(hmi_thread.hmi_config.keys[index].pressed[i], eventType);
            }
            if (audioPlayer.audio_config.key_audio_file!=nullptr)
                audioPlayer.play_audio(audioPlayer.audio_config.key_audio_file, audioPlayer.audio_config.audio_feedback_lvl);
        break;
        case AceButton::kEventReleased:
            hmi_thread.keyState &= ~(1<<index);
            for (int i=0; i<hmi_thread.hmi_config.keys[index].num_pressed_actions; i++) {
                hmi_thread.handleKeyAction(hmi_thread.hmi_config.keys[index].pressed[i], eventType);
            }
            for (int i=0; i<hmi_thread.hmi_config.keys[index].num_released_actions; i++) {
                hmi_thread.handleKeyAction(hmi_thread.hmi_config.keys[index].released[i], eventType);
            }
        break;
    }

    // Handle keyState change for haptic position persistence
    if (oldKeyState != hmi_thread.keyState) {
        hmi_thread.handleKeyStateChange(oldKeyState, hmi_thread.keyState);
    }

    KeyEvt keyEvt = { .type=eventType, .keyNum=(uint8_t)index, .keyState=hmi_thread.keyState };
    xQueueSend(hmi_thread._q_keyevt_out, &keyEvt, (TickType_t)0);
    hmi_thread.lastCheck = millis();
    hmi_thread.isIdle = false;
    hmi_thread.last_pos = -1;
    hmi_thread.updateKeyLeds();
};




void HmiThread::handleKeyAction(keyAction& action, uint8_t eventType) {
    StringMessage msg;
    switch (action.type) {
        case keyActionType::KA_MIDI:
            if (eventType==AceButton::kEventPressed) {
                if (midiUsbSettings.nano)
                    midiu.sendControlChange(action.midi.cc, action.midi.val, action.midi.channel);
                if (midi2Settings.nano)
                    midi2.sendControlChange(action.midi.cc, action.midi.val, action.midi.channel);
            }
        break;
        case keyActionType::KA_KEY:
            if (num_key_codes<6 && eventType==AceButton::kEventPressed)
                current_key_codes[num_key_codes++] = action.hid.key_codes[0];
            else if (num_key_codes>0 && eventType==AceButton::kEventReleased) {
                for (int i=0; i<num_key_codes; i++) {
                    if (current_key_codes[i]==action.hid.key_codes[0]) {
                        for (int j=i; j<num_key_codes-1; j++)
                            current_key_codes[j] = current_key_codes[j+1];
                        num_key_codes--;
                        current_key_codes[num_key_codes] = 0;
                        break;
                    }
                }
            }
        break;
        case keyActionType::KA_MOUSE:
            if (eventType==AceButton::kEventPressed)
                current_mouse_buttons |= action.mouse.buttons;
            else
                current_mouse_buttons &= ~action.mouse.buttons;
        break;
        case keyActionType::KA_GAMEPAD:
            if (eventType==AceButton::kEventPressed)
                current_pad_buttons |= action.pad.buttons;
            else
                current_pad_buttons &= ~action.pad.buttons;
        break;
        case keyActionType::KA_PROFILE_CHANGE:
            if (action.profile!="" && eventType==AceButton::kEventPressed) {
                StringMessage msg(new String(action.profile), STRING_MESSAGE_PROFILE);
                com_thread.put_string_message(msg);
            }
        break;
        case keyActionType::KA_PROFILE_NEXT:
            msg = StringMessage(nullptr, STRING_MESSAGE_NEXT_PROFILE);
            if (eventType==AceButton::kEventPressed)
                com_thread.put_string_message(msg);
        break;
        case keyActionType::KA_PROFILE_PREV:
            msg = StringMessage(nullptr, STRING_MESSAGE_PREV_PROFILE);
            if (eventType==AceButton::kEventPressed)
                com_thread.put_string_message(msg);
        break;
    }
};


// Static strings for LCD description (must persist after function returns)
static String lcdDescTitle = "";
static String lcdDescData1 = "";

void HmiThread::handleKeyStateChange(uint8_t oldKeyState, uint8_t newKeyState) {
    HapticProfile* profile = HapticProfileManager::getInstance().getCurrentProfile();
    if (profile == nullptr) return;

    // Determine position to save
    // If a dispatch happened recently, FOC might not have processed it yet
    // In that case, use the dispatched position instead of reading from FOC
    int16_t save_pos;
    if (millis() - last_dispatch_time < 20) {
        // Recent dispatch - use the dispatched value (FOC might be stale)
        save_pos = last_dispatched_pos;
    } else {
        // No recent dispatch - read from FOC
        save_pos = foc_thread.pass_cur_pos();
    }
    profile->saved_knob_pos[oldKeyState] = save_pos;

    // Find the knob config for the new keyState
    knobValue* knobConfig = nullptr;
    for (int i = 0; i < profile->hmi_config.knob.num; i++) {
        if (profile->hmi_config.knob.values[i].key_state == newKeyState) {
            knobConfig = &profile->hmi_config.knob.values[i];
            break;
        }
    }

    // Dispatch haptic config if we found one for this keyState
    if (knobConfig != nullptr) {
        int16_t restore_pos = profile->saved_knob_pos[newKeyState];
        if (restore_pos == INT16_MIN) {
            restore_pos = knobConfig->haptic.start_pos;
        }
        foc_thread.put_haptic_config(knobConfig->haptic, restore_pos);
        last_dispatched_pos = restore_pos;
        last_dispatch_time = millis();

        // Update LCD with description if available
        if (knobConfig->desc.length() > 0) {
            lcdDescTitle = profile->profile_name;
            lcdDescData1 = knobConfig->desc;
            LcdCommand cmd;
            cmd.type = LCD_LAYOUT_DEFAULT;
            cmd.title = &lcdDescTitle;
            cmd.data1 = &lcdDescData1;
            cmd.data2 = nullptr;
            cmd.data3 = nullptr;
            cmd.data4 = nullptr;
            lcd_thread.put_lcd_command(cmd);
        }
    }
};


void HmiThread::updateValue() {
    if (hmi_config.knob.num>0) {
        float angle = foc_thread.get_motor_angle();
        for (int i=0;i<hmi_config.knob.num;i++) {
            knobValue& v = hmi_config.knob.values[i];
            if (v.key_state==keyState) {
                float value = 0;
                if (v.angle_min<v.angle_max) {
                    _constrain(angle, v.angle_min, v.angle_max);
                }
                else {
                    _constrain(angle, v.angle_max, v.angle_min);
                }
                if (v.angle_max == v.angle_min) {
                    value = v.value_min;
                }
                else {
                    value = (angle - v.angle_min) * (v.value_max - v.value_min) / (v.angle_max - v.angle_min) + v.value_min;
                }
                if (v.step!=0) {
                    value = round(value / v.step) * v.step;
                }
                currentValue = value;
                currentValue = foc_thread.pass_cur_pos(); // TODO fix and remove this in future

                if (v.type==knobValueType::KV_PITCHBEND) {
                    // Use position from haptic system so display matches output
                    static int16_t lastPitchBend = 0;
                    static uint32_t lastPitchBendEpoch = 0; // Track config changes

                    int16_t pos = foc_thread.pass_cur_pos();
                    int16_t start = foc_thread.pass_start_pos();
                    int16_t end = foc_thread.pass_end_pos();
                    int32_t center = ((int32_t)start + (int32_t)end) / 2;
                    int32_t half_range = ((int32_t)end - (int32_t)start) / 2;

                    // Map position to pitch bend range (-8192 to 8191)
                    int16_t pb_value = 0;
                    if (half_range > 0) {
                        int32_t offset = (int32_t)pos - center;
                        pb_value = (int16_t)((offset * 8191L) / half_range);
                    }
                    // Clamp to valid pitch bend range
                    if (pb_value > 8191) pb_value = 8191;
                    if (pb_value < -8192) pb_value = -8192;

                    // Force send on profile switch (epoch change) to sync host state
                    bool configChanged = (hmi_config_epoch != lastPitchBendEpoch);
                    if (configChanged) {
                        lastPitchBendEpoch = hmi_config_epoch;
                        lastPitchBend = pb_value; // Reset state
                        // Always send current value on config change
                        if (midiUsbSettings.nano)
                            midiu.sendPitchBend(pb_value, v.midi.channel);
                        if (midi2Settings.nano)
                            midi2.sendPitchBend(pb_value, v.midi.channel);
                    }
                    else {
                        // Deadband to filter motor noise
                        int16_t diff = pb_value - lastPitchBend;
                        if (diff < 0) diff = -diff;
                        if (diff > 50) {
                            if (midiUsbSettings.nano)
                                midiu.sendPitchBend(pb_value, v.midi.channel);
                            if (midi2Settings.nano)
                                midi2.sendPitchBend(pb_value, v.midi.channel);
                            lastPitchBend = pb_value;
                        }
                    }
                }
                else if (currentValue!=lastValue) {
                    if (v.type==knobValueType::KV_MIDI) {
                        uint8_t midi_value = (uint8_t)(currentValue);
                        midi_value = _constrain(midi_value, 0, 127);
                        if (midiUsbSettings.nano)
                            midiu.sendControlChange(v.midi.cc, midi_value, v.midi.channel);
                        if (midi2Settings.nano)
                            midi2.sendControlChange(v.midi.cc, midi_value, v.midi.channel);
                    }
                    lastValue = currentValue;
                }
            }
        } // for over values
    }
};



void HmiThread::handleHid() {

    bool keys_changed = (num_key_codes!=last_num_key_codes);
    bool mouse_changed = (current_mouse_buttons!=last_mouse_buttons);
    bool pad_changed = (current_pad_buttons!=last_pad_buttons);

    if ( TinyUSBDevice.suspended() && (keys_changed||mouse_changed||pad_changed) ) {
        TinyUSBDevice.remoteWakeup();
    }

    if (usb_hid.ready()) {
        if (keys_changed) {
            if (num_key_codes>0)
                usb_hid.keyboardReport(RID_KEYBOARD, 0, current_key_codes);
            else
                usb_hid.keyboardRelease(RID_KEYBOARD);
            last_num_key_codes = num_key_codes;
        }
        if (mouse_changed) {
            usb_hid.mouseButtonPress(RID_MOUSE, current_mouse_buttons);
            last_mouse_buttons = current_mouse_buttons;
        }
        if (pad_changed) {
            hid_gamepad_report_t report = {
                .x = 0,
                .y = 0,
                .z = 0,
                .rz = 0,
                .rx = 0,
                .ry = 0,
                .hat = 0,
                .buttons = current_pad_buttons
            };
            usb_hid.sendReport(RID_GAMEPAD, &report, sizeof(report));
            last_pad_buttons = current_pad_buttons;
        }
    }
};



void HmiThread::handleMidi() {
    if (midiu.read()) {
        midi::MidiType t = midiu.getType();
        uint8_t d1 = midiu.getData1();
        uint8_t d2 = midiu.getData2();
        uint8_t c = midiu.getChannel();
        if (midiUsbSettings.route && midi2Settings.out) {
            midi2.send(t, d1, d2, c);        
        }
    }
    if (midi2.read()) {
        midi::MidiType t = midi2.getType();
        uint8_t d1 = midi2.getData1();
        uint8_t d2 = midi2.getData2();
        uint8_t c = midi2.getChannel();
        if (midi2Settings.route && midiUsbSettings.out) {
            midiu.send(t, d1, d2, c);        
        }
    }
};



void HmiThread::handleSysex(byte* array, unsigned size){
    if (array[0]==SYSEX_BINARIS_ID && array[1]==SYSEX_NANO_ID && array[2]==hmi_thread.midi_sysex_id) {
        Serial.println("Received a sysex message");
        // TODO handle sysex messages
    }
};




void HmiThread::updateKeyLeds() {
    int keys[4] = {0x1, 0x2, 0x4, 0x8};
    int leds[4][2] = {{3, 4}, {2, 5}, {1, 6}, {0, 7}};
    CRGB colors[4][2] = {
        {led_config.button_A_col_press, led_config.button_A_col_idle},
        {led_config.button_B_col_press, led_config.button_B_col_idle},
        {led_config.button_C_col_press, led_config.button_C_col_idle},
        {led_config.button_D_col_press, led_config.button_D_col_idle}
    };

    for (int i = 0; i < 4; i++) {
        CRGB color = (keyState & keys[i]) ? colors[i][0] : colors[i][1];
        ledsp[leds[i][0]] = color;
        ledsp[leds[i][1]] = color;
    }
    
};


// Define a variable to store the last time cur_pos was updated

void HmiThread::updateLeds() {
    // TODO: optimise this
    int16_t cur_pos = foc_thread.pass_cur_pos();
    int16_t start_pos = foc_thread.pass_start_pos();
    int16_t end_pos = foc_thread.pass_end_pos();
    uint8_t device_orientation = DeviceSettings::getInstance().deviceOrientation;
    uint8_t led_orientation = map(device_orientation, 0, 3, 0, 135);
    uint16_t point = map(cur_pos, end_pos, start_pos, 0, NANO_LED_A_NUM - 1);
    uint16_t start = map(start_pos, end_pos, start_pos, 0, NANO_LED_A_NUM - 1);
    uint16_t end = map(end_pos, end_pos, start_pos, 0, NANO_LED_A_NUM - 1);


     if (com_thread.global_sleep_flag) {
        hmi_thread.IdleLeds(25, CRGB::Red, CRGB::Green, CRGB::Blue);
        FastLED.setBrightness(25);
    } else {
        halvesPointer(point, start, end, led_orientation, (led_config.pointer_col), CRGB(led_config.primary_col), CRGB(led_config.secondary_col));
        updateKeyLeds();
        FastLED.setBrightness(led_config.led_brightness);
    }
};

    



// Standard Pointer with two halves
void HmiThread::halvesPointer(int indicator, int startpos, int endpos, int orientation, const struct CRGB& pointerCol, const struct CRGB& postCol, const struct CRGB& preCol){ 
    
    for (int i = NANO_LED_A_NUM - 1; i >= 0; i--) {
         if(i > indicator) {
            int index = ( i + orientation) % NANO_LED_A_NUM ;
             leds[index] = postCol;
         }
         if(i < indicator) {
            int index = ( i + orientation) % NANO_LED_A_NUM;
             leds[index] = preCol;
         }
    }
    int index = ( indicator + orientation) % NANO_LED_A_NUM;
    leds[index] = pointerCol;
    return;
};

/*
    IdleLed animation
    Animates the LEDs with a color gradient
*/

static uint8_t colorIndex = 0;
void HmiThread::IdleLeds(int fps, const struct CRGB& idleColStart, const struct CRGB& idleColMid, const struct CRGB& idleColEnd){
    
    CRGB colors[] = {idleColStart ,idleColMid, idleColEnd};
    static unsigned long lastUpdateTime = 0;
    static bool increasing = false;
    static uint8_t darkness = 255;
    static uint8_t progress = 0;
    CRGB beginColor = colors[colorIndex];
    CRGB endColor = colors[(colorIndex + 1) % ARRAY_SIZE(colors)];
    CRGB currentColor = blend(beginColor, endColor, progress);

    for (int i = 0; i < NANO_LED_A_NUM; i++) {
        leds[i] = currentColor;
    }

    for (int i = 0; i < NANO_LED_B_NUM; i++) {
        ledsp[i] = currentColor;
    }

    progress++;
    if (progress == 0) {  // Overflow, time to move to the next color
        colorIndex = (colorIndex + 1) % ARRAY_SIZE(colors);
    }

    if(!com_thread.global_sleep_flag)
        return;
}

STUSB4500 usb_pd;

PowerType HmiThread::init_pd() {
  Wire.begin(PIN_NANO_I2C_SDA, PIN_NANO_I2C_SCL);
  if (!usb_pd.begin()) {
    Serial.println("STUSB4500 not found");
  } else {
    Serial.println("STUSB4500 found");
  }
  if (usb_pd.getPdoNumber()!=2) {
    Serial.println("Setting USB profiles to NVM");
    usb_pd.setUsbCommCapable(true);
    usb_pd.setVoltage(1,5.0);
    usb_pd.setCurrent(1,3.0);
    usb_pd.setLowerVoltageLimit(1,20);
    usb_pd.setUpperVoltageLimit(1,20);
    usb_pd.setVoltage(2,9.0);
    usb_pd.setCurrent(2,3.0);
    usb_pd.setLowerVoltageLimit(2,20);
    usb_pd.setUpperVoltageLimit(2,10);
    usb_pd.setVoltage(3,9.0);
    usb_pd.setCurrent(3,3.0);
    usb_pd.setLowerVoltageLimit(3,20);
    usb_pd.setUpperVoltageLimit(3,10);
    usb_pd.setPdoNumber(2);
    usb_pd.write();  
  }

    // TODO: read status register to determine selected PDO

  return POWER_5V_USB;
}