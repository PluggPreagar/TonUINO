#include <DFMiniMp3.h>
#include <EEPROM.h>
#include <JC_Button.h>
#include <MFRC522.h>
#include <SPI.h>
#include <SoftwareSerial.h>

// 20190105 add Ultralight
#define TRACK_NONE      9999

#define MODE_UNKNWON    0
#define MODE_HOERSPIEL  1
#define MODE_ALBUM      2
#define MODE_PARTY      3
#define MODE_EINZEL     4
#define MODE_HOERBUCH   5
#define MODE_ADMIN      6
static char* modeName[]={"UNKNOWN","Hörspielmodus","Albummodus"
          ,"Party Modus","Einzelmodus","Hörbuchmodus","ADMIN"};

#define MENU_CHOOSE_OPTION    -1
#define MENU_CHOOSE_FOLDER    0

#define MENU_MP3_NUMBERS      300
#define MENU_MP3_MODE         310
#define MENU_MP3_SELECT_FILE  320  
  
#define MENU_MP3_OK           400
#define MENU_MP3_ERROR        401

#define MENU_MP3_RESET_TAG    800

// MFRC522
#define MFRC_RST_PIN 9                 // Configurable, see typical pin layout above
#define MFRC_SS_PIN 10                 // Configurable, see typical pin layout above
#define MFRC_SECTOR        1 
#define MFRC_BLOCKADDR     4
#define MFRC_TRAILERBLOCK  7

// this object stores nfc tag data
struct nfcTagObject {
  uint32_t cookie;
  uint8_t version;
  uint8_t folder;
  uint8_t mode;
  uint8_t special;
};

nfcTagObject myCard;

static uint16_t numTracksInFolder;
static uint16_t currentTrack = TRACK_NONE;
MFRC522::MIFARE_Key key; 

static unsigned long timer = millis();
static unsigned long timer2 = millis();

static void LOG(String msg) {
  Serial.print(msg);
}

static void LOG_MODE(String msg) {
  Serial.println(String(modeName[myCard.mode]) + " -> " + msg);
}

static void LOG_MODE_TRACK(String msg) {
  Serial.print(String(modeName[myCard.mode]) + " -> " + msg + ": ");
  Serial.println(currentTrack);
}

static void LOG_MFRC_STATUS(char* msg,MFRC522::StatusCode status) {
    Serial.print(msg);
    Serial.println(mfrc522.GetStatusCodeName(status));
}

static void LOG_BYTE_ARRAY(String msg, byte *buffer, byte bufferSize) {
  Serial.print(msg);
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
  Serial.println();
}

static void mp3NextTrack(uint16_t track);
static void mp3Sleep(){}   //    mp3.sleep(); // Je nach Modul kommt es nicht mehr zurück aus dem Sleep!
static bool mp3IsPlaying() { return !digitalRead(BUSY_PIN); }

int voiceMenu(int startMessage, int previewFromFolder,  int numberOfOptions = 0) ;

bool checkTimer(unsigned long& timer, unsigned long count){
  unsigned long cur = millis();
  match = cur > timer ; 
  if (match) timer += count;
  return match;
}

MFRC522 mfrc522(MFRC_SS_PIN, MFRC_RST_PIN); // Create MFRC522

int setupCard(bool ask = false) {
  int ret_value = MENU_MP3_ERROR;
  if (mfrc522.PICC_ReadCardSerial() && (ask || 0 == voiceMenu( MENU_MP3_RESET_TAG, MENU_CHOOSE_OPTION , 1))) {
    Serial.print( ask ? F("Karte reseten") : F("Neue Karte konfigurieren") );

    // Ordner abfragen
    myCard.folder = voiceMenu( MENU_MP3_NUMBERS, MENU_CHOOSE_FOLDER , 99);
    // Wiedergabemodus abfragen
    myCard.mode =   voiceMenu( MENU_MP3_MODE, MENU_CHOOSE_OPTION, 6);
    // Einzelmodus -> Datei abfragen
    if (myCard.mode == MODE_EINZEL) myCard.special = voiceMenu( MENU_MP3_SELECT_FILE, myCard.folder);
    // Admin Funktionen   // PP wann aufgerufen ??? , there are no files 320-322 // mean 330..332
    // else if (myCard.mode == MODE_ADMIN) myCard.special = voiceMenu( 320, MENU_CHOOSE_OPTION, 3);
  
    EEPROM.write(myCard.folder,1);
    ret_value = writeCard(myCard);
  }
  return ret_value;
}

