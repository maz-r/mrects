/*
 	 
 */
#define MAXSERVOS 16
#define SERVOMIN  150 // this is the 'minimum' pulse length count (out of 4096)
#define SERVOMAX  600 // this is the 'maximum' pulse length count (out of 4096)
#define OUTPUT_QUEUE_SIZE 32
#define SOM '<'
#define EOM '>'
#define PROGRAM_CONF_PIN 4
#define PROGRAM_PIN 3
#define CHIPSELECT  10
#define CONFIG_FILE F("SERVO.CFG")
#define DEFAULT_FILE F("SERVO.DEF")

#define DEBUG

#ifdef DEBUG
  #define DEBUG_print(x)    Serial.print(x)
  #define DEBUG_println(x)  Serial.println(x)
#else
  #define DEBUG_print(x)
  #define DEBUG_println(x)
#endif

#include <SD.h>
#include <EEPROM.h>
#include <Adafruit_PWMServoDriver.h>

// called this way, it uses the default address 0x40
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// File root;
File Datafile;
char serialOutputQueue[OUTPUT_QUEUE_SIZE];
byte QueueHead;
byte QueueTail;
char InputStr[12];
int  FileCount;
int  CurrentServo;
long LastTime;
int  InCount;
int  Target[MAXSERVOS];
int  Actual[MAXSERVOS];
byte Speed[MAXSERVOS];
long Delay[MAXSERVOS];
long NextStep[MAXSERVOS];
long nextStep;
char Line[40];
char ServoStartMessage[MAXSERVOS][4];
char ServoEndMessage[MAXSERVOS][4];
bool InsideMessage;
bool RemoteSetup;
bool CharState = false;

void ReadStartupConfig()
{
byte CharCount;
byte LineCount;
char ReadByte;
byte i;
byte ServoNum;
byte ArgCount;
byte ArgChar;
char Args[2][5];
bool endoffile;

  endoffile = false;

  Datafile = SD.open(DEFAULT_FILE, FILE_READ);

  if (Datafile)
  {
    while (!endoffile)
    {
      ReadByte = Datafile.read();
      if (ReadByte == -1 || ReadByte == 10 || ReadByte == 13)
      {
        if (CharCount != 0)
        {
          // terminate the line
          Line[CharCount] = 0;
          LineCount++;
          CharCount = 0;
        }
      }
      else
        Line[CharCount++] = ReadByte;
    
      if (LineCount > 0)
      {  
        for(i=0; i<2; i++)
          Args[i][0] = 0;
        
        ArgCount = 0;
        ArgChar = 0;
        for (i=0; i < strlen(Line); i++)
        {
          if(Line[i] == ',' || Line[i] == 0)
          {
            Args[ArgCount][ArgChar] = 0;
            ArgCount++;
            ArgChar = 0;
          }
          else
          {
            if(Line[i] != 10 && Line[i] != 13)
            {
              Args[ArgCount][ArgChar] = Line[i];
              ArgChar++;
            }
          }
        }
        
        Args[ArgCount][ArgChar] = 0;
        
        ServoNum = atoi(Args[0]);
        Target[ServoNum] = atoi(Args[1]);
        if (Target[ServoNum] <= 180)
          Target[ServoNum] = map(Target[ServoNum],1,180,SERVOMIN,SERVOMAX);
        else
          Target[ServoNum] = map(90,1,180,SERVOMIN,SERVOMAX);
        DEBUG_print(ServoNum);
        DEBUG_print(F(" : "));
        DEBUG_println(Target[ServoNum]);
        if (Target[ServoNum] != Actual[ServoNum])
          Speed[ServoNum] = 6;

        LineCount=0;
      }

      if (ReadByte == -1)
        endoffile = true;
    }

    Datafile.close();
  }
  else
    DEBUG_println(F("Failed to open defaults"));
}

