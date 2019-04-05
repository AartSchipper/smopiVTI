/*
    Video Time Inserter: http://smopi.news.nstrefa.pl/ version v2.2.
    
    Hardware requirements:
     - Arduino UNO,
     - U-Blox NEO-6M GPS receiver or compatible,
     - VideoOverlayShield MAX7456.
     The main module is responsible for reading the current position, date and time from the GPS receiver. These data
     they are then placed in the PAL or NTSC video image.

    Piotr Smolarz, e-mail: smopi.pl@gmail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    ******************* 

    - Translated the comments and OSD messages to English 
    - Adapted to "Micro OSD v2.4" hardware 
    Note: 
      - The AB7456 in the Micro OSD v2.4 hardware has a very strange font installed for the intended drone use of the module. 
      Replace this with the CUSTOM.mcm font using the UploadFont-sketch, both found in the MAX7456/examples directory of the 7456 library and 
      an XMODEM program. I used lrzsz, minicom should work too, but not for me.  

      Todo: Connect GPS module, Find  and connect info- and PPS pins 
      LEDpin = 7
      CURRENT pin = A1
       
    Aart 03-2019 

    *******************
*/

#define DEBUG

#define MOSD24        // Uncommend when using Micro OSD 2.4 hardware 
                      // Arduino board settings "Arduino Pro or Pro Mini", Atmega328P 5V, 16 MHz

#define HTTPSTRING   "--smopi.news.nstrefa.pl--"
#define VERSTRING    "- 2015-17 smopiVTI v2.2 -"
#define MAX_CHECKS 5
#define CPU_STEPS 16

#include "VTI.h"
// #include <Time.h>      // Time      (http://www.pjrc.com/teensy/td_libs_Time.html) by Michael Margolis
#include <TimeLib.h>   // Needed ? Todo: Why. Aart
#include <TinyGPS++.h> // TinyGPS++ (http://arduiniana.org/libraries/tinygpsplus/) by Mikal Hart
#include <SPI.h>
#include <MAX7456.h>

// System clock VTI
VTIclock clock1;

// Global variables
volatile bool          checkedClock = false;  // Is the clock frequency set ? 
volatile bool          clockCheckFinished = false;  // Is the clock frequency setting finished ? 
volatile unsigned int  checkNo      = 1;      // The number of clock frequency checks
volatile bool          isPPSready   = false;  // Is there a 1 PPS impulse ?
volatile unsigned long msTimeStamp  = 0;      // The number of milliseconds since the device was started
volatile unsigned int  counterPPS   = 0;      // PPS counter 
volatile unsigned long microsPPS[MAX_CHECKS]; // Microseconds between PPS pulses
unsigned long          averageClock = 0;      // Average clock frequency
unsigned long          counterVSync = 0;      // Frame counter VSync

#ifdef DEBUG
volatile unsigned int  vtiMillis      = 0;
volatile unsigned int  ppsTCNT1       = 0;
#endif

// Display mode switch: Date/Time <-> Info
const int displayModePin = 4;

//GPS RXPin = 0, TXPin = 1
const uint32_t     GPSBaud    = 9600;         // Communication speed with GPS

#ifdef MOSD24
  const byte         PPSpin     = A1;        // CURRENT pin on MOSD 2.4 used as PPS pin. PCINT9 
 
#else
  const byte         PPSpin     = 3;          // PPS pin
#endif
const unsigned int MAX_INPUT  = 20;           // Size of the buffer for GPS data

// Declaration of a GPS object
TinyGPSPlus GPS;

// Declaration of the OSD object
#ifdef MOSD24
  const byte osdChipSelect = 6;
#else
  const byte osdChipSelect = 10;
#endif

MAX7456 OSD( osdChipSelect );


// Interrupt service routine
// Every 1 ms, perform the VTI time update
ISR(TIMER1_COMPA_vect)
{
  clock1.Update();
}

ISR(PCINT1_vect) 
{
  if (digitalRead(A1)) { // rising edge only
    if (clockCheckFinished) {
      PPSevent(); 
    } else {
      checkClock(); 
    }
  }
}


