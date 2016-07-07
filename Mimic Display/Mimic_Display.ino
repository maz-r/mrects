/*
 	 
 */
#define MAXLEDS 64
#define SOM '<'
#define EOM '>'
#define CLOCK_PIN 14
#define DISPLAYCHIPSELECT 15
#define DATA_PIN 16
#define SDCHIPSELECT 10
#define PROGRAM_PIN 3
#define PROGRAM_LED 4
#define CONFIG_FILE F("MIMIC.CFG")
#define DEFAULT_FILE F("MIMIC.DEF")

// #define DEBUG

#ifdef DEBUG
  #define DEBUG_print(x)    Serial.print(x)
  #define DEBUG_println(x)  Serial.println(x)
#else
  #define DEBUG_print(x)
  #define DEBUG_println(x)
#endif

#include <SD.h>
#include <LedControl.h>

LedControl lc=LedControl(DATA_PIN,CLOCK_PIN,DISPLAYCHIPSELECT,1);

// File root;
File Datafile;
char InputStr[12];
int  FileCount;
int  CurrentLED;
long LastTime;
int  InCount;
int  TargetState[MAXLEDS];
int  ActualState[MAXLEDS];
byte CurrentState[MAXLEDS];
long nextStep;
char Line[40];
bool InsideMessage;
bool RemoteSetup;

