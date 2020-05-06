#include <can.h>
#include <mcp2515.h>
#include <SPI.h>

//comment to enable can
#define PROTO

//define pinout of transistors
#define TRANSISTOR_START_1 2  
#define TRANSISTOR_START_2 3  
#define TRANSISTOR_BRAKE 4
#define TRANSISTOR_KEY_POWER 5
#define TRANSISTOR_KEY_LOCK 6
#define TRANSISTOR_KEY_UNLOCK 7

#define PROTO_BUTTON_LOCK 10
#define PROTO_BUTTON_BRAKE 11
#define PROTO_BUTTON_KEY 12
#define PROTO_BUTTON_ENGINE_STATUS 13

//5 seconds of preheating
#define PREHEATING_DURATION 5000 

//booleans keeps track of statuses
bool status_engine_running = false;
bool status_lock_button = false;
bool status_key_present = false;
bool status_brake_light = false;

bool engine_preheating_1 = false;
bool engine_preheating_2 = false;
bool engine_preheating_3 = false;
unsigned long preheating_time = 0;
bool engine_start = false;
bool engine_stop = false;
bool remote_start = false;

//timing data
int lock_in_a_row = 0;
bool wait_for_lock_release = false;
unsigned long last_lock_detected_time = 0;
unsigned long cur_lock_detected_time = 0;

unsigned long transistor_hold_time = 0;
#define TRANSISTOR_HOLD_DURATION 500

//can library data
struct can_frame canMsg;
MCP2515 mcp2515(10);




/* CAN CODES
 * BREAK LIGHT
 * 0x21A : data: first bit of Byte[0] is 1 -> break light is on
 * 
 * KEYFOB 
 * 0x23A : data: 00 30 04 60 ->  lock button pressed
 * 0x23A : data: 00 30 00 60 ->  lock button released
 * 
 * ENGINE STATUS
 * 0x130 : data: Byte[0] = 0x45 -> if engine is running
 * 0x130 : data: Byte[1] = 0x40 -> key detected
 * 0x130 : data: Byte[1] = 0x00 -> no key 
 */


void can_updateStatus()
{
  if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK) {
    Serial.print(canMsg.can_id, HEX); // print ID
    Serial.print(" "); 
    Serial.print(canMsg.can_dlc, HEX); // print DLC
    Serial.print(" ");
    
    for (int i = 0; i < canMsg.can_dlc; i++)  {  // print the data
      Serial.print(canMsg.data[i], HEX);
      Serial.print(" ");
    }

    Serial.println(); 
  
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
      if(   (canMsg.data[0] == 0x00)
         && (canMsg.data[1] == 0x30)
         && (canMsg.data[2] == 0x04)
         && (canMsg.data[3] == 0x60)) //data = 00 30 04 60 ->  lock button pressed
       {
         Serial.println("Lock button pressed"); 
         status_lock_button = true;
       }
      else if((canMsg.data[0] == 0x00)
         && (canMsg.data[1] == 0x30)
         && (canMsg.data[2] == 0x00)
         && (canMsg.data[3] == 0x60)) //data = 00 30 00 60 ->  lock button released
       {
         Serial.println("Lock button released"); 
         status_lock_button = false;
       }
    }
    else if(canMsg.can_id == 0x130)  //can id for engine status
    {
      //engine running status
      if((canMsg.data[0] == 0x45)) //data: Byte[0] = 0x45 -> if engine is running
       {
         Serial.println("Engine is running"); 
         status_engine_running = true;
       }
       else
       {
         Serial.println("Engine is NOT running"); 
         status_engine_running = false;
         remote_start = false;
       }

       //key present status
       if((canMsg.data[1] == 0x40)) //data: Byte[1] = 0x40 -> key detected
       {
         Serial.println("Key detected"); 
         status_engine_running = true;
       }
       else if((canMsg.data[1] == 0x00)) //data: Byte[1] = 0x00 -> key not detected   
       {
         Serial.println("Key NOT detected"); 
         status_engine_running = false;
       }
    }
  }
}

