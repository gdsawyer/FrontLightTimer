/*
  * frontTimer -- The timer for the front lights
  * 
  * Version:  V0.2
  * Date:     2017-07-04
  * Author:   Gregory D. Sawyer
  *   
  * This turns the lights on three conditions:
  *   1)  Ten minutes before sunset, until 23:00
  *   2)  At 06:30 if the sunrise is at 07:00 or later
  *   3)  Tuesday Mornings 03:45 for 1.25 hours
  *   
  *   Adapted from the plugsout version that was used to test using 
  *   the battery.  This version (0.2) will include an interupt 
  *   service handler that will respond to a pushbutton to display 
  *   the current date time and other internal variables per the 
  *   non-plugout version.
 */

// Date and time functions using a DS3231 RTC connected via I2C and Wire lib
#include <Wire.h>
#include "RTClib.h"
#include "Dusk2Dawn.h"

#define LED 13
#define SW1 2 // this is an interupt switch for debugging

RTC_DS3231 rtc;

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const float localLat = 47.688149;
const float localLong = -122.338915;
const long epochRataDie = 719162;  // This is the RD for The Epoch.  It is actually 1969-12-31 because Unix day 1 is 1970-01-01
int localSunrise;  // Sunrise time, minutes past midnight
int localSunset;   // Sunset time, minutes past midnight -- both are for the current calendar day
int offTime;  // time to shut off the lights
long sunDate = 19700101;  // the date for with the sunrise and sunset are valid
bool isDst; // TRUE if daylight savings time
bool isMorningOn;  // TRUE if we turn on the lights at 6:30 AM
bool isTuesday; // TRUE if it is tuesday and we need to turn on the lights for milkman
bool isOn = false; // TRUE if the lights are on
long dateRataDie; // the current date rata die, used for gompulating stuff
volatile byte dbgDisplayTime = false;

// Debugging stuff here -- remove for final code
char leTime[6];


// initialize to our house, which is in zone 8
Dusk2Dawn localSea(localLat, localLong, -8);


void setup () {

  pinMode(LED, OUTPUT);
  pinMode(SW1, INPUT);

  //Serial.begin(9600);

  //delay(3000); // wait for console opening

  if (! rtc.begin()) {
    ////Serial.println("Couldn't find RTC");
    while (1);
  }

  if (rtc.lostPower()) {
    ////Serial.println("RTC lost power, lets set the time!");
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }

  rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));
  //rtc.adjust(DateTime(2017,12,27,23,21,44));

  /*
   * Because this is plugs out and we won't be
   * seeing printouts, I want to strobe the lights
   * so we can see when it goes on.
   */
  int idx;
  byte onOff = LOW;
  for (idx = 0; idx<= 10; idx++){
    onOff = (onOff == LOW ? HIGH : LOW);
    digitalWrite(LED, onOff);
    delay(125);
  }
  digitalWrite(LED,LOW);

  // set up the interupt handler to trap the SW1 pin
  attachInterrupt(digitalPinToInterrupt(SW1), strobeTime, FALLING);
}



long jdnFromUnix(long unixTime) {
  /*
  # Translate the UnixTime to a day (mod 86400 seconds per day)
  # then subtract out the epoch to get a JDN.  I suspect thisis
  # wrong.
  */  
  return (unixTime / 86400L) + 2440588L;
}

boolean isGregorianLeapYear(int gYear){
  int yearModFour = gYear % 4;
  int centuryModFour = gYear % 400;
  boolean isLeapYear = false;
  
  if (yearModFour == 0){
    isLeapYear = true;
    if ( (centuryModFour == 100) || (centuryModFour == 200) || (centuryModFour == 300)){
      isLeapYear = false;
    }
  } else {
    isLeapYear = false;
  }
  
  return isLeapYear;
}

long fixedFromGregorian( int gregYear, int gregMonth, int gregDay){
  long gregorianEpoch = 1;
  long yearDecr = gregYear - 1;
  long tempRataDie = gregorianEpoch - 1 + 365 * yearDecr;
 
  tempRataDie += (yearDecr / 4) - (yearDecr / 100) + (yearDecr / 400);
  
  tempRataDie +=  (367 * gregMonth - 362) / 12;
 
  
  if (gregMonth > 2){
    if (isGregorianLeapYear(gregYear)){
      tempRataDie -= 1;
    } else {
      tempRataDie -= 2;
    }
  }
    
  tempRataDie += gregDay;
    
  return tempRataDie;
}

byte dayOfWeekFromFixed(long dateRataDie){
  /*
   * The 0 below stands for Sunday
  */
  return dateRataDie % 7;
}

long kDayOnOrBefore(byte dayOfWeekId, long dateRataDie){
  return dateRataDie - dayOfWeekFromFixed(dateRataDie - dayOfWeekId) ;
}

long kDayBefore(byte dayOfWeekId, long dateRataDie){
  return kDayOnOrBefore(dayOfWeekId, dateRataDie);
}

long kDayAfter(byte dayOfWeekId, long dateRataDie){
  return kDayOnOrBefore(dayOfWeekId, dateRataDie + 7);
}

long nthKday(int occursCount, byte dayOfWeekId, long dateRataDie){

  long tempRataDie;
  
  if (occursCount > 0){
    tempRataDie = 7 * occursCount + kDayBefore(dayOfWeekId, dateRataDie);
  } else {
    tempRataDie = 7 * occursCount + kDayAfter(dayOfWeekId, dateRataDie);
  }
  return tempRataDie;
}

long dstStart(int forYear){
  return nthKday(2, 0, fixedFromGregorian(forYear, 3, 1));
}

