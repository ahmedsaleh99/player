/*
 *  Player - One Hell of a Robot Server
 *  Copyright (C) 2004  
 *      Elena Lazkano' , Aitzol Astigarraga 
 *      ccplaore@si.ehu.es, ccbaspaa@si.ehu.es
 *                      
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/** @addtogroup drivers Drivers */
/** @{ */
/** @defgroup player_driver_cannonVCC4 cannonVCC4

The cannonVCC4 driver controls a Cannon VC-C4 PTZ camera.

The cannonVCC4 driver operates over a direct serial link, not
through the P2OS microcontroller's AUX port, as is the normal
configuration for ActivMedia robots.  You may have to make or buy
a cable to connect your camera to a normal serial port.  Look <a
href="http://playerstage.sourceforge.net/faq.html#evid30_wiring">here</a>
for more information and wiring instructions.

The VC-C4 is a rather slow device. Actions and specially initialization
steps need time to take effect.

VC-C4 ranges:
- Pan range in degrees: -98...98
- Tilt in degrees: -30...88
- Zoom units: 0..2000

The cannonVCC4 driver only support position control.

@par Compile-time dependencies

- none

@par Provides

- @ref player_interface_ptz

@par Requires

- none

@par Configuration requests

- none

@par Configuration file options

- port (string)
  - Default: "/dev/ttyS1"
  - The serial port where the camera is connected

@par Example

@verbatim
driver 
(
  name "cannonVCC4" 
  provides ["ptz:0"]
  port "/dev/ttyS1"
)
@endverbatim

@todo
Add more functionalities. Actually Only pan, tilt and zoom
values can be set up.

@par Authors

Elena Lazkano', Aitzol Astigarraga

*/

/** @} */


#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>

#include <sys/stat.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>  /* for struct sockaddr_in, htons(3) */
#include <math.h>

#include <replace.h> /* for poll(2) */

#include <driver.h>
#include <drivertable.h>
#include <player.h>



#define CAM_ERROR_NONE 0x30
#define CAM_ERROR_BUSY 0x31
#define CAM_ERROR_PARAM 0x35
#define CAM_ERROR_MODE 0x39

#define PTZ_SLEEP_TIME_USEC 300000

#define MAX_PTZ_COMMAND_LENGTH 16
#define MAX_PTZ_REQUEST_LENGTH 14
#define COMMAND_RESPONSE_BYTES 6

#define PTZ_PAN_MAX 98.0   // 875 units 0x36B
#define PTZ_TILT_MAX 88.0  // 790 units 0x316
#define PTZ_TILT_MIN -30.0 // -267 units 0x10B
#define MAX_ZOOM 2140

#define PT_BUFFER_INC       512
#define PT_READ_TIMEOUT   10000
#define PT_READ_TRIALS        2

#define DEFAULT_PTZ_PORT "/dev/ttyS1"

#define kaska printf("KASKA\n");

static unsigned int error_code;

struct timeval substract_time(struct timeval *, struct timeval *);

class cannonVCC4:public Driver 
{
 private:
  bool ptz_fd_blocking;

  int SendCommand(unsigned char* str, int len);
  int SendRequest(unsigned char* str, int len);
  int ReceiveCommandAnswer();
  int ReceiveRequestAnswer(unsigned char* reply);
  int setControlMode();
  int setPower(int);
  int setDefaultTiltRange();
  int configurePort();
  int read_ptz(unsigned char *data, int size);
  // this function will be run in a separate thread
  virtual void Main();

  virtual int SendAbsPanTilt(int pan, int tilt);
  virtual int SendAbsZoom(int zoom);
  virtual int GetAbsZoom(int* zoom);
  virtual int GetAbsPanTilt(int* pan, int* tilt);
  virtual void PrintPacket(char* str, unsigned char* cmd, int len);


 public:
  //player_ptz_cmd_t* command;
  //player_ptz_data_t* data;

  int ptz_fd; // ptz device file descriptor
  /* device used to communicate with the ptz */
  char ptz_serial_port[MAX_FILENAME_SIZE];

  // Min and max values for camera field of view (degrees).
  // These are used to compute appropriate zoom values.

