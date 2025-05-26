#if defined(OPENFIRE_WIRELESS_ENABLE) && defined(ARDUINO_ARCH_ESP32)

#include "OpenFIRE_Wireless.h"


#if defined(DEVICE_LILYGO_T_DONGLE_S3)
  #include <Adafruit_ST7735.h>
  extern Adafruit_ST7735 tft;
#endif // DEVICE_LILYGO_T_DONGLE_S3



#define ESPNOW_WIFI_CHANNEL_DEFAULT 12

// la potenza di trasmissione può andare da 8 a 84, dove 84 è il valore massimo che corrisponde a 20 db
#define ESPNOW_WIFI_POWER_DEFAULT 84 

uint8_t espnow_wifi_channel = ESPNOW_WIFI_CHANNEL_DEFAULT;  // FATTA VARIABILE PER FUTURA CONFIGURAZIONE TRAMITE APP O OLED
uint8_t espnow_wifi_power = ESPNOW_WIFI_POWER_DEFAULT;      // FATTA VARIABILE PER FUTURA CONFIGURAZIONE TRAMITE APP O OLED


USB_Data_GUN_Wireless usb_data_wireless = {
  "OpenFIRE_DONGLE",  // MANIFACTURES
  "FIRECon",          // NAME
  0xF143,             // VID
  0x1998, // 0x0001   // PID
  1,                  // PLAYER
  espnow_wifi_channel
  //ESPNOW_WIFI_CHANNEL_DEFAULT // CHANNEL
  //,""               // ????
};

SerialWireless_ SerialWireless;
extern Adafruit_USBD_HID usbHid;
SemaphoreHandle_t tx_sem = NULL; // usato per callbalck dio fine trasmissione espnow

#ifdef MUTEX
  SemaphoreHandle_t mutex_tx_serial = NULL; // usato per trasmettere buffer seriale
  SemaphoreHandle_t mutex_writer_bin = NULL; // usato per trasmettere buffer seriale
#else
  SemaphoreHandle_t semaphore_tx_serial = NULL; // usato per trasmettere buffer seriale
  SemaphoreHandle_t semaphore_writer_bin = NULL; // usato per trasmettere buffer seriale
#endif

uint8_t buffer_espnow[ESP_NOW_MAX_DATA_LEN];
esp_now_peer_info_t peerInfo; // deve stare fuori funzioni da funzioni -- globale --variabile di utilità per configurazione

static void _esp_now_rx_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len); // callback esp_now
static void _esp_now_tx_cb(const uint8_t *mac_addr, esp_now_send_status_t status); // callback esp_now

void packet_callback_read_dongle(); // callback packet 
void packet_callback_read_gun(); // callback packet

///////////////////////////////////////////////////////////////////
//#define SIZE_BASE_AUX 13
uint8_t aux_buffer[13 + sizeof(usb_data_wireless)];
// 1 BYTE INDICATA L'AVANZAMENTO DELLO STABILIMENTO DELLA CONNESSIONE,A CHE PUNTO SIAMO
// 6 BYTE IL MAC ADDRESS DEL DISPOSITIVO CHE TRASMETTE
// 6 BYTE IL MAC ADDRESS DEL DISPOSITIVO A CUI E' INDIRIZZATO IL PACCHETTO
// ALL'ULTIMO INVIO LA GUN INVIA ANCHE I DATI DELLA STRUTTURA USB_DATA_WIRELESS CHE CONTIENE VID, PID, ECC. DELLA GUN IN MODO CHE IL DONGLE INZIALIZZI LA CONSSESIONE USB CON QUEI DATI


//uint8_t mac_esp_another_card[6];
//uint8_t stato_conneddione_wireless = 0;
uint8_t tipo_connessione = 0;

// stato della connessione 

// in caso di DONGLE
// 0 = non stabilita
// 1 = inviata richiesta blroad cast
// 2 = qualcuno ha risposto alla connessione ed ha chiesto di essere conesso
// 3 = inviato conferma connessione;

// in caso di GUN
// 0 = non stabilita
// 1 = arrivata richiesta broadcast di connessione
// 2 = accettata ed inviata richiesta di connessione
// 3 = arrivara accettazione di connessione

const uint8_t BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#if defined(DONGLE)
  ///////////////////////uint8_t peerAddress[6] = {0x24, 0x58, 0x7C, 0xDA, 0x38, 0xA0}; // quello montato con piedini su breackboard
  uint8_t peerAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // broadcast

  const functionPtr callbackArr[] = { packet_callback_read_dongle };
  uint8_t lastDongleAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  bool lastDongleSave = false; // se true significa che abbiamo un indirizzo dell'ultimo dongle altrimenti false
#elif defined(GUN)
  //const uint8_t peerAddress[6] = {0xA0, 0x85, 0xE3, 0xE8, 0x0F, 0xB8}; // espe32s3 con piedini (riceve ma non trasmette)
  ///////////////////////////uint8_t peerAddress[6] = {0xA0, 0x85, 0xE3, 0xE7, 0x65, 0xD4}; // espe32s3 senz apiedini
  uint8_t peerAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // broadcast
  //const uint8_t peerAddress[6] = {0xF0, 0x9E, 0x9E, 0x28, 0x9E, 0xB8}; // DONGLE LILYGO

  const functionPtr callbackArr[] = { packet_callback_read_gun };

  // nel caso si spenga la pistola ed abbiamo memorizzato l'ultimo dongle connesso, prova a riconnettersi subito
  uint8_t lastDongleAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  bool lastDongleSave = false; // se true significa che abbiamo un indirizzo dell'ultimo dongle altrimenti false
  // ==========================================================================================================

#endif
///////////////////////////////////////////////////////////////////

/*****************************
 *   SERIAL WIRELESS SECTION
 *****************************/

SerialWireless_::operator bool() {
  return true;
}

int SerialWireless_::peek() {
  if (lenBufferSerialRead) {
    return bufferSerialRead[_readerSerialRead];
  }
  return -1;
}

int SerialWireless_::peekBin() {
  if (_readLen) {
    return _queue[_reader];
  }
  return -1;
}

int SerialWireless_::read() {
  if (lenBufferSerialRead) {
    uint8_t ret = bufferSerialRead[_readerSerialRead];
    lenBufferSerialRead --;
    _readerSerialRead ++;
    if (_readerSerialRead >= FIFO_SIZE_READ_SERIAL) {
      _readerSerialRead -= FIFO_SIZE_READ_SERIAL;
    }
    return ret;
  }
  return -1;  
}