void OverwriteConfigFile()
{
char ThisChar;
bool eof;
int  ReturnCode;
bool IgnoreMessage;

  // close file
  Datafile.close();
  DEBUG_println(FreeRam());  
  // delete file
  ReturnCode = SD.remove(CONFIG_FILE);
  delay(10);

  // open file for writing
  Datafile = SD.open(CONFIG_FILE, FILE_WRITE);
  DEBUG_println(FreeRam());

  // loop around writing every received character to the file until we get a '>' end character
  eof = false;
  IgnoreMessage = false;

  while (InsideMessage)
  {
    ThisChar = (char)Serial.read();

    if (ThisChar != EOM)
    {
      Serial.write(ThisChar);
    }
    else
    {
      Serial.write(ThisChar);
      InsideMessage = false;
    }
  }

  while (!eof)
  {
    if(Serial.available() > 0)
    {
      // read a character
      ThisChar = (char)Serial.read();

      // if we get a 'SOM' while programming, then it is a spurious control message that we need to ignore, so keep relaying the received characters until we hit an EOM
      if (ThisChar == SOM)
      {
        IgnoreMessage = true;
        // Serial.write(ThisChar);
      }

      if (!IgnoreMessage)
      {
        // if it isn't the 'eof' character
        if (ThisChar != EOM)
        {
          // write out the character to the file
          Datafile.write(ThisChar);
          DEBUG_print('.');
        }
        else
          eof = true;
      }
      else
        Serial.write(ThisChar);

      if (ThisChar == EOM && IgnoreMessage)
      {
        IgnoreMessage = false;
      }
    }
  }
  // close the file to flush the written data out
  Datafile.close();
  
  delay(100);

  // and then reopen it....
  Datafile = SD.open(CONFIG_FILE, FILE_READ);
}

void OverwriteDefaultsFile()
{
byte i;
char ThisChar;
byte CurrentPos;
byte ThisServo;
// byte tempval;

  Datafile.close();
  DEBUG_println(FreeRam());

  // delete file
  SD.remove(DEFAULT_FILE);
  delay(100);

  // open file for writing
  Datafile = SD.open(DEFAULT_FILE, FILE_WRITE);
  DEBUG_println(FreeRam());

  // loop around writing every servo position as it stands
  for (i=0; i<MAXSERVOS; i++)
  {
    ThisServo = i;

    Datafile.write((ThisServo/10)+'0');
    ThisServo %= 10;
    Datafile.write(ThisServo+'0');

    Datafile.write(',');

    CurrentPos = EEPROM.read(i);
    Datafile.write((CurrentPos/100)+'0');
    CurrentPos %= 100;
    Datafile.write((CurrentPos/10)+'0');
    CurrentPos %= 10;
    Datafile.write(CurrentPos+'0');
    Datafile.write(10);    
  }

  // close the file to flush the written data out
  Datafile.close();

  delay(100);

  Datafile = SD.open(CONFIG_FILE, FILE_READ);

}

void ProcessFile(char *Trigger)
{
unsigned char LineCount;
unsigned char CharCount;
char ReadByte;
int  ServoNum;
char MessageID[5];
int  ServoDelay;
char StartMessage[5];
char EndMessage[5];
int  ServoTarget;
int  ServoSpeed;
int  i;
int  ArgCount;
int  ArgChar;
char Args[7][10];

  // rewind to start of file
  Datafile.seek(0);

	LineCount = 0;
	CharCount = 0;

	while (true) 
	{
		// read 3 lines
		ReadByte = Datafile.read();

		if (ReadByte == -1 || ReadByte == 10 || ReadByte == 13)
		{
			if (CharCount != 0)
			{
        // terminate the line
				Line[CharCount] = 0;
				LineCount++;
				CharCount = 0;
			}
		}
		else
			if (CharCount == 0 && ReadByte == '/')
				while (ReadByte != 10 )
					ReadByte = Datafile.read();
			else
				Line[CharCount++] = ReadByte;

		if (LineCount > 0)
		{
      StartMessage[0] = 0;
      EndMessage[0] = 0;

      for(i=0; i<7; i++)
        Args[i][0] = 0;
      
      if (strncmp(Trigger, Line, 4) == 0)
      {
        StartMessage[0] = 0;
        EndMessage[0] = 0;
  		   // extract the servo details
        ArgCount = 0;
        ArgChar = 0;
        for (i=0; i < strlen(Line); i++)
        {
          if(Line[i] == ',' || Line[i] == 0)
          {
            Args[ArgCount][ArgChar] = 0;
            ArgCount++;
            ArgChar = 0;
          }
          else
            if(Line[i] != 10 && Line[i] != 13)
            {
              Args[ArgCount][ArgChar] = Line[i];
              ArgChar++;
            }
        }
        Args[ArgCount][ArgChar] = 0;
        
        ServoNum = atoi(Args[1]);
        ServoDelay = atoi(Args[2]);
        strcpy (StartMessage, Args[3]);
        ServoTarget = atoi(Args[4]);
        ServoSpeed = atoi(Args[5]);
        strcpy (EndMessage, Args[6]);

		    Target[ServoNum] = map(ServoTarget,1,180,SERVOMIN,SERVOMAX);
		    // Speed[ServoNum] = 2;
        Speed[ServoNum] = ServoSpeed;
        if (StartMessage[0] != 0)
        {
          for (i=0; i<4; i++)
            ServoStartMessage[ServoNum][i] = StartMessage[i];
        }
        else
          ServoStartMessage[ServoNum][0] = 0;

        if (EndMessage[0] != 0)
        {
          for (i=0; i<4; i++)
            ServoEndMessage[ServoNum][i] = EndMessage[i];
        }
        else
          ServoEndMessage[ServoNum][0] = 0;
          
        if (ServoDelay != 0)
          Delay[ServoNum] = millis() + (ServoDelay * 100L);
        else
          Delay[ServoNum] = 0L;
      }

		  LineCount = 0;
		  CharCount = 0;

		}
      
    if (ReadByte == -1)
      break;
	}

  return;  
}

