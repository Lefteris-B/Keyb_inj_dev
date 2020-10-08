#include "../cmsis/stm32f0xx.h"
#include "../usb/usb.h"
#include "../usb/hid_rodata.h"
#include "../fatfs/ff.h"
#include "../fatfs/diskio.h"

extern ControlInfo_TypeDef ControlInfo;
extern DeviceDescriptor_TypeDef DeviceDescriptor;
extern unsigned short StringDescriptor_1[13];

static char PayloadBuffer[2048] __attribute__(( aligned(2) ));//κρατάει την προς εκτέλεση εντολή
static FATFS FATFSinfo;//πληροφορίες για το fs
static FIL openedFileInfo;//πληροφορίες για ανοιγμένα αρχεία
static unsigned int BytesRead;//πόσα byte διαβάστηκαν με την f_read()
static FRESULT FATFSresult;//επιστροφή αξιών\values που σχετίζονται με το FATFS 

static void readConfigFile(char* filename);
static void waitForInit();
static unsigned short countCapsToggles(unsigned short toggleLimit);
static void runDuckyPayload(char* filename);
static void runDuckyCommand();
static void repeatDuckyCommands(unsigned int count);
static char checkKeyword(char* referenceString);
static unsigned int checkDecValue();
static unsigned int checkHexValue();
static void setFilename(char* newName);
static void setSerialNumber(char* newSerialNumber);
static void sendString(char* stringStart);
static void sendKBreport(unsigned short modifiers, unsigned int keycodes);
static void sendMSreport(unsigned int mousedata);
static void skipSpaces();
static void skipString();

static inline void delay_us(unsigned int delay) __attribute__((always_inline));
static void delay_ms(unsigned int delay);
static void restart_tim6(unsigned short time);
static void enter_bootloader();

static void saveOSfingerprint();
static void checkOSfingerprint();

PayloadInfo_TypeDef PayloadInfo =
  {
    .DefaultDelay = 0,//εάν DEFAULT_DELAY δεν έχει τεθεί, βάλε 0ms 
    .StringDelay = 0,//εάν STRING_DELAY δεν έχει τεθεί, βάλε 0ms 
    .PayloadPointer = (char*) &PayloadBuffer,//ξεκίνα την μεταγλώτιση στην αρχή του PayloadBuffer
    .BytesLeft = 0,//ο PayloadBuffer ειναι άδειος
    .RepeatStart = 0,//repeat start είναι στην αρχή κάποιου αρχέιου
    .RepeatCount = 0xFFFFFFFF,//RepeatCount δεν έχει τεθεί ακόμα
    .HoldMousedata = MOUSE_IDLE,//δεν υπάρχει ποντίκι
    .HoldKeycodes= KB_Reserved,//δεν υπαρχουν ακόμα keycodes    
    .HoldModifiers = MOD_NONE,// δεν υπαρχουν ακόμα modifiers
    .LBAoffset = 0,//όλα τα μπλοκ κώδικα χρησιμοποιούνται ως MSD
    .FakeCapacity = 0,//χρησιμοποίησε την κανονική χωριτικότητα
    .Filename = {0x00},//δεν υπάρχει αρχείο
    .PayloadFlags = 0,//δεν υπάρχουν ορίσματα
    .DeviceFlags = 0//δεν  υπάρχουν ορίσματα συσκευής
  };

//----------------------------------------------------------------------------------------------------------------------

int main()
{
  unsigned short toggleCount = 0;//ποσες φορές πατήθηκε το capslock
  
  //αρχικοποίηση chip W25N01GVZEIG flash memory, φόρτωση συστήματος αρχείων, άνοιγμα εκτέλεση payload.txt
  delay_ms(5);//περίμενε μέχρι να μπορεί να εκτελέσει εντολές
  __enable_irq();//ενεργοποίησε iterupts
  
  if( f_mount(&FATFSinfo, "0:", 1) )//εάν δεν βρεθεί FAT
    {
      disk_initialize(0);//αρχικοποίηση μνημης
      usb_init();//αρχικοποίηση usb
      
      while(1)
	{
	  sendKBreport(MOD_NONE, KB_Reserved);
	  
	  if(countCapsToggles(65535) > 19)//εάν πατήθηκαν 20 capslock
	    { 
	      RCC->APB1RSTR |= (1<<23);
	      delay_ms(1000);
	      RCC->APB1RSTR &= ~(1<<23);
	      enter_bootloader();
	    }
	}
    }
  else//εάν το σύστημα αρχέιων φορτώθηκε κανονικά
    { 
      f_chdir("0:/");
      readConfigFile("config.txt");
      
      usb_init();
      while(ControlInfo.OSfingerprintCounter < 10);      
      if(PayloadInfo.DeviceFlags & (1<<3)) waitForInit();
      
      if(PayloadInfo.DeviceFlags & (1<<2))
	{
	  NVIC_DisableIRQ(31);      
	  saveOSfingerprint();
	  checkOSfingerprint();
	  f_chdir("0:/fgscript");
	  NVIC_EnableIRQ(31);
	  
	  
	  if( !(PayloadInfo.DeviceFlags & (1<<0)) ) runDuckyPayload(PayloadInfo.Filename);
	}
      else
	{
	  NVIC_DisableIRQ(31);
	  f_chdir("0:/");
	  NVIC_EnableIRQ(31);
	  
	  
	  if( !(PayloadInfo.DeviceFlags & (1<<0)) ) runDuckyPayload("payload.txt");
	}                  
      
      
      while(1)
	{
	  sendKBreport(MOD_NONE, KB_Reserved);
	  sendMSreport(MOUSE_IDLE);
	  	  
	  toggleCount = countCapsToggles(65535);
	  
	  //εάν πατήθηκε το capslock από 3 έως 19 φορές
	  if( (toggleCount > 2) && (toggleCount < 20) )
	    {
	      NVIC_DisableIRQ(31);
	      FATFSresult  = f_mount(&FATFSinfo, "0:", 1);
	      FATFSresult |= f_chdir("0:/ondemand");
	      setFilename("script__.txt");
	      PayloadInfo.Filename[6] = 48 + toggleCount / 10;
	      PayloadInfo.Filename[7] = 48 + toggleCount % 10;
	      NVIC_EnableIRQ(31);
	      
	      if(!FATFSresult) runDuckyPayload(PayloadInfo.Filename);
	    }
	  else if(toggleCount > 19)
	    {
	      PayloadInfo.LBAoffset = 0;
	      PayloadInfo.FakeCapacity = 0;
	      ControlInfo.EnumerationMode = 0;  
	      
	      RCC->APB1RSTR |= (1<<23);
	      delay_ms(1000);
	      RCC->APB1RSTR &= ~(1<<23);
	      if(PayloadInfo.DeviceFlags & (1<<3)) enter_bootloader();
	      else                                 usb_init();
	      
	      NVIC_DisableIRQ(31);
	      __DSB();
	      __ISB();
	      PayloadInfo.DeviceFlags |=  (1<<3);
	      PayloadInfo.DeviceFlags &= ~(1<<1);
	      NVIC_EnableIRQ(31);
	    }
	}
      
    }
  
  return 0;
}