bool SerialWireless_::checkForRxPacket() {
  uint16_t numAvailableBin = availableBin();
  uint8_t dato;
  for (uint16_t i = 0; i<numAvailableBin; i++) {
    dato = (uint8_t) readBin();
    if (dato == START_BYTE) packet.reset(); //resetta inizio pacchetto - // controllo dato .. se è uguale a packet::start_byte .. azzera tutto e fai partire da capo altrimenti
    // controllo dato .. se è uguale a packet::start_byte .. azzera tutto e fai partire da capo altrimenti (usare secondo parametro parse ?)
    packet.parse(dato, true);
  }
  return true;
}

int SerialWireless_::readBin() {
  if (_readLen) {
    uint8_t ret = _queue[_reader];
    _readLen --;
    _reader ++;
    if (_reader >= FIFO_SIZE_READ) {
      _reader -= FIFO_SIZE_READ;
    }
    return ret;
  }
  return -1;  
}

int SerialWireless_::available() {
  return lenBufferSerialRead;
}

int SerialWireless_::availableBin() {
  return _readLen;
}

int SerialWireless_::availableForWrite() {
  return FIFO_SIZE_WRITE_SERIAL - lenBufferSerialWrite;
}

int SerialWireless_::availableForWriteBin() {
  //xSemaphoreTake(semaphore_writer_bin, portMAX_DELAY);
  //int len = BUFFER_SIZE - _writeLen;
  //xSemaphoreGive(semaphore_writer_bin);
  //return len;
  return BUFFER_SIZE - _writeLen;
}

// bloccante fino a quando il buffer seriale non è vuoto (non bloccante fino a quando sono stati inviati effettivamente i dati)
void SerialWireless_::flush() {
  esp_timer_stop(timer_handle_serial); // spegni timer
  
  #ifdef MUTEX
    xSemaphoreTake(mutex_tx_serial, portMAX_DELAY); // prende semaforo / attende finchè è libero
  #else
    xSemaphoreTake(semaphore_tx_serial, portMAX_DELAY); // prende semaforo / attende finchè è libero
  #endif

  //esp_timer_stop(timer_handle_serial); // spegni timer
  while (lenBufferSerialWrite) {
    flush_sem();
    yield();
  }
  #ifdef MUTEX
    xSemaphoreGive(mutex_tx_serial);  // Rilascio il semaforo dopo la callback
  #else
    xSemaphoreGive(semaphore_tx_serial);  // Rilascio il semaforo dopo la callback
  #endif
}


// decidere se controllare se è vuoto anche il buffer_bin
bool SerialWireless_::flush_sem() { // è bloccante e non esce fino a quando il buffer di uscita è completamente vuoto // in virtual com con TinyUISB non è bloccante .. invia il pacchetto più grande che può e ritorna
  //while (lenBufferSerialWrite || _writeLen) { // non è bloccante in USB CDC, forza solo l'inmvio di quello che c'è nel buffer
    if (lenBufferSerialWrite) {
      
      if (availableForWriteBin() > (lenBufferSerialWrite + PREAMBLE_SIZE + POSTAMBLE_SIZE)) {
          //esp_timer_stop(timer_handle_serial); // ferma il timer
          memcpy(&packet.txBuff[PREAMBLE_SIZE], bufferSerialWrite, lenBufferSerialWrite);
          packet.constructPacket(lenBufferSerialWrite, PACKET_TX::SERIAL_TX);
          writeBin(packet.txBuff, lenBufferSerialWrite + PREAMBLE_SIZE+POSTAMBLE_SIZE);
          lenBufferSerialWrite = 0;
          //esp_timer_stop(timer_handle_serial);
          SendData(); // completata la transizione lasciare solo questo
          return true;
      }
      else SendData();
      //// ??? if (lenBufferSerialWrite) esp_timer_start_once(timer_handle_serial, TIMER_HANDLE_SERIAL_DURATION_MICROS);
      
      ///////SendData(); /////////////// provvisorio .. togliere una volta completata la transizione e lasciare solo quello sopra
      //return false;
    }
    return false;
    //SendData(); // try a send
  //}
}

void SerialWireless_::flushBin() { // mai usata
  SendData();
}

void SerialWireless_::SendData() {
  // spedire a gruppi di pacchetti, non mezzo pacchetto // sistemare TODO
  //if (xSemaphoreTake(tx_sem, 0) == pdTRUE) {
  if (xSemaphoreTake(tx_sem, 0) == pdTRUE) {

    #ifdef MUTEX
      xSemaphoreTake(mutex_writer_bin, portMAX_DELAY);
    #else
      xSemaphoreTake(semaphore_writer_bin, portMAX_DELAY);
    #endif

    if (_writeLen > 0) {
    //if (xSemaphoreTake(tx_sem, 0) == pdTRUE) {
       SendData_sem();
    //}
    } else xSemaphoreGive(/*SerialWireless.*/tx_sem);
    
    #ifdef MUTEX
      xSemaphoreGive(mutex_writer_bin);
    #else
      xSemaphoreGive(semaphore_writer_bin);
    #endif
  }
}

void SerialWireless_::SendData_sem() {
  uint16_t len_tx = _writeLen > ESP_NOW_MAX_DATA_LEN ? ESP_NOW_MAX_DATA_LEN : _writeLen;
  uint16_t bytesToSendEnd = len_tx > (BUFFER_SIZE - readIndex) ? BUFFER_SIZE - readIndex : len_tx;
  memcpy(buffer_espnow, &buffer[readIndex], bytesToSendEnd);
  if (len_tx > bytesToSendEnd) {
    memcpy(&buffer_espnow[bytesToSendEnd], &buffer[0], len_tx - bytesToSendEnd);
  }
  //if (uxSemaphoreGetCount(tx_sem)) {/*semaforo disponibile*/}
  //if (xSemaphoreTake(tx_sem, 0) == pdTRUE) {
  esp_err_t result = esp_now_send(peerAddress, buffer_espnow, len_tx);
      
  //readIndex += len_tx; 
  //if (readIndex >= BUFFER_SIZE) { readIndex -= BUFFER_SIZE; }
  //_writeLen -= len_tx;  


  if (result == ESP_OK) { // verificare se esistono casi in cui seppur non risporta ESP_OK vengono trasmessi lo steso
    readIndex += len_tx; 
    if (readIndex >= BUFFER_SIZE) { readIndex -= BUFFER_SIZE; }
    _writeLen -= len_tx;
    //return true;
  } //else return false;
  /*
  else if (result == ESP_ERR_ESPNOW_NOT_INIT) {
    //Serial.println("ESPNOW not init.");
  } else if (result == ESP_ERR_ESPNOW_ARG) {
    //Serial.println("Invalid argument");
  } else if (result == ESP_ERR_ESPNOW_INTERNAL) {
    //Serial.println("Internal Error");
  } else if (result == ESP_ERR_ESPNOW_NO_MEM) {
    //Serial.println("Our of memory");
  } else if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
    //Serial.println("Peer not found.");
  } else if (result == ESP_ERR_ESPNOW_IF) {
    //Serial.println("Interface does not match.");
  } //  else Serial.println("Errore sconosciuto");
  */
}