  cannonVCC4(ConfigFile* cf, int section);

  virtual int Setup();
  virtual int Shutdown();
};

/************************************************************************/
Driver* 
cannonVCC4_Init(ConfigFile* cf, int section)
{
  return((Driver*)(new cannonVCC4(cf, section)));
}

/************************************************************************/

// a driver registration function
void 
cannonVCC4_Register(DriverTable* table)
{
  table->AddDriver("cannonVCC4", cannonVCC4_Init);
}

/************************************************************************/

cannonVCC4::cannonVCC4(ConfigFile* cf, int section) :
  Driver(cf,section,PLAYER_PTZ_CODE, PLAYER_ALL_MODE,
          sizeof(player_ptz_data_t),sizeof(player_ptz_cmd_t),0,0)
{
  ptz_fd = -1;

  strncpy(ptz_serial_port,
          cf->ReadString(section, "port", DEFAULT_PTZ_PORT),
          sizeof(ptz_serial_port));
}

/************************************************************************/
int 
cannonVCC4::Setup()
{
  int pan,tilt;
  int flags;
  int err = 0;

  printf("PTZ connection initializing (%s)...", ptz_serial_port);
  fflush(stdout);

  // open it.  non-blocking at first, in case there's no ptz unit.
  if((ptz_fd = 
      open("/dev/ttyS1",
	   O_RDWR | O_SYNC | O_NONBLOCK, S_IRUSR | S_IWUSR )) < 0 )
  {
    perror("cannonVCC4::Setup():open():");
    return(-1);
  }  
  
  if(tcflush(ptz_fd, TCIFLUSH ) < 0 )
  {
    perror("cannonVCC4::Setup():tcflush():");
    close(ptz_fd);
    ptz_fd = -1;
    return(-1);
  }

  if (configurePort())
    {
      fprintf(stderr, "setup(); could not configure serial port.\n");
      close(ptz_fd);
      return -1;
    }

  //ptz_fd_blocking = false;
  err = setPower(0);
  fprintf(stderr, "\nPowering off/on the camera!!!!!!!!!!!!!!\n");  
  err = setPower(1);  
  while (error_code == CAM_ERROR_BUSY)
    {
      fprintf(stdout, "power on busy: %x\n", error_code);
      err = setPower(1);
    }
  if ((err != 0) && 
      (error_code != CAM_ERROR_NONE) && (error_code != CAM_ERROR_MODE))
    {
      printf("Could not set power on: %x\n", error_code);
      setPower(0);
      close(ptz_fd);
      return -1;
    }
  err = setControlMode();
  while (error_code == CAM_ERROR_BUSY)
    {
      printf("control mode busy: %x\n", error_code);
      err = setControlMode();
    }
  if (err)
    {
      printf("Could not set control mode\n");
      setPower(0);
      close(ptz_fd);
      return -1;
    }
  /* try to get current state, just to make sure we actually have a camera */
  err = GetAbsPanTilt(&pan,&tilt);
  if (err)
    {
      printf("Couldn't connect to PTZ device most likely because the camera\n"
	     "is not connected or is connected not to %s; %x\n", 
	     ptz_serial_port, error_code);
      setPower(0);
      close(ptz_fd);
      ptz_fd = -1;
      return(-1);
    }
  //fprintf(stdout, "getAbsPantilt: %d %d\n", pan, tilt);
  err = setDefaultTiltRange();
  while (error_code == CAM_ERROR_BUSY)
    {
      printf("control mode busy: %x\n", error_code);
      err = setDefaultTiltRange();
    }
  if (err)
    {
      printf("Could not set default tilt range\n");
      setPower(0);
      close(ptz_fd);
      return -1;
    }
  
  /* ok, we got data, so now set NONBLOCK, and continue */
  if ((flags = fcntl(ptz_fd, F_GETFL)) < 0)
    {
      perror("cannonVCC4::Setup():fcntl()");
      close(ptz_fd);
      ptz_fd = -1;
      return(1);
    }

  if (fcntl(ptz_fd, F_SETFL, flags ^ O_NONBLOCK) < 0)
    {
      perror("cannonVCC4::Setup():fcntl()");
      close(ptz_fd);
      ptz_fd = -1;
      return(1);
    }
  //  ptz_fd_blocking = true;
  puts("Done.");
  
  // zero the command and data buffers
  player_ptz_data_t data;
  player_ptz_cmd_t cmd;

  data.pan = data.tilt = data.zoom = 0;
  cmd.pan = cmd.tilt = cmd.zoom = 0;

  PutData((void*)&data,sizeof(data),NULL);
  PutCommand(this->device_id,(void*)&cmd,sizeof(cmd),NULL);
  
  //request_state = IDLE;
  // start the thread to talk with the camera
  StartThread();
  
  return(0);
}
/************************************************************************/
int 
cannonVCC4::configurePort()
{
  struct termios tio;

  if(tcgetattr(ptz_fd, &tio) < 0 )
    {
      perror("cannonVCC4::configurePort():tcgetattr():");
      close(ptz_fd);
      return(-1);
    }

  cfmakeraw(&tio);
  cfsetospeed(&tio, B9600);
  cfsetispeed(&tio, B9600);
  if (tcsetattr(ptz_fd, TCSAFLUSH, &tio) < 0) 
    {
      fprintf(stderr, "Could not config serial port.\n");
      return -1;
    }
  tcgetattr(ptz_fd, &tio);

  /* check for hardware flow control */
  
  //tio.c_cflag |= CRTSCTS;
  
  tio.c_cflag &= ~CRTSCTS;
  tcsetattr(ptz_fd, TCSAFLUSH, &tio);
  return 0;
}

