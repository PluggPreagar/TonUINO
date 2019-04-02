/*/d/temp/jj$   i=0;find * -name "*.mp3" | sort -n | while read f ; do let i=i+1; g="`echo ${i}_${f} | sed -e s:[^/0-9a-zA-Z._]:_:g -e 's/^\([0-9][^0-9]\)/0\1/' -e 's/^\([0-9][0-9][^0-9]\)/0\1/'  `" ;echo "$f    --->    $g"  ; done
*/
#include "Arduino.h"

#include <DFMiniMp3.h>
#include <EEPROM.h>
#include <JC_Button.h>
#include <MFRC522.h>
#include <SPI.h>
#include <SoftwareSerial.h>

/*
 20190105 add Ultralight
 20190329 pwd
*/


/*TODO
- admin menu on Long-press-ALL-Buttons (continue after release all buttons)
    - config settings
    - 
- shuffle partylist
- min/init/max - volumen
- von-bis Option für alle Playmodes
- shortcut - button click to play (if not playing) folder (use settings-mapping)
- 5 Buttons
- invertVolume-Buttons
- code lock
*/

static const char* modeName[]={"UNKNOWN","Hörspielmodus","Albummodus"
          ,"Party Modus","Einzelmodus","Hörbuchmodus","ADMIN"};
#define MODE_UNKNWON    0
#define MODE_HOERSPIEL  1
#define MODE_ALBUM      2
#define MODE_PARTY      3
#define MODE_EINZEL     4
#define MODE_HOERBUCH   5
#define MODE_ADMIN      6
#define TRACK_NONE      9999


#define DATA_TEMPALTE_IDX 0
#define DATA_SHORTCUT_COUNT 4
union Data { // == MIFARE_BUFFER == Shortcut
  byte read_buffer[18];   // inkl CRC
  byte write_buffer[16];
  struct {
    byte cookie[4];
    byte Version;
    byte mode;        // changed order (to Tonuino) !!
    byte folder;
    byte special;
    byte special2;
  } tag;
  byte shortcutIdx;     // only shortcut if between 1 and 4 / will be 0x13 (19) if valid cookie
} data[ DATA_SHORTCUT_COUNT + 2 ] = {
  { 0x13, 0x37, 0xb3, 0x47, 0x01 ,0, 0, 0, 0 } // 0x1337 0xb347 magic cookie to identify our nfc tags + Version 1
  ,{1} // shortcut ID 1..4
  ,{2}
  ,{3}
  ,{4}
};

Data& data_curr = data[ DATA_SHORTCUT_COUNT + 1 ];
static uint8_t& _mode = data_curr.tag.mode;
static uint8_t& _folder = data_curr.tag.folder;
static uint8_t  _track = TRACK_NONE;
static uint8_t& _special = data_curr.tag.special;
static uint8_t& _special2 = data_curr.tag.special2;
static uint8_t  _folderTracks = 0;

struct Settings
{
  uint8_t volume[4];
  uint8_t eq;
  uint8_t standbyTimer;
  uint8_t pwd[4];              // first byte indecates lock-status (KLUDGE convert to union)
  bool invertVolumeButtons;  
} settings, settingsDefault = {
  {0, 10, 30, 10}
  ,0
  ,0
  ,{1}
  ,false
};

static  uint8_t& _volume_min = settings.volume[0];
static  uint8_t& _volume_init = settings.volume[1];
static  uint8_t& _volume_max = settings.volume[2];
static  uint8_t& _volume = settings.volume[3];
static  uint8_t& _eq = settings.eq;
static  uint8_t& _standbyTimer = settings.standbyTimer;
static  uint8_t(&_pwd)[4] = settings.pwd; // first byte indecates lock-status (KLUDGE convert to union)
static  bool& _invertVolumeButtons = settings.invertVolumeButtons;

#define CPU_PWR_PIN  10

#define BTN_UP_PIN      A1
#define BTN_MID_PIN     A2
#define BTN_DOWN_PIN    A3

#define MFRC_RST_PIN       2    // Configurable, see typical pin layout above / other PSI pins are predefined 
#define MFRC_SS_PIN        6    // Configurable, see typical pin layout above

#define MP3_PWR_PIN   4
#define MP3_TX_PIN    3
#define MP3_RX_PIN    A5
#define MP3_BUSY_PIN  A4

#define LED_PWR_PIN       9
#define LED_PIN           8

#define MENU_CHOOSE_NUMBER       -2
#define MENU_CHOOSE_OPTION       -1
#define MENU_CHOOSE_FOLDER        0
#define MENU_MP3_NUMBERS        300
#define MENU_MP3_NUMBERS_COUNT   99
#define MENU_MP3_MODES          310
#define MENU_MP3_MODES_COUNT      6
#define MENU_MP3_SELECT_FILE    320    
#define MENU_MP3_OK             400
#define MENU_MP3_ERROR          401
#define MENU_MP3_RESET_TAG      800