//----------------------------------------------------------------------------------------------------------------------

//ανοιγμα αρχείου filename και εκτέλεση εντολών στον υπάρχων φάκελο
static void readConfigFile(char* filename)
{
  unsigned int commandStart = (unsigned int) &PayloadBuffer;
  unsigned int firstLBA;
  unsigned int LBAcount;
  unsigned char MSDbutton;
  
  //έλεγχος του κουμπιού MSD κατά την εισαγωγή
  if( !(GPIOA->IDR & (1<<2)) ) MSDbutton = 1;
  else                         MSDbutton = 0;
  
  if( !f_open(&openedFileInfo, filename, FA_READ | FA_OPEN_EXISTING) )
    {
      f_read(&openedFileInfo, (char*) &PayloadBuffer, 512, &BytesRead );
      PayloadInfo.PayloadPointer = (char*) &PayloadBuffer;//
      PayloadInfo.BytesLeft = BytesRead + 1;
      PayloadBuffer[BytesRead] = 0x0A;
      f_close(&openedFileInfo);
      
      //εκτέλεση εντολών προ-ρυθμισης
      while(PayloadInfo.BytesLeft)
	{
	  commandStart = (unsigned int) PayloadInfo.PayloadPointer;
	  skipSpaces();
	  
	  
	       if( checkKeyword("VID 0x") )     {DeviceDescriptor.idVendor  = (unsigned short) checkHexValue();}
	  else if( checkKeyword("PID 0x") )     {DeviceDescriptor.idProduct = (unsigned short) checkHexValue();}
	  else if( checkKeyword("SERIAL ") )    {setSerialNumber(PayloadInfo.PayloadPointer);}
	  else if( checkKeyword("MASS_ERASE") ) {mass_erase(); NVIC_SystemReset();}
	  else if( (MSDbutton == 0) && checkKeyword("HID_ONLY_MODE") ) {ControlInfo.EnumerationMode = 1;}
	  
	  else if( (MSDbutton == 0) && checkKeyword("USE_HIDDEN_REGION") )
	    {
	      
	      disk_read(0, (unsigned char*) &PayloadBuffer + 1024, 0, 1);
	      
	      
	      if( (PayloadBuffer[1534] == 0x55) && (PayloadBuffer[1535] == 0xAA) )
		{
		  firstLBA  = *( (unsigned short*) (&PayloadBuffer[1478]) ) <<  0;
		  firstLBA |= *( (unsigned short*) (&PayloadBuffer[1480]) ) << 16;
		  LBAcount  = *( (unsigned short*) (&PayloadBuffer[1482]) ) <<  0;
		  LBAcount |= *( (unsigned short*) (&PayloadBuffer[1484]) ) << 16;
		  
		  if( (firstLBA + LBAcount) <= 32768 ) PayloadInfo.LBAoffset = firstLBA + LBAcount;
		}
	    }
	  else if( (MSDbutton == 0) && checkKeyword("SHOW_FAKE_CAPACITY ") )
	    {
	      PayloadInfo.FakeCapacity = checkDecValue();
	      //ψεύτικη χωριτικότητα από 97MiB - 32GiB
	      if(PayloadInfo.FakeCapacity > 32768) PayloadInfo.FakeCapacity = 0;
	      if(PayloadInfo.FakeCapacity < 97)    PayloadInfo.FakeCapacity = 0;
	    }
	  else if( checkKeyword("FIRST_INSERT_ONLY") )
	    {
	      
	      if( !f_open(&openedFileInfo, "noinsert", FA_READ | FA_OPEN_EXISTING ) ) PayloadInfo.DeviceFlags |= (1<<0);
	      
	      else f_open(&openedFileInfo, "noinsert", FA_WRITE | FA_CREATE_ALWAYS);
	      
	      f_close(&openedFileInfo);
	    }
	  else if( checkKeyword("USE_FINGERPRINTER") )
	    {
	      f_mkdir("0:/fgscript");
	      f_mkdir("0:/fingerdb");
	      PayloadInfo.DeviceFlags |= (1<<2);
	    }
	  else if( checkKeyword("USE_LAYOUT ") )
	    {	      	      
	      f_mkdir("0:/kblayout");
	      f_chdir("0:/kblayout");
	      setFilename(PayloadInfo.PayloadPointer);
	      
	      
	      if( !f_open(&openedFileInfo, (char*) &(PayloadInfo.Filename), FA_READ | FA_OPEN_EXISTING) )
		{
		  f_read(&openedFileInfo, (unsigned char*) &Keymap, 107, &BytesRead );
		  f_close( &openedFileInfo );
		}
	    }	  	 
	  
	  
	  skipString();
	  if( PayloadInfo.PayloadPointer < ((char*) &PayloadBuffer + 2047) ) PayloadInfo.PayloadPointer++;
	  else PayloadInfo.PayloadPointer = (char*) &PayloadBuffer;
	  
	  PayloadInfo.BytesLeft = PayloadInfo.BytesLeft - ((unsigned int) PayloadInfo.PayloadPointer - commandStart);
	}
    }
  
  // εάν πατήθηκε το MSD κουμπί θέσε τη σημαία NoInsertFlag
  if(MSDbutton) PayloadInfo.DeviceFlags |= (1<<0);
  
  return;
}

