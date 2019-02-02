/************************************************************************************************************************** 
Plays a single-octave, diatonic toy xylophone.
Sensor to Arduino Uno connections: pin 9=xylophone SS; pin 10=mallet SS; pin 11=MOSI; pin 12=MISO; pin 13=SCLK.
Sensors are "factory zeroed" so the xylophone sensor 0 = key 1 and the mallet sensor 0 is with the mallet touching the key.
The xylophone sensor angle increases for higher key numbers (lower notes); Mallet angle more negative rotating away from key.
200 steps/rev. stepper motors; driver connections: pin 2=xylophone step; pin 3=dir; pin 4=mallet step; pin 5=enable.
Song selection switch on A0-A1; Play switch on A5 (ON=Play; OFF=Mute).
72-LED NeoPixel array (a 144 cut in half) on pin 7; LED #0 over key #1.
NVE Corporation (email: sensor-apps@nve.com), rev. 1/22/19
***************************************************************************************************************************/
#include <SPI.h>
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel strip = Adafruit_NeoPixel(72, 7, NEO_GRB + NEO_KHZ800); //72 LEDs on pin 7
const char melody[][52]={ //Key 1 is the highest note; key 8 the lowest in the single-octave range
//"Pop Goes the Weasel" melody notes
{8,8,7,7,6,4,6,8,8,8,8,7,7,6,8,8, 
 8,8,7,7,6,4,6,8,3,7,5,6,8,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0},

//"Brahms' Lullaby":
{6,6,4,6,6,4,6,4, //first line of 3 bars
 1,2,3,3,4,7,6,5,7,7,6, //Next line (3 bars)
 5,3,5,2,3,4,2,1,8,8, 
 1,3,5,4,6,8,5,4,3, 
 4,8,8,1,3,5,4,6,8, 
 5,6,7,8,0},

//"Camptown Races" melody:
{4,4,6,4,3,4,6,6,7,6,7,4,4,6,4,
 3,4,6,7,6,7,8,8,8,6,4,1,3,3,1,3,
 4,4,4,4,6,6,4,4,3,4,6,7,6,5,6,7,7,8,
 0,0,0}
};

const char duration[][52]={ //Duration in beats
//"Pop Goes the Weasel" durations
{2,1,2,1,1,1,1,2,1,2,1,2,1,3,2,1, 
 2,1,2,1,1,1,1,3,3,2,1,3,2,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0},

//"Brahms' Lullaby":
{1,1,4,1,1,4,1,1, 
 2,2,2,2,2,1,1,2,2,1,1,
 4,1,1,1,1,2,2,4,1,1, 
 4,1,1,4,1,1,2,2,2, 
 4,1,1,4,1,1,4,1,1, 
 2,2,2,4,0},

//"Camptown Races" (4/4 time; durations in 8ths):
{2,2,2,2,2,2,4,2,6,2,6,2,2,2,2,
 2,2,4,4,2,2,8,3,1,2,2,8,3,1,2,2,
 6,2,2,2,1,1,1,1,2,2,4,1,2,1,2,1,1,8,
 0,0,0}
};

const char keyColor[9][3]={  //LED RGB colors to match keys
{0,    0,   0}, //Key 0 (no key #0--dummy values)
{128,  0,   0}, //Key 1 (red)
{100, 20,   0}, //Key 2 (orange)
{64,  64,   0}, //Key 3 (yellow)
{0,  128,   0}, //Key 4 (green)
{0,   84,  44}, //Key 5 (cyan)
{0,    0, 128}, //Key 6 (blue)
{60,   0,  68}, //Key 7 (purple)
{30,   0,  88}};//Key 8 (dark purple)

const int tempo[]={400, 200, 500}; //BPM; assumes x/8 time signature; 8th note = 1 beat; length (ms) = duration*60000/tempo)
const int angles[] = {0, 0, 210, 430, 680, 860, 1100, 1330, 1550}; //Sensor angles for each xylophone key (tenths of a degree)
int malletTravel=35; //Retracted mallet angle (tenths of a degree)
int malletOvertravel=-10; //Mallet under- or over-travel (tenths of a degree; >0 ==> overtravel)

char song; //Song number (set by selector switch)
char songOld; //Previous song switch setting
char note; //Note number
int angle; //Measured angles
int stepCycle; //Xylophone motor step half-cycle time (microseconds)
int noteDelay; //Mallet strike delay to allow note durations (milliseconds)
long lastTime; //Mallet strike time used to calculate when to strike next note (milliseconds since startup)
char LEDn; //LED index number (0-71; 0 is over key #1)
char n; //LED index relative to mallet (0 = over mallet)