// This bizarre construct isn't Arduino code in the conventional sense.
// It exploits features of GCC's preprocessor to generate a PROGMEM
// table (in flash memory) holding an 8-bit unsigned sine wave (0-255).
const int _SBASE_ = __COUNTER__ + 1; // Index of 1st __COUNTER__ ref below
#define _S1_ (sin((__COUNTER__ - _SBASE_) / 128.0 * M_PI) + 1.0) * 127.5 + 0.5,
#define _S2_ _S1_ _S1_ _S1_ _S1_ _S1_ _S1_ _S1_ _S1_ // Expands to 8 items
#define _S3_ _S2_ _S2_ _S2_ _S2_ _S2_ _S2_ _S2_ _S2_ // Expands to 64 items
// const uint8_t PROGMEM sineTable[] = { _S3_ _S3_ _S3_ _S3_ }; // 256 items
 
// Similar to above, but for an 8-bit gamma-correction table.
#define _GAMMA_ 2.6
const int _GBASE_ = __COUNTER__ + 1; // Index of 1st __COUNTER__ ref below
#define _G1_ pow((__COUNTER__ - _GBASE_) / 255.0, _GAMMA_) * 255.0 + 0.5,
#define _G2_ _G1_ _G1_ _G1_ _G1_ _G1_ _G1_ _G1_ _G1_ // Expands to 8 items
#define _G3_ _G2_ _G2_ _G2_ _G2_ _G2_ _G2_ _G2_ _G2_ // Expands to 64 items
const uint8_t PROGMEM gammaTable[] = { _G3_ _G3_ _G3_ _G3_ }; // 256 items



static void LOG_(const __FlashStringHelper*  msg) {
  Serial.print(msg);
}

static void LOG_(String msg) {
  Serial.print(msg);
}

static void LOG_(int msg) {
  Serial.print(msg);
}

static void LOG_(uint8_t msg) {
  Serial.print(msg);
}

static void LOG(uint8_t msg) {
  Serial.println(msg);
}
 
static void LOG(const __FlashStringHelper* msg) {
  Serial.println(msg);
}

static void LOG(const __FlashStringHelper* msg, int val) {
  Serial.print(msg);
  Serial.println(val);
}

static void LOGu(const __FlashStringHelper* msg, unsigned long val) {
  Serial.print(msg);
  Serial.println(val);
}

static void LOG(const __FlashStringHelper* msg, String val) {
  Serial.print(msg);
  Serial.println(val);
}

static bool LOG_IF(bool value, String name, String msg) {
  if (value) {Serial.print(name);Serial.print(": ");Serial.println(msg);}
  return value;
}

static void LOG_MODE(const __FlashStringHelper*  msg) {
  Serial.print(modeName[_mode]);Serial.print(" -> ");Serial.println(msg);
}

static void LOG_MODE_TRACK(const __FlashStringHelper*  msg) {
  Serial.print(modeName[_mode]);Serial.print(" -> ");Serial.print(msg);
  Serial.print(" (track: ");Serial.print(_track);Serial.println(")");
}

static void LOG_BYTE_ARRAY(const __FlashStringHelper* msg, byte *buffer, byte bufferSize) {
  Serial.print(msg);
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
  Serial.println();
}


static unsigned long _millis = millis();
static unsigned long timer = millis();
static unsigned long timer_mfrc = millis();
static unsigned long timer_all = millis();

bool checkTimer(unsigned long& timer, unsigned long count){
  bool match = _millis > timer && !!timer; 
  if (match || !timer) timer = _millis + count;
  return match;
}


bool idle(unsigned long sleep);
void wait4buttonRelease();

// ------------------
//    B u t t o n
// ------------------
#define BTN_LONG_PRESS  5
#define BTN_DEBOUNCE    -1
class MyButton
{
  //private:
  public:
    int m_pin = -1;
    char* m_name ;
    int m_stable = BTN_DEBOUNCE;
    int m_stable_last = BTN_DEBOUNCE;
    bool m_isPressed = false;
    
  public:
    static int _buttons_idx; // = 0; 
    static MyButton* _buttons[5]; 

    MyButton(int pin, char* name)
      : m_pin(pin), m_name(name){
        // if (_buttons_idx < sizeof(_buttons))
          _buttons[_buttons_idx++] = this;
      };

    void init() {
      pinMode(m_pin, INPUT_PULLUP );
    }
    
    static bool readAll() {
      bool isPressed = false;  
      for (int i = 0 ; i < _buttons_idx ; i++) {
        isPressed =  _buttons[i]->read() || isPressed ; // force read to be executed everytime
        // Serial.print(_buttons[i]->m_isPressed ? "+" : "." );Serial.print(_buttons[i]->m_stable);Serial.print(" ");Serial.print(_buttons[i]->m_stable_last);Serial.print(" / ");
      }
      // Serial.println();
      return isPressed;
    }