static void waitForInit()
{
  unsigned char LEDstate;
  
  sendKBreport(MOD_NONE, KB_Reserved);
  delay_ms(50);
  LEDstate = *((unsigned char*) (BTABLE_BaseAddr + BTABLE->ADDR1_RX + ControlInfo.HIDprotocol)) & (1<<1);
  
  while( !LEDstate )
    {
      sendKBreport(MOD_NONE, KB_CAPSLOCK);
      delay_ms(5);
      sendKBreport(MOD_NONE, KB_Reserved);
      
      restart_tim6(200);
      while( (TIM6->CR1 & (1<<0)) && !LEDstate ) LEDstate = *((unsigned char*) (BTABLE_BaseAddr + BTABLE->ADDR1_RX + ControlInfo.HIDprotocol)) & (1<<1);
    }
  
  while( LEDstate )
    {
      sendKBreport(MOD_NONE, KB_CAPSLOCK);
      delay_ms(5);
      sendKBreport(MOD_NONE, KB_Reserved);
      
      restart_tim6(1010);
      while( (TIM6->CR1 & (1<<0)) &&  LEDstate ) LEDstate = *((unsigned char*) (BTABLE_BaseAddr + BTABLE->ADDR1_RX + ControlInfo.HIDprotocol)) & (1<<1);
    }
  
  delay_ms(50);
  LEDstate = *((unsigned char*) (BTABLE_BaseAddr + BTABLE->ADDR1_RX + ControlInfo.HIDprotocol)) & (1<<1);
  if( LEDstate )
    {
      sendKBreport(MOD_NONE, KB_CAPSLOCK);
      delay_ms(5);
      sendKBreport(MOD_NONE, KB_Reserved);
    }
  
  
  while( (ControlInfo.EnumerationMode == 0) && !(PayloadInfo.DeviceFlags & (1<<1)) );
  
  return;
}

static unsigned short countCapsToggles(unsigned short toggleLimit)
{
  unsigned short toggleCount = 0;    
  unsigned char oldLEDstate;
  unsigned char newLEDstate;
  
  restart_tim6(1010);

  
  while( (TIM6->CR1 & (1<<0)) && (toggleCount < toggleLimit) )
    {
      
      oldLEDstate = *((unsigned char*) (BTABLE_BaseAddr + BTABLE->ADDR1_RX + ControlInfo.HIDprotocol)) & (1<<1);
      delay_ms(1);//wait for 1 millisecond
      garbage_collect();//erase an invalid block if found
      delay_ms(1);//wait for 1 millisecond
      newLEDstate = *((unsigned char*) (BTABLE_BaseAddr + BTABLE->ADDR1_RX + ControlInfo.HIDprotocol)) & (1<<1);
      
      
      if( oldLEDstate != newLEDstate )
	{
	  restart_tim6(1010);
	  toggleCount++;
	}
    }
  
  return toggleCount;
}

//άνοιξε το αρχείο filename και εκτέλεσε όλες τις εντολές
static void runDuckyPayload(char* filename)
{
  unsigned char replaceFlag = 0;//

  //επαναρχικοποίηση μεταβλητών που έχουν σχέση με το script
  PayloadInfo.DefaultDelay = 0;
  PayloadInfo.StringDelay = 0;
  PayloadInfo.PayloadPointer = (char*) &PayloadBuffer;
  PayloadInfo.BytesLeft = 0;
  PayloadInfo.RepeatStart = 0;
  PayloadInfo.RepeatCount = 0xFFFFFFFF;
  PayloadInfo.HoldMousedata = MOUSE_IDLE;
  PayloadInfo.HoldKeycodes = KB_Reserved;
  PayloadInfo.HoldModifiers = MOD_NONE;
  PayloadInfo.PayloadFlags = 0;
  
  NVIC_DisableIRQ(31);
  FATFSresult = f_open(&openedFileInfo, filename, FA_READ | FA_OPEN_EXISTING);
  NVIC_EnableIRQ(31);
  
  //εάν βρέθηκε και φορτώθηκε σωστά ένα script κανε μεταγλώτιση
  if( !FATFSresult )
    {
      NVIC_DisableIRQ(31);
      f_read(&openedFileInfo, (char*) &PayloadBuffer, 2048, &BytesRead );
      PayloadInfo.BytesLeft = BytesRead + 1;
      if(BytesRead < 2048) PayloadBuffer[BytesRead] = 0x0A;
      NVIC_EnableIRQ(31);
      
      //συνέχισε την εκτέλεση των εντολών μέχρι το τέλος του αρχείου
      while(PayloadInfo.BytesLeft)
	{ 	  	  
	  
               if( !(PayloadInfo.PayloadFlags & (1<<0)) && (PayloadInfo.PayloadPointer >= ((char*) &PayloadBuffer + 1024)) ) replaceFlag = 1;
	  else if(  (PayloadInfo.PayloadFlags & (1<<0)) && (PayloadInfo.PayloadPointer <  ((char*) &PayloadBuffer + 1024)) ) replaceFlag = 1;
	  
	  if(replaceFlag)
	    {
	      NVIC_DisableIRQ(31);
	      f_read( &openedFileInfo, (char*) &PayloadBuffer + (PayloadInfo.PayloadFlags % 2) * 1024, 1024, &BytesRead );
	      PayloadInfo.BytesLeft = PayloadInfo.BytesLeft + BytesRead;
	      if(BytesRead < 1024) PayloadBuffer[ (PayloadInfo.PayloadFlags % 2) * 1024 + BytesRead ] = 0x0A;
	      
	      PayloadInfo.PayloadFlags ^= (1<<0);      
	      replaceFlag = 0;
	      NVIC_EnableIRQ(31);
	    }

	  runDuckyCommand();
	}
    }
  
  NVIC_DisableIRQ(31);
  f_close(&openedFileInfo); 
  NVIC_EnableIRQ(31);t
  
  return;
}

