#define VERSION "\n\n-- ESP_radio_mini V2.1, Webradio for ESP8266 & VS1053 MP3 module --"
// This is a stepdown version of Esp_radio by Edzelf   https://github.com/Edzelf/Esp-radio

// #define STATION        "icecast.omroep.nl:80/radio1-bb-mp3"                   // Radio 1, NL
// #define STATION     "us2.internet-radio.com:8132"                             // Magic 60s Florida 60s Top 40 Classic Rock
// #define STATION     "205.164.62.15:10032"                                     // 1.FM - GAIA, 64k
// #define STATION     "skonto.ls.lv:8002/mp3"                                   // Skonto 128k
// #define STATION     "aifae8cah8.lb.vip.cdn.dvmr.fr/franceinfo-lofi.mp3"         // France Info 32
// #define STATION     "aifae8cah8.lb.vip.cdn.dvmr.fr/franceinter-lofi.mp3"      //  France Inter 32
// #define STATION     "audio.scdn.arkena.com/11012/francemusique-midfi128.mp3"  // France Musique
// #define STATION     "205.164.62.22:70102"                                     // FM - ABSOLUTE TRANCE (EURO) RADIO 64k
// See http://www.internet-radio.com for stations
#define STATION       "uk2.internet-radio.com:8024"
//#define STATION     "108.178.13.122:8126"

String stations[] = {"uk2.internet-radio.com:8024",
                     "icecast.omroep.nl:80/radio1-bb-mp3",
                     "us2.internet-radio.com:8132",
                     "205.164.62.15:10032",
                     "skonto.ls.lv:8002/mp3",
                     "aifae8cah8.lb.vip.cdn.dvmr.fr/franceinfo-lofi.mp3",
                     "aifae8cah8.lb.vip.cdn.dvmr.fr/franceinter-lofi.mp3",
                     "audio.scdn.arkena.com/11012/francemusique-midfi128.mp3",
                     "205.164.62.22:70102",
                     "108.178.13.122:8126"};
int stationCnt = 0;

int volume[] = {30, 40, 50, 60, 70, 80, 90};
int volumeCnt = 0;
boolean intFree = true;

#define VOLUME     80

//  GPIO16    XDCS    D0
//  GPIO5     XCS     D1
//  GPIO4     DREQ    D2
//  GPIO0     (reset ESP)
//  GPIO2     (reset ESP)
//  GPIO14    SCK     D5
//  GPIO12    MISO    D6
//  GPIO13    MOSI
//  GPIO15    XRST
//  GPI03     (RX)
//  GPIO1     (TX)

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>
#include "VS1053.h"
#include "TickerScheduler.h"
#include "ThingSpeak.h"

extern "C"  {
#include "user_interface.h"
}

#define VS1053_CS       5                   // D1 - Pins for VS1053 module
#define VS1053_DCS      16                  // D0
#define VS1053_DREQ     4                   // D2
#define VS1053_RST      15                  // D8

#define MAXHOSTSIZ 128                    // Maximal length of the URL of a host
#define RINGBFSIZ 40000                   // Ringbuffer for smooth playing. 20000 bytes is 160 Kbits, about 1.5 seconds at 128kb bitrate.
#define DEBUG_BUFFER_SIZE 100             // Debug buffer size

const char* ssid = "FiOS-KNRXW";                      // Your specific WiFi network credentials 
const char* pass = "flux2383gap9255hug";

void   handlebyte ( uint8_t b ) ;
char*  dbgprint( const char* format, ... ) ;
TickerScheduler ts(1);
enum datamode_t { INIT, HEADER, DATA, METADATA } ;         // State for datastream

int              DEBUG = 1 ;

WiFiClient       mp3client ;                               // An instance of the mp3 client
uint32_t         totalcount = 0 ;                          // Counter mp3 data
datamode_t       datamode ;                                // State of datastream
int              metacount ;                               // Number of bytes in metadata
int              datacount ;                               // Counter databytes before metadata
char             metaline[200] ;                           // Readable line in metadata
int              bitrate ;                                 // Bitrate in kb/sec
int              metaint = 0 ;                             // Number of databytes between metadata
char             host[MAXHOSTSIZ] ;                        // The hostname to connect to or file to play
bool             playing = false ;                         // Playing active (for data guard)
char             sname[100] ;                              // Stationname
int              port ;                                    // Port number for host
uint8_t*         ringbuf ;                                 // Ringbuffer for VS1053
uint16_t         rbwindex = 0 ;                            // Fill pointer in ringbuffer
uint16_t         rbrindex = RINGBFSIZ - 1 ;                // Emptypointer in ringbuffer
uint16_t         rcount = 0 ;                              // Number of bytes in ringbuffer
bool             NetworkFound ;                            // True if WiFi network connected