void can_setup()
{
  // uses can lib default pin mode 
  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS);
  mcp2515.setNormalMode();

  //init the masks
  mcp2515.setFilterMask(MCP2515::MASK0, 0, 0x7ff);
  mcp2515.setFilterMask(MCP2515::MASK1, 0, 0x7ff);
  
  //init the filters
  mcp2515.setFilter(MCP2515::RXF0, 0, 0x21A);
  mcp2515.setFilter(MCP2515::RXF1, 0, 0x23A);
  mcp2515.setFilter(MCP2515::RXF2, 0, 0x130);
}


void proto_setup()
{
  pinMode(PROTO_BUTTON_LOCK, INPUT);
  pinMode(PROTO_BUTTON_BRAKE, INPUT);
  pinMode(PROTO_BUTTON_KEY, INPUT);
  pinMode(PROTO_BUTTON_ENGINE_STATUS, INPUT);
}

void proto_updateStatus()
{
  status_engine_running = digitalRead(PROTO_BUTTON_ENGINE_STATUS) == HIGH;
  if(!status_engine_running)
             remote_start = false;
  status_lock_button = digitalRead(PROTO_BUTTON_LOCK) == HIGH;
  status_key_present = digitalRead(PROTO_BUTTON_KEY) == HIGH;
  status_brake_light = digitalRead(PROTO_BUTTON_BRAKE) == HIGH;
}

void engine_do_preheating_1()
{
  //open the car (hold button on keyfob)
  if(transistor_hold_time == 0)
  {
    digitalWrite(TRANSISTOR_KEY_POWER, HIGH);
    digitalWrite(TRANSISTOR_KEY_UNLOCK, HIGH);
    
    transistor_hold_time = millis();
    Serial.print("car opening\n");
  }
  else if(millis() - transistor_hold_time > TRANSISTOR_HOLD_DURATION) //stop holding button after some time
  {
    digitalWrite(TRANSISTOR_KEY_UNLOCK, LOW);

    engine_preheating_1 = false;
    engine_preheating_2 = true;
    
    transistor_hold_time = 0;
    Serial.print("car opened\n");
  }
}


void engine_do_preheating_2()
{
  //turn on the car (hold button)
  if(transistor_hold_time == 0)
  {
    digitalWrite(TRANSISTOR_START_1, HIGH);
    digitalWrite(TRANSISTOR_START_2, HIGH);
    
    transistor_hold_time = millis();
    Serial.print("car preheating starting\n");
  }
  else if(millis() - transistor_hold_time > TRANSISTOR_HOLD_DURATION) //stop holding button after some time
  {
    digitalWrite(TRANSISTOR_START_1, LOW);
    digitalWrite(TRANSISTOR_START_2, LOW);

    engine_preheating_2 = false;
    engine_preheating_3 = true;
    
    transistor_hold_time = 0;
    Serial.print("car preheating started\n");
  }
}


void engine_do_preheating_3()
{
  //close the car
  if(preheating_time == 0) 
  {
    if(transistor_hold_time == 0)          //start holding the transistor
    {
      digitalWrite(TRANSISTOR_KEY_LOCK, HIGH);
      
      transistor_hold_time = millis();
      Serial.print("car closing\n");
    }
    else if(millis() - transistor_hold_time > TRANSISTOR_HOLD_DURATION)  //stop holding transistors
    {
      digitalWrite(TRANSISTOR_KEY_LOCK, LOW);
      preheating_time = millis();
      
      transistor_hold_time = 0;
      Serial.print("car closed\n");
    }
  }
  else if(millis() - preheating_time > PREHEATING_DURATION) //stop preheating and start the engine
  {
    engine_preheating_3 = false;
    engine_start = true;
  }
}