bool readCard(nfcTagObject *nfcTag) {
  MFRC522::StatusCode status;
  byte buffer[18];
  byte size = sizeof(buffer);
  nfcTag->cookie = 0;
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  LOG_BYTE_ARRAY(F("Card UID:"),mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.print(String("PICC type: ") + mfrc522.PICC_GetTypeName(piccType));

  if (MFRC522::PICC_TYPE_MIFARE_UL == piccType) {
    Serial.println(F("Ultralight - skipp Authenticating"));
    status = MFRC522::STATUS_OK;
  } else {
    Serial.println(F("Authenticating using key A..."));
    status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, MFRC_BLOCKADDR, &key, &(mfrc522.uid));
    if (status != MFRC522::STATUS_OK) {
      LOG_MFRC_STATUS("PCD_Authenticate() failed: ",status);
      Serial.println(F("Current data in sector:"));
      mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, MFRC_SECTOR);
      Serial.println();
    }
  }

  if (MFRC522::STATUS_OK == status) {
    // Read data from the block
    Serial.println(String("Reading data from block ") + String(MFRC_BLOCKADDR) + " ...");
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(MFRC_BLOCKADDR, buffer, &size);
    if (status != MFRC522::STATUS_OK) {
      LOG_MFRC_STATUS("MIFARE_Read() failed: ",status);
    } else {
      LOG_BYTE_ARRAY(String("Data in block ") + String(MFRC_BLOCKADDR),buffer, 16);
      nfcTag->cookie = (uint32_t)buffer[0] << 24 
                      |(uint32_t)buffer[1] << 16
                      |(uint32_t)buffer[2] << 8
                      |(uint32_t)buffer[3];
      nfcTag->version = buffer[4];
      nfcTag->folder = buffer[5];
      nfcTag->mode = buffer[6];
      nfcTag->special = buffer[7];
    }  
  }
  return status == MFRC522::STATUS_OK;
}

int writeCard(nfcTagObject nfcTag) {
  byte buffer[16] = {0x13, 0x37, 0xb3, 0x47, // 0x1337 0xb347 magic cookie to identify our nfc tags
                     0x01,                   // version 1
                     nfcTag.folder,          // the folder picked by the user
                     nfcTag.mode,            // the playback mode picked by the user
                     nfcTag.special,         // track or function for admin cards
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  byte size = sizeof(buffer);
  MFRC522::StatusCode status;

  MFRC522::PICC_Type piccType =  mfrc522.PICC_GetType(mfrc522.uid.sak);
  if (MFRC522::PICC_TYPE_MIFARE_UL == piccType) {
    Serial.println(F("Ultralight - skipp Authenticating"));
    for (int i=0; i < 4; i++) { //data is writen in blocks of 4 bytes (4 bytes per page)
      status = (MFRC522::StatusCode) mfrc522.MIFARE_Ultralight_Write(MFRC_BLOCKADDR+i, &buffer[i*4], 4);
    }    
  } else {
    Serial.println(F("Authenticating again using key B..."));
    status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_B, MFRC_BLOCKADDR, &key, &(mfrc522.uid));
    if (status != MFRC522::STATUS_OK) {
      LOG_MFRC_STATUS("PCD_Authenticate() failed: ",status);
    } else {
      LOG_BYTE_ARRAY(String("Writing data into block ") + String(MFRC_BLOCKADDR),buffer, 16);
      status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(MFRC_BLOCKADDR, buffer, 16);
    }
  }
  if (status != MFRC522::STATUS_OK) LOG_MFRC_STATUS("MIFARE_Write() failed: ",status);
  return status == MFRC522::STATUS_OK ? MENU_MP3_OK : MENU_MP3_ERROR ;
}

// implement a notification class,
class Mp3Notify {
  public:
    static void OnError(uint16_t errorCode) {
      Serial.println();
      Serial.print("Com Error ");
      Serial.println(errorCode);// see DfMp3_Error for code meaning
    }
    static void OnPlayFinished(uint16_t track) {
      Serial.print("Track beendet ");
      Serial.println(track);
      delay(100);
      mp3NextTrack(track);
    }
    