    bool read() {
      if (m_stable <0 ) {
        m_stable++ ; // debounce
        m_stable_last = BTN_DEBOUNCE;
      } else {
        bool wasPressed = m_isPressed;
        m_isPressed = LOW == digitalRead(m_pin);
        if (wasPressed != m_isPressed) {
          m_stable_last = m_stable;
          m_stable = BTN_DEBOUNCE ;
        } else if (m_stable < 10 ) {
          m_stable++;
        }
      }
      // Serial.print( _name);Serial.print("  ");Serial.print(_stable_last);Serial.print( _isPressed ? " pressed " : " not pressed ");Serial.println( _stable);
      return m_isPressed;
    }; 
    
    bool isPressed() { 
      return m_isPressed; 
    }
    bool longPress(String msg = "") { // and still pressing
      return LOG_IF(m_isPressed && m_stable >= BTN_LONG_PRESS, m_name, msg);
    }
    bool shortPress(String msg = "") { // isRelease() - after shortPress
      return LOG_IF(!m_isPressed && 0 <= m_stable_last && m_stable_last < BTN_LONG_PRESS, m_name, msg) ; // tricky after init it should be >0
    }
    int deltaByLongPressOrRelease(String msg){
      return longPress(msg) ? 10 : shortPress(msg) ? 1 : 0 ;
    }
};
int MyButton::_buttons_idx = 0;
MyButton* MyButton::_buttons[5] = {0,0,0,0,0};
MyButton buttonDown = MyButton(BTN_DOWN_PIN,"DOWN");
MyButton buttonMid = MyButton(BTN_MID_PIN,"MID");
MyButton buttonUp = MyButton(BTN_UP_PIN,"UP");

// ------------------
//    L E D  RING  
// ------------------
#define LED_NUMPIXELS     24
#define LED_EFFECT_BOOT   FX_MODE_RAINBOW_CYCLE
#define LED_EFFECT_PLAY   FX_MODE_COMET 
#define LED_EFFECT_IDLE   FX_MODE_BREATH
#define LED_COLOR_MAIN    0,0,255
#define LED_COLOR_TRACK   0,255,0
#define LED_COLOR_SETUP   255,0,0
#define LED_BRIGHTNESS    10

#define REDUCED_MODES     1  // reduce 20KB to 18KB
// #define MAX_NUM_SEGMENTS  2  // patch WS2812FX.h - does not realy reduce size 
// PATCH WS2812FX.h and disable not needed effects
#include <WS2812FX.h>
class LED: public WS2812FX
{
  public:
      LED()
        : WS2812FX(LED_NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800) {};

      void init(){
        pinMode(LED_PWR_PIN,OUTPUT);        
        digitalWrite(LED_PWR_PIN,HIGH);       
        WS2812FX::init();
        setColor(LED_COLOR_MAIN);
        setBrightness(LED_BRIGHTNESS);
        setSpeed(125);
        setMode(LED_EFFECT_BOOT);
        start();
        service();
      } 
};
static LED led;


// --------------
//    M P 3
// --------------
#include "SoftwareSerial.h"
#include <DFMiniMp3.h>
void updateColor();
class MP3: public DFMiniMp3< SoftwareSerial, MP3>
{
  public: // TODO make private
    SoftwareSerial mySerial;
    static MP3 *__mp3;

  public:
      MP3() 
          : mySerial(MP3_RX_PIN,MP3_TX_PIN), DFMiniMp3(mySerial)
          {}//{ MP3* __mp3 = this; } 
          
      static void OnError(uint16_t errorCode) {
        LOG(F("MP3 ERROR: Com Error "),errorCode);// see DfMp3_Error for code meaning
      }
      
      static void OnCardOnline(uint16_t code) {
        LOG(F("MP3: SD Karte online "));
      }
      static void OnCardInserted(uint16_t code) {
        LOG(F("MP3: SD Karte bereit "));
      }
      
      static void OnCardRemoved(uint16_t code) {
        LOG(F("MP3: SD Karte entfernt "));
      }

      static void OnPlayFinished(uint16_t track) {
        LOG_(F("MP3: Track beendet "));LOG_((int)track);LOG_(F(" / "));LOG_MODE_TRACK(F(""));
        if(_mode != MODE_UNKNWON && _track == track   /* spiel sonst nicht next 153 (globale trackid) != 2 (folder trackid) ... */) { // suppress next-triggern whilst card setup is running ... 
          delay(100);
          __mp3->next(); 
        }
      }

      bool waitAvailable(unsigned long duration, bool wait4serial = false){
        unsigned long timer  = millis() + ( !duration ? 2000 : duration);
        bool serialRead = false; // wait for serial to be avail and than read by loop - falling edge
        bool busy = false;
        while (millis() < timer && ( wait4serial ? !serialRead || mySerial.available() : (busy = (LOW == digitalRead(MP3_BUSY_PIN))) ) ){
          LOG( F(" wait") );
          serialRead = mySerial.available();
          idle(wait4serial ? 100 : 10);
          //LOG_(" ");LOG_( serialRead ? " serialRead " : "-serialRead-" );LOG_( !!mySerial.available() ? " avail " : " not-avail " );
        }
        if (!busy)
            loop();
        LOG_( wait4serial ? mySerial.available() ? F(" avail serial") : F(" miss serial") : F(" ign serial ")); LOG_( busy ? F(" busy") : F(" no busy")); LOG( F(" waited ") , millis() - (timer - ( !duration ? 2000 : duration)) );
        return !busy;
      }
      