//εκτέλεσε την τρέχουσα εντολή από τον PayloadBuffer, μετακινησε τον PayloadPointer στην επόμενη γραμμή
static void runDuckyCommand()
{
  unsigned int commandStart = (unsigned int) PayloadInfo.PayloadPointer;
  unsigned int bytesUsed = 0;
  unsigned short limit = 11;
  
  unsigned int   mousedata = PayloadInfo.HoldMousedata;
  unsigned char* mousedataPointer = (unsigned char*) &mousedata;
  unsigned int   keycodes = PayloadInfo.HoldKeycodes;
  unsigned char* keycodePointer = (unsigned char*) &keycodes;
  unsigned short modifiers = PayloadInfo.HoldModifiers;
  
  //ψάξε για συγκεκριμένες λέξεις κλειδια, εάν βρήκες εκτέλεσε,συνέχισε μέχρι να βρεις καινούργια γραμμή
  while( *(PayloadInfo.PayloadPointer) != 0x0A )
    {
      if(limit) limit--;
      else while(1) sendKBreport(MOD_NONE, KB_Reserved);
      
      skipSpaces();
      
      //εάν ο  PayloadPointer δεν είναι καποιος ASCII χαρακτήρας άφησε όλη την γραμμή εκτός
      if( ( *(PayloadInfo.PayloadPointer) < 32 ) || ( *(PayloadInfo.PayloadPointer) > 126 ) ) skipString();

      
      else if( (limit == 10) && checkKeyword("REM") )              {skipString();}
      else if( (limit == 10) && checkKeyword("REPEAT_START") )     {PayloadInfo.RepeatStart = openedFileInfo.fptr + 1 - PayloadInfo.BytesLeft; PayloadInfo.PayloadFlags |= (1<<1); skipString();}
      else if( (limit == 10) && checkKeyword("REPEAT ") )          {commandStart = (unsigned int) &PayloadBuffer; limit = 11; repeatDuckyCommands( checkDecValue() );}
      else if( (limit == 10) && checkKeyword("ONACTION_DELAY ") )  {PayloadInfo.DefaultDelay = checkDecValue(); PayloadInfo.DeviceFlags |=  (1<<4); skipString();}
      else if( (limit == 10) && checkKeyword("DEFAULT_DELAY ") )   {PayloadInfo.DefaultDelay = checkDecValue(); PayloadInfo.DeviceFlags &= ~(1<<4); skipString();}
      else if( (limit == 10) && checkKeyword("DEFAULTDELAY ") )    {PayloadInfo.DefaultDelay = checkDecValue(); PayloadInfo.DeviceFlags &= ~(1<<4); skipString();}
      else if( (limit == 10) && checkKeyword("STRING_DELAY ") )    {PayloadInfo.StringDelay  = checkDecValue(); skipString();}
      else if( (limit == 10) && checkKeyword("DELAY ") )           {delay_ms( checkDecValue() ); skipString();}
      else if( (limit == 10) && checkKeyword("WAITFOR_INIT") )     {waitForInit(); skipString();}
      else if( (limit == 10) && checkKeyword("WAITFOR_RESET") )    {while( ControlInfo.DeviceState != DEFAULT ); skipString();}
      else if( (limit == 10) && checkKeyword("WAITFOR_CAPSLOCK") ) {while( countCapsToggles(2) < 2 ); skipString();}
      else if( (limit == 10) && checkKeyword("ALLOW_EXIT") )       {if( countCapsToggles(65535) > 1 ) {PayloadInfo.BytesLeft = 0; return;} else skipString(); }
      
      else
	{
	  PayloadInfo.PayloadFlags |= (1<<4);
	  
	       if( (limit == 10) && checkKeyword("STRING ") ) {sendString( PayloadInfo.PayloadPointer ); skipString();}
	  else if( (limit == 10) && checkKeyword("HOLD ") )   {PayloadInfo.PayloadFlags |= (1<<2); modifiers = 0; keycodes = 0; mousedata = 0;}
	  else if( (limit == 10) && checkKeyword("RELEASE") ) {PayloadInfo.PayloadFlags |= (1<<2); modifiers = 0; keycodes = 0; mousedata = 0; skipString();}
	  
	  else if( checkKeyword("GUI") )        modifiers = modifiers | MOD_LGUI;
	  else if( checkKeyword("WINDOWS") )    modifiers = modifiers | MOD_LGUI;
	  else if( checkKeyword("CTRL") )       modifiers = modifiers | MOD_LCTRL;
	  else if( checkKeyword("CONTROL") )    modifiers = modifiers | MOD_LCTRL;
	  else if( checkKeyword("SHIFT") )      modifiers = modifiers | MOD_LSHIFT;
	  else if( checkKeyword("ALT") )        modifiers = modifiers | MOD_LALT;
	  else if( checkKeyword("RGUI") )       modifiers = modifiers | MOD_RGUI;
	  else if( checkKeyword("RCTRL") )      modifiers = modifiers | MOD_RCTRL;
	  else if( checkKeyword("RSHIFT") )     modifiers = modifiers | MOD_RSHIFT;
	  else if( checkKeyword("RALT") )       modifiers = modifiers | MOD_RALT;
	  
	  else if( checkKeyword("MOUSE_LEFTCLICK") )  {mousedata = mousedata | MOUSE_LEFTCLICK;  PayloadInfo.PayloadFlags |= (1<<3);}
	  else if( checkKeyword("MOUSE_RIGHTCLICK") ) {mousedata = mousedata | MOUSE_RIGHTCLICK; PayloadInfo.PayloadFlags |= (1<<3);}
	  else if( checkKeyword("MOUSE_MIDCLICK") )   {mousedata = mousedata | MOUSE_MIDCLICK;   PayloadInfo.PayloadFlags |= (1<<3);}
	  
	  else if( checkKeyword("MOUSE_RIGHT ") )      {mousedataPointer[1] = +(checkDecValue() % 128); PayloadInfo.PayloadFlags |= (1<<3);}
	  else if( checkKeyword("MOUSE_LEFT ") )       {mousedataPointer[1] = -(checkDecValue() % 128); PayloadInfo.PayloadFlags |= (1<<3);}
	  else if( checkKeyword("MOUSE_DOWN ") )       {mousedataPointer[2] = +(checkDecValue() % 128); PayloadInfo.PayloadFlags |= (1<<3);}
	  else if( checkKeyword("MOUSE_UP ") )         {mousedataPointer[2] = -(checkDecValue() % 128); PayloadInfo.PayloadFlags |= (1<<3);}
	  else if( checkKeyword("MOUSE_SCROLLUP ") )   {mousedataPointer[3] = +(checkDecValue() % 128); PayloadInfo.PayloadFlags |= (1<<3);}
	  else if( checkKeyword("MOUSE_SCROLLDOWN ") ) {mousedataPointer[3] = -(checkDecValue() % 128); PayloadInfo.PayloadFlags |= (1<<3);}
	  
	  //key press commands are only valid if no more than 4 non-modifier keys are pressed simultaneously
	  else if( (keycodePointer[3] == 0) && checkKeyword("KEYCODE 0x") )  keycodes = (keycodes << 8) | (checkHexValue() % 222);
	  else if( (keycodePointer[3] == 0) && checkKeyword("KEYCODE ") )    keycodes = (keycodes << 8) | (checkDecValue() % 222);
	  else if( (keycodePointer[3] == 0) && checkKeyword("MENU") )        keycodes = (keycodes << 8) | KB_COMPOSE;
	  else if( (keycodePointer[3] == 0) && checkKeyword("APP") )         keycodes = (keycodes << 8) | KB_COMPOSE;
	  else if( (keycodePointer[3] == 0) && checkKeyword("ENTER") )       keycodes = (keycodes << 8) | KB_RETURN;
	  else if( (keycodePointer[3] == 0) && checkKeyword("RETURN") )      keycodes = (keycodes << 8) | KB_RETURN;
	  else if( (keycodePointer[3] == 0) && checkKeyword("DOWNARROW") )   keycodes = (keycodes << 8) | KB_DOWNARROW;
	  else if( (keycodePointer[3] == 0) && checkKeyword("LEFTARROW") )   keycodes = (keycodes << 8) | KB_LEFTARROW;
	  else if( (keycodePointer[3] == 0) && checkKeyword("RIGHTARROW") )  keycodes = (keycodes << 8) | KB_RIGHTARROW;
	  else if( (keycodePointer[3] == 0) && checkKeyword("UPARROW") )     keycodes = (keycodes << 8) | KB_UPARROW;
	  else if( (keycodePointer[3] == 0) && checkKeyword("DOWN") )        keycodes = (keycodes << 8) | KB_DOWNARROW;
	  else if( (keycodePointer[3] == 0) && checkKeyword("LEFT") )        keycodes = (keycodes << 8) | KB_LEFTARROW;
	  else if( (keycodePointer[3] == 0) && checkKeyword("RIGHT") )       keycodes = (keycodes << 8) | KB_RIGHTARROW;
	  else if( (keycodePointer[3] == 0) && checkKeyword("UP") )          keycodes = (keycodes << 8) | KB_UPARROW;
	  else if( (keycodePointer[3] == 0) && checkKeyword("PAUSE") )       keycodes = (keycodes << 8) | KB_PAUSE;
	  else if( (keycodePointer[3] == 0) && checkKeyword("BREAK") )       keycodes = (keycodes << 8) | KB_PAUSE;
	  else if( (keycodePointer[3] == 0) && checkKeyword("CAPSLOCK") )    keycodes = (keycodes << 8) | KB_CAPSLOCK;
	  else if( (keycodePointer[3] == 0) && checkKeyword("DELETE") )      keycodes = (keycodes << 8) | KB_DELETE;
	  else if( (keycodePointer[3] == 0) && checkKeyword("END")  )        keycodes = (keycodes << 8) | KB_END;
	  else if( (keycodePointer[3] == 0) && checkKeyword("ESCAPE") )      keycodes = (keycodes << 8) | KB_ESCAPE;
	  else if( (keycodePointer[3] == 0) && checkKeyword("ESC") )         keycodes = (keycodes << 8) | KB_ESCAPE;
	  else if( (keycodePointer[3] == 0) && checkKeyword("HOME") )        keycodes = (keycodes << 8) | KB_HOME;
	  else if( (keycodePointer[3] == 0) && checkKeyword("INSERT") )      keycodes = (keycodes << 8) | KB_INSERT;
	  else if( (keycodePointer[3] == 0) && checkKeyword("NUMLOCK") )     keycodes = (keycodes << 8) | KP_NUMLOCK;
	  else if( (keycodePointer[3] == 0) && checkKeyword("PAGEUP") )      keycodes = (keycodes << 8) | KB_PAGEUP;
	  else if( (keycodePointer[3] == 0) && checkKeyword("PAGEDOWN") )    keycodes = (keycodes << 8) | KB_PAGEDOWN;
	  else if( (keycodePointer[3] == 0) && checkKeyword("PRINTSCREEN") ) keycodes = (keycodes << 8) | KB_PRINTSCREEN;
	  else if( (keycodePointer[3] == 0) && checkKeyword("SCROLLLOCK") )  keycodes = (keycodes << 8) | KB_SCROLLLOCK;	  
	  else if( (keycodePointer[3] == 0) && checkKeyword("SPACEBAR") )    keycodes = (keycodes << 8) | KB_SPACEBAR;
	  else if( (keycodePointer[3] == 0) && checkKeyword("SPACE") )       keycodes = (keycodes << 8) | KB_SPACEBAR;
	  else if( (keycodePointer[3] == 0) && checkKeyword("TAB") )         keycodes = (keycodes << 8) | KB_TAB;
	  else if( (keycodePointer[3] == 0) && checkKeyword("F12") )         keycodes = (keycodes << 8) | KB_F12;      
	  else if( (keycodePointer[3] == 0) && checkKeyword("F11") )         keycodes = (keycodes << 8) | KB_F11;
	  else if( (keycodePointer[3] == 0) && checkKeyword("F10") )         keycodes = (keycodes << 8) | KB_F10;
	  else if( (keycodePointer[3] == 0) && checkKeyword("F9") )          keycodes = (keycodes << 8) | KB_F9;
	  else if( (keycodePointer[3] == 0) && checkKeyword("F8") )          keycodes = (keycodes << 8) | KB_F8;
	  else if( (keycodePointer[3] == 0) && checkKeyword("F7") )          keycodes = (keycodes << 8) | KB_F7;
	  else if( (keycodePointer[3] == 0) && checkKeyword("F6") )          keycodes = (keycodes << 8) | KB_F6;     
	  else if( (keycodePointer[3] == 0) && checkKeyword("F5") )          keycodes = (keycodes << 8) | KB_F5;
	  else if( (keycodePointer[3] == 0) && checkKeyword("F4") )          keycodes = (keycodes << 8) | KB_F4;
	  else if( (keycodePointer[3] == 0) && checkKeyword("F3") )          keycodes = (keycodes << 8) | KB_F3;	       
	  else if( (keycodePointer[3] == 0) && checkKeyword("F2") )          keycodes = (keycodes << 8) | KB_F2;	       
	  else if( (keycodePointer[3] == 0) && checkKeyword("F1") )          keycodes = (keycodes << 8) | KB_F1;
	  
	  else if( keycodePointer[3] == 0)//εάν η λέξη δεν αναγνωρίζετε αλλά είναι σωστός ASCII χαρκατήρας
	    {
	      keycodes = (keycodes << 8) | (Keymap[ *(PayloadInfo.PayloadPointer) - 32 ] & 0x7F);
	      if( Keymap[  *PayloadInfo.PayloadPointer - 32 ] & (1<<7) ) modifiers = modifiers | MOD_LSHIFT;
	      if( Keymap[ (*PayloadInfo.PayloadPointer - 32) / 8 + 95 ] & (1 << (*PayloadInfo.PayloadPointer % 8)) ) modifiers = modifiers | MOD_RALT;
	      
	      
	      if( PayloadInfo.PayloadPointer < ((char*) &PayloadBuffer + 2047) ) PayloadInfo.PayloadPointer++;
	      else PayloadInfo.PayloadPointer = (char*) &PayloadBuffer;
	    }
	  
	  
	       if(keycodePointer[0] == keycodePointer[1]) keycodes = keycodes >> 8;
	  else if(keycodePointer[0] == keycodePointer[2]) keycodes = keycodes >> 8;
	  else if(keycodePointer[0] == keycodePointer[3]) keycodes = keycodes >> 8;
	}
    }
  
  
  sendKBreport(modifiers, keycodes);
  if(PayloadInfo.PayloadFlags & (1<<3)) sendMSreport(mousedata);
  
  if(PayloadInfo.PayloadFlags & (1<<2))
    { 
      PayloadInfo.HoldModifiers = modifiers;
      PayloadInfo.HoldKeycodes = keycodes;
      PayloadInfo.HoldMousedata = mousedata & 0x000000FF;
      PayloadInfo.PayloadFlags &= ~(1<<2);
    }

  
  sendKBreport(PayloadInfo.HoldModifiers, PayloadInfo.HoldKeycodes);
  if(PayloadInfo.PayloadFlags & (1<<3)) sendMSreport(PayloadInfo.HoldMousedata);
  
  if(  (PayloadInfo.DeviceFlags & (1<<4)) && (PayloadInfo.PayloadFlags & (1<<4)) ) delay_ms(PayloadInfo.DefaultDelay);
  if( !(PayloadInfo.DeviceFlags & (1<<4)) )                                        delay_ms(PayloadInfo.DefaultDelay);

  PayloadInfo.PayloadFlags &= ~(1<<4);
  PayloadInfo.PayloadFlags &= ~(1<<3);
  
  
  if( !(PayloadInfo.PayloadFlags & (1<<1)) ) PayloadInfo.RepeatStart = openedFileInfo.fptr + 1 - PayloadInfo.BytesLeft;
  
  //πήγαινε στην επόμενη γραμμή του script
  if( PayloadInfo.PayloadPointer < ((char*) &PayloadBuffer + 2047) ) PayloadInfo.PayloadPointer++;
  else PayloadInfo.PayloadPointer = (char*) &PayloadBuffer;
  
  
  if( (unsigned int) PayloadInfo.PayloadPointer >= commandStart) bytesUsed = (unsigned int) PayloadInfo.PayloadPointer - commandStart;
  else                                                           bytesUsed = (unsigned int) PayloadInfo.PayloadPointer - commandStart + 2048;  
  PayloadInfo.BytesLeft = PayloadInfo.BytesLeft - bytesUsed;
  
  return;
}