size_t SerialWireless_::write(uint8_t c) {
  return write(&c, 1);
}

size_t SerialWireless_::writeBin(uint8_t c) {
  return writeBin(&c, 1);
}

size_t SerialWireless_::write(const uint8_t *data, size_t len) { // deve essere bloccante se nel buffer di uscita non c'è abbastanza spazio, finchè non ha inviato tutto
  
  //xSemaphoreTake(semaphore_tx_serial, portMAX_DELAY);

  bool aux_tx = true;
  size_t len_remainer = len;
  size_t pos_remainer = 0;
  
  #ifdef MUTEX
    xSemaphoreTake(mutex_tx_serial, portMAX_DELAY);
  #else
    xSemaphoreTake(semaphore_tx_serial, portMAX_DELAY);
  #endif
  
  do
  {   
    //xSemaphoreTake(semaphore_tx_serial, portMAX_DELAY);
    if (lenBufferSerialWrite + len_remainer <= FIFO_SIZE_WRITE_SERIAL) {
      //xSemaphoreTake(semaphore_tx_serial, portMAX_DELAY);
      memcpy(&bufferSerialWrite[lenBufferSerialWrite], &data[pos_remainer], len_remainer);
      if (lenBufferSerialWrite == 0) esp_timer_start_once(timer_handle_serial, TIMER_HANDLE_SERIAL_DURATION_MICROS);
      lenBufferSerialWrite += len_remainer;
      //xSemaphoreGive(semaphore_tx_serial);
      aux_tx = false;

      ////////////esp_timer_start_once(timer_handle_serial, TIMER_HANDLE_SERIAL_DURATION_MICROS);
    }
    else if (lenBufferSerialWrite < FIFO_SIZE_WRITE_SERIAL) {
      esp_timer_stop(timer_handle_serial);
      len_remainer -= FIFO_SIZE_WRITE_SERIAL - lenBufferSerialWrite;
      memcpy(&bufferSerialWrite[lenBufferSerialWrite], &data[pos_remainer], FIFO_SIZE_WRITE_SERIAL - lenBufferSerialWrite);
      lenBufferSerialWrite = FIFO_SIZE_WRITE_SERIAL;
      pos_remainer += FIFO_SIZE_WRITE_SERIAL - lenBufferSerialWrite; 
      //esp_timer_stop(timer_handle_serial);
      flush_sem();
      //yield();  
    }
    else flush_sem();
    yield();
  } while (aux_tx);
  
  #ifdef MUTEX
    xSemaphoreGive(mutex_tx_serial);
  #else
    xSemaphoreGive(semaphore_tx_serial);
  #endif
  
  return len;
}

size_t SerialWireless_::writeBin(const uint8_t *data, size_t len) {
  
  #ifdef MUTEX
    xSemaphoreTake(mutex_writer_bin, portMAX_DELAY);
  #else
    xSemaphoreTake(semaphore_writer_bin, portMAX_DELAY);
  #endif
  
  if ((BUFFER_SIZE - _writeLen) >= len) {
    size_t firstChunk = BUFFER_SIZE - writeIndex;
    if (firstChunk < len) {
      memcpy(&buffer[writeIndex], data, firstChunk);
      writeIndex = len - firstChunk;  
      memcpy(&buffer[0], data + firstChunk, writeIndex);
    }
    else {
      memcpy(&buffer[writeIndex], data, len);
      writeIndex += len;
      if (writeIndex == BUFFER_SIZE) writeIndex = 0;
    }
    _writeLen += len;
    #ifdef MUTEX
      xSemaphoreGive(mutex_writer_bin);
    #else
      xSemaphoreGive(semaphore_writer_bin);
    #endif
    return len;
  }
  else {
    _overflow_write = true;
    //Serial.println("Overflow nello scrivere nel BUFFER scrittura");
    #ifdef MUTEX
      xSemaphoreGive(mutex_writer_bin);
    #else
      xSemaphoreGive(semaphore_writer_bin);
    #endif
    //xSemaphoreGive(semaphore_writer_bin);  
    return 0;
  }
  //xSemaphoreGive(semaphore_writer_bin);
}

void SerialWireless_::SendPacket(const uint8_t *data, const uint8_t &len,const uint8_t &packetID) { 
  /*
  switch (packetID)
  {
  case PACKET_TX::MOUSE_TX:
    // gestire una fifo separata ?
    break;
  case PACKET_TX::KEYBOARD_TX:
    //
    break;
  case PACKET_TX::GAMEPADE_TX:
    //
    break;
  default:
    break;
  }
  */
  if (availableForWriteBin() > (len + PREAMBLE_SIZE + POSTAMBLE_SIZE)) {
    memcpy(&packet.txBuff[PREAMBLE_SIZE], data, len);
    packet.constructPacket(len, packetID);
    writeBin(packet.txBuff, len + PREAMBLE_SIZE + POSTAMBLE_SIZE);
    SendData(); // try_Send
  }
}

void SerialWireless_::write_on_rx_serialBuffer(const uint8_t *data, int len) {
  if ((FIFO_SIZE_READ_SERIAL - lenBufferSerialRead) >= len) {
    size_t firstChunk = FIFO_SIZE_READ_SERIAL - _writerSerialRead;
    if (firstChunk < len) {
      memcpy(&bufferSerialRead[_writerSerialRead], data, firstChunk);
      _writerSerialRead = len - firstChunk;  
      memcpy(&bufferSerialRead[0], data + firstChunk, _writerSerialRead);
    }
    else {
      memcpy(&bufferSerialRead[_writerSerialRead], data, len);
      _writerSerialRead += len;
      if (_writerSerialRead == FIFO_SIZE_READ_SERIAL) _writerSerialRead = 0;
    }
    lenBufferSerialRead += len;
  }
  else {
    _overflow_bufferSerialRead = true;
    //Serial.println("Overflow nello scrivere nel BUFFER lettura del SERIAL BUFFER");
  }
}