      void init(){
        LOG(F("init mp3 ... "));
        pinMode(MP3_PWR_PIN,OUTPUT);        
        digitalWrite(MP3_PWR_PIN,HIGH);        
        begin(); // idle(2000);
        waitAvailable(3000, true); LOG(F("init mp3 begin"));
        setVolume(1);
        waitAvailable(1000); LOG(F("init mp3 volume"));
        start();
        waitAvailable(1000); LOG(F("init mp3 track"));
        folder(1);
        //mp3.sendPacket(0x17, _folder); // loop folder
        waitAvailable(1000); LOG(F("init mp3 DONE"));
      }

      void sleep(){
        
      }

      bool isPlaying() {
        return !digitalRead(MP3_BUSY_PIN);
      }

      void pauseResume(){
        if (isPlaying()) { 
          led.pause();
          pause(); 
        } else {  
          led.resume();           
          start(); 
        }
      }

      void playMp3FolderTrackAndWait(uint16_t track){
        playMp3FolderTrack(track);
        unsigned long timeout = _millis + 1000;
        while(!isPlaying() && _millis <= timeout){
          idle(100);
        };
        do{
          idle(500);
        } while(isPlaying());
        idle(1000);
      }

      void playMp3FolderTrackAndWait(bool val){
        playMp3FolderTrackAndWait( val ? MENU_MP3_OK : MENU_MP3_ERROR )
      }
      
      void folder(uint16_t folder) {
        _folder=folder;
        led.resume();           
        led.setColor(LED_COLOR_MAIN);
        playMp3FolderTrackAndWait( _folder );   //playAdvertisement( _folder ); // not playing always
        _track=1;
        first();
      }

      void previous() {
        if (_mode == MODE_HOERSPIEL || _mode == MODE_EINZEL || _mode == MODE_PARTY) {
          LOG_MODE(F("Track von vorne spielen"));
        } else {
          LOG_MODE(F("vorheriger Track"));
          if (_track != 1) _track = _track - 1;
          if (_mode == MODE_HOERBUCH) EEPROM.write(_folder, _track);
        }
        playFolderTrack(_folder, _track);
      }

      void first() {
        led.resume();           
        _folderTracks = getFolderTrackCount(_folder);
        LOG_( _folderTracks );LOG_( F(" Dateien in Ordner ") );LOG( _folder );
        if (_mode == MODE_ALBUM) {
          _track = 1;
          LOG_MODE_TRACK(F("kompletten Ordner wiedergeben"));
        } else if (_mode == MODE_HOERSPIEL || _mode == MODE_PARTY) {
          _track = random(1, _folderTracks + 1);
          LOG_MODE_TRACK(F("zufälligen Track wiedergeben"));
        } else if (_mode == MODE_EINZEL) {
          _track = _special;
          LOG_MODE_TRACK(F("eine Datei aus dem Odrdner abspielen"));
        } else if (_mode == MODE_HOERBUCH) {
          _track = EEPROM.read(_folder);
          LOG_MODE_TRACK(F("kompletten Ordner spielen")); //   "Fortschritt merken"));
        }
        playFolderTrack(_folder, _track); 
      }

      void next() {
        led.resume();           
        if (_mode == MODE_PARTY) {                                        
          _track = _track + random(1, _folderTracks ) ; // all but not current
          LOG_MODE_TRACK(F("zufälliger Track"));
        } else if (_mode == MODE_HOERSPIEL || _mode == MODE_EINZEL || _track == _folderTracks) {  // random | _spezial    // Hoerbuch = EEPROM.read
          LOG_MODE(_track == _folderTracks ? F("letzter Titel beendet") : F("keinen neuen Track spielen") );
          _track = TRACK_NONE;
          sleep();
        } else {
          _track = _track + 1;
          LOG_MODE_TRACK(F("nächster Track"));
        }
        if (_track != TRACK_NONE) {
          _track = ( _track + _folderTracks ) % _folderTracks; 
          playFolderTrack(_folder, _track);
          if (_mode == MODE_HOERBUCH) EEPROM.write(_folder, _track);
        }
      }

      bool setValueByVoiceMenu(uint8_t& target, uint16_t startMessage, uint16_t previewFromFolder,  uint8_t numberOfOptions = 0, uint8_t numberOptionCancel = 0, uint8_t defaultOption = 0 ) {
        uint16_t value = voiceMenu( startMessage, previewFromFolder, numberOfOptions, numberOptionCancel, defaultOption);
        if (!!value) target = value; // target does not allow implicit conversion as it is hand over as rvalue
        return !!value;
      }

