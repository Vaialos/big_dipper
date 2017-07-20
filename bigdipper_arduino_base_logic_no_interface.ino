/* ====== "Big Dipper" control software v0.0 ======
 * Runs an automated loop of n cycles to raise/lower samples into/out of a bath of LN2. 
 * While in bath, waits for sample to hit low temp threshold.
 * While out of bath, provides power to heater until sample hits high temp threshold.
 * Breaks cycle if we lose readings, if we go overpressure, or if user hits the pause/abort button.
 *  
 * J. Acevedo & C. Flores for PIPER team; 19 Jul 2017
 * ================================================ */
// ==== LET'S GET IT STARTED ===============================
#include <string.h>;
#include <Wire.h>;
#include "Adafruit_ADS1015.h"
Adafruit_ADS1115 ads1115;       // construct an ads1115 at address 0x48  

// ==== VARS FROM THE INTERFACE ============================
int     cycles_desired        = 10;   //[#] (user-defined) Number of times we thermally cycle the sample.
int     cycles_begun          = 0;    //[#] (calculated)   Number of cycles we have started.
int     cycles_completed      = 0;    //[#] (calculated)   Number of cycles we have completed.
double  sample_pres_limit_hi  = 790;  //[torr] (user-defined) Threshold above which our sample should not go.
double  sample_temp_limit_hi  = 290;  //[K] (user-defined) Temperature that we warm the sample to during bake cycle.
double  sample_temp_limit_lo  = 80;   //[K] (user-defined) Temperature that we cool the sample to during cryo cycle.
double  heater_temp_target    = 300;  //[K] (user-defined) This should be close-to-but-more-than sample_temp_limit_hi. 
double  heater_temp_deviate   = 3;    //[K] (user-defined) Give us a dead band so the heater isn't fluttering open/shut.

// ==== DATA FROM SENSORS ==================================
//TODO reset the initializers to 0 before publishing; we are only initializing to non-zeros for the simulation
double  sample_pressure       = 760;   //[torr] Pressure inside sample chamber, read from ??? inside sample chamber.
double  sample_temp           = 285;   //[K]    Temperature inside sample chamber, read from diode inside sample chamber.
double  heater_temp           = 290;   //[K]    Temperature measured by a diode on outside of sample chamber.
double  time_elapsed          = 0;   //[s]    Read from ChronDot Real-Time Clock

// ==== ENUMERATE PIN #s etc. WITH HUMAN-READABLE NAMES ====
int const HEATER    = 2; //Pin that controls the heater-controlling relay.
int const PISTON    = 3; //Pin that controls air which raises piston. Powering it lowers the piston.
//  enum piston_status  = {down = -2, moving_down = -1, unused_middle = 0, moving_up = 1, up = 2}; //Piston can be in one of four states.
int piston_status   = 2; /* PISTON STATUS -2 == down, -1 == moving down, 0 == unused, 1 == moving up, 2 == up */    
int const OFF       = 0;
int const ON        = 1;
            
// ==== OTHER HELPFUL VARIABLES AND CONSTS ==================
int const room_temp = 296;    //[K] Only used for our 'physics simulator'.
String cycle_status = "bake"; //    Only used for our 'physics simulator'.
String heater_mode = "off"; //    Only used for our 'physics simulator'.

  
void setup() 
{
  // General startup.
  Serial.begin(9600);
  ads1115.begin();                      // Initialize ads1115 (16Bit ADC + PGA chip)
  
  // Define default behavior of pins.
  pinMode(HEATER, OUTPUT);
}

void loop() 
{
  displaySimpleInterface();
  if (cycle_status == "bake")
  {
    if (sample_temp < sample_temp_limit_hi)
    {
      bakeTheSample();
    }
    else
    {
      checkIfDone(); 
      moveThePistonDown();
      coolTheSample();
    } 
  }
  if (cycle_status == "cool")
  {
    if (sample_temp > sample_temp_limit_lo)
    {  
      coolTheSample();
    }
    else
    {
      moveThePistonUp();
      bakeTheSample(); 
    }
  }
  delay(1000);
  simulatePhysics();
}

void checkIfDone()
// This function counts the number of cycles we've been through and stops the program if we are done.
{
   Serial.println("Updating cycle numbers.");
   if (cycles_begun > 0)
    {
      cycles_completed++;
      //TODO will need to add in a write call to update the LED board
    }
    cycles_begun++;
    if (cycles_completed >= cycles_desired)
    {
      Serial.println("PROGRAM COMPLETED.");
      exit(0);
    }
}