/************************************************************************/
int 
cannonVCC4::Shutdown()
{
  puts("cannonVCC4::Shutdown");
  
  if(ptz_fd == -1)
    return(0);
  
  StopThread();
  usleep(PTZ_SLEEP_TIME_USEC);
  SendAbsPanTilt(0,0);
  usleep(PTZ_SLEEP_TIME_USEC);
  SendAbsZoom(0);
  setPower(0);
  if(close(ptz_fd))
    perror("cannonVCC4::Shutdown():close():");
  ptz_fd = -1;
  puts("PTZ camera has been shutdown");
  return(0);
}

/************************************************************************/
int
cannonVCC4::SendCommand(unsigned char *str, int len)
{
  int err = 0;

  if(len > MAX_PTZ_COMMAND_LENGTH)
  {
    fprintf(stderr, 
	    "CANNONvcc4::SendCommand(): message is too large (%d bytes)\n",
	    len);
    return(-1);
  }

  err = write(ptz_fd, str, len);

  if (err < 0)
  {
    perror("cannonVCC4::Send():write():");
    return(-1);
  }
  return(0);
}
/************************************************************************/
int
cannonVCC4::SendRequest(unsigned char* str, int len)
{
  int err = 0;

  if(len > MAX_PTZ_REQUEST_LENGTH)
  {
    fprintf(stderr, 
	    "cannonVCC4::SendRequest(): message is too large (%d bytes)\n",
	    len);
    return(-1);
  }
  err = write(ptz_fd, str, len);

  if(err < 0)
    {
      perror("cannonVCC4::Send():write():");
      return(-1);
    }
  return 0;
}


/************************************************************************/
void
cannonVCC4::PrintPacket(char* str, unsigned char* cmd, int len)
{
  for(int i=0;i<len;i++)
    printf(" %.2x", cmd[i]);
  puts("");
}

/************************************************************************/