      uint16_t voiceMenu(uint16_t startMessage, uint16_t previewFromFolder,  uint8_t numberOfOptions = 0, uint8_t numberOptionCancel = 0, uint8_t defaultOption = 0 ) {
        uint16_t returnValue = defaultOption;
        uint16_t delta = 0;
        if (0 == numberOfOptions) numberOfOptions = getFolderTrackCount(previewFromFolder);
        if (startMessage != 0) playMp3FolderTrackAndWait(startMessage);
        wait4buttonRelease();
        do {        
          idle(200);
          MyButton::readAll();
          delta = buttonUp.deltaByLongPressOrRelease(F("VoiceMenu")) - buttonDown.deltaByLongPressOrRelease(F("VoiceMenu"));
          if (buttonMid.longPress(F("VoiceMenu"))) {
             returnValue = 0; // cancel by long press mid button
          }  else if (delta != 0 || !returnValue ) {
            returnValue = min(max(returnValue + delta, 1), numberOfOptions);
            if (MENU_CHOOSE_OPTION == previewFromFolder) {
              playMp3FolderTrack( startMessage + returnValue); // say message
            } else {
              playMp3FolderTrackAndWait( returnValue); // say number
              if (MENU_CHOOSE_FOLDER == previewFromFolder)
                playFolderTrack(returnValue, 1);
              else if (MENU_CHOOSE_NUMBER != previewFromFolder)
                playFolderTrack(previewFromFolder, returnValue);
            }
          }
        
        } while (!buttonMid.shortPress(F("VoiceMenu")) && !!returnValue); 
        if (returnValue == numberOptionCancel) returnValue = 0; // cancel by option
        LOG(F("voiceMenu: "), returnValue);
        wait4buttonRelease();
        return returnValue ;
      }


};
static MP3 mp3;
MP3 * MP3::__mp3 = &mp3; // TODO place within class / SINGELTON ... 


// --------------
//    M F R C   
// --------------
#define MFRC_SECTOR        1 
#define MFRC_BLOCKADDR     4
#define MFRC_TRAILERBLOCK  7
static void LOG_MFRC_STATUS(char* msg, MFRC522::StatusCode status);
class MFRC: public MFRC522
{
  public:
    static MIFARE_Key key; // = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 
    static StatusCode status; // = STATUS_OK;
    static Uid uid_last;
    static byte state ;  // 0 antenna off / 1 halt / 2 init ( / 3 card)

  public:
    MFRC()
      : MFRC522(MFRC_SS_PIN, MFRC_RST_PIN)  
      {}

    bool init(){
      SPI.begin(); // Init SPI bus
      PCD_Init();  // Init MFRC522
      PCD_DumpVersionToSerial(); // Show details of PCD - MFRC522 Card Reader
    }

    bool validCookie() {
      return LOG_IF( 0 == memcmp ( &data_template.tag.cookie, &data_curr.tag.cookie, sizeof( data.tag.cookie )) , "mfrc", "valid cookie" );
    }

    bool same() {
      return LOG_IF( 0 == memcmp ( &uid, &uid_last, sizeof(MFRC522::Uid)) , "mfrc", "same = card still present" );
      // return LOG_IF( uid.size == uid_last.size && 0 == memcmp ( uid.uidByte, uid_last.uidByte, sizeof(uid_last.uidByte)) , "mfrc", "same = card still present" );
    }

    bool reInit( int initRetry = 3) {
      bool found = 2 == state; // keep alive when waiting for card
      if (found) {
        unsigned long timeOut = initRetry >= 0 ? 0 : _millis + 1000;
        if (0 != timeOut)
            PCD_Init();
        do {
          idle(200);
        } while(!(found=PICC_IsNewCardPresent()) && _millis <= timeOut);        
        LOG( found ? F("mfrc: reInit - card found") : F("mfrc: reInit - timeOut") );
        found &=  LOG_IF(PICC_ReadCardSerial(), F("mfrc"), F("readSerial"));
        state = found ? 2 : 1 ;        
      }
      return found || (initRetry>0 && reInit( --initRetry ));       // TODO ... there must be a better way to retry 
    }

    bool halt() {
      mfrc.PICC_HaltA();
      mfrc.PCD_StopCrypto1();      
      state = 1 ;
    }

    void clear() { 
      uid_last.size = 0 ; 
    }