void engine_do_start()
{
  //starting engine (begin holding buttons)
  if(transistor_hold_time == 0)
  {
    digitalWrite(TRANSISTOR_BRAKE, HIGH);
    digitalWrite(TRANSISTOR_START_1, HIGH);
    digitalWrite(TRANSISTOR_START_2, HIGH);
    
    transistor_hold_time = millis();
    Serial.print("engine starting\n");
  }
  else if(millis() - transistor_hold_time > TRANSISTOR_HOLD_DURATION) //stop holding buttons after some time
  {
    engine_start = false;
    digitalWrite(TRANSISTOR_START_1, LOW);
    digitalWrite(TRANSISTOR_START_2, LOW);
    digitalWrite(TRANSISTOR_KEY_POWER, LOW);
    digitalWrite(TRANSISTOR_BRAKE, LOW);
    remote_start = true;
    
    transistor_hold_time = 0;
    Serial.print("engine started\n");
  }
}

void engine_do_stop()
{
  if(transistor_hold_time == 0) //start holding the transistor
  {
    digitalWrite(TRANSISTOR_START_1, HIGH);
    digitalWrite(TRANSISTOR_START_2, HIGH);

    transistor_hold_time = millis();
    Serial.print("stopping engine\n");
  }
  else if(millis() - transistor_hold_time > TRANSISTOR_HOLD_DURATION)  //stop holding transistors
  {
    digitalWrite(TRANSISTOR_START_1, LOW);
    digitalWrite(TRANSISTOR_START_2, LOW);
    transistor_hold_time = 0;
    engine_stop = false;

    Serial.print("engine stopped\n");
  }
}



void setup() {  
  Serial.begin(9600);
  Serial.print("[ BMW REMOTE START v1 ]\n");
  
  pinMode(TRANSISTOR_START_1, OUTPUT);       
  pinMode(TRANSISTOR_START_2, OUTPUT); 
  pinMode(TRANSISTOR_BRAKE, OUTPUT);
  pinMode(TRANSISTOR_KEY_POWER, OUTPUT);
  pinMode(TRANSISTOR_KEY_LOCK, OUTPUT);
  pinMode(TRANSISTOR_KEY_UNLOCK, OUTPUT);


#ifdef PROTO 
  proto_setup();
#else
  can_setup();
#endif
}  

 
void loop() {  

    //update status
#ifdef PROTO 
    proto_updateStatus();
#else
    can_updateStatus();
#endif

    //jreset the timer if timeout from previous lock
     if(millis() - last_lock_detected_time > 1000) 
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
      if(millis() - cur_lock_detected_time < 500) //if release happened in a short time (not an hold), count it as click in a row
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

      //use triple click action only if the key is not inside the car
      if(!status_key_present && !engine_preheating_1 && !engine_preheating_2 && !engine_preheating_3)
        if(!remote_start && !status_engine_running) //if not in remote start phase and the engine is not already running
        {
          //init preheating
          engine_preheating_1 = true;
          preheating_time = 0;
        }
        else
        {
          //engine stop
          engine_stop = true;
        }
    }
    
    //brake behavoir, if someone enter the remote started car
    if(remote_start && status_brake_light)
    {
      if(!status_key_present) //antitheft stop
      {
          //engine stop
          Serial.print("antitheft engine stop\n");
          engine_stop = true;
      }

      //out of remote start scope
      remote_start = false;

      Serial.print("no more remote scope\n");
    }

    //Now that everything is decided, do the things it has to do

    

    //engine preheating phase 1 (open car)
    if(engine_preheating_1)
      engine_do_preheating_1();

    //engine preheating phase 2 (turn on the car (NOT THE ENGINE))
    if(engine_preheating_2)
      engine_do_preheating_2();

    //engine preheating phase 3 (close the car)
    if(engine_preheating_3)
      engine_do_preheating_3();

    //engine start
    if(engine_start)
      engine_do_start();

    //engine preheating
    if(engine_stop)
      engine_do_stop();
    
    
    
}