static void repeatDuckyCommands(unsigned int count)
{
  
  unsigned int repeatEnd = openedFileInfo.fptr + 1 - PayloadInfo.BytesLeft;
  
  
  if(repeatEnd == 0) return;
  
  NVIC_DisableIRQ(31);
  if(PayloadInfo.RepeatCount)
    { 
      if(PayloadInfo.RepeatCount == 0xFFFFFFFF) PayloadInfo.RepeatCount = count;
      f_lseek(&openedFileInfo, PayloadInfo.RepeatStart);
      PayloadInfo.RepeatCount--;
    }
  else
    {
      f_lseek(&openedFileInfo, repeatEnd);     
      PayloadInfo.RepeatCount = 0xFFFFFFFF;
      PayloadInfo.RepeatStart = repeatEnd;
      PayloadInfo.PayloadFlags &= ~(1<<1);	  
    }
  
  //φόρτωσε τα δεδομένα από την αρχή του PayloadBuffer
  f_read(&openedFileInfo, (char*) &PayloadBuffer, 2048, &BytesRead);
  PayloadInfo.PayloadPointer = (char*) &PayloadBuffer;
  PayloadInfo.BytesLeft = BytesRead + 1;
  if(BytesRead < 2048) PayloadBuffer[BytesRead] = 0x0A;
  PayloadInfo.PayloadFlags &= ~(1<<0);
  NVIC_EnableIRQ(31);
  
       if( checkKeyword("REPEAT_START") ) skipString();
  else if( checkKeyword("REPEAT_SIZE") )  skipString();
  else if( checkKeyword("REPEAT") )       skipString();
  
  return;
}