    static void OnCardOnline(uint16_t code) {
      Serial.println(F("SD Karte online "));
    }
    static void OnCardInserted(uint16_t code) {
      Serial.println(F("SD Karte bereit "));
    }
    
    static void OnCardRemoved(uint16_t code) {
      Serial.println(F("SD Karte entfernt "));
    }
};

// DFPlayer Mini
SoftwareSerial mySoftwareSerial(2, 3); // RX, TX
#define BUSY_PIN 4
static DFMiniMp3<SoftwareSerial, Mp3Notify> mp3(mySoftwareSerial);

#define LONG_PRESS 1000

// adopted from JC_BUTTON
class TonButton : public Button {
  public:

    TonButton(uint8_t pin, String descr) 
            : Button( pin), m_ignoreRelease(false),m_descr(descr + ": ")  {
      pinMode(pin,  INPUT_PULLUP);
    }

    bool read() {
      bool val = Button::read();
      if (wasReleased() & m_ignoreRelease) {
        m_ignoreRelease = false;
        val = Button::read(); // reset / m_changed=>false ...
      }
      return val;
    }

    bool wasReleased(String msg)  {
      LOG(m_descr + msg);
      return wasReleased();
    }

    bool longPress(String msg)  {
      LOG(m_descr + msg);
      m_ignoreRelease = true;
      return pressedFor( LONG_PRESS );
    }
    
    int deltaByLongPressOrRelease(String msg){
      return longPress(msg) ? 10 : wasReleased() ? 1 : 0 ;
    }
  
  private: 
    bool m_ignoreRelease;
    String m_descr;
};

TonButton buttonPause(A0,"Pause");
TonButton buttonUp(A1,"UP");
TonButton buttonDown(A2,"DOWN");

int voiceMenu(int startMessage, int previewFromFolder,  int numberOfOptions ) {
  int returnValue = 0;
  int delta = 0;
  mp3.pause();
  if (0 == numberOfOptions) numberOfOptions = mp3.getFolderTrackCount(previewFromFolder);
  if (startMessage != 0) mp3.playMp3FolderTrack(startMessage);
  do {
  
    delay(1000);
    buttonPause.read();
    buttonUp.read();
    buttonDown.read();
    mp3.loop();

    delta = buttonUp.deltaByLongPressOrRelease() - buttonDown.deltaByLongPressOrRelease();  
    if (delta != 0 ) {
      returnValue = min(max(returnValue + delta, 1), numberOfOptions);
      if (MENU_CHOOSE_OPTION == previewFromFolder) {
        mp3.playMp3FolderTrack( startMessage + returnValue); // say message
      } else {
        mp3.playMp3FolderTrack( returnValue); // say number
        do {
          delay(10);
        } while (mp3IsPlaying());
        if (previewFromFolder == MENU_CHOOSE_FOLDER)
          mp3.playFolderTrack(returnValue, 1);
        else
          mp3.playFolderTrack(previewFromFolder, returnValue);
      }
    }
  
  } while (!buttonPause.wasPressed() || returnValue == 0);
  return returnValue;
}

static void mp3PrevTrack() {
  if (myCard.mode == MODE_HOERSPIEL || myCard.mode == MODE_EINZEL || myCard.mode == MODE_PARTY) {
    LOG_MODE("Track von vorne spielen");
  } else {
    LOG_MODE("vorheriger Track");
    if (currentTrack != 1) currentTrack = currentTrack - 1;
    if (myCard.mode == MODE_HOERBUCH) EEPROM.write(myCard.folder, currentTrack);
  }
  mp3.playFolderTrack(myCard.folder, currentTrack);
}

static void mp3FirstTrack() {
  numTracksInFolder = mp3.getFolderTrackCount(myCard.folder);
  LOG(String(numTracksInFolder) + " Dateien in Ordner " + myCard.folder);

  if (myCard.mode == MODE_ALBUM) {
    currentTrack = 1;
    LOG_MODE_TRACK("kompletten Ordner wiedergeben");
  } else if (myCard.mode == MODE_HOERSPIEL || myCard.mode == MODE_PARTY) {
    currentTrack = random(1, numTracksInFolder + 1);
    LOG_MODE_TRACK("zufälligen Track wiedergeben");
  } else if (myCard.mode == MODE_EINZEL) {
    currentTrack = myCard.special;
    LOG_MODE_TRACK("eine Datei aus dem Odrdner abspielen");
  } else if (myCard.mode == MODE_HOERBUCH) {
    currentTrack = EEPROM.read(myCard.folder);
    LOG_MODE_TRACK("kompletten Ordner spielen"); //   "Fortschritt merken"));
  }
  mp3.playFolderTrack(myCard.folder, currentTrack); 
}