boolean displayWeather = false;

VS1053 vs1053 (VS1053_CS, VS1053_DCS, VS1053_DREQ) ;    // The object for the MP3 player

void setup()  {
  Serial.begin(115200);
  delay(10);
  Serial.println(VERSION);
  system_update_cpu_freq (160);                        // Set to 80/160 MHz
  ringbuf = (uint8_t *) malloc (RINGBFSIZ);            // Create ring buffer
  WiFi.persistent(false);                              // Do not save SSID and password
  WiFi.disconnect();                                   // After restart the router could still keep the old connection
  WiFi.mode(WIFI_STA);                                 // This ESP is a station

  ThingSpeak.begin(mp3client); 
  
  SPI.begin();                                         // Init SPI bus
  pinMode (VS1053_RST, OUTPUT);                        // Input for control VS1053 reset  XXXXXXXXXXXXXXXX
  digitalWrite(VS1053_RST, LOW);                      // Low will reset VS1053       XXXXXXXXXXXXXXX
  Serial.println("Initialize VS1053 player");
  vs1053.begin();                                         // Initialize VS1053 player
  delay(10);
  vs1053.setVolume (0);                                   // Mute
  NetworkFound = connectwifi();                        // Connect to WiFi network
  connecttohost(STATION);                                     // Connect to web radio host
  vs1053.setVolume(VOLUME);

  ts.add(0, 20000, displaySomething, true);
  
  pinMode(0, INPUT);
}

void displaySomething()
{
  displayWeather = true;
}

void loop() {
 if (intFree && !displayWeather)
// if (intFree)
  {
    while(ringspace() && mp3client.available())   putring (mp3client.read());      // get data to ringbuffer
    yield() ;
    while(vs1053.data_request() && ringavail())      handlebyte(getring());           // feed VS1053
  }
  if (digitalRead(0) == 0)
  {
    emptyring();
    stationCnt++;
    if (stationCnt == 10) stationCnt = 0;
    Serial.println("Changing station");
    Serial.println(stations[stationCnt]);
    connecttohost((char *)(stations[stationCnt].c_str()));
    delay(2000);
    intFree = 0;
  }
  else
  {
      intFree = 1;
  }

  if (displayWeather)
  {
      emptyring();
      Serial.println("Getting text weather report");
      String cWeather = ThingSpeak.readStringField(203041, 5, "RBFRKX8OP1LBN84S");
      Serial.println(cWeather);
      connecttohost((char *)(stations[stationCnt].c_str()));
      displayWeather = false;
      delay(2000);

  }
  
  ts.update();
}

void interrupt0()
{
  
  intFree = false;
//  Serial.println("interrupt0");
  //WiFi.disconnect();
//  detachInterrupt(9);
//  Serial.println(stations[stationCnt]);
//  yield();
}

void interrupt1()
{
  intFree = false;
 // Serial.println("interrupt1");
  //WiFi.disconnect();
//  detachInterrupt(10);
//  Serial.println(volume[volumeCnt]);
//  yield();
}

// --- Ringbuffer (fifo) routines ---------------------------------------------------------------------------
inline bool ringspace() {
  return (rcount < RINGBFSIZ) ;                       // True is at least one byte of free space is available
}

inline uint16_t ringavail() {
  return rcount ;                                     // Return number of bytes available
}

void putring(uint8_t b)  {                            // Put one byte in ringbuffer, No check on available space, see ringspace()
  *(ringbuf + rbwindex) = b ;                         // Put byte in ringbuffer
  if(++rbwindex == RINGBFSIZ)    rbwindex = 0 ;       // Increment pointer and wrap at end
  rcount++ ;                                          // Count number of bytes in ringbuffer
}

uint8_t getring() {                                   // Assume there is always something in the bufferpace
  if(++rbrindex == RINGBFSIZ)    rbrindex = 0 ;       // Increment pointer and wrap at end
  rcount-- ;                                          // Count is now one less
  return *(ringbuf + rbrindex) ;                      // return the oldest byte
}