static char checkKeyword(char* referenceString)
{
  char* whereToCheck = PayloadInfo.PayloadPointer;

  
  while(*referenceString)
    {
      
      if( *whereToCheck != *referenceString ) return 0;  
      referenceString++;
      
      if( whereToCheck < ((char*) &PayloadBuffer + 2047) ) whereToCheck++;
      else whereToCheck = (char*) &PayloadBuffer;
    }
  
  
  PayloadInfo.PayloadPointer = whereToCheck;
  return 1;
}


static unsigned int checkDecValue()
{
  unsigned int result = 0;
  unsigned char digit = 0;
  unsigned char limit = 6;//maximum number of digits in a string to be interpreted
  
  while(limit)
    {
      limit--;
      
      
      if( ( *(PayloadInfo.PayloadPointer) > 47 ) && ( *(PayloadInfo.PayloadPointer) <  58 ) ) digit = *(PayloadInfo.PayloadPointer) - 48;
      else break;
      
      result = result * 10 + digit;//compute preliminary result
      
   
      if( PayloadInfo.PayloadPointer < ((char*) &PayloadBuffer + 2047) ) PayloadInfo.PayloadPointer++;
      else PayloadInfo.PayloadPointer = (char*) &PayloadBuffer;
    }
  
  return result;
}


static unsigned int checkHexValue()
{
  int result = 0;
  unsigned char digit = 0;
  unsigned char limit = 4;
  
  while(limit)
    {
      limit--;

      
           if( ( *(PayloadInfo.PayloadPointer) > 47 ) && ( *(PayloadInfo.PayloadPointer) <  58 ) ) digit = *(PayloadInfo.PayloadPointer) - 48;
      else if( ( *(PayloadInfo.PayloadPointer) > 64 ) && ( *(PayloadInfo.PayloadPointer) <  71 ) ) digit = *(PayloadInfo.PayloadPointer) - 65 + 10;
      else if( ( *(PayloadInfo.PayloadPointer) > 96 ) && ( *(PayloadInfo.PayloadPointer) < 103 ) ) digit = *(PayloadInfo.PayloadPointer) - 97 + 10;
      else break;
      
      result = result * 16 + digit;
      
      
      if( PayloadInfo.PayloadPointer < ((char*) &PayloadBuffer + 2047) ) PayloadInfo.PayloadPointer++;
      else PayloadInfo.PayloadPointer = (char*) &PayloadBuffer;
    }
  
  return result;
}