void setup() {
DDRD |= 0xFC; //Set stepper driver and LED array pins (pins 2 - 7) as outputs
DDRB |= 6; //Set sensor SS pins (pins 9 & 10) as outputs (other SPI pins are setup in SPI.h)  
digitalWrite(9, HIGH); //Disable sensors
digitalWrite(10, HIGH);
pinMode(A0, INPUT_PULLUP); //Initialize song select switch inputs (A0=LOW ==> song 1;
pinMode(A1, INPUT_PULLUP); //      A1=LOW ==> song 2; A0 and A1 both HIGH ==> song 0).
pinMode(A5, INPUT_PULLUP); //Initialize Play/Mute switch input (Mute = switch off ==> HIGH)
strip.begin(); //Intitialize LED strip
strip.setBrightness(100); // 0 = off-->255 = maximum brightness
SPI.begin ();
SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0)); //Initialize SPI; 2 Mbits/s 
}
void loop() {
//Home the mallet
digitalWrite(10, LOW); //Enable the mallet sensor
digitalWrite(3, -getAngle() > malletTravel); //Set direction (TRUE = down)
while((getAngle()+malletTravel)*((digitalRead(3)<<1)-1)<0) {
delay(4); //Step time
digitalWrite(4, !digitalRead(4));
}
note=0; 
song=3-digitalRead(A0)-(digitalRead(A1)<<1); //Intial song switch reading
while(melody[song][note] > 0) { //Play notes in sequence; "0" indicates song end
song=3-digitalRead(A0)-(digitalRead(A1)<<1); //Poll the song switch
if(song!=songOld) note=0; //Start from the beginning if the song has changed
songOld=song;

//Prepare to move to the melody key
digitalWrite(10, HIGH); //Disable mallet sensor
digitalWrite(9, LOW); //Enable xylophone sensor
angle = getAngle(); //Get current xylophone angle
digitalWrite(3, angle > angles[melody[song][note]]); //Set motor rotation direction (0 = increasing angle ==> CCW; 1 ==> CW)

//Step to the new key if it's the first note or the note has changed
stepCycle=10000; //Minimum stepper speed (max. cycle time)
while(digitalRead(3) == (angle > angles[melody[song][note]]) && ((melody[song][note]!=melody[song][note-1]) || (note==0))) { 
delayMicroseconds (stepCycle); //Stepper half-cycle (accelerates with successive pulses)
song=3-digitalRead(A0)-(digitalRead(A1)<<1); //Read the song switch
digitalWrite(2, !digitalRead(2)); //Step pulse
angle = getAngle(); 
stepCycle-=1000; //Accelerate the stepper
if (stepCycle<10) stepCycle=10; //Maximum stepper speed (+program loop overhead)
strip.clear(); //Reset the LED array
for(n=-1; n <= 1; n++) { //Sequence through the LEDs surrounding the mallet
LEDn=(angle<<4)/344+n; //Calculation for the LEDs to follow the sensor
if (LEDn<0) LEDn=0; //Correct out-of-range
if (LEDn>71) break;
strip.setPixelColor(LEDn, 11*(4-3*abs(n)), 11*(4-3*abs(n)), 11*(4-3*abs(n))); //3 white LEDs; center brighter for arrow effect
}
strip.show();
}
digitalWrite(9, HIGH); //Disable xylophone sensor
//Mallet strike
digitalWrite(10, LOW); //Enable mallet sensor
digitalWrite(3, 1); //Set direction (1 = down for mallet strike)
//Select LEDs to highlight the active key
strip.clear(); //Reset the LED array
for(n=-1; n <= 1; n++) { //Sequence through the three LEDs over the active key
LEDn=(angles[melody[song][note]]<<4)/344+n;
if (LEDn<0) LEDn=0; //Correct out-of-range
if (LEDn>71) break;
strip.setPixelColor(LEDn, keyColor[melody[song][note]][0], keyColor[melody[song][note]][1], keyColor[melody[song][note]][2]);
}
strip.show(); //Light LEDs over the active key with the color of the key
noteDelay=lastTime-millis()+60000/tempo[song]*duration[song][note-1]; //Calculate delay for previous note duration
if((noteDelay>0) && (note>0)) delay(noteDelay); //Wait until it's time to strike the key
lastTime=millis(); //Save time of this key strike
while(getAngle() < malletOvertravel-digitalRead(A5)*malletTravel) { //Reduce mallet travel if Play switch off (A5 = 1)
delay(3+(digitalRead(A5)<<4)); //Stepper half-cycle time (slow down if muted to avoid overtravel)
digitalWrite(4, !digitalRead(4)); //Step pulse
}
digitalWrite(3, 0); //Reverse direction to retract mallet (0 = up)
while(-getAngle() < malletTravel) { //Retract mallet
delay(3); //Step time
digitalWrite(4, !digitalRead(4)); }
note++; //Next note
}
delay (5000); //Delay before repeating the song
}
int getAngle(){ //Procedure to read the angle from the selected ASR002 sensor
int angle;
SPI.transfer (0); //Send 0 for address angle
delayMicroseconds (3); //Allow time between address bytes
SPI.transfer (0); //2nd address byte (0 for read)
delayMicroseconds (10); //Allow time for data
angle = (SPI.transfer (0))<<8; //Send 0 for address angle; receive angle MSB
delayMicroseconds (3); //Allow time between address bytes
angle |= SPI.transfer (0); //2nd address byte (0 for read); receive angle LSB
if (angle>1800) angle-=3600; //Correct angle to avoid 0/360 discontinuity
return angle;
}