int FindMatch(char *MatchString, char *ReturnString)
{
int MatchCount;
int Best;
int BestIndex;
int i;
unsigned char LineCount;
unsigned char CharCount;
char ReadByte;

  Datafile.seek(0);
  Best = -1;
  BestIndex = -1;
  LineCount = 0;
  CharCount = 0;

  ReadByte = 0;
  while (ReadByte != -1)
  {
    // read a line
    ReadByte = Datafile.read();

    if (ReadByte == -1 || ReadByte == 10 || ReadByte == 13)
    {
      if (CharCount != 0)
      {
        // terminate the line
        Line[CharCount] = 0;
        LineCount++;
        CharCount = 0;
      }
    }
    else
      if (CharCount == 0 && ReadByte == '/')
        while (ReadByte != 10 )
          ReadByte = Datafile.read();
      else
        Line[CharCount++] = ReadByte;

    if (LineCount > 0)
    {
      MatchCount = 0;
      if (Line[0] == MatchString[0])
        MatchCount+=10;
      else
        if (Line[0] == '%' && isdigit(MatchString[0]))
          MatchCount+=2;
        else
          if (Line[0] == 'R' && random(10) == 1)
            MatchCount+=1;
          else
            continue;

      if (Line[1] == MatchString[1])
        MatchCount+=10;
      else
        if (Line[1] == '%')
          MatchCount+=2;
        else
          if (Line[1] == 'R' && random(10) == 1)
            MatchCount+=1;
          else
            continue;

      if (Line[2] == MatchString[2])
        MatchCount+=10;
      else
        if (Line[2] == '%')
          MatchCount+=2;
        else
          if (Line[2] == 'R' && random(10) == 1)
            MatchCount+=1;
          else
            continue;

      if (Line[3] == MatchString[3])
        MatchCount+=10;
      else
        if (Line[3] == '%')
          MatchCount+=2;
        else
          if (Line[3] == 'R' && random(10) == 1)
            MatchCount+=1;
          else
            continue;

      if (MatchCount > 0 && MatchCount > Best)
      {
        Best = MatchCount;
        strncpy(ReturnString, Line, 4);
        ReturnString[4] = 0;
      }

      LineCount = 0;
      CharCount = 0;
    }
  }

	return Best;
}

void GenerateFullMessage(char *fullmessage, char *message)
{
int i;
int j;

  i = 0;
  j = 0;
  fullmessage[i++] = SOM;
  fullmessage[i++] = message[j++];
  fullmessage[i++] = message[j++];
  fullmessage[i++] = message[j++];
  fullmessage[i++] = message[j++];
  fullmessage[i++] = ' ';
  fullmessage[i++] = ' ';
  fullmessage[i++] = ' ';
  fullmessage[i++] = ' ';
  fullmessage[i++] = EOM;
  fullmessage[i++] = 0;
  
  return;
}