int
cannonVCC4::SendAbsPanTilt(int pan, int tilt)
{
  unsigned char command[MAX_PTZ_COMMAND_LENGTH];
  int convpan, convtilt;
  unsigned char buf[5];
  int ppan, ttilt;

  ppan = pan; ttilt = tilt;
  if (abs(pan) > PTZ_PAN_MAX) 
    {
      if(pan < -PTZ_PAN_MAX)
	ppan = (int)-PTZ_PAN_MAX;
      else 
	if(pan > PTZ_PAN_MAX)
	  ppan = (int)PTZ_PAN_MAX;
      //puts("Camera pan angle thresholded");
    }
  if (tilt > PTZ_TILT_MAX) 
    ttilt = (int)PTZ_TILT_MAX;
  else 
    if(tilt < PTZ_TILT_MIN)
      ttilt = (int)PTZ_TILT_MIN;
  //puts("Camera pan angle thresholded");
  
  //puts("Camera tilt angle thresholded");

  convpan = (int)floor(ppan/.1125) + 0x8000;
  convtilt = (int)floor(ttilt/.1125) + 0x8000;
//   fprintf(stdout, "ppan: %d ttilt: %d conpan: %d contilt: %d\n",
// 	  ppan,ttilt,convpan,convtilt);
  command[0] = 0xFF;
  command[1] = 0x30;
  command[2] = 0x30;
  command[3] = 0x00;
  command[4] = 0x62;
  // pan position

  sprintf((char *)buf, "%X", convpan);

  command[5] = buf[0]; 
  command[6] = buf[1];
  command[7] = buf[2];
  command[8] = buf[3];
  // tilt position
  sprintf((char *)buf, "%X", convtilt);
  command[9]  = buf[0];
  command[10] = buf[1];
  command[11] = buf[2];
  command[12] = buf[3];
  command[13] = (unsigned char) 0xEF;
  SendCommand(command, 14);
  //  PrintPacket( "sendabspantilt: ", command, 14);
  return(ReceiveCommandAnswer());
}
/************************************************************************/
int 
cannonVCC4::setDefaultTiltRange()
{
  unsigned char command[MAX_PTZ_COMMAND_LENGTH];
  unsigned char buf[8];
  int maxtilt, mintilt;

  command[0] = 0xFF;
  command[1] = 0x30;
  command[2] = 0x30;
  command[3] = 0x00;
  command[4] = 0x64;
  command[5] = 0x31;

  mintilt = (int)(floor(PTZ_TILT_MIN/.1125) + 0x8000);
  sprintf((char *)buf, "%X", mintilt);

  command[6] = buf[0]; 
  command[7] = buf[1];
  command[8] = buf[2];
  command[9] = buf[3];
  // tilt position
  maxtilt = (int)(floor(PTZ_TILT_MAX/.1125) + 0x8000);
  sprintf((char *)buf, "%X", maxtilt);

  command[10] = buf[0];
  command[11] = buf[1];
  command[12] = buf[2];
  command[13] = buf[3];
  command[14] = (unsigned char) 0xEF;

  SendCommand(command, 15);
  //  PrintPacket( "setDefaultRange: ", command, 15);
  return(ReceiveCommandAnswer());
  
}

/************************************************************************/
int
cannonVCC4::GetAbsPanTilt(int* pan, int* tilt)
{
  unsigned char command[MAX_PTZ_COMMAND_LENGTH];
  unsigned char reply[MAX_PTZ_REQUEST_LENGTH];
  int reply_len;
  unsigned char buf[4];
  char byte;
  unsigned int u_val;
  int val;
  int i;

  command[0] = 0xFF;
  command[1] = 0x30;
  command[2] = 0x30;
  command[3] = 0x00;
  command[4] = 0x63;
  command[5] = 0xEF;

  if (SendRequest(command, 6))
    return(-1);
  //  PrintPacket("getabspantilt: ", command, 6);
  reply_len = ReceiveRequestAnswer(reply);
  // remove the ascii encoding, and put into 4-byte array
  for (i = 0; i < 4; i++)
    {
      byte = reply[i+5];
      if (byte < 0x40)
	byte = byte - 0x30;
      else
	byte = byte - 'A' + 10;
      buf[i] = byte;
    }

  // convert the 4-bytes into a number
  u_val = buf[0] * 0x1000 + buf[1] * 0x100 + buf[2] * 0x10 + buf[3];

  // convert the number to a value that's meaningful, based on camera specs
  val = (int)(((int)u_val - (int)0x8000) * 0.1125);

  // now set myPan to the response received for where the camera thinks it is
  *pan = val;

  // repeat the steps for the tilt value
  for (i = 0; i < 4; i++)
    {
      byte = reply[i+9];
      if (byte < 0x40)
	byte = byte - 0x30;
      else
	byte = byte - 'A' + 10;
      buf[i] = byte;
    }
  u_val = buf[0] * 0x1000 + buf[1] * 0x100 + buf[2] * 0x10 + buf[3];
  val =(int)(((int)u_val  - (int)0x8000) * 0.1125);
  *tilt = val;

  return(0);
}