long dstEnd(int forYear){
  return nthKday(1,0, fixedFromGregorian(forYear, 11,1));
}


/*
 * INTERUPT HANDLER  
 * 
 * For debugging, this will display the date and other cool things
 * when the button is pressed.  The dumps to the serial port at
 * 9600 baud.  
 */
void showTime(DateTime zeTime){
  Serial.begin(57600);

   
  int curMinOfDay;

  Serial.println();
  Serial.println("*****AT THE TONE, the time will be*****");
  Serial.print("Current Time is");
  Serial.print(daysOfTheWeek[zeTime.dayOfTheWeek()]);
  Serial.print(" ");
  printIsoDigit(zeTime.year(),false);
  printIsoDigit(zeTime.month(), true);
  printIsoDigit(zeTime.day(), true);
  Serial.print("T");
  printIsoDigit(zeTime.hour(), false);
  printIsoDigit(zeTime.minute(), false);
  printIsoDigit(zeTime.second(), false);
  Serial.println();
  Serial.print("Unix/JDN:      ");
  Serial.print(zeTime.unixtime());
  Serial.print(" Seconds = ");
  Serial.print(zeTime.unixtime() / 86400);
  Serial.print(" days, JDN = ");
  Serial.print(jdnFromUnix(zeTime.unixtime()));
  Serial.println();

  curMinOfDay = zeTime.hour() * 60 + zeTime.minute();
  Serial.println("******** SunStats *********");
  Serial.print("Current Minute of Day:  ");
  Serial.println(curMinOfDay, DEC);
  Serial.print("Date Rata Die:  ");
  Serial.println(dateRataDie, DEC);
  Serial.print("sunDate:  ");
  Serial.println(sunDate, DEC);
  Serial.print("Sunrise Minutes:  ");
  Serial.println(localSunrise, DEC);
  Dusk2Dawn::min2str(leTime, localSunrise);
  Serial.println(leTime);
  Serial.print("Sunset Minutes:  ");
  Serial.println(localSunset, DEC);
  Dusk2Dawn::min2str(leTime, localSunset);
  Serial.println(leTime);
  Serial.print("Is DST:  ");
  Serial.println(isDst, DEC);
  Serial.print("Tuesday:  ");
  Serial.println(isTuesday, DEC);
  Serial.print("Is Early On:  ");
  Serial.println(isMorningOn, DEC);
      
  Serial.end();
  dbgDisplayTime = false;
  //return();
  

  
}

void printIsoDigit(int digitToPrint, bool isDatePart){
  if (isDatePart){
    Serial.print("-");
  } else {
    Serial.print(":");    
  }
  if (digitToPrint < 10){
    Serial.print("0");
  } 
  Serial.print(digitToPrint);
}

void strobeTime(){
  /*
   * Interupt handler that sets the dbgDisplayTime var 
   */
   dbgDisplayTime = true;
}
void loop () {
    int curMinOfDay; // current minutes past midnight
    
    //DateTime now = rtc.now();
    DateTime now = rtc.now();
    curMinOfDay = now.hour() * 60 + now.minute();

    if (dbgDisplayTime){
      showTime(now);
    }

    
    if (now.year() * 10000L + now.month() * 100L + now.day() != sunDate){
      /*
       * We have a new date, so we need to compute the sunrise and sunset.
       * Because that requires all kinds of funky things like determining
       * if we are in Daylight Savings Time, we need to generate a RD date
       * so we can feed this to the CC3 functions.  We also determine if 
       * we need to turn the lights on at 0630 because the sun does not
       * rise until after 0645.  Once we have done all this, we can
       * reset the sunDate.
       */
      dateRataDie = epochRataDie + now.unixtime() / 86400L;

      isDst = (dateRataDie >= dstStart(now.year())) & (dateRataDie <= dstEnd(now.year()));
      localSunrise = localSea.sunrise(now.year(), now.month(), now.day(), isDst);
      //localSunrise = localSea.sunrise(2017, 06, 28, true);
      localSunset = localSea.sunset(now.year(), now.month(), now.day(), isDst);
      
      // determine if we need to turn on at 0630
      isMorningOn = localSunrise >= 405L; // 405 is 06:45
      // determine if it is tuesday so we need to turn on the lights at 0400
      isTuesday = now.dayOfTheWeek() == 2;
      sunDate = now.year() * 10000L + now.month() * 100L + now.day();
    }

    /*
     * Turn on the lights -- if the time is 20 minutes before sunset
     * Turn on the light, set the off time, and set isOn indicator true
     * We set the off time to 2300 which will leave time for Pam to get
     * up the stairs on nights she has class.
     */

    if ((curMinOfDay == localSunset - 20) && (!isOn)){
      digitalWrite(LED, HIGH);
      isOn = true; 
      offTime = 23 * 60;
    }

    /*
     *  Turn off the light -- this is universal
     *  
    */

    if ( isOn && (curMinOfDay == offTime)){
      digitalWrite(LED, LOW);
      isOn = false;
      offTime = 0;
    }

    /*
     *  Turn on if we're tuesday and it's 3:30, keep on for an hour
     */
    if ( isTuesday && !isOn && (curMinOfDay == 210)){
      digitalWrite(LED, HIGH);
      isOn = true;
      offTime = 285;
    }

    /*
     * For the mornings that we need to turn the lights on 
     * because the sun doesn't rise until well after 7:00
     * Then turn the lights on at 0630, and shut them off
     * 15 minutes after sunrise.
     */

    if (isMorningOn && !isOn && (curMinOfDay == 390)){
      digitalWrite(LED, HIGH);
      isOn = true;
      offTime = localSunrise + 15;
    }   
    delay(15000);
}