    bool read( bool halt = true, int initRetry = 3) {
      status = MFRC522::STATUS_ERROR;
      if (reInit( initRetry ) && !same()) {

        byte size = sizeof(data_curr.read_buffer);
        status = MFRC522::STATUS_OK;
        MFRC522::PICC_Type piccType = PICC_GetType(uid.sak);
        LOG_BYTE_ARRAY(F("Card UID:"),uid.uidByte, uid.size);
        LOG(F("PICC type: "), PICC_GetTypeName(piccType));
        
        if (MFRC522::PICC_TYPE_MIFARE_UL == piccType) {
          LOG(F("Ultralight - skipp Authenticating"));
        } else {
          LOG(F("Authenticating using key A..."));
          status = PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, MFRC_BLOCKADDR, &key, &(uid));
          LOG_MFRC_STATUS("PCD_Authenticate()",status);
        }

        if (MFRC522::STATUS_OK == status) {
          LOG(F("Reading data from block "));
          status = MIFARE_Read(MFRC_BLOCKADDR, data_curr.read_buffer, &size);
          LOG_MFRC_STATUS("MIFARE_Read()",status);
          if (MFRC522::STATUS_OK == status) {
            LOG_BYTE_ARRAY(F("Data in block ") ,data_curr.read_buffer, size);
            uid_last = uid;
            decodeBuffer();
          }  
        }
        if (status == MFRC522::STATUS_OK && halt)
          halt(); 
      }
      return status == MFRC522::STATUS_OK;
    }

    int  write(){
      if (read(false)) {
        if (MFRC522::PICC_TYPE_MIFARE_UL == PICC_GetType(uid.sak)) {
          LOG(F("Ultralight - skipp Authenticating"));
          for (int i=0; i < 4; i++) { //data is writen in blocks of 4 bytes (4 bytes per page)
            status =  MIFARE_Ultralight_Write( MFRC_BLOCKADDR+i, &data_curr.write_buffer[i*4], 4); // ignore last 2Byte  CRC
          }    
        } else {
          LOG(F("Authenticating again using key B..."));
          status = PCD_Authenticate( MFRC522::PICC_CMD_MF_AUTH_KEY_B, MFRC_BLOCKADDR, &key, &(uid));
          LOG_MFRC_STATUS("PCD_Authenticate()",status);
          if (status == MFRC522::STATUS_OK) {
            LOG_BYTE_ARRAY(F("Writing data into block ") ,data_curr.write_buffer, sizeof(data_curr.write_buffer));
            status = MIFARE_Write(MFRC_BLOCKADDR, data_curr.write_buffer, sizeof(data_curr.write_buffer));
          }
        }
      }
      LOG_MFRC_STATUS("MIFARE_Write()",status);
      return status == MFRC522::STATUS_OK  ;
    }

};
static MFRC mfrc; // Create MFRC522
MFRC522::Uid MFRC::uid_last;
MFRC522::MIFARE_Key  MFRC::key = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 
MFRC522::StatusCode MFRC::status = MFRC522::STATUS_OK;


static void LOG_MFRC_STATUS(char* msg,MFRC522::StatusCode status) {
  if (status != MFRC522::STATUS_OK){
    Serial.print(msg);
    Serial.println(MFRC522::GetStatusCodeName(status));
  }
}



void tick() {
  _millis = millis();
  led.service();
  mp3.loop(); // to check for trackFinished
}

bool idle(unsigned long sleep) {
  unsigned long end = _millis + sleep;
  while (end > _millis) {
    tick(); // wait - but tick while waiting
    delay(5);
  }
  return true;
}

void wait4buttonRelease() {
  do {
    tick(); // wait for release all Buttons
  } while (MyButton::readAll());
  idle(500);  
}

void updateColor(){
  led.setColor( mp3.isPlaying() ? LED_COLOR_TRACK : LED_COLOR_MAIN);
}

void fadeOut(){
  LOG(F("mp3 stop playing (fadeout / card is removed)"));
  mfrc.clear();
  unsigned int vol_org = mp3.getVolume();
  unsigned int vol = vol_org;
  while (--vol > 0){
    mp3.setVolume( vol ) ;
    Serial.print(vol_org);Serial.print(" ");Serial.print(vol);Serial.print(" ");Serial.print( vol * pgm_read_byte(&gammaTable[ 255 * vol/vol_org ]) / 255 );Serial.println(" ");
    led.setBrightness( LED_BRIGHTNESS * pgm_read_byte(&gammaTable[ 255 * vol/vol_org ]) / 255 );
    idle(1000);
  }
  mp3.pause();
  mp3.setVolume(vol_org);
  led.clear();
  led.stop();
  led.setBrightness( LED_BRIGHTNESS);
}

void halt(){
  LOG(F("halt - shut down"));
  digitalWrite(MP3_PWR_PIN, LOW);
  digitalWrite(LED_PWR_PIN, LOW);
  digitalWrite(CPU_PWR_PIN, LOW);  
}


void writeSettings(){
  int address = sizeof(uint8_t) * 100;
  EEPROM.get(address, settings);
}

void readSettings(bool reset = false){
  int address = sizeof(uint8_t) * 100;
  EEPROM.get(address, settings);
  if (reset || !settings.volume[1]) { // KLUDGE 
    LOG(F("reset Settings"));
    mmemcpy( settings, settingsDefault, sizeof(settingsDefault) );
    writeSettings();    
  }
}

bool checkPWd() {
  led.setColor(LED_COLOR_SETUP);
  while (!_pwd[0]) {
    _pwd[0] = 1;// TODO password intro mp3
    for(uint8_t i = 1 ; i < sizeof(_pwd) / sizeof(uint8_t); i++) {
      _pwd[0] &= _pwd[i] == mp3.voiceMenu( MENU_MP3_NUMBERS, MENU_CHOOSE_NUMBER, 255);
    }
  }
  led.setColor(LED_COLOR_MAIN);
  mp3.playMp3FolderTrackAndWait(0!=_pwd[0]);
  return _pwd[0];
}

