#include <can.h>
#include <mcp2515.h>
#include <SPI.h>

//relays use inverted logic
enum RELAY_STATUS {
    RELAY_HIGH = 0,
    RELAY_LOW = 1
};

//define pinout of transistors
enum RELAYS {
  RELAY_START_1 = 2,  
  RELAY_START_2 = 3,  
  RELAY_BRAKE = 4,
  RELAY_KEY_POWER = 5,
  RELAY_KEY_LOCK = 6,
  RELAY_KEY_UNLOCK = 7
};


//booleans keep track of statuses read from canbus
bool status_engine_running = false;
bool status_lock_button = false;
bool status_brake_light = false;

//engine actions data
bool engine_start = false;
bool engine_stop = false;
unsigned long engine_do_start_time = 0;
unsigned long engine_do_stop_time = 0;

//remote start data
bool remote_started = false;
unsigned long remote_start_time = 0; 

//timing data
int lock_in_a_row = 0;
bool wait_for_lock_release = false;
unsigned long last_lock_detected_time = 0;
unsigned long cur_lock_detected_time = 0;

//can library data
struct can_frame canMsg;
MCP2515 mcp2515(10);


/* CAN CODES
 * BREAK LIGHT
 * 0x21A : data: first bit of Byte[0] is 1 -> break light is on
 * 
 * KEYFOB 
 * 0x23A : data: xx F3 04 3F ->  lock button pressed
 * 0x23A : data: xx F3 01 3F ->  unlock button pressed
 * 0x23A : data: xx F3 00 3F ->  key button released
 * 
 * ENGINE STATUS
 * 0x0A5 : data: Byte[5] = 0x00 and Byte[5] = 0x00 -> if engine is NOT running
 */


void can_updateStatus()
{
  if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK) {
   /* Serial.print(canMsg.can_id, HEX); // print ID
    Serial.print(" "); 
    Serial.print(canMsg.can_dlc, HEX); // print DLC
    Serial.print(" ");
    
    for (int i = 0; i < canMsg.can_dlc; i++)  {  // print the data
      Serial.print(canMsg.data[i], HEX);
      Serial.print(" ");
    }

    Serial.println(); 
  */
    //doing the check!
    if(canMsg.can_id == 0x21A)       //can id for brake light
    {
      if (canMsg.data[0] & 0x80)     //first bit of first byte is 1
      {
        Serial.println("Brake on"); 
        status_brake_light = true;
      }
      else
      {
        Serial.println("Brake off"); 
        status_brake_light = false;
      }
    }
    else if(canMsg.can_id == 0x23A)  //can id for keyfob
    {
      if(   (canMsg.data[1] == 0xF3)
         && (canMsg.data[2] == 0x04)
         && (canMsg.data[3] == 0x3F)) //data = 11 F3 04 3F ->  lock button pressed
       {
         Serial.println("Lock button pressed"); 
         status_lock_button = true;
       }
      else if((canMsg.data[1] == 0xF3)
         && (canMsg.data[2] == 0x01)
         && (canMsg.data[3] == 0x3F)) //data = 11 F3 01 3F ->  unlock button pressed
       {
         Serial.println("Unlock button pressed, out of remote start scope"); 
         remote_started = false;
       }
      else if((canMsg.data[1] == 0xF3)
         && (canMsg.data[2] == 0x00)
         && (canMsg.data[3] == 0x3F)) //data = 11 F3 00 3F ->  key button released
       {
         Serial.println("Key button released"); 
         status_lock_button = false;
       }
      
    }
    else if(canMsg.can_id == 0x0A5)  //can id for engine status
    {
      //engine running status
      if((canMsg.data[5] == 0x00 &&
          canMsg.data[6] == 0x00)) //data: Byte[5] = 0x00 and Byte[5] = 0x00 -> if engine is NOT running
       {
         //Serial.println("Engine is NOT running"); 
         status_engine_running = false;

         
         //give one second to update canbus
         if(remote_started && (millis() - remote_start_time > 1000))
            remote_started = false;
       }
       else
       {
         Serial.println("Engine is running"); 
         status_engine_running = true;
       }
    }
  }
}