void AddToOutputQueue(char *message)
{
int  i;
char fullmessage[12];

  i = 0;

  GenerateFullMessage(fullmessage, message);
  
  while (fullmessage[i] != 0)
  {
    serialOutputQueue[QueueHead] = fullmessage[i++];
    QueueHead = (QueueHead + 1) % OUTPUT_QUEUE_SIZE;
  }

  message[0] = 0;
  
  // now process the new message as if it just arrived over the wire.
  // We do this so that at the end of a servo move we can get others to 
  // start moving without have any external triggers...
  ProcessFile(&fullmessage[1]);
}

void ProcessOutputQueue()
{
  while (QueueTail != QueueHead)
  {
    Serial.write(serialOutputQueue[QueueTail]);
    QueueTail = (QueueTail + 1) % OUTPUT_QUEUE_SIZE;
  }
}

void setup()
{
int i;
int j;
byte tempval;

	FileCount = 0;
	CurrentServo = 0;

//	delay(1000);

	Serial.begin(9600);

	DEBUG_print("Trying to initialise SD card...");

  pinMode (CHIPSELECT, OUTPUT);
  digitalWrite(CHIPSELECT, HIGH);

  pinMode(PROGRAM_CONF_PIN, OUTPUT);

  digitalWrite(PROGRAM_CONF_PIN, HIGH);
  delay(1000);
  digitalWrite(PROGRAM_CONF_PIN, LOW);

  pinMode(PROGRAM_PIN, INPUT);
  digitalWrite(PROGRAM_PIN, HIGH); // pullup the pin

  while (!SD.begin(CHIPSELECT))
  {
    DEBUG_print(".");
    // delay(250);
  }
  DEBUG_print("SD card initialised...");
  RemoteSetup = false;

	nextStep = millis() + 10L;

	LastTime = 0;
	InCount = 0;
  QueueHead = 0;
  QueueTail = 0;

//  Datafile = SD.open(CONFIG_FILE, O_READ);
  
  pwm.begin();
  
  pwm.setPWMFreq(60);

  for (i=0; i<MAXSERVOS; i++)
  {
    Delay[i] = 0L;
    // read the last value we knew about for this servo
    tempval = EEPROM.read(i);
    DEBUG_print(i);
    DEBUG_print(F(" : "));
    DEBUG_print(tempval);
    DEBUG_print(F(" : "));
    // if it is 255 (not a valid setting)
    if (tempval == 255)
      // then set it to a middle position
      Actual[i] = map(90,1,180,SERVOMIN,SERVOMAX);
    else
      Actual[i] = map(tempval,1,180,SERVOMIN,SERVOMAX);

    Target[i] = Actual[i];
    DEBUG_println(Actual[i]);
    NextStep[i] = 0L;
    // and then start sending the pwn signal
    pwm.setPWM(i, 0, Actual[i]);
  }

  // now read the DEFAULTS file from the SD card
  ReadStartupConfig();

  // and finally, open the config file to compare to incoming messages
//  Datafile = SD.open(CONFIG_FILE, O_READ);
  Datafile = SD.open(CONFIG_FILE, FILE_READ);
  if (!Datafile)
    DEBUG_println(F("Failed to open datafile"));
  
	return;
}