static void setFilename(char* newName)
{
  unsigned char i;
  
  for(i=0; i<13; i++) PayloadInfo.Filename[i] = 0x00;
  
  for(i=0; i<12; i++)
    { 

           if(   *(newName) == 46 )                           PayloadInfo.Filename[i] = *(newName);
      else if(   *(newName) == 95 )                           PayloadInfo.Filename[i] = *(newName);
      else if( ( *(newName) >  47 ) && ( *(newName) <  58 ) ) PayloadInfo.Filename[i] = *(newName);
      else if( ( *(newName) >  64 ) && ( *(newName) <  91 ) ) PayloadInfo.Filename[i] = *(newName);
      else if( ( *(newName) >  96 ) && ( *(newName) < 123 ) ) PayloadInfo.Filename[i] = *(newName);
      else break;
      
      newName++;
    }
  
  return;
}

//θέτει τον StringDescriptor_1 (serial number) σε νέα τιμή
static void setSerialNumber(char* newSerialNumber)
{
  unsigned char i;//used in a for() loop
  
  for(i=0; i<12; i++) StringDescriptor_1[i+1] = '0';
  
  for(i=0; i<12; i++)
    { 
      
           if( ( *(newSerialNumber) >  47 ) && ( *(newSerialNumber) <   58 ) ) StringDescriptor_1[i+1] = *(newSerialNumber);
      else if( ( *(newSerialNumber) >  64 ) && ( *(newSerialNumber) <   71 ) ) StringDescriptor_1[i+1] = *(newSerialNumber);
      else break;
      
      newSerialNumber++;
    }
  
  return;
}

static void sendString(char* stringStart)
{
  unsigned int keycodes;
  unsigned char* keycodePointer = (unsigned char*) &keycodes;
  unsigned short modifiers;
  unsigned short limit = 1000;
  
  if(PayloadInfo.HoldKeycodes >> 24) return;  
  
  while( ( *stringStart > 31 ) && ( *stringStart < 127 ) )
    {
      if(limit) limit--;
      else while(1) sendKBreport(MOD_NONE, KB_Reserved);
      
      keycodes  = PayloadInfo.HoldKeycodes;
      modifiers = PayloadInfo.HoldModifiers;
      
      keycodes = (keycodes << 8) | (Keymap[*stringStart - 32] & 0x7F);
      if( Keymap[*stringStart - 32] & (1<<7) ) modifiers = modifiers | MOD_LSHIFT;
      if( Keymap[ (*stringStart - 32) / 8 + 95 ] & (1 << (*stringStart % 8)) ) modifiers = modifiers | MOD_RALT;
      
      
           if(keycodePointer[0] == keycodePointer[1]) keycodes = keycodes >> 8;
      else if(keycodePointer[0] == keycodePointer[2]) keycodes = keycodes >> 8;
      else if(keycodePointer[0] == keycodePointer[3]) keycodes = keycodes >> 8;
            
      sendKBreport(modifiers, keycodes);
      delay_ms(PayloadInfo.StringDelay);
      sendKBreport(PayloadInfo.HoldModifiers, PayloadInfo.HoldKeycodes);
      delay_ms(PayloadInfo.StringDelay);
      
      
      if(stringStart == PayloadInfo.PayloadPointer)
	{
	  
	  if( PayloadInfo.PayloadPointer < ((char*) &PayloadBuffer + 2047) ) PayloadInfo.PayloadPointer++;
	  else PayloadInfo.PayloadPointer = (char*) &PayloadBuffer;
	  stringStart = PayloadInfo.PayloadPointer;
	}
      else stringStart++;
    }
  
  return;
}


static void sendKBreport(unsigned short modifiers, unsigned int keycodes)
{
  if(ControlInfo.HIDprotocol)
    { 
      
      *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX +  0) ) = (modifiers << 8) | 0x01;
      *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX +  2) ) = keycodes <<  8;
      *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX +  4) ) = keycodes >>  8;
      *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX +  6) ) = keycodes >> 24;
      *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX +  8) ) = 0;
      
      BTABLE->COUNT1_TX = 9;
      USB->EP1R = (1<<15)|(1<<10)|(1<<9)|(1<<7)|(1<<4)|(1<<0);
      while( (USB->EP1R & 0x0030) == 0x0030 );
    }
  
  else
    {
      
      *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX +  0) ) = modifiers;
      *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX +  2) ) = keycodes >>  0;
      *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX +  4) ) = keycodes >> 16;
      *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX +  6) ) = 0;
      
      BTABLE->COUNT1_TX = 8;/
      USB->EP1R = (1<<15)|(1<<10)|(1<<9)|(1<<7)|(1<<4)|(1<<0);
      while( (USB->EP1R & 0x0030) == 0x0030 );
    }
  
  return;
}


static void sendMSreport(unsigned int mousedata)
{
  
  *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX + 0) ) = (mousedata <<  8) | 0x02;
  *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX + 2) ) = mousedata >>  8;
  *( (unsigned short*) (BTABLE_BaseAddr + BTABLE->ADDR1_TX + 4) ) = mousedata >> 24;
  
  BTABLE->COUNT1_TX = 5;
  USB->EP1R = (1<<15)|(1<<10)|(1<<9)|(1<<7)|(1<<4)|(1<<0);
  while( (USB->EP1R & 0x0030) == 0x0030 );
  
  return;
}


static void skipSpaces()
{
  unsigned short limit = 10;
  
  while( *(PayloadInfo.PayloadPointer) == 32 )
    {
      
      if(limit == 0) skipString();
      else limit--;
      
      if( PayloadInfo.PayloadPointer < ((char*) &PayloadBuffer + 2047) ) PayloadInfo.PayloadPointer++;
      else PayloadInfo.PayloadPointer = (char*) &PayloadBuffer;
    }
  
  return;
}


static void skipString()
{
  unsigned short limit = 1000;
  
  while( *(PayloadInfo.PayloadPointer) != 0x0A )
    {
      if(limit) limit--;
      else while(1) sendKBreport(MOD_NONE, KB_Reserved);
      
      
      if( PayloadInfo.PayloadPointer < ((char*) &PayloadBuffer + 2047) ) PayloadInfo.PayloadPointer++;
      else PayloadInfo.PayloadPointer = (char*) &PayloadBuffer;
    }
  
  return;
}

//----------------------------------------------------------------------------------------------------------------------

static inline void delay_us(unsigned int delay)
{
  if(delay)
    {
      TIM2->CR1 = (1<<7)|(1<<3)|(1<<2);
      TIM2->ARR = delay * 48 - 10;
      TIM2->PSC = 0;
      TIM2->EGR = (1<<0);
      TIM2->SR = 0;
      TIM2->CR1 = (1<<7)|(1<<3)|(1<<2)|(1<<0);
      while(TIM2->CR1 & (1<<0));
    }
  
  return;
}