void can_setup()
{
  //reset mcp2515 and set can at 500KBPS and MCP to 8MHz quartz
  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);

  //init the mask
  mcp2515.setFilterMask(MCP2515::MASK0, 0, 0x3ff);
  mcp2515.setFilterMask(MCP2515::MASK1, 0, 0x3ff);

  //init the filters
  mcp2515.setFilter(MCP2515::RXF0, 0, 0x21A); //break light
  mcp2515.setFilter(MCP2515::RXF1, 0, 0x23A); //keyfob
  mcp2515.setFilter(MCP2515::RXF2, 0, 0x0A5); //rpm

  //start the sniffing
  mcp2515.setNormalMode();
  
}


/* ENGINE START:
 * 1) Turn on the in-car key (100 ms)
 * 2) Press lock button (300 ms)
 * 3) Release lock button and wait (400 ms)
 * 4) Start holding the brake and wait (5 sec)
 * 5) Press the start button (2 sec)
 * 6) Release the start button and wait (1 sec)
 * 7) Release the brake and wait (1 sec)
 * 8) Turn off the in-car key (1 sec)
 */

void engine_do_start()
{
  if(engine_do_start_time == 0)
  {
   engine_do_start_time = millis();
   Serial.println("*** Engine starting ***");
  }
  
  //divided in phases:
  //KEY_POWER_ON - 100 ms
  if(millis() - engine_do_start_time < 100)
  {
    digitalWrite(RELAY_KEY_POWER, RELAY_HIGH);
    Serial.println("1) Turn on the in-car key (100 ms)");
  }
  //KEY_LOCK - 300 ms
  else if(millis() - engine_do_start_time < 400)
  {
    digitalWrite(RELAY_KEY_LOCK, RELAY_HIGH);
    Serial.println("2) Press lock button (300 ms)");
  }
  //KEY_LOCK_RELEASE - 400 ms
  else if(millis() - engine_do_start_time < 800)
  {
    digitalWrite(RELAY_KEY_LOCK, RELAY_LOW);
    Serial.println("3) Release lock button and wait (400 ms)");
  }
  //BRAKE - 100 ms
  else if(millis() - engine_do_start_time < 2800)
  {
    digitalWrite(RELAY_BRAKE, RELAY_HIGH);
    Serial.println("8) Start holding the brake and wait (2000 ms)");
  }
  //START - 1500 ms
  else if(millis() - engine_do_start_time < 4300)
  {
    digitalWrite(RELAY_START_1, RELAY_HIGH);
    digitalWrite(RELAY_START_2, RELAY_HIGH);
    Serial.println("9) Press the start button (1500 ms)");
  }
  //START_RELEASE - 100 ms
  else if(millis() - engine_do_start_time < 4400)
  {
    digitalWrite(RELAY_START_1, RELAY_LOW);
    digitalWrite(RELAY_START_2, RELAY_LOW);
    Serial.println("10) Release the start button and wait (100 ms)");
  }
  //BRAKE_RELEASE - 200 ms
  else if(millis() - engine_do_start_time < 4600)
  {
    digitalWrite(RELAY_BRAKE, RELAY_LOW);
    Serial.println("11) Release the brake and wait (200 ms)");
  }
  //KEY_POWER_RELEASE - 100 ms
  else if(millis() - engine_do_start_time < 4700)
  {
    digitalWrite(RELAY_KEY_POWER, RELAY_LOW);
    Serial.println("12) Turn off the in-car key (100 ms)");
  }
  //ENGINE STARTED
  else
  {
    remote_started = true;
    remote_start_time = millis();
    engine_do_start_time = 0;
    engine_start = false;

    Serial.println("*** Engine started ***");
  }
}
  


/* ENGINE STOP:
 * 1) Press start button (700 ms)
 * 2) Release the start button and wait (100 ms)
 */

