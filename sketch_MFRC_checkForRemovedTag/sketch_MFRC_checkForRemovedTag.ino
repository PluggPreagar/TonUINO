// RFID Includes
#include <SPI.h>
#include <MFRC522.h>

// RFID Defines
#define RST_PIN    9
#define SS_PIN    10

const int NO_CARD_DETECTIONS_BEFORE_NEW_READ = 3;
MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance
byte buffATQA[2]={0x00, 0x00};
byte buff[18]={0x00, 0x00};
int noCardCount = 0;

MFRC522::Uid uid;

void setup() {
  Serial.begin(9600); // Initialize serial communications with the PC
  while (!Serial); // Do nothing if no serial port is opened (needed for Arduinos based on ATMEGA32U4)
  SPI.begin(); // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522
  Serial.println("Waiting for RFID-chip...");
}

bool check(char* msg, MFRC522::StatusCode stat, MFRC522::StatusCode stat2 = MFRC522::STATUS_OK){
  Serial.println(String(msg)  + ": " + String( MFRC522::GetStatusCodeName(stat)) );
  return stat2 == stat;
}

bool check1(char* msg, MFRC522::StatusCode stat, MFRC522::StatusCode stat2 = MFRC522::STATUS_OK){
  if (stat2 != stat) Serial.println(String(msg)  + ": " + String( MFRC522::GetStatusCodeName(stat)) );
  return stat2 == stat;
}

bool check2(char* msg, MFRC522::StatusCode stat, MFRC522::StatusCode stat2 = MFRC522::STATUS_OK){
  return stat2 == stat;
}

bool w(){
  delay(100);
  return false;
}

void loop() {
   loop_try();
  // loop_keepActive();
  // loop_wakeUpForResponse();
  // loop_ReInit() ;
  // loop_woHalt() ;
}

void loop_try() {
  delay( 0 == uid.size || 0 == uid.sak ? 1000 : 5000); 
  if (    check2( "0 PICC_WakeupA  " , mfrc522.PICC_WakeupA( buffATQA, sizeof(buffATQA)) )
      // is not needed !?!?!?!? || check( "1 PICC_RequestA ", mfrc522.PICC_RequestA( buffATQA, sizeof(buffATQA)) ) 
       )
  {
    if (check2( "PICC_Select", mfrc522.PICC_Select( &uid, uid.size * 8 ))) {
      mfrc522.PICC_HaltA(); // will HALT card (save energy?) - card will not be found with PICC_RequestA
      Serial.print(String("  --- current --- ") + String(uid.size) + "  " );
      for (byte i = 0; i < uid.size; i++) Serial.print( uid.uidByte[i ], HEX);
      Serial.println();
    } else {
      Serial.println(" - new  ");
      uid.size = 0; // scan for new .. / need RequestA before retry - only on hot-swap
    }
  } else {
    Serial.println(" - none - ");
    uid.sak = 0; // just to speed up checking time whilst keeping UID
  }

}


void loop_keepActive() {
  delay(3000);
  if (check2(     "PICC_RequestA   ", mfrc522.PICC_RequestA( buffATQA, sizeof(buffATQA)) ) 
       || check2( "PICC_RequestA 2x", mfrc522.PICC_RequestA( buffATQA, sizeof(buffATQA))) )
  {
    if (check2( "PICC_Select", mfrc522.PICC_Select( &uid, uid.size * 8 ))) {
      Serial.print(String("  --- current --- ") + String(uid.size) + "  " );
      for (byte i = 0; i < uid.size; i++) Serial.print( uid.uidByte[i ], HEX);
      Serial.println();
    } else {
      Serial.println(" - new  ");
      uid.size = 0; // scan for new .. / need RequestA before retry 
    }
  } else {
    Serial.println(" - none - ");
  }
}

void loop_wakeUpForResponse() {
  delay(3000);
  if (mfrc522.PICC_IsNewCardPresent()) {
      Serial.println(String("PICC_ReadCardSerial: ") + String( mfrc522.PICC_ReadCardSerial() ? "OK" :"FAIL") );
      mfrc522.PICC_HaltA(); // need PCD_INIT after HaltA
      mfrc522.PCD_StopCrypto1();
  }else if (MFRC522::STATUS_TIMEOUT == mfrc522.PICC_WakeupA(buff,sizeof(buff))) {
      Serial.println("Card gone!");
  } else { 
      Serial.println("Card still here");
      mfrc522.PICC_HaltA(); 
  }
}

/*
 * FORCE to Read Card everytime by Reset, 
 * Recognize same card via same CARD-Serial-ID
 */
void loop_ReInit() {
  Serial.println(noCardCount);
  delay(1000);
  if (mfrc522.PICC_IsNewCardPresent()) {
    if(noCardCount > NO_CARD_DETECTIONS_BEFORE_NEW_READ){
      Serial.println("Card present!");
      mfrc522.PICC_ReadCardSerial();
      Serial.println(String("PICC_ReadCardSerial: ") + String( mfrc522.PICC_ReadCardSerial() ? "OK" :"FAIL") );
      mfrc522.PICC_HaltA(); // need PCD_INIT after HaltA
      mfrc522.PCD_StopCrypto1();
      Serial.println("Force - forget current Card!");
      mfrc522.PCD_Init(); // force - once readCardSerial it will not be a new card unless re_init
    }
    noCardCount = 0;
  }else{ // not present
    noCardCount++;
  }
}

/*
 * Keep track on card be keeping Kontakt
 * use effect that keeping card in near PICC_IsNewCardPresent will be still true every now and than
 *    (usually every 2nd call)
 * switching card within NO_CARD_DETECTIONS_BEFORE_NEW_READ loop cycles
 *    will not be detected
 */
void loop_woHalt() {
  delay(1000);
  if (mfrc522.PICC_IsNewCardPresent()) {
    if(noCardCount > NO_CARD_DETECTIONS_BEFORE_NEW_READ){
      Serial.println("Card present!");
      mfrc522.PICC_ReadCardSerial();
      Serial.println(String("PICC_ReadCardSerial: ") + String( mfrc522.PICC_ReadCardSerial() ? "OK" :"FAIL") );
      mfrc522.PCD_StopCrypto1();
    } else {
      Serial.println("probably same ...");      
    }
    noCardCount = 0;
  }else{ // not present
    Serial.println(noCardCount);
    noCardCount++;
  }
}