//******************************************************//
//SETUP - run one time
//******************************************************//
void setup()
{
  unsigned char system_video_in = NULL;
  bool          gpsReady        = false;

  TimeElements  tm;


  pinMode(PPSpin, INPUT);                // PPS Impulse
  
#ifdef MOSD24  
  PCICR |= 0b00000010;                      // Disable PCINT
  PCMSK1 = (1<<PCINT9);                     // Unmask pin change interrupt on pin A1 
#endif

  pinMode(displayModePin, INPUT_PULLUP); // Data / Time switch <-> Info

  Serial.begin(GPSBaud);                 // Initialization of the serial interface to communication with GPS

  // Initialize the SPI connection:
  SPI.begin();
  SPI.setClockDivider( SPI_CLOCK_DIV4 ); // Speed must be less than 10 MHz.

  // Initialize the MAX7456 OSD:
  OSD.begin();                           // Use NTSC with default area.
  OSD.setSwitchingTime( 5 );             // Set video croma distortion to a minimum.


  // !!! Only needed if ascii font
  // !!! Uncomment only when using ASCII fonts
  // !!! http://smopi.news.nstrefa.pl/index.php?pages/Zmiana-czcionki-w-MAX7456
  OSD.setCharEncoding( MAX7456_ASCII );

  system_video_in = OSD.videoSystem();

  if (NULL != system_video_in)
  {
    OSD.setDefaultSystem(system_video_in);
  }
  else
  {
    OSD.setDefaultSystem(MAX7456_NTSC);
  }


  OSD.display(); // Activate OSD

  OSDfooter();   // Display information about the VTI version

  //******************************************************//
  // Configuration of the GPS receiver
  //******************************************************//
  OSD.setCursor( 0, 0 );
  OSD.print("Configurate GPS....");
  do
  {
    static int i = 1;

    // Do it if not in VSYNC
    while (OSD.notInVSync())
    {}

    OSD.setCursor( 22, 0 );
    OSD.print(i);
    i++;
  } while (!configureGPS());

  OSD.setCursor( 22, 0 );
  OSD.print("OK");


  //******************************************************//
  // Waiting for the impulse of PPS
  //******************************************************//
  OSD.setCursor( 0, 1 );
  OSD.print("Waiting for PPS.......");

#ifdef MOSD24
  PCICR |= 0x01 << 1; // Enable Pin Change Interrupt 1 
#else
  attachInterrupt(digitalPinToInterrupt(PPSpin), checkClock, RISING);
#endif

  do
  {
    static int i = 1;

    delay(1000);

    // Do it if not in VSYNC
    while (OSD.notInVSync())
    {}

    OSD.setCursor( 22, 1 );
    OSD.print(i);
    i++;
  } while (!isPPSready);

  OSD.setCursor( 22, 1 );
  OSD.print("OK ");


  //******************************************************//
  // Calibration of the uC clock
  //******************************************************//
  OSD.setCursor(0, 2);
  OSD.print("Calibrate clock...");

  while (!checkedClock)
  {
    // Do it if not in VSYNC
    while (OSD.notInVSync())
    {}

    OSD.setCursor( 22, 2 );
    OSD.print(checkNo);
  }

  
#ifdef MOSD24
  PCICR |= 0x01 << 0; // Disable Pin Change Interrupt 1 
#else
  detachInterrupt(digitalPinToInterrupt(PPSpin));
#endif

  OSD.setCursor( 22, 2 );
  OSD.print("OK ");

  // List the read frequencies
  for (int i = 0; i < MAX_CHECKS; i++)
  {
    OSD.setCursor(1, 3 + i);
    averageClock += microsPPS[i];
    OSD.print(CPU_STEPS * microsPPS[i]);
    OSD.print(" Hz");
  }
  // Average frequency:
  averageClock = (averageClock / MAX_CHECKS) * CPU_STEPS;
  
  clockCheckFinished == true; 

  //******************************************************//
  // I'm waiting for GPS data
  //******************************************************//
  OSD.setCursor(0, 8);
  OSD.print("Waiting for GPS data..");

  OSD.setCursor(0, 9);
  OSD.print("NMEA with Fix.......");

  // determine if the GPS receiver is in 3D Fix mode - on this basis determine the readiness of VTI to work.
  // What about "leap second"? Is it possible to say that the GPS receiver has already downloaded information about it?

  while (!gpsReady)
  {
    static unsigned int  i  = 0;
    static unsigned long ms = millis();

    //updateGPSobj(0);
    if (Serial.available() > 0)
    {
      char a = Serial.read();
      gpsReady = GPS.encode(a) && (GPS.sentencesWithFix() > 5 && GPS.location.isValid() && GPS.time.isValid() && GPS.date.isValid());
    }

    if (millis() - ms > 1000)
    {
      i++;
      ms = millis();

      OSD.setCursor( 22, 8 );
      OSD.print(i);

      OSD.setCursor( 22, 9 );
      OSD.print(GPS.sentencesWithFix());

    }
  }

  OSD.setCursor( 22, 8 );
  OSD.print("OK ");


  //******************************************************//
  // Set the VTI system date and time
  //******************************************************//
  tm.Second = GPS.time.second();
  tm.Minute = GPS.time.minute();
  tm.Hour   = GPS.time.hour();
  //tm.Wday   = NULL;
  tm.Day    = GPS.date.day();
  tm.Month  = GPS.date.month();
  tm.Year   = GPS.date.year() - 1970;

  clock1.setDateTime(makeTime(tm), 0);   // Set the VTI time


  //******************************************************//
  // Counter configuration: timer1
  //******************************************************//
  noInterrupts();                      // disable all interrupts
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;

  OCR1A  = (averageClock / 1000) - 1;  // compare match register 16MHz/1/1000Hz
  TCCR1B |= (1 << WGM12);              // CTC mode
  TCCR1B |= (1 << CS10);               // Prescaler: 1
  TIMSK1 |= (1 << OCIE1A);             // enable timer compare interrupt
  interrupts();                        // enable all interrupts
  // timer1 configured

  // Set the interrupt function for the 1PPS pulse
  
#ifdef MOSD24
  PCICR |= 0x01 << 1; // Enable Pin Change Interrupt 1 
#else
  attachInterrupt(digitalPinToInterrupt(PPSpin), PPSevent, RISING);
#endif

  // Do the first call to the PPSevent () function after calling it
  // internal VTI time is set with accuracy of +/- 1ms
  while (counterPPS < 2);

  // Clear the screen
  OSD.clear();

}