int SerialWireless_::availablePacket() {
  return numAvailablePacket;
}

void SerialWireless_::begin() {
  configST myConfig; // variabile di utilità per configurazione
  // ============ inizializzazione semafori =============
  tx_sem = xSemaphoreCreateBinary();
  
  #ifdef MUTEX
  mutex_tx_serial = xSemaphoreCreateMutex();
  mutex_writer_bin = xSemaphoreCreateMutex();
  #else
  semaphore_tx_serial = xSemaphoreCreateBinary(); // semaforo per tx dati seriali
  semaphore_writer_bin = xSemaphoreCreateBinary();
  #endif


  xSemaphoreGive(tx_sem);

  #ifdef MUTEX
  xSemaphoreGive(mutex_tx_serial);
  xSemaphoreGive(mutex_writer_bin);
  #else
  xSemaphoreGive(semaphore_tx_serial);
  xSemaphoreGive(semaphore_writer_bin);
  #endif  

  // ==== fine inizializzazione semafori


  WiFi.mode(WIFI_STA); 
  
  /*
  esp_err_t err = esp_wifi_start();
  if (err != ESP_OK) {
    log_e("WiFi not started! 0x%x)", err);
    return false;
  }
  */

  //WiFi.macAddress(mac_esp_inteface); // registra il mac addres della scheda

  esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac_esp_inteface);
  if (err != ESP_OK) {
    Serial.println("Failed to read MAC address");
  }
  
  err = esp_wifi_set_channel(espnow_wifi_channel, WIFI_SECOND_CHAN_NONE);
  if (err != ESP_OK) {
    Serial.printf("esp_wifi_set_channel failed! 0x%x", err);
  }
  
  err = esp_wifi_set_max_tx_power(espnow_wifi_power); // tra 8 e 84 corrispondenti a 2dbm a 20 dbm);
  if (err != ESP_OK) {
    Serial.printf("esp_wifi_set_max_tx_power failed! 0x%x", err);
  }

  WiFi.disconnect();   // ???????????????????

  delay (1000); // ??????????????????????????
  
  err = esp_now_init();
  if (err != ESP_OK) {
    Serial.printf("esp_now_init failed! 0x%x", err);
  }


  
    if (lastDongleSave) {
      memcpy(peerAddress, lastDongleAddress,6);
      stato_connessione_wireless = CONNECTION_STATE::NONE_CONNECTION_DONGLE;
    } else {
      memcpy(peerAddress, BROADCAST_ADDR, 6);
      stato_connessione_wireless = CONNECTION_STATE::NONE_CONNECTION;
    }
  

  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = espnow_wifi_channel;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Errore nell'aggiunta del peer");
  }
  
  /*
  //esp_err_t result = esp_now_del_peer(mac_addr);
  //esp_err_t result = esp_now_mod_peer(&peer);
  
  */

  /*
  esp_err_t err = esp_now_deinit();
  if (err != ESP_OK) {
    log_e("esp_now_deinit failed! 0x%x", err);
    return false;
  }
  */


  err = esp_now_register_recv_cb(_esp_now_rx_cb);
  if (err != ESP_OK) {
    Serial.printf("esp_now_register_recv_cb failed! 0x%x", err);
  }

  err = esp_now_register_send_cb(_esp_now_tx_cb);
  if (err != ESP_OK) {
    Serial.printf("esp_now_register_send_cb failed! 0x%x", err);
  }

  myConfig.port         = &Serial; // questo andrà tolta - rimasta solo per contabilità =========================================
  myConfig.debug        = false; //true; //false; //true;
  myConfig.debugPort    = &Serial;
  myConfig.timeout      = DEFAULT_TIMEOUT; // 50ms
  myConfig.callbacks    = callbackArr;
  //myConfig.callbacks    = NULL;
  myConfig.callbacksLen = sizeof(callbackArr) / sizeof(functionPtr);
  //myConfig.callbacksLen = 0;
  
  packet.begin(myConfig);
  //TinyUSBDevices.onBattery = true;
  /*
  stato_connessione_wireless = CONNECTION_STATE::NONE_CONNECTION; //0;
  #ifdef GUN
    if (lastDongleSave) stato_connessione_wireless = CONNECTION_STATE::NONE_CONNECTION_DONGLE;
  #endif // GUN
  */
  TinyUSBDevices.wireless_mode = WIRELESS_MODE::ENABLE_ESP_NOW_TO_DONGLE;
  setupTimer(); // crea i timer .. timer per invio dati seriali
}

bool SerialWireless_::end() {

  esp_now_del_peer(peerAddress);
  esp_err_t err = esp_now_deinit();
  if (err != ESP_OK) {
    Serial.printf("esp_now_deinit failed! 0x%x", err);
    return false;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF); // Disattiva il WiFi
  esp_timer_delete(timer_handle_serial);
  return true;
}