/************************************************************************/
int
cannonVCC4::GetAbsZoom(int* zoom)
{
  unsigned char command[MAX_PTZ_COMMAND_LENGTH];
  unsigned char reply[MAX_PTZ_REQUEST_LENGTH];
  int reply_len;
  char byte;
  unsigned char buf[4];
  unsigned int u_zoom;
  int i;
  
  command[0] = 0xFF;
  command[1] = 0x30;
  command[2] = 0x30;
  command[3] = 0x00;
  command[4] = 0xA4;
  command[5] = 0xEF;

  if (SendRequest(command, 6))
    return(-1);
  //  PrintPacket( "getabszoom: ", command, 6);

  reply_len = ReceiveRequestAnswer(reply);

//   if (reply_len < 0)
//     return -1;

  // remove the ascii encoding, and put into 2 bytes
  for (i = 0; i < 4; i++)
  {
    byte = reply[i + 5];
    if (byte < 0x40)
      byte = byte - 0x30;
    else
      byte = byte - 'A' + 10;
    buf[i] = byte;
  }

  // convert the 2 bytes into a number
  u_zoom = 0;
  for (i = 0; i < 4; i++)
    u_zoom += buf[i] * (unsigned int) pow(16.0, (double)(3 - i));

  *zoom = u_zoom;  
  return(0);
}

/************************************************************************/
int
cannonVCC4::SendAbsZoom(int zoom)
{
  unsigned char command[MAX_PTZ_COMMAND_LENGTH];
  unsigned char buf[5];
  int i;

  if(zoom < 0) 
    zoom = 0;
  //puts("Camera zoom thresholded");
  
  else 
    if(zoom > MAX_ZOOM)
      zoom = MAX_ZOOM;

  command[0] = 0xFF;
  command[1] = 0x30;
  command[2] = 0x30;
  command[3] = 0x00;
  command[4] = 0xB3;

  sprintf((char *)buf, "%4X", zoom);

  for (i=0;i<3;i++)
    if (buf[i] == ' ')
      buf[i] = '0';

  // zoom position
  command[5] = buf[0]; 
  command[6] = buf[1];  
  command[7] = buf[2];  
  command[8] = buf[3];  
  command[9] = 0xEF;

  if (SendCommand(command, 10))
    return -1;
  //  PrintPacket( "setabszoom: ", command, 10);
  return (ReceiveCommandAnswer());
}

/************************************************************************/
 int
 cannonVCC4::read_ptz(unsigned char *reply, int size)
{
  struct pollfd poll_fd;
  int len = 0;

  poll_fd.fd = ptz_fd;
  poll_fd.events = POLLIN;
  
  if (poll(&poll_fd, 1, 1000) <= 0)
    return -1;
  len = read(ptz_fd, reply, size);
  if (len < 0)
    return -1;
  
  return len;
}