//******************************************************//
//END SETUP
//******************************************************//



//******************************************************//
//MAIN LOOP - run over and over
//******************************************************//
void loop()
{
  static char          input_line [MAX_INPUT]; // Buffer for GPS data
  static unsigned int  input_pos  = 0;         // How much data is in the buffer
  static boolean       isInfoMode = false;     // Selecting the OSD mode
  DateTimeMS           tmpDateTimeMS;          // Structure containing hour, date and millisecond.
  DateTimeMS           *ptr;                   // A pointer to the tmpDateTimeMS structure


  //Perform until when not in VSYNC (i.e. for PAL ~ 19ms)
  while (OSD.notInVSync())
  {
    // Perform if data from the GPS receiver and space in the buffer are available
    if (Serial.available() > 0 && input_pos < MAX_INPUT)
    {
      input_line[input_pos] = Serial.read();
      input_pos += 1;
    }
  }
  //end while


  //////////////////////////////////
  // SWITCH - select display mode //
  //////////////////////////////////
  switch (digitalRead(displayModePin))
  {
    // Date/Time mode
    case HIGH:
      noInterrupts();           // disable all interrupts
      tmpDateTimeMS = clock1.getDateTimeMS();
      ptr           = &tmpDateTimeMS;
      interrupts();             // enable all interrupts

      if (isInfoMode) OSD.clear();
      osdMillis(ptr);
      osdVSync();
      osdTime(isInfoMode, ptr);
      osdDate(isInfoMode, ptr);
      osdShortInfo();
      isInfoMode = false;
      break;

    // Info mode
    case LOW:
      if (!isInfoMode)
      {
        isInfoMode = true;
        OSD.clear();
      }
      osdInfo();
      break;
  }
  //////////////////////////
  //      END SWITCH      //
  //////////////////////////


  // Update the image counter
  counterVSync++;


  // Are there any data from the GPS receiver in the buffer ?
  if (input_pos > 0)
  {
    for (uint8_t i = 0; i < input_pos; i++)
    {
      // Get GPS data from the buffer and decode
      if (GPS.encode(input_line[i]) && GPS.time.isValid())
      {
        // Compare the system time with GPS no more often than every 1000 ms
        // In the case of a difference, display an error message.
        if ( !checkVTItime(1000) ) OSDFatalError(); // TODO: think about what should be checked and error messages!
      }
    }
    input_pos = 0;
  }
  //endif


  // Do until when in VSYNC
  while (!OSD.notInVSync())
  {
  }

}
//******************************************************//
//END MAIN LOOP
//******************************************************//