void loop()
{
int  val;
char ReadByte;
char BestMatch[5];
char TempChar;
long ThisTime;
int  ServoNum;
int  ServoTarget;
int  ServoSpeed;
int  Adj;
bool RemoteConfigSet;

// <SOM>RHHMMSSA<EOM>

  if (!digitalRead(PROGRAM_PIN))
  {
    RemoteConfigSet = true;
    digitalWrite(PROGRAM_CONF_PIN, HIGH);
  }
  else
  {
    RemoteConfigSet = false;
    digitalWrite(PROGRAM_CONF_PIN, LOW);
  }

  if(Serial.available() > 0)
	{
    TempChar = Serial.read();

    Serial.write(TempChar);

		if (TempChar == SOM)
		{
			InCount = 0;
      InsideMessage = true;
		}

		InputStr[InCount] = (char)TempChar;
    InCount = (InCount + 1) % 12;

		if (TempChar == EOM)
		{
      InsideMessage = false;

			InputStr[11] = 0;

			if (InputStr[1] == 'R')
			{
				ThisTime = (InputStr[2] - '0') * 600L;
				ThisTime += (InputStr[3] - '0') * 60L;
				ThisTime += (InputStr[4] - '0') * 10L;
				ThisTime += (InputStr[5] - '0');
        if (LastTime != ThisTime)
				{
          LastTime = ThisTime;
          if (!RemoteSetup)
          {
  					val = FindMatch(&InputStr[2], BestMatch);
	  				if (val != -1)
		  			{
			  			ProcessFile(BestMatch);
				  	}
          }
				}
			}

      // if it is a remote config message....
      if (InputStr[1] == 'C' && InputStr[2] == 'S' && RemoteConfigSet)
      {
        ServoNum = (InputStr[3] - '0') * 10;
        ServoNum += (InputStr[4] - '0');

        ServoTarget = (InputStr[5] - '0') * 100;
        ServoTarget += (InputStr[6] - '0') * 10;
        ServoTarget += (InputStr[7] - '0');

        ServoSpeed = (InputStr[8] - '0') * 10;
        ServoSpeed += (InputStr[9] - '0');

        Target[ServoNum] = map(ServoTarget,1,180,SERVOMIN,SERVOMAX);
        Speed[ServoNum] = ServoSpeed;
        ServoStartMessage[CurrentServo][0] = 0;
        ServoEndMessage[CurrentServo][0] = 0;
        Delay[ServoNum] = 0L;
      }

      if (InputStr[1]=='W' && InputStr[2]=='C' && RemoteConfigSet)
      {
        OverwriteConfigFile();
      }

      if (InputStr[1]=='W' && InputStr[2]=='D' && RemoteConfigSet)
      {
        OverwriteDefaultsFile();
      }

      if (InputStr[1]=='S' || InputStr[1]=='U' || InputStr[1]=='I')
      {
        ProcessFile(&InputStr[1]);
      }
		}
	}

	if(millis() > nextStep)
	{
    if (millis() > Delay[CurrentServo])
    {
      if (ServoStartMessage[CurrentServo][0] != 0)
      {
        //send before message
        AddToOutputQueue(ServoStartMessage[CurrentServo]);
      }

      if (Actual[CurrentServo] != Target[CurrentServo])
      {
        if (millis() >= NextStep[CurrentServo])
        {
          NextStep[CurrentServo] = millis() + (Speed[CurrentServo]);
          if (Speed[CurrentServo] < 10)
            Adj = 10 - Speed[CurrentServo];
          else
            Adj = 1;
    			val = Actual[CurrentServo];
  	  		if (val > Target[CurrentServo])
  		  	{
            val -= Adj;
  				  if (val < Target[CurrentServo])
  					  val = Target[CurrentServo];
    			}
  	  		else
  		  	{
            val += Adj;
  				  if (val > Target[CurrentServo])
  					  val = Target[CurrentServo];
  			  }
  
          pwm.setPWM(CurrentServo, 0, val);
  
  			  Actual[CurrentServo] = val;
  
          if (val == Target[CurrentServo])
          {
            val = map(val,SERVOMIN,SERVOMAX,1,180);

            EEPROM.write(CurrentServo, (byte)val);
            DEBUG_print (F("writing "));
            DEBUG_print(CurrentServo);
            DEBUG_print(F(" : "));
            DEBUG_print(val);

            val = EEPROM.read(CurrentServo);
            DEBUG_print (F(" and reading "));
            DEBUG_println(val);
          }
        }
      }
      else
      {
        if (ServoEndMessage[CurrentServo][0] != 0)
        {
          //send finish message
          AddToOutputQueue(ServoEndMessage[CurrentServo]);
        }
      }
		}

		CurrentServo = (CurrentServo + 1) % MAXSERVOS;

		nextStep = millis()+1L;
	}

  if (!InsideMessage)
  {
    ProcessOutputQueue();
  }

	return;
}