/************************************************************************/
int
cannonVCC4::ReceiveCommandAnswer()
{
  int num;
  unsigned char reply[COMMAND_RESPONSE_BYTES];
  int len = 0;
  unsigned char byte;
  int err;

  bzero(reply, COMMAND_RESPONSE_BYTES);

  for (num = 0; num <= COMMAND_RESPONSE_BYTES + 1; num++)
    {
      // if we don't get any bytes, or if we've just exceeded the limit
      // then return null
      err = read_ptz(&byte, 1);
      if (byte == (unsigned char)0xFE)
	{
	  reply[0] = byte;
	  len ++;
	  break;
	}
    }
  if (len == 0)
    return -1;

  // we got the header character so keep reading bytes for MAX_RESPONSE_BYTES more
  for(num = 1; num <= MAX_PTZ_REQUEST_LENGTH; num++)
    {
      err = read_ptz(&byte, 1);
      if (err == 0) 
	continue;
      if (err < 0)
	{
	  // there are no more bytes, so check the last byte for the footer
	  if (reply[len - 1] !=  (unsigned char)0xEF)
	    {
	      fputs("cannonVCC4::receiveRequest: Discarding bad packet.",
		    stderr);
	      return -1;
	    }
	  else
	    break;
	}
      else
	{
	  // add the byte to the array
	  reply[len] = byte;
	  len ++;
	}
    }

  // Check the response
  if (len != 6)
    {
      fputs("cannonVCC4::receiveCommand:Incorrect number of bytes in response packet.", stderr);
      return -1;
    }

  // check the header and footer
  if (reply[0] != (unsigned char)0xFE || reply[5] != (unsigned char)0xEF)
    {
      fputs("cannonVCC4::receiveCommand: Bad header or footer character in response packet.", stderr);
      return -1;
    }
  // so far so good.  Set myError to the error byte
  error_code = reply[3];
  //  PrintPacket("receivecommandasnwer: ", reply, 6);
  if (error_code == CAM_ERROR_NONE)
      return 0;

  return -1;

}

/************************************************************************/
int
cannonVCC4::ReceiveRequestAnswer(unsigned char *data)
{
  int num;
  unsigned char reply[MAX_PTZ_REQUEST_LENGTH];
  int len = 0;
  unsigned char byte;
  int err = 0;

  bzero(reply, MAX_PTZ_REQUEST_LENGTH);

  for (num = 0; num <= COMMAND_RESPONSE_BYTES + 1; num++)
    {
      // if we don't get any bytes, or if we've just exceeded the limit
      // then return null
      err = read_ptz(&byte, 1); 
      if (byte == (unsigned char)0xFE)
	{
	  reply[0] = byte;
	  len ++;
	  break;
	}
    }
  if (len == 0)
    return -1;
  // we got the header character so keep reading bytes for MAX_RESPONSE_BYTES more
  for(num = 1; num <= MAX_PTZ_REQUEST_LENGTH; num++)
    {
      err = read_ptz(&byte, 1);
      if (err == 0)
	continue;
      if (err < 0) 
	{
	  // there are no more bytes, so check the last byte for the footer
	  if (reply[len - 1] !=  (unsigned char)0xEF)
	    {
	      fputs("cannonVCC4::receiveRequest: Discarding bad packet.",
		    stderr);
	      return -1;
	    }
	  else
	    break;
	}
      else
	{
	  // add the byte to the array
	  reply[len] = byte;
	  len ++;
	}
    }
  // Check the response length: pt: 14; zoom: 10
  if (len != 6 && len != 8 && len != 14)
    {
      fputs("ArVCC4::packetHandler: Incorrect number of bytes in response packet.", stderr);
      return -1;
    }
  
  if (reply[0] !=  (unsigned char)0xFE || 
      reply[len - 1] != (unsigned char)0xEF)
    {
      fputs("cannonVCC4::receiveRequestArVCC4: Bad header or footer character in response packet.", stderr);
      return -1;
    }

  // so far so good.  Set myError to the error byte
  error_code = reply[3];
  //  PrintPacket("receiverequestasnwer: ", reply, len);
  if (error_code == CAM_ERROR_NONE)
    {
      memcpy(data, reply, len);
      return 0;
    }
  return -1;
}

/************************************************************************/
int
cannonVCC4::setControlMode()
{
  unsigned char command[MAX_PTZ_COMMAND_LENGTH];

  command[0] = 0xFF;
  command[1] = 0x30;
  command[2] = 0x30;
  command[3] = 0x00;
  command[4] = 0x90;
  command[5] = 0x30;
  command[6] = 0xEF;

  if (SendCommand(command, 7))
    return -1;
  usleep(5000000);
  return (ReceiveCommandAnswer());
}
/************************************************************************/
int
cannonVCC4::setPower(int on)
{
  unsigned char command[MAX_PTZ_COMMAND_LENGTH];

  command[0] = 0xFF;
  command[1] = 0x30;
  command[2] = 0x30;
  command[3] = 0x00;
  command[4] = 0xA0;
  if (on)
    command[5] = 0x31;
  else
    command[5] = 0x30;
  command[6] = 0xEF;

  if (SendCommand(command, 7))
    return -1;
  usleep(5000000);
  return (ReceiveCommandAnswer());
}