// View milliseconds
void osdMillis(struct DateTimeMS *ptr)
{
  static unsigned int counter = 0;
  char msChars[] = "       ";
  byte posOffset = 4 * (counter % 2); // 0 or 4

  msChars[2 + posOffset] = ptr->MS       % 10 + '0';
  msChars[1 + posOffset] = ptr->MS / 10  % 10 + '0';
  msChars[0 + posOffset] = ptr->MS / 100 % 10 + '0';

  OSD.setCursor( 10 , 0 );
  OSD.print( msChars );

  counter++;
}


// Display the current time
void osdTime(boolean mustDisplay, struct DateTimeMS *ptr)

{
  static char t[] = "00:00:00 ";
  static int prevSecond = 0;
  static int prevMinute = 0;
  static int prevHour   = 0;

  int curSecond = second(ptr->DT);
  int curMinute = minute(ptr->DT);
  int curHour   = hour(ptr->DT);

  boolean updatedTime = false;

  //Time
  if (prevSecond != curSecond)
  {
    prevSecond = curSecond;
    updatedTime = true;
    t[6]  = curSecond / 10 + '0';
    t[7]  = curSecond % 10 + '0';

    t[8] = (millis() - msTimeStamp > 1000) ? ' ' : 'P';

    if (prevMinute != curMinute)
    {
      prevMinute = curMinute;
      t[3]  = curMinute / 10 + '0';
      t[4]  = curMinute % 10 + '0';

      if (prevHour != curHour)
      {
        prevHour = curHour;
        t[0]  = curHour / 10 + '0';
        t[1]  = curHour % 10 + '0';
      }
    }
  }

  if (updatedTime || mustDisplay)
  {
    OSD.setCursor( 0, 0 );
    OSD.print( t );
  }
}


// Display the current date
void osdDate(boolean mustDisplay, struct DateTimeMS *ptr)
{
  static char d[] = "0000-00-00";
  static int prevDay    = 0;

  int curDay    = day(ptr->DT);
  boolean updatedDate = false;

  // Date
  if (prevDay != curDay)
  {
    prevDay = curDay;
    updatedDate = true;

    d[8]  = curDay / 10 + '0';
    d[9]  = curDay % 10 + '0';

    d[5]  = month(ptr->DT) / 10 + '0';
    d[6]  = month(ptr->DT) % 10 + '0';

    d[0]  = year(ptr->DT) / 1000 + '0';
    d[1]  = year(ptr->DT) / 100 % 10 + '0';
    d[2]  = year(ptr->DT) / 10  % 10 + '0';
    d[3]  = year(ptr->DT) % 10       + '0';
  }

  if (updatedDate || mustDisplay)
  {
    OSD.setCursor( 0, OSD.rows() - 1 );
    OSD.print( d );
  }
}


// Display the counter of half pictures
void osdVSync()
{
  char charVSync[]   = "      ";

  charVSync[5] = counterVSync          % 10 + '0';
  charVSync[4] = counterVSync / 10     % 10 + '0';
  charVSync[3] = counterVSync / 100    % 10 + '0';
  charVSync[2] = counterVSync / 1000   % 10 + '0';
  charVSync[1] = counterVSync / 10000  % 10 + '0';
  charVSync[0] = counterVSync / 100000 % 10 + '0';

  OSD.setCursor( OSD.columns() - 6, 0 );
  OSD.print( charVSync );

#ifdef DEBUG
  char charMillis[] = "   ";
  char charTCNT1[]  = "     ";

  charMillis[2] = vtiMillis       % 10 + '0';
  charMillis[1] = vtiMillis / 10  % 10 + '0';
  charMillis[0] = vtiMillis / 100 % 10 + '0';

  charTCNT1[4] = ppsTCNT1         % 10 + '0';
  charTCNT1[3] = ppsTCNT1 / 10    % 10 + '0';
  charTCNT1[2] = ppsTCNT1 / 100   % 10 + '0';
  charTCNT1[1] = ppsTCNT1 / 1000  % 10 + '0';
  charTCNT1[0] = ppsTCNT1 / 10000 % 10 + '0';


  OSD.setCursor( OSD.columns() - 6, 1 );
  OSD.print( charMillis );

  OSD.setCursor( OSD.columns() - 6, 2 );
  OSD.print( charTCNT1 );
#endif
}