void emptyring()  {
  rbwindex = 0 ;                                      // Reset ringbuffer administration
  rbrindex = RINGBFSIZ - 1 ;
  rcount = 0 ;
}
// -----------------------------------------------------------------------------------------------------

char* dbgprint ( const char* format, ... )  {          // Print only if DEBUG flag is true
  static char sbuf[DEBUG_BUFFER_SIZE] ;                // For debug lines
  va_list varArgs ;                                    // For variable number of params

  va_start ( varArgs, format ) ;                       // Prepare parameters
  vsnprintf ( sbuf, sizeof(sbuf), format, varArgs ) ;  // Format the message
  va_end ( varArgs ) ;                                 // End of using parameters
  return sbuf ;                                        // Return stored string
}

bool connectwifi()  {
  WiFi.disconnect() ;                                     // After restart the router could still keep the old connection
  WiFi.softAPdisconnect(true) ;
  Serial.print("Connecting to "); Serial.println(ssid);
  WiFi.begin (ssid, pass) ;                               // Connect to selected SSID
  if (WiFi.waitForConnectResult() != WL_CONNECTED)  {     // Wait for connection
    Serial.println( "** WiFi Connection failed! **" ) ;
    return false ;
  }
  Serial.println( "Connected to router" ) ;
  return true ;
}

void connecttohost(char* station)   {               // Connect to the specified Internet radio server
  char* p ;                                         // Pointer in hostname
  char* pfs ;                                       // Pointer to formatted string  //  XXXXXXXXXXXX ????????????????
  String extension;                                 // May be like "/mp3" in "skonto.ls.lv:8002/mp3"
 
  strcpy(host, station);
  dbgprint("Connecting to %s", host);
  if (mp3client.connected())  {
    dbgprint ("Stop client");                       // Stop conection to host
    mp3client.flush();
    mp3client.stop();
  }
  port = 80 ;                                       // Default port
  p = strstr ( host, ":" ) ;                        // Search for separator
  if ( p )  {                                       // Portnumber available?
    *p++ = '\0' ;                                   // Remove port from string and point to port
    port = atoi ( p ) ;                             // Get portnumber as integer
  }
  else    p = host ;                                // No port number, reset pointer to begin of host
  // After the portnumber or host there may be an extension
  extension = String ( "/" ) ;                      // Assume no extension
  p = strstr ( p, "/" ) ;                           // Search for begin of extension
  if ( p )  {                                        // Is there an extension?
    extension = String ( p ) ;                      // Yes, change the default
    *p = '\0' ;                                     // Remove extension from host
    dbgprint ( "Slash in station" ) ;
  }
  pfs = dbgprint("Connect to host %s on port %d, extension %s", host, port, extension.c_str());
//  delay ( 2000 ) ;                                  // Show for some time
  mp3client.flush() ;
  if ( mp3client.connect(host, port)) {
    dbgprint ("Connected to Web radio server");
    mp3client.print ( String ("GET ") +            // This will send the request to the server. Request metadata.
                      extension +
                      " HTTP/1.1\r\n" +
                      "Host: " + host + "\r\n" +
                      "Icy-MetaData:1\r\n" +
                      "Connection: close\r\n\r\n");
    char ext[20];
    extension.toCharArray(ext, 20);
    dbgprint("GET %s%s%s%d%s", ext, " HTTP/1.1-", "Host: ", host, "-Icy-MetaData:1-Connection: close");
  }
  datamode = INIT;                                  // Start in metamode
  playing = true;                                   // Allow data guard
}

bool chkhdrline ( const char* str ) {                 // Check if a line in the header is a reasonable headerline. something like "icy-xxxx:abcdef"
  char    b ;                                         // Byte examined
  int     len = 0 ;                                   // Lengte van de string

  while ( ( b = *str++ ) )   {                         // Search to end of string
    len++ ;                                           // Update string length
    if ( ! isalpha ( b ) )   {                         // Alpha (a-z, A-Z)
      if ( b != '-' )   {                              // Minus sign is allowed
        if(b == ':')   return((len > 5 ) && (len < 50)) ; // Found a colon?
        else           return false ;                  // Not a legal character
      }
    }
  }
  return false ;                                       // End of string without colon
}