void ReadStartupConfig()
{
byte CharCount;
byte LineCount;
char ReadByte;
byte i;
byte LEDNum;
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
        if (CharCount == 0 && ReadByte == '/')
          while (ReadByte != 10 )
            ReadByte = Datafile.read();
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
        
        LEDNum = atoi(Args[0]);
        TargetState[LEDNum] = atoi(Args[1]);
        CurrentState[LEDNum] = TargetState[LEDNum];
        ActualState[LEDNum]  = TargetState[LEDNum];
        
        DEBUG_print(LEDNum);
        DEBUG_print(F(" : "));
        DEBUG_println(TargetState[LEDNum]);

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
byte CurrentState;
byte ThisLED;
// byte tempval;

  Datafile.close();
  DEBUG_println(FreeRam());

  // delete file
  SD.remove(DEFAULT_FILE);
  delay(100);

  // open file for writing
  Datafile = SD.open(DEFAULT_FILE, FILE_WRITE);
  DEBUG_println(FreeRam());

  // loop around writing every LED position as it stands
  for (i=0; i<MAXLEDS; i++)
  {
    ThisLED = i;

    Datafile.write((ThisLED/10)+'0');
    ThisLED %= 10;
    Datafile.write(ThisLED+'0');

    Datafile.write(',');

    CurrentState = TargetState[i];

    Datafile.write((CurrentState/100)+'0');
    CurrentState %= 100;
    Datafile.write((CurrentState/10)+'0');
    CurrentState %= 10;
    Datafile.write(CurrentState+'0');
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
int  FirstLEDNum;
int  LastLEDNum;
char PropogationType;
int  LEDState;
int  i;
int  ArgCount;
int  ArgChar;
char Args[5][10];
bool eof;

  // rewind to the start of the data file
  Datafile.seek(0);

	LineCount = 0;
	CharCount = 0;
  eof = false;

	while (!eof)
	{
		ReadByte = Datafile.read();
    DEBUG_print(ReadByte);
    if (ReadByte == -1)
      eof = true;

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
				while (ReadByte != 10 && ReadByte != -1)
					ReadByte = Datafile.read();
			else
				Line[CharCount++] = ReadByte;

		if (LineCount > 0)
		{
      if (strncmp(Trigger, Line, 4) == 0)
      {
        for(i=0; i<5; i++)
          Args[i][0] = 0;
        
 		    // extract the LED details
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

        PropogationType = Args[1][0];
        if (PropogationType == 0)
          PropogationType = 'S';
          
        DEBUG_println(PropogationType);

        FirstLEDNum = atoi(Args[2]) % 64;
        if (Args[3][0] != 0)
          LastLEDNum = atoi(Args[3]) % 64;
        else
          LastLEDNum = FirstLEDNum;
          
        LEDState = atoi(Args[4]);

        for(i=FirstLEDNum; i<=LastLEDNum; i++)
        {
          TargetState[i] = LEDState;
          CurrentState[i] = LEDState;
          ActualState[i]  = LEDState;            
          if (PropogationType == 'A')
          {
            if (LEDState == 0)
              LEDState = 1;
            else
              if (LEDState == 1)
                LEDState = 0;
              else
                if (LEDState > 1)
                  LEDState = 0 - LEDState;
                else
                  if (LEDState < 0)
                    LEDState = abs(LEDState);
          }
          DEBUG_print(i);
          DEBUG_print(F(" : "));
          DEBUG_println(TargetState[i]);
        }
      }

		  LineCount = 0;
		  CharCount = 0;
		}
  }

  return;
}

int FindMatch(char *MatchString, char *ReturnString)
{
int MatchCount;
int Best;
int CurrentPosition;
int BestIndex;
int i;
unsigned char LineCount;
unsigned char CharCount;
char ReadByte;
bool eof;

  Datafile.seek(0);
  eof = false;
  Best = -1;
  BestIndex = -1;
  LineCount = 0;
  CharCount = 0;

  ReadByte = 0;
  while (!eof)
  {
    // read a line
    ReadByte = Datafile.read();
    DEBUG_print(ReadByte);
    
    if (ReadByte == -1)
      eof = true;

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
        while (ReadByte != 10 && ReadByte != -1)
          ReadByte = Datafile.read();
      else
        Line[CharCount++] = ReadByte;

    if (LineCount > 0)
    {
      MatchCount = 0;
      for (i=0; i<4 && MatchCount >= 0; i++)
      {
        if (Line[i] == MatchString[i])
          MatchCount+=10;
        else
          if (Line[i] == '%' && isdigit(MatchString[i]))
            MatchCount+=2;
          else
            if (Line[i] == 'R' && random(10) == 1)
              MatchCount+=1;
            else
            {
              MatchCount = -1;
              continue;
            }
      }

      if (MatchCount > 0 && MatchCount > Best)
      {
        Best = MatchCount;
        strncpy(ReturnString, Line, 4);
        ReturnString[4] = 0;
        DEBUG_print("Best : ");
        DEBUG_print (ReturnString);
        // we have found the perfect match, so stop looking further
        if (MatchCount == 40)
          eof = true;
      }
      
      LineCount = 0;
      CharCount = 0;
    }

    if (ReadByte == -1)
      eof = true;
  }

	return Best;
}

void setup()
{
int i;
int j;
byte tempval;

	FileCount = 0;
	CurrentLED = 0;

	Serial.begin(9600);

	DEBUG_print("Trying to initialise SD card...");

  lc.shutdown(0,false);
  /* Set the brightness to a medium values */
  lc.setIntensity(0,8);
  /* and clear the display */
  lc.clearDisplay(0);
  
  pinMode (SDCHIPSELECT, OUTPUT);
  digitalWrite(SDCHIPSELECT, HIGH);

  pinMode(PROGRAM_LED, OUTPUT);

  pinMode(PROGRAM_PIN, INPUT);
  digitalWrite(PROGRAM_PIN, HIGH); // pullup the pin

  while (!SD.begin(SDCHIPSELECT))
  {
    DEBUG_print(".");
  }
  DEBUG_print("SD card initialised...");
  RemoteSetup = false;

	nextStep = millis() + 10L;

	LastTime = 0;
	InCount = 0;

  // ALL OFF BY DEFAULT
  for (i=0;i<64;i++)
  {
    TargetState[i] = 0;
    ActualState[i] = 0;
    CurrentState[i] = 0;
  }

  // now read the DEFAULTS file from the SD card
  ReadStartupConfig();

  // and finally, open the config file to compare to incoming messages
  Datafile = SD.open(CONFIG_FILE, FILE_READ);
  if (!Datafile)
    DEBUG_println(F("Failed to open datafile"));

  return;
}

void loop()
{
int  val;
char BestMatch[5];
char TempChar;
long ThisTime;
int  LEDNum;
int  LEDTarget;
int  LEDrow;
int  LEDcol;
bool RemoteConfigSet;
bool flash;

// <SOM>RHHMMSSA<EOM>

  if (!digitalRead(PROGRAM_PIN))
  {
    RemoteConfigSet = true;
    digitalWrite(PROGRAM_LED, HIGH);
  }
  else
  {
    RemoteConfigSet = false;
    digitalWrite(PROGRAM_LED, LOW);
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

					val = FindMatch(&InputStr[2], BestMatch);
          DEBUG_print("Best = ");
          DEBUG_println(BestMatch);
  				if (val != -1)
	  			{
		  			ProcessFile(BestMatch);
			  	}
				}
			}

      // if it is a remote config message....
      if (InputStr[1] == 'C' && InputStr[2] == 'S' && RemoteConfigSet)
      {
        LEDNum = (InputStr[3] - '0') * 10;
        LEDNum += (InputStr[4] - '0');

        LEDTarget = (InputStr[5] - '0') * 100;
        LEDTarget += (InputStr[6] - '0') * 10;
        LEDTarget += (InputStr[7] - '0');
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

  if (millis() > nextStep)
  {
    LEDrow = CurrentLED/8;
    LEDcol = CurrentLED%8;
  
    if (TargetState[CurrentLED] == 0 || TargetState[CurrentLED] == 1)
    {
      CurrentState[CurrentLED] = TargetState[CurrentLED];
    }
    
    if (TargetState[CurrentLED] != 0 && TargetState[CurrentLED] != 1)
    {
      ActualState[CurrentLED]--;
      if (ActualState[CurrentLED] <= 0)
      {
        ActualState[CurrentLED] = abs(TargetState[CurrentLED]);
        if (CurrentState[CurrentLED] > 0)
          CurrentState[CurrentLED] = 0;
        else
          CurrentState[CurrentLED] = 1;
      }
    }
    lc.setLed(0, LEDrow, LEDcol, CurrentState[CurrentLED]);
    CurrentLED = (CurrentLED + 1) % MAXLEDS;
    if(CurrentLED == 0)
      nextStep = millis()+10L;
  }

	return;
}