bool SerialWireless_::connection_dongle() {

  uint8_t channel = espnow_wifi_channel;   // tra e 1 e 13 (il 14 dovrebbe essere riservato)
  #define TIMEOUT_TX_PACKET 500 // in millisecondi
  #define TIMEOUT_CHANGE_CHANNEL 2000 // in millisecondi - cambia canale ogni
  #define TIMEOUT_DIALOGUE 6000 // in millisecondi - tempo massimo per completare operazione accoppiamento
  unsigned long lastMillis_tx_packet = millis ();
  unsigned long lastMillis_change_channel = millis ();
  unsigned long lastMillis_start_dialogue = millis ();
  uint8_t aux_buffer_tx[13];

  
  // fare il begin qui o nel main setup ?
  
  
  stato_connessione_wireless = CONNECTION_STATE::NONE_CONNECTION;
  aux_buffer_tx[0] = CONNECTION_STATE::TX_DONGLE_SEARCH_GUN_BROADCAST; //1; // CONNECTION_STATE::TX_DONGLE_SEARCH_GUN_BROADCAST
  memcpy(&aux_buffer_tx[1], SerialWireless.mac_esp_inteface, 6);
  memcpy(&aux_buffer_tx[7], peerAddress, 6);
  //stato_connessione_wireless = 0;

  #if defined(DEVICE_LILYGO_T_DONGLE_S3)
    tft.fillRect(95,60,50,20,0/*BLACK*/);
    tft.setCursor(95, 60);  
    tft.printf("%2d", channel);   
  #endif // DEVICE_LILYGO_T_DONGLE_S3
  
  while (stato_connessione_wireless != CONNECTION_STATE::DEVICES_CONNECTED /*5*/) {
    if (stato_connessione_wireless == CONNECTION_STATE::NONE_CONNECTION /*0*/) {
      if (((millis() - lastMillis_change_channel) > TIMEOUT_CHANGE_CHANNEL) && 
         ((millis() - (lastMillis_tx_packet-50))) > TIMEOUT_TX_PACKET) {  // aggiunta impostato 50 ms come margine, per evitare che quando invia pacchetto cambi subito casnale senza dare possibilità risposta
        channel++;
        if (channel >13) channel = 1;
        
        #if defined(DEVICE_LILYGO_T_DONGLE_S3)
          tft.fillRect(95,60,50,20,0/*BLACK*/);  
          tft.setCursor(95, 60);
          tft.printf("%2d", channel);   
        #endif // DEVICE_LILYGO_T_DONGLE_S3        
        
        if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
          Serial.printf("DONGLE - esp_wifi_set_channel failed!");
        }
        peerInfo.channel = channel;
        if (esp_now_mod_peer(&peerInfo) != ESP_OK) {  // modifica il canale del peer
          Serial.println("DONGLE - Errore nella modifica del canale");
        }             
        //lastMillis_tx_packet = millis (); 
        lastMillis_change_channel = millis ();
        lastMillis_tx_packet = 0; // per fare inviare subito un pacchetto sul nuovo canale
      }
      if ((millis() - lastMillis_tx_packet) > TIMEOUT_TX_PACKET) {
        SerialWireless.SendPacket((const uint8_t *)aux_buffer_tx, 13, PACKET_TX::CONNECTION);
        Serial.print("DONGLE - inviato pacchetto broadcast sul canale: ");
        Serial.println(channel);
        lastMillis_tx_packet = millis (); 
      }
      lastMillis_start_dialogue = millis();
    }
    else {
      if (((millis() - lastMillis_start_dialogue) > TIMEOUT_DIALOGUE) && stato_connessione_wireless != CONNECTION_STATE::DEVICES_CONNECTED /*5*/) {
        stato_connessione_wireless = CONNECTION_STATE::NONE_CONNECTION; //0; // CONNECTION_STATE::NONE;
        //lastMillis_tx_packet = millis ();
        Serial.println("DONGLE - Non si è conclusa la negoziazione tra DONGLE/GUN e si riparte da capo");
        lastMillis_change_channel = millis ();
      }  
    }
    yield(); // in attesa dello stabilimento di una connessione
  }
  
  Serial.println("DONGLE - Negosazione completata - associazione dei dispositivi GUN/DONGLE");
  if (esp_now_del_peer(peerAddress) != ESP_OK) {  // cancella il broadcast dai peer
    Serial.println("DONGLE - Errore nella cancellazione del peer broadcast");
  }
  memcpy(peerAddress, mac_esp_another_card, 6);
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  //peerInfo.channel = ESPNOW_WIFI_CHANNEL;
  //peerInfo.encrypt = false;              
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {  // inserisce il dongle nei peer
    Serial.println("DONGLE - Errore nell'aggiunta del nuovo peer della GUN");
  }
  TinyUSBDevices.onBattery = true;
  return true;
}

bool SerialWireless_::connection_gun_at_last_dongle() {
  #define TIMEOUT_TX_PACKET_LAST_DONGLE 300 // in millisecondi - tempo di invio pacchetti ogni millisecondi quindi 4-5 pacchetti
  #define TIMEOUT_DIALOGUE_LAST_DONGLE 2000 // in millisecondi - tempo massimo per ricerca ultimo dongle
  unsigned long lastMillis_tx_packet_last_dongle = 0;
  unsigned long lastMillis_start_dialogue_last_dongle = millis ();

  uint8_t aux_buffer_tx[13];
  
  stato_connessione_wireless = CONNECTION_STATE::NONE_CONNECTION_DONGLE;
  aux_buffer_tx[0] = CONNECTION_STATE::TX_CHECK_CONNECTION_LAST_DONGLE;
  memcpy(&aux_buffer_tx[1], SerialWireless.mac_esp_inteface, 6);
  memcpy(&aux_buffer_tx[7], peerAddress, 6);
  // INVIA PACCHETTO .. VALUTARE SE INVIARNE PIU' DI UNO  NELL'ARCO DI POCO TEMPO
  //SerialWireless.SendPacket((const uint8_t *)aux_buffer_tx, 13, PACKET_TX::CHECK_CONNECTION_LAST_DONGLE);
  lastMillis_tx_packet_last_dongle = 0;
  lastMillis_start_dialogue_last_dongle = millis();
  while (!TinyUSBDevice.mounted() && 
         (stato_connessione_wireless != CONNECTION_STATE::DEVICES_CONNECTED_WITH_LAST_DONGLE) &&
         ((millis() - lastMillis_start_dialogue_last_dongle) < TIMEOUT_DIALOGUE_LAST_DONGLE)) { 
      if ((millis() - lastMillis_tx_packet_last_dongle) > TIMEOUT_TX_PACKET_LAST_DONGLE)
      {
        SerialWireless.SendPacket((const uint8_t *)aux_buffer_tx, 13, PACKET_TX::CHECK_CONNECTION_LAST_DONGLE);
        lastMillis_tx_packet_last_dongle = millis();
      }
    yield();
  }
  if (stato_connessione_wireless == CONNECTION_STATE::DEVICES_CONNECTED_WITH_LAST_DONGLE) {    
    stato_connessione_wireless = CONNECTION_STATE::DEVICES_CONNECTED;
    
    // ================ aggiunto =============================
    /*
    if (esp_now_del_peer(peerAddress) != ESP_OK) {  // cancella il broadcast dai peer
      Serial.println("Errore nella cancellazione del peer");
    }
    */
    // =============================================

    TinyUSBDevices.onBattery = true;
    return true;
  } else {
    stato_connessione_wireless = CONNECTION_STATE::NONE_CONNECTION; 
    return false;
  }
}