void displaySimpleInterface()
{
  Serial.print("[");Serial.print(cycles_completed); Serial.print("/");Serial.print(cycles_desired);Serial.print(" cycles]\t");
  Serial.print("[Sample: "); Serial.print(sample_pressure); Serial.print(" torr, "); Serial.print(sample_temp); Serial.print(" K]\t");
  Serial.print("[Heater: "); Serial.print(heater_mode);Serial.print("  ");Serial.print(heater_temp); Serial.print(" K]\t");
  Serial.print("[Piston: "); 
    if (piston_status == -2) 
      { Serial.print("DOWN ]\t"); }
    if (piston_status == 2) 
      { Serial.print("~UP~ ]\t"); }
  Serial.print("[Process: "); Serial.print(cycle_status);Serial.println("]");
  { }
}

void moveThePistonDown()
// Check to make sure the piston isn't already down, or isn't moving.
// If it isn't down/moving, move the piston and wait X seconds.
{
  // PISTON STATUS: -2 == down, 2 == up
  Serial.println("Trying to move the piston down.");
  if ( (piston_status == -2) || (piston_status == -1) || (piston_status == 1))
  { }
  else 
  {
    digitalWrite(PISTON, ON);
    piston_status = -2; //DOWN
  }
}

void moveThePistonUp()
// Check to make sure the piston isn't already up, or isn't moving.
{
  // PISTON STATUS: -2 == down,  2 == up
  Serial.println("Trying to move the piston up.");
  if ( (piston_status == 2) || (piston_status == -1) || (piston_status == 1))
  { }
  else 
  {
    digitalWrite(PISTON, OFF);
    piston_status = 2; //UP
  }
}

void bakeTheSample()
//bakeTheSample turns the heater on until it hits a certain temperature.
{
    cycle_status = "bake";
    //Serial.println("Baking the sample."); 
    heaterControl();
    if (sample_temp >= sample_temp_limit_hi)
    {
      moveThePistonDown();
      cycle_status = "cool";
    }
}

void coolTheSample()
//Keeps the sample in LN2 (and makes extra sure the heater stays off) until we cool below a certain threshold.
{
    //Serial.println("Cooling the sample.");
    digitalWrite(HEATER, OFF);
    heater_mode = "off";
    cycle_status = "cool";
    if (sample_temp < sample_temp_limit_lo)
    {
      moveThePistonUp();
      cycle_status = "bake";
    }
}

void heaterControl()
//Regulates the heater and cuts it off when it gets too hot.
{
  int heater_temp_plus_delta  = heater_temp_target + heater_temp_deviate;
  int heater_temp_minus_delta = heater_temp_target - heater_temp_deviate;
  if (cycle_status == "bake")
  {
    if (heater_temp < heater_temp_minus_delta)
    {
      digitalWrite(HEATER, ON);
      heater_mode = "on";
    }
  }
  if ( (heater_temp >= heater_temp_plus_delta) || (cycle_status == "cool") )
  {
     //Serial.println("Heater getting too hot; switching it off.");
     digitalWrite(HEATER, OFF);
     heater_mode = "off";
  }
}
  
void simulatePhysics()
{
  heater_temp = diffuseHeater(heater_temp); //Heater approaches room temperature incrementally, unless it is on.
  double temp_difference = 0;
  if (cycle_status == "cool") 
  {
    //Simulate sample temp approaching LN2 temp (77K, but I'm setting it to 70 to speed testing time).
    if (sample_temp > 70)
    {
      temp_difference = sample_temp - 70;
      sample_temp = sample_temp - (temp_difference * 0.1);
    }
  }
  if (cycle_status == "bake") 
  {
    //Simulate sample temp approaching heater temp.
    if (sample_temp < heater_temp)
    {
      temp_difference = heater_temp - sample_temp;
      sample_temp = sample_temp + (temp_difference * 0.05);
    }
  }  
}

double diffuseHeater( double undiffused_heat )
//Uses Zeno's law of cooling because there's no need to implement Newton's, even though I want to. -JEA
{
  double temp; //temporary variable for temp of heater
  double temp_difference = 0;
  
  if (heater_mode == "off")
  {
    if (undiffused_heat > room_temp)
    {
       temp_difference = undiffused_heat - room_temp;
       temp = undiffused_heat - (temp_difference * 0.05);
    }
    if (undiffused_heat < room_temp)
    {
       temp_difference = room_temp - undiffused_heat;
       temp = undiffused_heat + (temp_difference * 0.05);
    }
  }
  if (heater_mode == "on")
  {

     temp = undiffused_heat + 1; //Arbitrary amount of heating power
  }
  return temp;
}