void handlebyte ( uint8_t b )    {                      // Handle next byte of buffer, sent to VS1053 most of the time
  static uint16_t  metaindex ;                          // Index in metaline
  static bool      firstmetabyte ;                      // True if first metabyte (counter)
  static int       LFcount ;                            // Detection of end of header
  static __attribute__((aligned(4))) uint8_t buf[32] ;  // Buffer for chunk
  static int       chunkcount = 0 ;                     // Data in chunk
  char*            p ;                                  // Pointer in metaline
  int              i ;                                  // Loop control

  if ( datamode == INIT )  {                           // Initialize for header receive
    metaint = 0 ;                                      // No metaint found
    LFcount = 0 ;                                      // For detection end of header
    bitrate = 0 ;                                      // Bitrate still unknown
    metaindex = 0 ;                                    // Prepare for new line
    datamode = HEADER ;                                // Handle header
    totalcount = 0 ;                                   // Reset totalcount
  }
  if ( datamode == DATA )  {                           // Handle next byte of MP3/Ogg data
    buf[chunkcount++] = b ;                            // Save byte in cunkbuffer
    if ( chunkcount == sizeof(buf) )  {                 // Buffer full?
      vs1053.playChunk ( buf, chunkcount ) ;              // Yes, send to player
      chunkcount = 0 ;                                 // Reset count
    }
    totalcount++ ;                                     // Count number of bytes, ignore overflow
    if ( metaint != 0 )  {                             // No METADATA on Ogg streams
       if ( --datacount == 0 )  {                      // End of datablock?
        if ( chunkcount )  {                           // Yes, still data in buffer?
          vs1053.playChunk ( buf, chunkcount ) ;          // Yes, send to player
          chunkcount = 0 ;                             // Reset count
        }
        datamode = METADATA ;
        firstmetabyte = true ;                         // Expecting first metabyte (counter)
      }
    }
    return ;
  }
  if ( datamode == HEADER )    {                       // Handle next byte of MP3 header
    if(( b > 0x7F) || (b == '\r') || (b == '\0'))  {   // Ignore unprintable characters, CR, NULL
      // Yes, ignore
    }
    else if ( b == '\n' )   {                           // Linefeed ?
      LFcount++ ;                                       // Count linefeeds
      metaline[metaindex] = '\0' ;                      // Mark end of string
      metaindex = 0 ;                                   // Reset for next line
      if ( chkhdrline ( metaline ) )   {                // Reasonable input?
//        dbgprint ( metaline ) ;                       // Yes, Show it
        if ( ( p = strstr ( metaline, "icy-br:" ) ) ) {
          bitrate = atoi ( p + 7 ) ;                    // Found bitrate tag, read the bitrate
          if ( bitrate == 0 )     bitrate = 87 ;        // For Ogg br is like "Quality 2"  Dummy bitrate
        }
        else if(( p = strstr(metaline, "icy-metaint:")))    metaint = atoi ( p + 12 ) ; // Found metaint tag, read the value
        else if ((p = strstr(metaline, "icy-name:"))) {
          strncpy ( sname, p + 9, sizeof ( sname ) ) ;  // Found station name, save it, prevent overflow
          sname[sizeof(sname) - 1] = '\0' ;
        }
      }
      if ( LFcount == 2 ) {
        dbgprint("Switch to DATA, bitrate is %d", bitrate);   // Show bitrate
        datamode = DATA ;                               // Expecting data now
        datacount = metaint ;                           // Number of bytes before first metadata
        chunkcount = 0 ;                                // Reset chunkcount
        vs1053.startSong() ;                            // Start a new song
      }
    }
    else  {
      metaline[metaindex] = (char)b ;                  // Normal character, put new char in metaline
      if(metaindex < (sizeof(metaline) - 2))      metaindex++ ;  // Prevent buffer overflow
      LFcount = 0 ;                                    // Reset double CRLF detection
    }
    return ;
  }
  if ( datamode == METADATA )   {                       // Handle next bye of metadata
    if ( firstmetabyte )   {                            // First byte of metadata?
      firstmetabyte = false ;                          // Not the first anymore
      metacount = b * 16 + 1 ;                         // New count for metadata including length byte
      metaindex = 0 ;                                  // Place to store metadata
    }
    else  {
      metaline[metaindex] = (char)b ;                 // Normal character, put new char in metaline
      if(metaindex < (sizeof(metaline) - 2))     metaindex++ ;  // Prevent buffer overflow
    }
    if ( --metacount == 0 ) {
      if ( metaindex )   {                             // Any info present?
        metaline[metaindex] = '\0' ;
      }
      datacount = metaint ;                           // Reset data count
      chunkcount = 0 ;                                // Reset chunkcount
      datamode = DATA ;                               // Expecting data
    }
  }
}

