#include <EasyNextionLibrary.h>
#include <HardwareSerial.h>

//Initialize nextion display object on serial 2, pins 16 and 17 on esp32
HardwareSerial mySerial(2);
EasyNex myNex(mySerial);

//Create global variables to utalize throughout program
int tempGoal = 70; //Default to 70
volatile int state = 0;

//Create three "triggers" that will fire when a specific string is received from nextion display
//
// Trigger occurs when toggleState button is released
// Sent from nextion by command printh 23 02 54 00
// Will toggle the program state to enter automatic or manual control mode
//
void trigger0()
{
  state = !state;
}

//
// Trigger occurs when downTemp button is released
// Sent from nextion by command printh 23 02 54 01
// Will decrement global var tempGoal
//
void trigger1()
{
   tempGoal-=1;
}

//
// Trigger occurs when upTemp button is released
// Sent from nextion by command printh 23 02 54 02
// Will increment global var tempGoal
//
void trigger2()
{
  tempGoal+=1;
}