static void mp3NextTrack(uint16_t track) {
	// Wenn eine neue Karte angelernt wird soll das Ende eines Tracks nicht   
	if ( myCard.mode != MODE_UNKNWON && currentTrack == track) {
    if (myCard.mode == MODE_PARTY) {                                        
      currentTrack = currentTrack + random(1, numTracksInFolder ) ; // all but not current
      LOG_MODE_TRACK("zufälliger Track");
  	} else if (myCard.mode == MODE_HOERSPIEL || myCard.mode == MODE_EINZEL || currentTrack == numTracksInFolder) {  // random | myCard.spezial    // Hoerbuch = EEPROM.read
  		LOG_MODE(currentTrack == numTracksInFolder ? "letzter Titel beendet" : "keinen neuen Track spielen" );
  		mp3Sleep();
  	} else {
  		currentTrack = currentTrack + 1;
  		LOG_MODE_TRACK("nächster Track");
  	}
    if (currentTrack != track) {
      currentTrack = ( currentTrack + numTracksInFolder ) % numTracksInFolder; 
      mp3.playFolderTrack(myCard.folder, currentTrack);
      if (myCard.mode == MODE_HOERBUCH) EEPROM.write(myCard.folder, currentTrack);
    }
  }
}

void setup() {
  LOG("TonUINO - Endavour Version 2.0 - PP-Edition");
  Serial.begin(115200); // Debug Ausgaben über die serielle Schnittstelle

  // DFPlayer Mini initialisieren
  pinMode(BUSY_PIN,    INPUT);
  mp3.begin();
  mp3.setVolume(15);

  // NFC Leser initialisieren
  SPI.begin();        // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial(); // Show details of PCD - MFRC522 Card Reader
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  // RESET --- ALLE DREI KNÖPFE BEIM STARTEN GEDRÜCKT HALTEN 
  if (buttonPause.isPressed() && buttonDown.isPressed() && buttonUp.isPressed()) {
    LOG(F("Reset -> EEPROM wird gelöscht"));
    for (int i = 0; i < EEPROM.length(); i++) {
      EEPROM.write(i, 0);
    }
  }

}

void loop() {
  mp3.loop();
  buttonPause.read();
  buttonUp.read();
  buttonDown.read();

  if (buttonPause.longPress()) {
      if (mp3IsPlaying()) mp3.playAdvertisement( currentTrack );
      else                mp3.playMp3FolderTrack( setupCard( true ) ); // rename "abgebrochen" 802 in 801 / pause will setupCard..
  } else if (buttonPause.wasReleased()) {
	    if (mp3IsPlaying())	mp3.pause();
	    else				        mp3.start();
  } else if (buttonUp.longPress("Volume")) {
	    mp3.increaseVolume();
  } else if (buttonUp.wasReleased()) {
	    mp3NextTrack(currentTrack);
  } else if (buttonDown.longPress("Volume")) {
      mp3.decreaseVolume();
  } else if (buttonDown.wasReleased()) {
      mp3PrevTrack();
  }	

  if (checkTimer( timer, 1000)) { 
    // RFID Karte wurde aufgelegt
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
  	  if (readCard(&myCard)) {
  		  if (myCard.cookie != 322417479 || myCard.folder == 0 || MODE_UNKNWON == myCard.mode) 
  		       mp3.playMp3FolderTrack( setupCard() );
  		  else mp3FirstTrack();
  	  }
  	  mfrc522.PICC_HaltA();
  	  mfrc522.PCD_StopCrypto1();
    } else if (mp3IsPlaying() && checkTimer(timer2,20000)) { // now proper way - DOES not work >> hack according https://github.com/miguelbalboa/rfid/issues/279#
      mfrc522.PCD_Init();
      if (!mfrc522.PICC_IsNewCardPresent()) mp3.pause;
    }
  }
 
}