// Display information in "Info" mode: position, altitude, HDOP, etc.
void osdInfo()
{
  static unsigned long lastUpdated = 0;
  unsigned int tmpHDOP = 0;

  if (millis() - lastUpdated > 1000)
  {
    lastUpdated = millis();

    OSD.setCursor( 0, 0 );
    OSD.print(GPS.location.lat(), 5);
    (GPS.location.rawLat().negative) ? OSD.print(" S") : OSD.print(" N");

    OSD.setCursor( 0, 1 );
    OSD.print(GPS.location.lng(), 5);
    (GPS.location.rawLng().negative) ? OSD.print(" W") : OSD.print(" E");

    OSD.setCursor( 0, 2 );
    (GPS.altitude.isValid()) ? OSD.print(GPS.altitude.meters(), 0) : OSD.print("---");
    OSD.print(" m  ");

    OSD.setCursor( 0, 4 );
    OSD.print("satellites: ");
    OSD.print(GPS.satellites.value());

    tmpHDOP = GPS.hdop.value();
    OSD.print(" HDOP: ");
    OSD.print(tmpHDOP / 100);
    OSD.print(".");
    OSD.print((tmpHDOP / 10) % 10);
    OSD.print("  ");

    OSD.setCursor( 0, 6 );
    OSD.print("NMEA failed..: ");
    OSD.print(GPS.failedChecksum());

    OSD.setCursor( 0, 7 );
    OSD.print("NMEA passed..: ");
    OSD.print(GPS.passedChecksum());

    OSD.setCursor( 0, 8 );
    OSD.print("NMEA with fix: ");
    OSD.print(GPS.sentencesWithFix());
  }
  OSDfooter();
  OSD.home();
}


// Display short information: currently the number of satellites
void osdShortInfo()
{
  static unsigned long ms = 0;

  if (millis() - ms > 1000)
  {
    OSD.setCursor(12, OSD.rows() - 1);
    OSD.print(GPS.satellites.value());
    OSD.print(" ");

    ms = millis();
  }
}


// View version information
void OSDfooter()
{
  OSD.setCursor( 0, OSD.rows() - 2 );
  OSD.print( HTTPSTRING );

  OSD.setCursor( 0, OSD.rows() - 1 );
  OSD.print( VERSTRING );
}


// Decode data read from GPS
void updateGPSobj(unsigned long timeout)
{
  unsigned long ms = millis();

  while (Serial.available() > 0)
  {
    GPS.encode(Serial.read());

    if (millis() - ms > timeout && timeout != 0)
    {
      // Timeout!
      return;
    }
  }
}


// Compare the system time with GPS time
bool checkVTItime(unsigned long msParam)
{
  static unsigned long ms = millis();
  bool val  = true;
  time_t DT = clock1.getDateTime();

  if (millis() - ms >= msParam)
  {
    ms = millis();
    // val = (GPS.time.hour() == hour(DT) && GPS.time.minute() == minute(DT) && GPS.time.second() == second(DT)) ? true : false;
    val = (GPS.time.second() == second(DT));
  }
  return val;
}


// Display information about a serious error
void OSDFatalError()
{
  time_t DT = clock1.getDateTime();

  //OSD.clear();
  OSD.setCursor(0, 2);
  OSD.print("TIME ERROR-RESTART VTI!");

  OSD.setCursor(0, 4);
  OSD.print("VTI time: ");
  OSD.print(hour(DT)); OSD.print(":"); OSD.print(minute(DT)); OSD.print(":"); OSD.print(second(DT)); OSD.print("   ");

  OSD.setCursor(0, 5);
  OSD.print("GPS time: ");
  OSD.print(GPS.time.hour()); OSD.print(":"); OSD.print(GPS.time.minute()); OSD.print(":"); OSD.print(GPS.time.second()); OSD.print("   ");

  //while(1) {};
}


// Perform when PPS pulse
void PPSevent()
{
#ifdef DEBUG
  vtiMillis = clock1.getMillis();
  ppsTCNT1  = TCNT1;
#endif

  msTimeStamp = millis();
  counterPPS++;

  clock1.roundDateTime();
  TCNT1  = 0;            // (0 <-> OCR1A) Reset the register in timer1 ATmega328
}


// Determine the real value of the uC clock
void checkClock()
{
  static unsigned long microsTS = 0;
  static unsigned long millisTS = 0;

  isPPSready = true;

  long tmpMS = (millis() - millisTS) - 1000;

  if (abs(tmpMS) < 5)
  {
    microsPPS[checkNo - 1] = micros() - microsTS;
    checkNo += 1;
  }

  microsTS = micros();
  millisTS = millis();

  checkedClock = (checkNo - 1 == MAX_CHECKS) ? true : false;
}


