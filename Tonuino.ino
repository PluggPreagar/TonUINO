/*/d/temp/jj$  find 0* -name "*.mp3" | while read f ; do  g="`echo $f | sed -e s:[^/0-9a-zA-Z._]:_:g -e 's:/\([0-9][0-9][^0-9]\):/0\1:' `" ;echo "$f    --->    $g"  ; done
*/
#include "Arduino.h"

#include <DFMiniMp3.h>
#include <EEPROM.h>
#include <JC_Button.h>
#include <MFRC522.h>
#include <SPI.h>
#include <SoftwareSerial.h>

// 20190105 add Ultralight

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


static uint8_t _version;
static uint8_t _mode;
static uint8_t _folder;
static uint8_t _folderTracks;
static uint8_t _track = TRACK_NONE;
static uint8_t _special;

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
const uint8_t PROGMEM sineTable[] = { _S3_ _S3_ _S3_ _S3_ }; // 256 items
 
// Similar to above, but for an 8-bit gamma-correction table.
#define _GAMMA_ 2.6
const int _GBASE_ = __COUNTER__ + 1; // Index of 1st __COUNTER__ ref below
#define _G1_ pow((__COUNTER__ - _GBASE_) / 255.0, _GAMMA_) * 255.0 + 0.5,
#define _G2_ _G1_ _G1_ _G1_ _G1_ _G1_ _G1_ _G1_ _G1_ // Expands to 8 items
#define _G3_ _G2_ _G2_ _G2_ _G2_ _G2_ _G2_ _G2_ _G2_ // Expands to 64 items
const uint8_t PROGMEM gammaTable[] = { _G3_ _G3_ _G3_ _G3_ }; // 256 items


static void LOG_(String msg) {
  Serial.print(msg);
}

static void LOG_(int msg) {
  Serial.print(msg);
}
 
static void LOG(String msg) {
  Serial.println(msg);
}

static void LOG(String msg, int val) {
  Serial.print(msg);
  Serial.println(val);
}

static void LOGu(String msg, unsigned long val) {
  Serial.print(msg);
  Serial.println(val);
}

static void LOG(String msg, String val) {
  Serial.print(msg);
  Serial.println(val);
}

static bool LOG_IF(bool value, String name, String msg) {
  if (value) Serial.println(name + ": " + msg);
  return value;
}

static void LOG_MODE(String msg) {
  Serial.println(String(modeName[_mode]) + " -> " + msg);
}

static void LOG_MODE_TRACK(String msg) {
  Serial.print(String(modeName[_mode]) + " -> " + msg + ": ");
  Serial.println(_track);
}

static void LOG_BYTE_ARRAY(String msg, byte *buffer, byte bufferSize) {
  Serial.print(msg);
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
  Serial.println();
}

bool idle(unsigned long sleep);

// ------------------
//    B u t t o n
// ------------------
#define BTN_LONG_PRESS  5
#define BTN_DEBOUNCE    -1
class MyButton
{
  private:
    //static int _buttons_idx = 0; 
    //static MyButton _buttons[3]; 
    int m_pin = -1;
    char* m_name ;
    int m_stable = BTN_DEBOUNCE;
    int m_stable_last = BTN_DEBOUNCE;
    bool m_isPressed = false;
    
  public:

    MyButton(int pin, char* name)
      : m_pin(pin), m_name(name){};

    void init() {
      pinMode(m_pin, INPUT_PULLUP );
    }

    /*
    MyButton(pin): _pin(pin) {
      if (_buttons_idx < sizeof(_buttons))
          _buttons[_buttons_idx++] = this;
    };
    // could be moved to longPress when longPress is called first 
    // and assume relevant keys are checked for longPress (will skipp if first key is pressed and evaluated) 
    static void readAll() {  
      for (int i = 0 ; i < _buttons_idx ; i++)
          _buttons[i].read();
    } */
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
      return true;
    }; 
    
    bool longPress(String msg = "") { // and still pressing
      read();
      return LOG_IF(m_isPressed && m_stable >= BTN_LONG_PRESS, m_name, msg);
    }
    bool shortPress(String msg = "") { // isRelease() - after shortPress
      return LOG_IF(!m_isPressed && 0 <= m_stable_last && m_stable_last < BTN_LONG_PRESS, m_name, msg) ; // tricky after init it should be >0
    }
    int deltaByLongPressOrRelease(String msg){
      return longPress(msg) ? 10 : shortPress(msg) ? 1 : 0 ;
    }
};
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