void engine_do_stop()
{
  if(engine_do_stop_time == 0)
  {
    engine_do_stop_time = millis();
    Serial.println("*** Engine stopping ***");
  }

  //START - 700 ms
  if(millis() - engine_do_stop_time < 700)
  {
    digitalWrite(RELAY_START_1, RELAY_HIGH);
    digitalWrite(RELAY_START_2, RELAY_HIGH);
    Serial.println("1) Press start button (700 ms)");
  }
  //START_RELEASE - 1 sec
  else if(millis() - engine_do_stop_time < 800)
  {
    digitalWrite(RELAY_START_1, RELAY_LOW);
    digitalWrite(RELAY_START_2, RELAY_LOW);
    Serial.println("2) Release the start button (100 ms)");
  }
  //ENGINE STOPPED
  else
  {
    engine_do_stop_time = 0;
    engine_stop = false;
    remote_started = false;
    Serial.println("*** Engine stopped ***");
  }
}



void setup() {  
  Serial.begin(9600);
  Serial.println("[ BMW REMOTE START v1 ]");
  
  pinMode(RELAY_START_1, OUTPUT);       
  pinMode(RELAY_START_2, OUTPUT); 
  pinMode(RELAY_BRAKE, OUTPUT);
  pinMode(RELAY_KEY_POWER, OUTPUT);
  pinMode(RELAY_KEY_LOCK, OUTPUT);
  pinMode(RELAY_KEY_UNLOCK, OUTPUT);

  digitalWrite(RELAY_START_1, RELAY_LOW);
  digitalWrite(RELAY_START_2, RELAY_LOW);
  digitalWrite(RELAY_BRAKE, RELAY_LOW);
  digitalWrite(RELAY_KEY_POWER, RELAY_LOW);
  digitalWrite(RELAY_KEY_LOCK, RELAY_LOW);
  digitalWrite(RELAY_KEY_UNLOCK, RELAY_LOW);

  //setup MCP2515
  can_setup();
}  
 
void loop() {  

    //update status
    can_updateStatus();


    //reset the timer if timeout from previous lock
     if(millis() - last_lock_detected_time > 1500) 
      lock_in_a_row = 0;

    //working on the triple lock click thing
    if(!wait_for_lock_release && status_lock_button) //lock clicked for first time
    {
      cur_lock_detected_time = millis(); //I save the lock button pressed
      wait_for_lock_release = true;
    }
    else if(wait_for_lock_release && !status_lock_button) //lock released for first time
    {
      wait_for_lock_release = false;
      if(millis() - cur_lock_detected_time < 1000) //if release happened in a short time (not an hold), count it as click in a row
      {
        lock_in_a_row++;
        last_lock_detected_time = millis(); //store the lock click detected time

        Serial.print("lock detected: ");
        Serial.print(lock_in_a_row, DEC);
        Serial.print("/3\n");
      }
    }

    //at third lock detected
    if(lock_in_a_row == 3) //if we reached the number we wanted
    {
      lock_in_a_row = 0;
      last_lock_detected_time = 0;

      //use triple click action only if it's not already turning on or off
      if(!engine_start && !engine_stop)
        if(!remote_started && !status_engine_running) //if not in remote start phase and the engine is not already running
        {
          //init preheating
          engine_start = true;
        }
        else
        {
          //engine stop
          engine_stop = true;
        }
    }
    
    /* --- EMERGENCY STOP ---
    * if you enter the remote started car (1 sec bonus for canbus to update), 
    * without unlocking the car (eg. a thief trying to steal the car)
    * and press the brake, exit remote start scope
    */
    if(remote_started && (millis() - remote_start_time > 2000) && status_brake_light)
    {
      //out of remote start scope
      Serial.println("** EMERGENCY ANTI-THIEF STOP **");
      engine_stop = true;
      remote_started = false;
    }

    //remote start timeout
    //car in remote start shutsoff after 15 minutes
    if(remote_started && (millis() - remote_start_time > (unsigned long) 1000 * 60 * 15))
    {
      //engine stop
      Serial.println("** TIMEOUT STOP **");
      engine_stop = true;
      remote_started = false;
    }

    //Now that everything is decided, do the things it has to do

    //engine start
    if(engine_start)
      engine_do_start();

    //engine preheating
    if(engine_stop)
      engine_do_stop();
    
    
    
}