// Configure the GPS receiver
boolean configureGPS()
{
  
  int counter = 0;
  boolean gps_set_success = false;

  // Portmode:
  uint8_t setPORTMODE[] = {
    0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00, 0x80, 0x25, 0x00, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA1, 0xAF
  };
  while (!gps_set_success && counter < 3)
  {
    sendUBX(setPORTMODE, sizeof(setPORTMODE) / sizeof(uint8_t));
    gps_set_success = getUBX_ACK(setPORTMODE);
    counter += 1;
  }
  if (gps_set_success)
  {
    gps_set_success = false;
    counter = 0;
  }
  else
  {
    return false;
  }
  

  // Turn off the NMEA GLL message:
  uint8_t setGLL[] = {
    0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x2B
  };
  while (!gps_set_success && counter < 3)
  {
    sendUBX(setGLL, sizeof(setGLL) / sizeof(uint8_t));
    gps_set_success = getUBX_ACK(setGLL);
    counter += 1;
  }
  if (gps_set_success)
  {
    gps_set_success = false;
    counter = 0;
  }
  else
  {
    return false;
  }

  // Turn off the NMEA GSA message:
  uint8_t setGSA[] = {
    0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x32
  };
  while (!gps_set_success && counter < 3)
  {
    sendUBX(setGSA, sizeof(setGSA) / sizeof(uint8_t));
    gps_set_success = getUBX_ACK(setGSA);
    counter += 1;
  }
  if (gps_set_success)
  {
    gps_set_success = false;
    counter = 0;
  }
  else
  {
    return false;
  }

  // Turn off the NMEA GSV message:
  uint8_t setGSV[] = {
    0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x39
  };
  while (!gps_set_success && counter < 3)
  {
    sendUBX(setGSV, sizeof(setGSV) / sizeof(uint8_t));
    gps_set_success = getUBX_ACK(setGSV);
    counter += 1;
  }
  if (gps_set_success)
  {
    gps_set_success = false;
    counter = 0;
  }
  else
  {
    return false;
  }

  // Turn off the NMEA VTG message:
  uint8_t setVTG[] = {
    0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x05, 0x47
  };
  while (!gps_set_success && counter < 3)
  {
    sendUBX(setVTG, sizeof(setVTG) / sizeof(uint8_t));
    gps_set_success = getUBX_ACK(setVTG);
    counter += 1;
  }
  if (gps_set_success)
  {
    gps_set_success = false;
    counter = 0;
  }
  else
  {
    return false;
  }
 
  return true; //Configuration success
}


// Send a byte array of UBX protocol to the GPS
void sendUBX(uint8_t *MSG, uint8_t len) {
  for (int i = 0; i < len; i++) {
    Serial.write(MSG[i]);
  }
  Serial.println();
}


// Calculate expected UBX ACK packet and parse UBX response from GPS
boolean getUBX_ACK(uint8_t *MSG) {
  uint8_t b;
  uint8_t ackByteID = 0;
  uint8_t ackPacket[10];
  unsigned long startTime = millis();
  Serial.print(" * Reading ACK response: ");

  // Construct the expected ACK packet
  ackPacket[0] = 0xB5;  // header
  ackPacket[1] = 0x62;  // header
  ackPacket[2] = 0x05;  // class
  ackPacket[3] = 0x01;  // id
  ackPacket[4] = 0x02;  // length
  ackPacket[5] = 0x00;
  ackPacket[6] = MSG[2];  // ACK class
  ackPacket[7] = MSG[3];  // ACK id
  ackPacket[8] = 0;   // CK_A
  ackPacket[9] = 0;   // CK_B

  // Calculate the checksums
  for (uint8_t i = 2; i < 8; i++) {
    ackPacket[8] = ackPacket[8] + ackPacket[i];
    ackPacket[9] = ackPacket[9] + ackPacket[8];
  }

  while (1) {

    // Test for success
    if (ackByteID > 9) {
      // All packets in order!
      return true;
    }

    // Timeout if no valid response in 3 seconds
    if (millis() - startTime > 3000) {
      return false;
    }

    // Make sure data is available to read
    if (Serial.available()) {
      b = Serial.read();

      // Check that bytes arrive in sequence as per expected ACK packet
      if (b == ackPacket[ackByteID]) {
        ackByteID++;
      }
      else {
        ackByteID = 0;  // Reset and look again, invalid order
      }

    }
  }
}