bool SerialWireless_::connection_gun() {

  // fare il begin qui o nel main setup ?

  #define TIMEOUT_GUN_DIALOGUE 1000 // in millisecondi
  unsigned long lastMillis_start_dialogue = millis ();

  // fare il begin qui o nel main setup ?

  //stato_connessione_wireless = 0;

  
  lastMillis_start_dialogue = millis ();
  stato_connessione_wireless = CONNECTION_STATE::NONE_CONNECTION;
  //IMPOSTARE IL PEER BOARCAST QUI ======================
  if (esp_now_del_peer(peerAddress) != ESP_OK) {  // cancella il broadcast dai peer
    Serial.println("Errore nella cancellazione del peer");
  }
  memcpy(peerAddress, BROADCAST_ADDR, 6);
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  //peerInfo.channel = ESPNOW_WIFI_CHANNEL;
  //peerInfo.encrypt = false;              
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {  // inserisce il dongle nei peer
    Serial.println("Errore nell'aggiunta del nuovo peer");
  }                       
  // ====================================================

  while(!TinyUSBDevice.mounted() && stato_connessione_wireless != CONNECTION_STATE::TX_GUN_TO_DONGLE_CONFERM /*4*/) { 
    if (stato_connessione_wireless == CONNECTION_STATE::NONE_CONNECTION /*0*/) {
      lastMillis_start_dialogue = millis ();
    }
    if (((millis() - lastMillis_start_dialogue) > TIMEOUT_GUN_DIALOGUE) && stato_connessione_wireless != CONNECTION_STATE::TX_GUN_TO_DONGLE_CONFERM /*4*/) {
      stato_connessione_wireless = CONNECTION_STATE::NONE_CONNECTION; //0; // CONNECTION_STATE::NONE;
    }
  }

  if (stato_connessione_wireless == CONNECTION_STATE::TX_GUN_TO_DONGLE_CONFERM /*4*/) {
    if (esp_now_del_peer(peerAddress) != ESP_OK) {  // cancella il broadcast dai peer
      Serial.println("Errore nella cancellazione del peer");
    }
    
    // =============== tolto ===========================
    
    memcpy(peerAddress, mac_esp_another_card, 6);
    memcpy(peerInfo.peer_addr, peerAddress, 6);
    //peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    //peerInfo.encrypt = false;              
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {  // inserisce il dongle nei peer
      Serial.println("Errore nell'aggiunta del nuovo peer");
    }                       
    
    // =========================================
    
    TinyUSBDevices.onBattery = true;
    return true;
  }
  return false;
}


void packet_callback_read_dongle() {
  switch (SerialWireless.packet.currentPacketID()) {
    case PACKET_TX::SERIAL_TX:
      Serial.write(&SerialWireless.packet.rxBuff[PREAMBLE_SIZE], SerialWireless.packet.bytesRead);
      break;
    case PACKET_TX::MOUSE_TX :
      usbHid.sendReport(HID_RID_e::HID_RID_MOUSE, &SerialWireless.packet.rxBuff[PREAMBLE_SIZE], SerialWireless.packet.bytesRead);  
      break;
    case PACKET_TX::GAMEPADE_TX:
      usbHid.sendReport(HID_RID_e::HID_RID_GAMEPAD, &SerialWireless.packet.rxBuff[PREAMBLE_SIZE], SerialWireless.packet.bytesRead);
      break;
    case PACKET_TX::KEYBOARD_TX:
      usbHid.sendReport(HID_RID_e::HID_RID_KEYBOARD, &SerialWireless.packet.rxBuff[PREAMBLE_SIZE], SerialWireless.packet.bytesRead);
      break; 
    case PACKET_TX::CHECK_CONNECTION_LAST_DONGLE:
      // CODICE
      memcpy(aux_buffer, &SerialWireless.packet.rxBuff[PREAMBLE_SIZE], SerialWireless.packet.bytesRead);
      switch (aux_buffer[0])
      {
      case CONNECTION_STATE::TX_CHECK_CONNECTION_LAST_DONGLE:
        /* code */
        if ((memcmp(&aux_buffer[1],peerAddress,6) == 0) && 
           (memcmp(&aux_buffer[7],SerialWireless.mac_esp_inteface,6) == 0) &&
           SerialWireless.stato_connessione_wireless == CONNECTION_STATE::DEVICES_CONNECTED) { 
              aux_buffer[0] = CONNECTION_STATE::TX_CONFERM_CONNECTION_LAST_DONGLE; 
              memcpy(&aux_buffer[1], SerialWireless.mac_esp_inteface, 6);
              memcpy(&aux_buffer[7], peerAddress, 6);
              // valutare se inviarlo un paio di volte il pacchetto o solo una volta
              // lo invia 3 volte - una volta ogni 70ms
              for (uint8_t i = 0; i<3; i++) {
                if (i>0) {
                  //vTaskDelay(pdMS_TO_TICKS(1000)); // equivalente a delay(1000) ma non bloccante su esp32
                  unsigned long lastMillis_tx_packet_connection_last_dongle = millis();
                  while ((millis() - lastMillis_tx_packet_connection_last_dongle) < 70) yield(); 
                }
                SerialWireless.SendPacket((const uint8_t *)aux_buffer, 13, PACKET_TX::CHECK_CONNECTION_LAST_DONGLE);
              }
              // pachhetto da inviare
              ///////SerialWireless.SendPacket((const uint8_t *)aux_buffer, 13, PACKET_TX::CHECK_CONNECTION_LAST_DONGLE);
        }
        break;
      default:
        break;
      }     
      break;
    case PACKET_TX::CONNECTION:
      // code
      //tipo_connessione = (uint8_t)SerialWireless.packet.rxBuff[PREAMBLE_SIZE];
      memcpy(aux_buffer, &SerialWireless.packet.rxBuff[PREAMBLE_SIZE], SerialWireless.packet.bytesRead); //sizeof(aux_buffer));
      Serial.println("DONGLE - arrivato richiesta di connessione");
      switch (aux_buffer[0]) {
        case CONNECTION_STATE::TX_GUN_TO_DONGLE_PRESENCE: //2: // CONNECTION_STATE::TX_GUN_TO_DONGLE_PRESENCE
          // code
          if ((memcmp(&aux_buffer[7],SerialWireless.mac_esp_inteface,6) == 0) && SerialWireless.stato_connessione_wireless == CONNECTION_STATE::NONE_CONNECTION /*0*/) {
            memcpy(SerialWireless.mac_esp_another_card, &aux_buffer[1], 6);
            aux_buffer[0] = CONNECTION_STATE::TX_DONGLE_TO_GUN_ACCEPT; //3; // CONNECTION_STATE::TX_DONGLE_TO_GUN_ACCEPT
            memcpy(&aux_buffer[1], SerialWireless.mac_esp_inteface, 6);
            memcpy(&aux_buffer[7], SerialWireless.mac_esp_another_card, 6);
            SerialWireless.SendPacket((const uint8_t *)aux_buffer, 13, PACKET_TX::CONNECTION);
            Serial.println("DONGLE - inviato pacchetto di conferma di connessione");

            // assicurati che i dati siano stati spediti
 
            SerialWireless.stato_connessione_wireless = CONNECTION_STATE::TX_DONGLE_TO_GUN_ACCEPT; //3; // CONNECTION_STATE::TX_DONGLE_TO_GUN_ACCEPT
          }
          break;
        case CONNECTION_STATE::TX_GUN_TO_DONGLE_CONFERM: //4:  // CONNECTION_STATE::TX_GUN_TO_DONGLE_CONFERM
          // code
          if ((memcmp(&aux_buffer[1],SerialWireless.mac_esp_another_card,6) == 0) && 
             (memcmp(&aux_buffer[7],SerialWireless.mac_esp_inteface,6) == 0) &&
             SerialWireless.stato_connessione_wireless == CONNECTION_STATE::TX_DONGLE_TO_GUN_ACCEPT /*3*/) {           
            // SALVA I DATI RELATIVI ALLA GUN VID, PID, PLAYER , ECC.ECC.
            memcpy(&usb_data_wireless, &aux_buffer[13], sizeof(usb_data_wireless));
            // ===================================
            
            SerialWireless.stato_connessione_wireless = CONNECTION_STATE::DEVICES_CONNECTED; //5; //  CONNECTION_STATE::DEVICES_CONNECTED
            Serial.println("DONGLE - ricevuto pacchetto di avvenuta connessione con dati della GUN");
            //TinyUSBDevices.onBattery = true;
        }
          break;
        default:
          break;
      }
      break;  
    default:
      break;
  }
}