/************************************************************************/

// this function will be run in a separate thread
void 
cannonVCC4::Main()
{
  player_ptz_data_t data;
  player_ptz_cmd_t command;
  int pan, tilt, zoom;
  int pandemand = 0, tiltdemand = 0, zoomdemand = 0;
  bool newpantilt = true, newzoom = true;
  int err;

  while(1) 
    {
      pthread_testcancel();
      GetCommand((void*)&command, sizeof(player_ptz_cmd_t),NULL);
      pthread_testcancel();
      
      if(pandemand != (short)ntohs((unsigned short)(command.pan)))
	{
	  pandemand = (short)ntohs((unsigned short)(command.pan));
	  newpantilt = true;
	}
      if(tiltdemand != (short)ntohs((unsigned short)(command.tilt)))
	{
	  tiltdemand = (short)ntohs((unsigned short)(command.tilt));
	  newpantilt = true;
	}
      if(zoomdemand != (short)ntohs((unsigned short)(command.zoom)))
	{
	  zoomdemand = (short) ntohs((unsigned short)(command.zoom));
	  newzoom = true;
	}
      
      // Do some coordinate transformatiopns.  Pan value must be
      // negated.  The zoom value must be converted from a field of view
      // (in degrees) into arbitrary Sony PTZ units.
      pandemand = -pandemand;
      
      if(newpantilt)
	{
	  err = SendAbsPanTilt(pandemand,tiltdemand);
// 	  fprintf(stdout, "newpantilt: %d %d err: %d error_code= %d\n", 
// 		  pandemand, tiltdemand, err, error_code);
	  if ((error_code != CAM_ERROR_NONE) && (error_code != CAM_ERROR_BUSY))
	    {
	      fputs("cannonVCC4:Main():SendAbsPanTilt() errored. bailing.\n", 
		    stderr);
	      pthread_exit(NULL);
	    }
	}
      if (newzoom)
	{
	  err = SendAbsZoom(zoomdemand);
	  if ((error_code != CAM_ERROR_NONE) && (error_code != CAM_ERROR_BUSY))
	    {
	      fputs("cannonVCC4:Main():SendAbsZoom() errored. bailing.\n", 
		    stderr);
	      pthread_exit(NULL);
	    }
	}
      
      /* get current state */
      if(GetAbsPanTilt(&pan,&tilt) < 0)
	{
	  fputs("cannonVCC4:Main():GetAbsPanTilt() errored. bailing.\n", 
		stderr);
	  pthread_exit(NULL);
	}
      /* get current state */
      if(GetAbsZoom(&zoom) < 0)
	{
	  fputs("cannonVCC4:Main():GetAbsZoom() errored. bailing.\n", stderr);
	  pthread_exit(NULL);
	}
      
      // Do the necessary coordinate conversions.  Camera's natural pan
      // coordinates increase clockwise; we want them the other way, so
      // we negate pan here.  Zoom values are converted from arbitrary
      // units to a field of view (in degrees).
      pan = -pan;
      
      
      // Copy the data.
      data.pan = htons((unsigned short)pan);
      data.tilt = htons((unsigned short)tilt);
      data.zoom = htons((unsigned short)zoom);
      
      /* test if we are supposed to cancel */
      pthread_testcancel();
      PutData((void*)&data, sizeof(player_ptz_data_t),NULL);
      
      newpantilt = false;
      newzoom = false;
      
      usleep(PTZ_SLEEP_TIME_USEC);
    }
}
/************************************************************************/
struct timeval
substract_time(struct timeval *now, struct timeval *before)
{
  struct timeval res;
  int sec, usec;

  sec = now->tv_sec - before->tv_sec;
  usec = now->tv_usec - before->tv_usec;
  if (usec < 0)
    {
      sec -= 1;
      usec += 1000000;
    }
  res.tv_sec = sec;
  res.tv_usec = usec;
  return(res);  
}