void setup()
{
  pinMode(CPU_PWR_PIN,OUTPUT);
  digitalWrite( CPU_PWR_PIN, HIGH);

  Serial.begin(9600);
  LOG(F("Arduino BluePlayer starting ..."));
  while (!Serial);    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)

  readSettings();

  led.init();
  mfrc.init();
  mp3.init();
  buttonDown.init();
  buttonMid.init();
  buttonUp.init();

  led.clear();
  led.stop();
  led.setBrightness(LED_BRIGHTNESS);
  led.setColor(LED_COLOR_MAIN);
  led.setSpeed(3000);
  led.setMode(FX_MODE_COMET);
  led.start();

  checkPWd();
}

void readData(uint8_t shortcutIdx = data_curr.shortcutIdx) {
  if (!shortcutIdx) {
    memcpy( data[ shortcutIdx], data_curr, sizeof(Data) );
  } else if ( data_template.cookie[0] == shortcutIdx) {
    mp3.playMp3FolderTrackAndWait(mfrc.read());
  } else if { shortcutIdx < 5} {
    memcpy( data[ shortcutIdx], data_curr, sizeof(Data) );
    mp3.playMp3FolderTrackAndWait(MENU_MP3_OK);
  }
}

void writeData(uint8_t shortcutIdx = data_curr.shortcutIdx) {
  if (!shortcutIdx)
  else if ( data_template.cookie[0] == shortcutIdx) {
    mp3.playMp3FolderTrackAndWait(mfrc.write());
  } else if { shortcutIdx < 5} {
    memcpy( data_curr, data[ shortcutIdx], sizeof(Data) );
    mp3.playMp3FolderTrackAndWait(MENU_MP3_OK);
  }
  EEPROM.write(_folder,1); // init track
}


int menu(uint8_t option = 0;) {

  led.setColor(LED_COLOR_SETUP);
  bool state = true;
  do{ // allow to get option from outside
    switch(option) {
      case 1: 
      case 7: LOG_( 1 == option ? F("Karte reseten und konfigurieren") : F("define shortcut") );
              readData(DATA_TEMPALTE_IDX);
              // if ((uid.size>0 || PICC_ReadCardSerial()) && (force || 0 == mp3.voiceMenu( MENU_MP3_RESET_TAG, MENU_CHOOSE_OPTION , 1))) {0
              if (7 == option)
                  state = mp3.setValueByVoiceMenu( data_curr.shortcutIdx, 940, MENU_CHOOSE_NUMBER, 4); // 0940_shortcut_into
              state = state 
                      && (7 == option || mfrc.read())
                      && 0 != data_curr.shortcutIdx
                      && mp3.setValueByVoiceMenu( _folder, MENU_MP3_NUMBERS, MENU_CHOOSE_FOLDER, MENU_MP3_NUMBERS_COUNT)
                      && mp3.setValueByVoiceMenu( _mode, MENU_MP3_MODES, MENU_CHOOSE_OPTION, MENU_MP3_MODES_COUNT);
              if (state && _mode == MODE_EINZEL)
                  state = mp3.setValueByVoiceMenu( _special, MENU_MP3_SELECT_FILE, _folder);
              if (!state) {
                LOG(F("SETUP abgebrochen! "));
                _mode = MODE_UNKNWON;
                _folder = 1;
                _track = 1;
                mp3.playMp3FolderTrackAndWait(MENU_MP3_ERROR);
              } else  
                  writeData();
              break;
      case 2: state &= mp3.setValueByVoiceMenu( _special, MENU_MP3_SELECT_FILE, _folder) ;
              state &= mp3.setValueByVoiceMenu( _special2, MENU_MP3_SELECT_FILE, _folder,0 ,_special) ; // Von-Bis
              if (state) 
                  writeData();  
              break;
      case 3: state &= mp3.setValueByVoiceMenu( _volume_min , 931, MENU_CHOOSE_NUMBER, 30) ; 
              state &= mp3.setValueByVoiceMenu( _volume_init, 932, MENU_CHOOSE_NUMBER, 30) ; 
              state &= mp3.setValueByVoiceMenu( _volume_max , 930, MENU_CHOOSE_NUMBER, 30) ; 
              if (state)
                  writeSettings();
              break;
      case 4: state &= mp3.setValueByVoiceMenu( _eq, 920, MENU_CHOOSE_OPTION, 6) ; 
              if (state)
                  writeSettings();
              break;
      case 8: const uint8_t standby[] = {  5 , 15 , 30 , 60 , 0};
              state &= mp3.setValueByVoiceMenu( _standbyTimer, 960, MENU_CHOOSE_OPTION, sizeof(standby)) ;
              if (state) {
                  _standbyTimer = standby[ _standbyTimer - 1 ] ; 
                  writeSettings();
              }
              break;        
      case 10: // Invert Functions for Up/Down Buttons;
              const bool invert[] = { _invertVolumeButtons , true , false };
              _invertVolumeButtons = invert[ mp3.voiceMenu( 933, MENU_CHOOSE_OPTION, sizeof(invert) - 1 ) ] ;
              if (state)
                  writeSettings();
              break;        
      case 11: LOG(F("Reset -> EEPROM wird gelöscht"));
              for (int i = 0; i < EEPROM.length(); i++) {
                EEPROM.update(i, 0);
              }
              readSettings(true);
              mp3.playMp3FolderTrack(999);
              break;
      case 9: LOG(F("Einzel-Moduse - erstelle Karten im Batch"));
              state &= mp3.setValueByVoiceMenu( _special, MENU_MP3_SELECT_FILE, _folder) ;
              state &= mp3.setValueByVoiceMenu( _special2, MENU_MP3_SELECT_FILE, _folder,0 ,_special) ; // Von-Bis
              mp3.playMp3FolderTrackAndWait(936); // 0936_batch_cards_intro
              for (; _special <= special2 && !buttonMid.longPress() ; _special++) {
                mp3.playMp3FolderTrackAndWait(_special);
                LOG(F(" Karte auflegen für Track "), _special);
                while (!mfrc.read( true ) && !(checkTimer( timer, 200) && MyButton::readAll() && buttonMid.longPress()) ) {
                  idle(200);
                }
                if (buttonMid.longPress()) { //  upButton.wasReleased() || downButton.wasReleased()) {
                  LOG(F("Abgebrochen!"));
                  mp3.playMp3FolderTrackAndWait(802); // 0802_reset_aborted              
                } else {
                  LOG(F("schreibe Karte..."));
                  mp3.playMp3FolderTrackAndWait(mfrc.write());
                }
              }
              break;
    } 
  } while (state && mp3.setValueByVoiceMenu(option , 900, MENU_CHOOSE_NUMBER, 5 ) ); // Options ..  END / Von-Bis / Min-Init-Max Volumne / Batch-Produce Card / EQ / ShortCut(1 bis 4) / StandBy-Timer / 
  led.setColor(LED_COLOR_MAIN);
}