void packet_callback_read_gun() {
  switch (SerialWireless.packet.currentPacketID()) {
    case PACKET_TX::SERIAL_TX:
      SerialWireless.write_on_rx_serialBuffer(&SerialWireless.packet.rxBuff[PREAMBLE_SIZE], SerialWireless.packet.bytesRead);
      break;
    case PACKET_TX::MOUSE_TX :
      /* code */
      break;
    case PACKET_TX::GAMEPADE_TX:
      /* code */
      break;
    case PACKET_TX::KEYBOARD_TX:
      /* code */  
      break; 
    case PACKET_TX::CHECK_CONNECTION_LAST_DONGLE:
      // CODICE VALUTARE SE FARE CONTROLLO SE PISTOLA ANCORA CONNESSA .. SE NON CONNESSA RIAVVIA LA SCHEDA ?
      memcpy(aux_buffer, &SerialWireless.packet.rxBuff[PREAMBLE_SIZE], SerialWireless.packet.bytesRead);
      switch (aux_buffer[0])
      {
      case CONNECTION_STATE::TX_CONFERM_CONNECTION_LAST_DONGLE:
        /* code */
        if ((memcmp(&aux_buffer[1],peerAddress,6) == 0) && 
           (memcmp(&aux_buffer[7],SerialWireless.mac_esp_inteface,6) == 0) &&
           SerialWireless.stato_connessione_wireless == CONNECTION_STATE::NONE_CONNECTION_DONGLE) { 
            SerialWireless.stato_connessione_wireless = CONNECTION_STATE::DEVICES_CONNECTED_WITH_LAST_DONGLE;
        }
        break;
      default:
        break;
      }     
      break;
    case PACKET_TX::CONNECTION:
      // code
      //tipo_connessione = (uint8_t)SerialWireless.packet.rxBuff[PREAMBLE_SIZE];
      memcpy(aux_buffer, &SerialWireless.packet.rxBuff[PREAMBLE_SIZE], SerialWireless.packet.bytesRead); //13); //sizeof(aux_buffer)); // qui va bene anche 13 come dati da copiare
      switch (aux_buffer[0]) {
        case CONNECTION_STATE::TX_DONGLE_SEARCH_GUN_BROADCAST: //1: // CONNECTION_STATE::TX_DONGLE_SEARCH_GUN_BROADCAST
          if (SerialWireless.stato_connessione_wireless == CONNECTION_STATE::NONE_CONNECTION /*0*/) { // prende la prima dongle disposnibile
            /* code */
            
            memcpy(SerialWireless.mac_esp_another_card, &aux_buffer[1], 6);
            // memcpy(peerAddress, &aux_buffer[1], 6);
            // invia richiesta connessione
            aux_buffer[0] = CONNECTION_STATE::TX_GUN_TO_DONGLE_PRESENCE; //2; // CONNECTION_STATE::TX_GUN_TO_DONGLE_PRESENCE
            memcpy(&aux_buffer[1], SerialWireless.mac_esp_inteface, 6);
            memcpy(&aux_buffer[7], SerialWireless.mac_esp_another_card, 6);
            SerialWireless.SendPacket((const uint8_t *)aux_buffer, 13, PACKET_TX::CONNECTION);
            SerialWireless.stato_connessione_wireless = CONNECTION_STATE::TX_GUN_TO_DONGLE_PRESENCE; //2; // CONNECTION_STATE::TX_GUN_TO_DONGLE_PRESENCE
            // assicurati che i dati siano stati spediti

          }
          break;
        case 2:
          /* code */
          break;
        case CONNECTION_STATE::TX_DONGLE_TO_GUN_ACCEPT: //3:  // CONNECTION_STATE::TX_DONGLE_TO_GUN_ACCEPT
          /* code */
          if ((memcmp(&aux_buffer[1],SerialWireless.mac_esp_another_card,6) == 0) && 
             (memcmp(&aux_buffer[7],SerialWireless.mac_esp_inteface,6) == 0) &&
              SerialWireless.stato_connessione_wireless == CONNECTION_STATE::TX_GUN_TO_DONGLE_PRESENCE/*2*/) {
              
              aux_buffer[0] = CONNECTION_STATE::TX_GUN_TO_DONGLE_CONFERM; //4;  // CONNECTION_STATE::TX_GUN_TO_DONGLE_CONFERM
              memcpy(&aux_buffer[1], SerialWireless.mac_esp_inteface, 6);
              memcpy(&aux_buffer[7], SerialWireless.mac_esp_another_card, 6);
              
              // INVIA ANCHE DATI RELATIVI A VID, PID, ECC,ECC, DELLA GUN
              memcpy(&aux_buffer[13], &usb_data_wireless, sizeof(usb_data_wireless));

              // =========================================================
              // INVIARE IL PACCHETTO FINALE PIU' VOLTE  
              // =========================================================

              //vTaskDelay(pdMS_TO_TICKS(1000)); // equivalente a delay(1000) ma non bloccante su esp32

              //unsigned long lastMillis_tx_packet_gun_to_dongle_conferm = millis();
              // invia il pacchetto di avvenuta connessione  3 volte - un pacchetto ogni 70ms
              for (uint8_t i = 0; i<3; i++) {
                if (i>0) {
                  //vTaskDelay(pdMS_TO_TICKS(1000)); // equivalente a delay(1000) ma non bloccante su esp32
                  unsigned long lastMillis_tx_packet_gun_to_dongle_conferm = millis();
                  while ((millis() - lastMillis_tx_packet_gun_to_dongle_conferm) < 70) yield(); 
                  //lastMillis_tx_packet_gun_to_dongle_conferm = millis();
                }
                SerialWireless.SendPacket((const uint8_t *)aux_buffer, sizeof(aux_buffer), PACKET_TX::CONNECTION);
              }

              /*
              while (!TinyUSBDevice.mounted() && 
                     (stato_connessione_wireless != CONNECTION_STATE::DEVICES_CONNECTED_WITH_LAST_DONGLE) &&
                     ((millis() - lastMillis_start_dialogue_last_dongle) < TIMEOUT_DIALOGUE_LAST_DONGLE)) { 
                  if ((millis() - lastMillis_tx_packet_last_dongle) > TIMEOUT_TX_PACKET_LAST_DONGLE)
                  {
                    // INSERIRE CHIAMATA INVIO PACCHETTO
                    SerialWireless.SendPacket((const uint8_t *)aux_buffer, sizeof(aux_buffer), PACKET_TX::CONNECTION);
                    lastMillis_tx_packet_last_dongle = millis();
                  }
                yield();
              }
              */




              // PACCHETO DA INVIARE
              ////////////SerialWireless.SendPacket((const uint8_t *)aux_buffer, sizeof(aux_buffer), PACKET_TX::CONNECTION);
              
              // assicurati che i dati siano stati spediti

              // aspetta fino a quando ha spedito il pacchetto
                //TinyUSBDevices.onBattery = true;

                SerialWireless.stato_connessione_wireless = CONNECTION_STATE::TX_GUN_TO_DONGLE_CONFERM; //4; // CONNECTION_STATE::TX_GUN_TO_DONGLE_CONFERM
          }
          break;        
        default:
          break;
      }
      break;
    default:
      break;
  }
}

