#include <Wire.h>
#include "Adafruit_MCP23017.h"

#define MAX_INPUTS  32
#define MAX_DEBOUNCE_TIME 150L
#define RECEIVED_MESSAGE_TIMEOUT  200L
#define QUEUE_SIZE  200
#define MESSAGE_LENGTH  10
#define SOM '<'
#define EOM '>'

// #define DEBUG

#ifdef DEBUG
  #define DEBUG_print(x)    Serial.print(x)
  #define DEBUG_println(x)  Serial.println(x)
#else
  #define DEBUG_print(x)
  #define DEBUG_println(x)
#endif

Adafruit_MCP23017 mcp1;
Adafruit_MCP23017 mcp2;

bool InitialOutput;
bool InsideMessage;

char BoardAddress;

unsigned char ThisStatus[MAX_INPUTS];
unsigned char LastStatus[MAX_INPUTS];
unsigned char SentStatus[MAX_INPUTS];
long          lastDebounceTime[MAX_INPUTS];
long          lastReceivedCharTimeout;
int           QueueHead;
int           QueueTail;
char          OutputQueue[QUEUE_SIZE];


void SendStatus(int Input, int Status)
{
char Message[MESSAGE_LENGTH];
int  i;

  Message[0] = SOM;
  if (Status == 1)
    Message[1] = 'U';
  else
    Message[1] = 'S';
  Message[2] = BoardAddress;
  Message[3] = (Input / 10) + '0';
  Message[4] = (Input % 10) + '0';
  Message[5] = ' ';
  Message[6] = ' ';
  Message[7] = ' ';
  Message[8] = ' ';
  Message[9] = EOM;

  for (i=0; i<MESSAGE_LENGTH; i++)
  {
    OutputQueue[QueueHead] = Message[i];
    QueueHead = (QueueHead + 1) % QUEUE_SIZE;
  }
}

void ProcessOutputQueue()
{
  while(QueueTail != QueueHead)
  {
    Serial.write(OutputQueue[QueueTail]);
    QueueTail = (QueueTail + 1) % QUEUE_SIZE;
  }
}

void ProcessSerialData()
{
char ReadChar;

  if (Serial.available())
  {
    lastReceivedCharTimeout = millis()+ RECEIVED_MESSAGE_TIMEOUT;
    ReadChar = Serial.read();
    if (ReadChar == EOM)
      InsideMessage = false;
    else
      InsideMessage = true;

    Serial.write(ReadChar);
  }

  if (InsideMessage)
    if (millis() > lastReceivedCharTimeout)
      InsideMessage = false;

}

void setup() 
{
int   i;
long  sendNextStartupMessage;
bool  StillInitialising;

  mcp1.begin(0);      // first expander chip address is 0
  mcp2.begin(1);      // second expander chip address is 1

  Serial.begin(9600);

  // work out our board address from the dip switch/jumpers
  pinMode(5, INPUT);
  pinMode(6, INPUT);
  pinMode(7, INPUT);
  pinMode(8, INPUT);
  digitalWrite(5, HIGH);
  digitalWrite(6, HIGH);
  digitalWrite(7, HIGH);
  digitalWrite(8, HIGH);

  delay(1);

  BoardAddress = 0;

  if (!digitalRead(5))
    BoardAddress += 1;

  if (!digitalRead(6))
    BoardAddress += 2;

  if (!digitalRead(7))
    BoardAddress += 4;

  if (!digitalRead(8))
    BoardAddress += 8;

  if (BoardAddress > 9)
    BoardAddress = 9;
  
  BoardAddress += '0';

  for (i=0; i< MAX_INPUTS/2; i++)
  {
    mcp1.pinMode(i, INPUT);
    mcp1.pullUp(i, HIGH);  // turn on a 100K pullup internally

    mcp2.pinMode(i, INPUT);
    mcp2.pullUp(i, HIGH);  // turn on a 100K pullup internally

    ThisStatus[i] = 0;
    LastStatus[i] = 0;
    ThisStatus[i+16] = 0;
    LastStatus[i+16] = 0;
  }

  InsideMessage = false;
  QueueHead = 0;
  QueueTail = 0;

  // read all 32 inputs
  for (i=0; i < 16; i++)
  {
    ThisStatus[i] = mcp1.digitalRead(i);
    ThisStatus[i+16] = mcp2.digitalRead(i);
  }

  StillInitialising = true;
  sendNextStartupMessage = millis() + 5000L +(BoardAddress*500L);
  i = 0;
  while (StillInitialising)
  {
    ProcessSerialData();

    if (millis() > sendNextStartupMessage)
    {
      if (!InsideMessage)
      {
          SendStatus(i, ThisStatus[i]);
          SentStatus[i] = ThisStatus[i];
          i++;
          sendNextStartupMessage = millis() + 20L;
          if (i >= MAX_INPUTS)
            StillInitialising = false;     
          ProcessOutputQueue();
      }
    }
  }

  DEBUG_println("Starting...");
}

void loop() 
{
int i;

  ProcessSerialData();
  
  if (!InsideMessage)
  {
    ProcessOutputQueue();
  }
  
  // read all 32 inputs into the 'current state'
  for (i=0; i < 16; i++)
  {
    ThisStatus[i] = mcp1.digitalRead(i);
    ThisStatus[i+16] = mcp2.digitalRead(i);
  }

  for (i=0; i< MAX_INPUTS; i++)
  {
    if (ThisStatus[i] != LastStatus[i])
    {
      lastDebounceTime[i] = millis();
      LastStatus[i] = ThisStatus[i];
    }

    if(lastDebounceTime[i] > 0L)
    {
      if ((millis() - lastDebounceTime[i]) > MAX_DEBOUNCE_TIME)
      {
        if (SentStatus[i] != ThisStatus[i])
        {
          SendStatus(i, ThisStatus[i]);
          SentStatus[i] = ThisStatus[i];
          // beep to show there has been a change
          if (ThisStatus[i] == 0)
            tone(7,1700,75);
        }
        lastDebounceTime[i] = 0L;
      }
    }
  }
}