void loop() {
  tick(); 

  if (checkTimer( timer, 200)) {
    MyButton::readAll();
    if (buttonMid.longPress(F("Info/Setup/Halt/Admin"))) {
        if (buttonDown.longPress(F("ADMIN")) && buttonUp.longPress(F("ADMIN"))) { // buttonUp cant be read ... whenn long press Down+Mid
          mp3.pause();
          if (mfrc.read()) {
            menu( 1 ) ;
            mp3.first();
            led.setColor( LED_COLOR_TRACK );
          }
          timer_all = 0;
        } else if (buttonDown.isPressed() || buttonUp.isPressed()) {
        } else if (buttonDown.shortPress(F("Halt"))) {
          halt();
        } else if (mp3.isPlaying()) {  // !!! isPlaying
          mp3.playAdvertisement( _track );
          wait4buttonRelease();
        } 
    } else if (buttonUp.longPress(F("Volume"))) {
        if (MODE_UNKNWON == _mode && !mp3.isPlaying()) {
          mp3.folder(_folder+10);
          wait4buttonRelease();
        } else  
          mp3.increaseVolume(); 
    } else if (buttonDown.longPress(F("Volume"))) {
        if (MODE_UNKNWON == _mode && !mp3.isPlaying()) {
          mp3.folder(_folder-10);
          wait4buttonRelease();
        } else  
          mp3.decreaseVolume();
    } else if (buttonUp.shortPress(F("Next"))) {
        if (MODE_UNKNWON == _mode && !mp3.isPlaying())
          mp3.folder(_folder+1);
        else  
          mp3.next();
    } else if (buttonDown.shortPress(F("Prev"))) {
        if (MODE_UNKNWON == _mode && !mp3.isPlaying())
          mp3.folder(_folder-1);
        else  
          mp3.previous();
    } else if (buttonMid.shortPress(F("Start/Pause"))) {
        mp3.pauseResume();
    } else if (mfrc.read()) { // RFID Karte wurde aufgelegt
        if (!mfrc.validCookie()) 
          menu( 1 );
        if (!mfrc.same())
          mp3.first();
        timer_mfrc = 0;
        timer_all = 0; // setup may take a while
    } else if (MODE_UNKNWON == _mode) {
      //LOG(F("keep playing w/o card"));
    } else if (mp3.isPlaying() && checkTimer(timer_mfrc,10000) ) { // now proper way - DOES not work >> hack according https://github.com/miguelbalboa/rfid/issues/279#
      LOG(F("card is removed"));
      fadeOut();
      led.setColor( LED_COLOR_MAIN );
      timer_all = 0;
    } else if (checkTimer(timer_all,60000UL)) {
      LOGu(F("halt timer reached "),timer_all);
      halt();
    } // timer 

  } 
}