static void delay_ms(unsigned int delay)
{
  if(delay)
    {      
      TIM2->CR1 = (1<<7)|(1<<3)|(1<<2);
      TIM2->ARR = delay * 48;
      TIM2->PSC = 999;
      TIM2->EGR = (1<<0);
      TIM2->SR = 0;
      TIM2->CR1 = (1<<7)|(1<<3)|(1<<2)|(1<<0);
      while(TIM2->CR1 & (1<<0));
    }
  
  return;
}

static void restart_tim6(unsigned short time)
{

  TIM6->CR1 = (1<<7)|(1<<3)|(1<<2);
  TIM6->ARR = time * 48 - 1;
  TIM6->PSC = 999; 
  TIM6->EGR = (1<<0);
  TIM6->SR = 0;
  TIM6->CR1 = (1<<7)|(1<<3)|(1<<2)|(1<<0);
  
  return;
}

static void enter_bootloader()
{
  
  void (*bootloader)(void) = (void (*)(void)) ( *((unsigned int*) (0x1FFFC804U)) );
  
  
  NVIC_DisableIRQ(31);
  NVIC_DisableIRQ(18);
  NVIC_DisableIRQ(10);

  
  NVIC_ClearPendingIRQ(31);
  NVIC_ClearPendingIRQ(18);
  NVIC_ClearPendingIRQ(10);
  
  
  GPIOB->ODR   = 0x00000000;
  GPIOB->MODER = 0x00000000;
  GPIOA->ODR   = 0x00000000;
  GPIOA->MODER = 0x28000000;
  GPIOA->PUPDR = 0x24000000;

  
  RCC->AHBENR  = 0x00000014;
  RCC->APB1ENR = 0x00000000;
  RCC->APB2ENR = 0x00000000;
  
  RCC->CR |= (1<<0);
  while( !(RCC->CR & (1<<1)) );
  RCC->CFGR = 0;
  while( !((RCC->CFGR & 0x0F) == 0b0000) ); 
  RCC->CR = 0x0083;

  __DSB();
  __set_MSP(0x20003FFC);
  __ISB();
  bootloader();
  
  NVIC_SystemReset();
  return;
}

//----------------------------------------------------------------------------------------------------------------------

static void saveOSfingerprint()  
{
  unsigned char* currentFingerprint = (unsigned char*) &ControlInfo.OSfingerprintData;
  unsigned char i;//used in a for() loop
  
  
  if( !f_open(&openedFileInfo, "0:/fingerdb/current.fgp", FA_READ | FA_OPEN_EXISTING) )
    {
      
      f_read(&openedFileInfo, (char*) &PayloadBuffer, 40, &BytesRead );
      f_close( &openedFileInfo );
      
     
      for(i=0; i<40; i++)
	{
	  if( (i % 4) == 2 ) continue;
	  if( currentFingerprint[i] != PayloadBuffer[i] ) break;
	}
      
      if(i<40)
	{
	  
	  if( !f_open(&openedFileInfo,  "0:/fingerdb/previous.fgp", FA_WRITE | FA_CREATE_ALWAYS) )
	    {
	      f_write( &openedFileInfo, (unsigned char*) &PayloadBuffer, 40, &BytesRead );
	      f_close( &openedFileInfo );
	    }
	}
    }
  
  
  if( !f_open(&openedFileInfo,  "0:/fingerdb/current.fgp", FA_WRITE | FA_CREATE_ALWAYS) )
    {
      f_write( &openedFileInfo, (unsigned char*) &ControlInfo.OSfingerprintData, 40, &BytesRead );
      f_close( &openedFileInfo );
    }
  
  return;
}


static void checkOSfingerprint()
{
  unsigned char* currentFingerprint = (unsigned char*) &ControlInfo.OSfingerprintData;
  unsigned char i;
  
  DIR     fingerdbDirInfo;   
  FILINFO fingerdbFileInfo;  
  DIR     OSspecificDirInfo; 
  FILINFO OSspecificFileInfo;
  
  for(i=0; i<13; i++) PayloadInfo.Filename[i] = 0x00;
  
  
  FATFSresult  = f_opendir(&fingerdbDirInfo, "0:/fingerdb");
  FATFSresult |= f_readdir(&fingerdbDirInfo, &fingerdbFileInfo);  
  
  
  while( !FATFSresult && fingerdbFileInfo.fname[0] )
    {
      if(fingerdbFileInfo.fattrib & AM_DIR)
	{
	  FATFSresult  = f_chdir("0:/fingerdb");
	  FATFSresult |= f_findfirst(&OSspecificDirInfo, &OSspecificFileInfo, fingerdbFileInfo.fname, "*.fgp");
	  FATFSresult |= f_chdir(fingerdbFileInfo.fname);
	  
	  while( !FATFSresult && OSspecificFileInfo.fname[0] )
	    {
	      
	      f_open( &openedFileInfo, OSspecificFileInfo.fname, FA_READ | FA_OPEN_EXISTING);
	      f_read( &openedFileInfo, (char*) &PayloadBuffer, 40, &BytesRead );
	      f_close(&openedFileInfo);
	      
	      
	      for(i=0; i<40; i++)
		{
		  if( (i % 4) == 2 ) continue;
		  if( currentFingerprint[i] != PayloadBuffer[i] ) break;
		}
	      
	      if(i == 40)
		{
		  for(i=0; i<13; i++)
		    {
		      
		      PayloadInfo.Filename[i] = fingerdbFileInfo.fname[i];
		      
		      
		      if( (PayloadInfo.Filename[i] == 0x00) && (i < 9) )
			{
			  PayloadInfo.Filename[i + 0] = '.';
			  PayloadInfo.Filename[i + 1] = 't';
			  PayloadInfo.Filename[i + 2] = 'x';
			  PayloadInfo.Filename[i + 3] = 't';
			  PayloadInfo.Filename[i + 4] = 0x00;
			  break;
			}
		    }
		}
	      
	      
	      if( PayloadInfo.Filename[0] ) break;
	      else FATFSresult = f_findnext(&OSspecificDirInfo, &OSspecificFileInfo);
	    }
	  
	  f_closedir(&OSspecificDirInfo);
	}
      
      
      if( PayloadInfo.Filename[0] ) break;
      else FATFSresult = f_readdir(&fingerdbDirInfo, &fingerdbFileInfo);
    }

  
  if( PayloadInfo.Filename[0] == 0 ) setFilename("other.txt");
  
  return;
}