/*
// -- SAFE SPACE !!! with WS2812FX about 37300 Bytes - have to save 6500 Bytes
//    WS2812FX is about 20.000 Bytes 
#include <Adafruit_NeoPixel.h>
#define FX_MODE_COMET 1
#define FX_MODE_BREATH 2
#define FX_MODE_RAINBOW_CYCLE 3
class LED: public Adafruit_NeoPixel
{
  public:
      LED()
        : Adafruit_NeoPixel(LED_NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800) {};

      void init(){
        pinMode(LED_PWR_PIN,OUTPUT);        
        digitalWrite(LED_PWR_PIN,HIGH);      
        setPixelColor(11,255,0,255);
        begin();
        show(); 
      } 

      void service(){
        // TBD       
      }

      void setMode(int mode){
        //TBD
      }

      void setBrightness(int brightness){
        //TBD
      }

      void start(){
        //TBD
      }
      
};
static LED led;
*/


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
        LOG(F("MP3: Track beendet "), track);LOG_(_track);LOG_(" ");LOG(_mode);
        if(_mode != MODE_UNKNWON /*&& _track == track   spiel sonst nicht next 153 (globale trackid) != 2 (folder trackid) ... */) { // suppress next-triggern whilst card setup is running ... 
          delay(100);
          __mp3->next(); 
        }
      }

      bool waitAvailable(unsigned long duration, bool wait4serial = false){
        unsigned long timer  = millis() + ( !duration ? 2000 : duration);
        bool serialRead = false; // wait for serial to be avail and than read by loop - falling edge
        bool busy = false;
        while (millis() < timer && ( wait4serial ? !serialRead || mySerial.available() : (busy = (LOW == digitalRead(MP3_BUSY_PIN))) ) ){
          LOG( " wait");
          serialRead = mySerial.available();
          idle(wait4serial ? 100 : 10);
          //LOG_(" ");LOG_( serialRead ? " serialRead " : "-serialRead-" );LOG_( !!mySerial.available() ? " avail " : " not-avail " );
        }
        if (!busy)
            loop();
        LOG_( wait4serial ? mySerial.available() ? " avail serial" : " miss serial" : " ign serial "); LOG_( busy ? " busy" : " no busy"); LOG(" waited " , millis() - (timer - ( !duration ? 2000 : duration)) );
        return !busy;
      }
      
      void init(){
        LOG(F("init mp3 ... "));
        pinMode(MP3_PWR_PIN,OUTPUT);        
        digitalWrite(MP3_PWR_PIN,HIGH);        
        begin(); // idle(2000);
        waitAvailable(3000, true); LOG(F("init mp3 begin"));
        setVolume(10);
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

      void folder(uint8_t folder) {
        _folder=folder;
        led.resume();           
        led.setColor(LED_COLOR_MAIN);
        playMp3FolderTrack(_folder );   //playAdvertisement( _folder ); // not playing always
        idle(500);
        while(isPlaying()){
          idle(100);
        }
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
        LOG(String(_folderTracks) + " Dateien in Ordner " + _folder);
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
          LOG_MODE(_track == _folderTracks ? "letzter Titel beendet" : "keinen neuen Track spielen" );
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


      int voiceMenu(int startMessage, int previewFromFolder,  int numberOfOptions = 0 ) {
        int returnValue = 0;
        int delta = 0;
        if (0 == numberOfOptions) numberOfOptions = getFolderTrackCount(previewFromFolder);
        if (startMessage != 0) playMp3FolderTrack(startMessage);
        do {
        
          idle(200);
          delta = buttonUp.deltaByLongPressOrRelease("VoiceMenu") - buttonDown.deltaByLongPressOrRelease("VoiceMenu");  
          if (delta != 0 ) {
            returnValue = min(max(returnValue + delta, 1), numberOfOptions);
            if (MENU_CHOOSE_OPTION == previewFromFolder) {
              playMp3FolderTrack( startMessage + returnValue); // say message
            } else {
              playMp3FolderTrack( returnValue); // say number
              do {
                idle(200);
              } while (isPlaying());
              if (previewFromFolder == MENU_CHOOSE_FOLDER)
                playFolderTrack(returnValue, 1);
              else
                playFolderTrack(previewFromFolder, returnValue);
            }
          }
        
        } while (!buttonMid.longPress("VoiceMenu") && (!buttonMid.shortPress("VoiceMenu") || returnValue == 0)); // tricky need longPress before shortPress
        return buttonMid.longPress("VoiceMenu") ? 0 : returnValue ;
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
static void LOG_MFRC_STATUS(char* msg,MFRC522::StatusCode status);
class MFRC: public MFRC522
{
  public:
    static MIFARE_Key key; // = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 
    static StatusCode status; // = STATUS_OK;
    static byte buffer[18]; // = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static Uid uid_last;

  public:
    MFRC()
      : MFRC522(MFRC_SS_PIN, MFRC_RST_PIN)  
      {}

    bool init(){
      SPI.begin(); // Init SPI bus
      PCD_Init();  // Init MFRC522
      PCD_DumpVersionToSerial(); // Show details of PCD - MFRC522 Card Reader
    }

    int setup(bool force = false);

    void clear() { 
      uid_last.size = 0 ; 
    }

    bool same() {
      return LOG_IF( 0 == memcmp ( &uid, &uid_last, sizeof(MFRC522::Uid)) , "mfrc", "same = card still present" );
      // return LOG_IF( uid.size == uid_last.size && 0 == memcmp ( uid.uidByte, uid_last.uidByte, sizeof(uid_last.uidByte)) , "mfrc", "same = card still present" );
    }
    
    bool read() {
      byte size = sizeof(buffer);
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
        LOG(F("Reading data from block "), String(MFRC_BLOCKADDR) + " ...");
        status = MIFARE_Read(MFRC_BLOCKADDR, buffer, &size);
        LOG_MFRC_STATUS("MIFARE_Read()",status);
        if (MFRC522::STATUS_OK == status) {
          LOG_BYTE_ARRAY(String("Data in block ") + String(MFRC_BLOCKADDR),buffer, size);
          uint32_t cookie =  (uint32_t)buffer[0] << 24 
                           | (uint32_t)buffer[1] << 16
                           | (uint32_t)buffer[2] << 8
                           | (uint32_t)buffer[3];
          _version = buffer[4];
          _folder = buffer[5];
          _special = buffer[7];
          _mode = cookie != 322417479 || _folder == 0 ? MODE_UNKNWON : buffer[6];
          uid_last = uid;
        }  
      }
            
      return status == MFRC522::STATUS_OK;
    }


    int  write(){
      byte buffer[16] = {0x13, 0x37, 0xb3, 0x47, // 0x1337 0xb347 magic cookie to identify our nfc tags
                         0x01,                   // version 1
                         _folder,          // the folder picked by the user
                         _mode,            // the playback mode picked by the user
                         _special,         // track or function for admin cards
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

      if (MFRC522::PICC_TYPE_MIFARE_UL == PICC_GetType(uid.sak)) {
        LOG(F("Ultralight - skipp Authenticating"));
        for (int i=0; i < 4; i++) { //data is writen in blocks of 4 bytes (4 bytes per page)
          status =  MIFARE_Ultralight_Write( MFRC_BLOCKADDR+i, &buffer[i*4], 4);
        }    
      } else {
        LOG(F("Authenticating again using key B..."));
        status = PCD_Authenticate( MFRC522::PICC_CMD_MF_AUTH_KEY_B, MFRC_BLOCKADDR, &key, &(uid));
        LOG_MFRC_STATUS("PCD_Authenticate()",status);
        if (status == MFRC522::STATUS_OK) {
          LOG_BYTE_ARRAY(String("Writing data into block ") + String(MFRC_BLOCKADDR),buffer, sizeof(buffer));
          status = MIFARE_Write(MFRC_BLOCKADDR, buffer, sizeof(buffer));
        }
      }
      LOG_MFRC_STATUS("MIFARE_Write()",status);
      return status == MFRC522::STATUS_OK ? MENU_MP3_OK : MENU_MP3_ERROR ;
    }


    int setupCard(bool force = false) {
      int ret_value = MENU_MP3_ERROR;
      if ((uid.size>0 || PICC_ReadCardSerial()) && (force || 0 == mp3.voiceMenu( MENU_MP3_RESET_TAG, MENU_CHOOSE_OPTION , 1))) {
        LOG( force ? F("Karte reseten und konfigurieren") : F("Neue Karte konfigurieren") );
        led.setColor(LED_COLOR_SETUP);

        // Ordner abfragen
        _folder = mp3.voiceMenu( MENU_MP3_NUMBERS, MENU_CHOOSE_FOLDER, MENU_MP3_NUMBERS_COUNT);
        // Wiedergabemodus abfragen
        if (!!_folder) {
           _mode = mp3.voiceMenu( MENU_MP3_MODES, MENU_CHOOSE_OPTION, MENU_MP3_MODES_COUNT);
          // Einzelmodus -> Datei abfragen
          if (_mode == MODE_EINZEL && !!_mode) 
            _special = mp3.voiceMenu( MENU_MP3_SELECT_FILE, _folder);
            // Admin Funktionen   // PP wann aufgerufen ??? , there are no files 320-322 // mean 330..332
            // else if (_mode == MODE_ADMIN) _special = mp3VoiceMenu( 320, MENU_CHOOSE_OPTION, 3);
        }
        if (!_folder && !_mode){
          EEPROM.write(_folder,1);
          ret_value = write();
        } else {
          _mode = MODE_UNKNWON;
          _folder = TRACK_NONE;
        }
      }
      led.setColor(LED_COLOR_MAIN);
      return ret_value;
    }


};
static MFRC mfrc; // Create MFRC522
MFRC522::Uid MFRC::uid_last;
MFRC522::MIFARE_Key  MFRC::key = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 
MFRC522::StatusCode MFRC::status = MFRC522::STATUS_OK;
byte MFRC::buffer[18] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };


static void LOG_MFRC_STATUS(char* msg,MFRC522::StatusCode status) {
  if (status != MFRC522::STATUS_OK){
    Serial.print(msg);
    Serial.println(MFRC522::GetStatusCodeName(status));
  }
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
    
    //mp3.setVolume( vol * pgm_read_byte(&gammaTable[ 255 * vol/vol_org ]) / 255 );
    //Serial.print(vol_org);Serial.print(" ");Serial.print(vol);Serial.print(" ");Serial.print( vol * pgm_read_byte(&gammaTable[ 255 * vol/vol_org ]) / 255 );Serial.print(" ");
    //led.setBrightness( LED_BRIGHTNESS * pgm_read_byte(&gammaTable[ 255 * vol/vol_org ]) / 255 );
    //Serial.print(LED_BRIGHTNESS);Serial.print(" ");Serial.print(LED_BRIGHTNESS * (vol / vol_org));Serial.print(" ");Serial.println( LED_BRIGHTNESS * pgm_read_byte(&gammaTable[ 255 * vol/vol_org ]) / 255 );
    
    //mp3.setVolume( pgm_read_byte(&gammaTable[ vol ]));
    //Serial.print(vol_org);Serial.print(" ");Serial.print(vol);Serial.print(" ");Serial.print(pgm_read_byte(&gammaTable[ vol ]));Serial.print(" ");
    //led.setBrightness( pgm_read_byte(&gammaTable[ LED_BRIGHTNESS * (vol / vol_org) ]) );
    //Serial.print(LED_BRIGHTNESS);Serial.print(" ");Serial.print(LED_BRIGHTNESS * (vol / vol_org));Serial.print(" ");Serial.println(pgm_read_byte(&gammaTable[ LED_BRIGHTNESS * (vol / vol_org) ]));
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

void setup()
{
  pinMode(CPU_PWR_PIN,OUTPUT);
  digitalWrite( CPU_PWR_PIN, HIGH);

  Serial.begin(9600);
  LOG(F("Arduino BluePlayer starting ..."));
  while (!Serial);    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
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

}


void loop() {

  tick(); 

  if (checkTimer( timer, 200)) {
    if (buttonUp.longPress("Volume")) {
        if (MODE_UNKNWON == _mode && !mp3.isPlaying())
          mp3.folder(_folder+10);
        else  
          mp3.increaseVolume(); 
    } else if (buttonDown.longPress("Volume")) {
        if (MODE_UNKNWON == _mode && !mp3.isPlaying())
          mp3.folder(_folder+10);
        else  
          mp3.decreaseVolume();
    } else if (buttonMid.longPress("Info/Setup/Halt")) {
        if (buttonDown.shortPress("Halt")) 
          halt();
        else // if (mp3.isPlaying()) 
          mp3.playAdvertisement( _track );
        //else                
        //  mp3.playMp3FolderTrack( mfrc.setupCard( ) ); // rename "abgebrochen" 802 in 801 / pause will mfrcSetupCard..
    } else if (buttonUp.shortPress("Next")) {
        if (MODE_UNKNWON == _mode && !mp3.isPlaying())
          mp3.folder(_folder+1);
        else  
          mp3.next();
    } else if (buttonDown.shortPress("Prev")) {
        if (MODE_UNKNWON == _mode && !mp3.isPlaying())
          mp3.folder(_folder-1);
        else  
          mp3.previous();
    } else if (buttonMid.shortPress("Start/Pause")) {
        mp3.pauseResume();
    } else if (mfrc.PICC_IsNewCardPresent() && mfrc.PICC_ReadCardSerial()) { // RFID Karte wurde aufgelegt
        if (!mfrc.same() && mfrc.read()) {
          LOG( MODE_UNKNWON == _mode ? F("mfrc setup card (unknown)") : F("mfrc play card"));
          led.start();
          timer_mfrc = 0;
          if (MODE_UNKNWON == _mode) 
              mp3.playMp3FolderTrack( mfrc.setupCard( true ) );
          else {
              mp3.first();
              led.setColor( LED_COLOR_TRACK );
          }
        }
        mfrc.PICC_HaltA();
        mfrc.PCD_StopCrypto1();
        idle(1000); // give time to release the button
        timer_all = 0; // setup may take a while
    } else if (MODE_UNKNWON == _mode) {
      //LOG(F("keep playing w/o card"));
    } else if (mp3.isPlaying() && checkTimer(timer_mfrc,10000)) { // now proper way - DOES not work >> hack according https://github.com/miguelbalboa/rfid/issues/279#
      mfrc.PCD_Init();
      idle(200); // leider erkennt er manchmal nicht die karte ...
      if (!mfrc.PICC_IsNewCardPresent() && idle(400) && !mfrc.PICC_IsNewCardPresent()) {
        LOG(F("card is removed - retry "));
        idle(200); // leider erkennt er manchmal nicht die karte ...
        if (!mfrc.PICC_IsNewCardPresent()) {
          LOG(F("card is removed - 1.2nd "));
          mfrc.PCD_Init();
          idle(200); // leider erkennt er manchmal nicht die karte ...
          if (!mfrc.PICC_IsNewCardPresent()) {
            LOG(F("card is removed - 2nd"));
            fadeOut();
            led.setColor( LED_COLOR_MAIN );
          }
        }
      }
      timer_all = 0;
    } else if (checkTimer(timer_all,60000UL)) {
      LOGu(F("halt timer reached (1 min)"),timer_all);
      halt();
    } // timer 

  } 
}