static void _esp_now_rx_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if ((FIFO_SIZE_READ - SerialWireless._readLen) >= len) {
    size_t firstChunk = FIFO_SIZE_READ - SerialWireless._writer;
    if (firstChunk < len) {
      memcpy(&SerialWireless._queue[SerialWireless._writer], data, firstChunk);
      SerialWireless._writer = len - firstChunk;  
      memcpy(&SerialWireless._queue[0], data + firstChunk, SerialWireless._writer);
    }
    else {
      memcpy(&SerialWireless._queue[SerialWireless._writer], data, len);
      SerialWireless._writer += len;
      if (SerialWireless._writer == FIFO_SIZE_READ) SerialWireless._writer = 0;
    }
    SerialWireless._readLen += len;
  }
  else {
    SerialWireless._overflow_read = true;
    //Serial.println("Overflow nello scrivere nel BUFFER lettura");
  }
  SerialWireless.checkForRxPacket();
}



static void _esp_now_tx_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  
  #ifdef MUTEX
    xSemaphoreTake(mutex_writer_bin, portMAX_DELAY);
  #else
    xSemaphoreTake(semaphore_writer_bin, portMAX_DELAY);
  #endif

  if (SerialWireless._writeLen > 0 ) {
    SerialWireless.SendData_sem();
  }
  else xSemaphoreGive(/*SerialWireless.*/tx_sem);
    
  
  #ifdef MUTEX
    xSemaphoreGive(mutex_writer_bin);
  #else
    xSemaphoreGive(semaphore_writer_bin);
  #endif
  
}

// ================================== TIMER ===================================
// CALLBACK
void timer_callback_serial(void* arg) {
    //Serial.println("Timer scaduto!");
  
  #ifdef MUTEX
    xSemaphoreTake(mutex_tx_serial, portMAX_DELAY);  // fai solo se semaforo libero, altrimenti
  #else
    xSemaphoreTake(semaphore_tx_serial, portMAX_DELAY);  // fai solo se semaforo libero, altrimenti
  #endif
    //Serial.println("Timer scaduto e semaforo rilasciato: eseguo la funzione!");
  //if (SerialWireless.lenBufferSerialWrite) {
  while (SerialWireless.lenBufferSerialWrite) {
    SerialWireless.flush_sem();
    yield();
  }
    //SerialWireless.flush_sem(); // valutare flush normale ... per essere sicuri invii tutti idati
    
    
    //if (SerialWireless.lenBufferSerialWrite) esp_timer_start_once(SerialWireless.timer_handle_serial, TIMER_HANDLE_SERIAL_DURATION_MICROS);
  //}
  
  #ifdef MUTEX
    xSemaphoreGive(mutex_tx_serial);  // Rilascio il semaforo dopo la callback
  #else
    xSemaphoreGive(semaphore_tx_serial);  // Rilascio il semaforo dopo la callback
  #endif

}
// ===============================================================================

void SerialWireless_::setupTimer() {
    esp_timer_create_args_t timer_args = {
        .callback = &timer_callback_serial,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "timer_serial"
    };
    
    esp_timer_create(&timer_args, &timer_handle_serial);
    //esp_timer_delete(timer_handle_serial);
    //esp_timer_start_once(timer_handle_serial, TIMER_HANDLE_SERIAL_DURATION_MICROS);
}

void SerialWireless_::stopTimer_serial() {
    esp_timer_stop(timer_handle_serial);
}

void SerialWireless_::resetTimer_serial(uint64_t duration_us) {
    esp_timer_stop(timer_handle_serial);
    esp_timer_start_once(timer_handle_serial, duration_us);
}




// ==========================   FINE TIMER ====================================

#endif //OPENFIRE_WIRELESS_ENABLE