/*
 *  Player - One Hell of a Robot Server
 *  Copyright (C) 2000  Brian Gerkey   &  Kasper Stoy
 *                      gerkey@usc.edu    kaspers@robotics.usc.edu
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
///////////////////////////////////////////////////////////////////////////
//
// Desc: Gazebo (simulator) position3d driver
// Author: Pranav Srivastava
// Date: 28 Feb 2004
// CVS: $Id$
//
///////////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>       // for atoi(3)
#include <sys/types.h>
#include <netinet/in.h>

#include "player.h"
#include "device.h"
#include "drivertable.h"

#include "gazebo.h"
#include "gz_client.h"


// Incremental navigation driver
class GzPosition3d : public CDevice
{
  // Constructor
  public: GzPosition3d(char* interface, ConfigFile* cf, int section);

  // Destructor
  public: virtual ~GzPosition3d();

  // Setup/shutdown routines.
  public: virtual int Setup();
  public: virtual int Shutdown();

  // Check for new data
  public: virtual void Update();

  // Commands
  public: virtual void PutCommand(void* client, unsigned char* src, size_t len);

  // Request/reply
  public: virtual int PutConfig(player_device_id_t* device, void* client,
                                void* req, size_t reqlen);

  // Handle geometry requests
  private: void HandleGetGeom(void *client, void *req, int reqlen);

  // Handle motor configuration
  private: void HandleMotorPower(void *client, void *req, int reqlen);

  // Gazebo id
  private: char *gz_id;

  // Gazebo client object
  private: gz_client_t *client;
  
  // Gazebo Interface
  private: gz_position3d_t *iface;

  // Timestamp on last data update
  private: double datatime;
};


// Initialization function
CDevice* GzPosition3d_Init(char* interface, ConfigFile* cf, int section)
{
  if (GzClient::client == NULL)
  {
    PLAYER_ERROR("unable to instantiate Gazebo driver; did you forget the -g option?");
    return (NULL);
  }
  if (strcmp(interface, PLAYER_POSITION3D_STRING) != 0)
  {
    PLAYER_ERROR1("driver \"gz_position3d\" does not support interface \"%s\"\n", interface);
    return (NULL);
  }
  return ((CDevice*) (new GzPosition3d(interface, cf, section)));
}


// a driver registration function
void GzPosition3d_Register(DriverTable* table)
{
  table->AddDriver("gz_position3d", PLAYER_ALL_MODE, GzPosition3d_Init);
  return;
}


////////////////////////////////////////////////////////////////////////////////
// Constructor
GzPosition3d::GzPosition3d(char* interface, ConfigFile* cf, int section)
    : CDevice(sizeof(player_position3d_data_t), sizeof(player_position3d_cmd_t), 10, 10)
{
    // Get the globally defined Gazebo client (one per instance of Player)
  this->client = GzClient::client;

  // Get the id of the device in Gazebo.
  // TODO: fix potential buffer overflow
  this->gz_id = (char*) calloc(1024, sizeof(char));
  strcat(this->gz_id, GzClient::prefix_id);
  strcat(this->gz_id, cf->ReadString(section, "gz_id", ""));
  
  // Create an interface
  this->iface = gz_position3d_alloc();

  this->datatime = -1;

  return;
}


////////////////////////////////////////////////////////////////////////////////
// Destructor
GzPosition3d::~GzPosition3d()
{
  gz_position3d_free(this->iface); 
  return;
}


////////////////////////////////////////////////////////////////////////////////
// Set up the device (called by server thread).
int GzPosition3d::Setup()
{
  // Open the interface
  if (gz_position3d_open(this->iface, this->client, this->gz_id) != 0)
    return -1;

  return 0;
}


////////////////////////////////////////////////////////////////////////////////
// Shutdown the device (called by server thread).
int GzPosition3d::Shutdown()
{
  gz_position3d_close(this->iface);
  
  return 0;
}


////////////////////////////////////////////////////////////////////////////////
// Check for new data
void GzPosition3d::Update()
{
  player_position3d_data_t data;
  uint32_t tsec, tusec;

  gz_position3d_lock(this->iface, 1);

  if (this->iface->data->time > this->datatime)
  {
    this->datatime = this->iface->data->time;
    
    tsec = (int) (this->iface->data->time);
    tusec = (int) (fmod(this->iface->data->time, 1) * 1e6);
  
    data.xpos = htonl((int) (this->iface->data->xpos));
    data.ypos = htonl((int) (this->iface->data->ypos));
    data.zpos = htonl((int) (this->iface->data->zpos));
    data.yaw = htonl((int) (this->iface->data->yaw * 180 / M_PI));

    data.xspeed = htonl((int) (this->iface->data->xspeed));
    data.yspeed = htonl((int) (this->iface->data->yspeed));
    data.zspeed = htonl((int) (this->iface->data->zspeed));
    data.yawspeed = htonl((int) (this->iface->data->yawspeed * 180 / M_PI));

    data.stall = (uint8_t) this->iface->data->stall;

    this->PutData(&data, sizeof(data), tsec, tusec);    
  }

  gz_position3d_unlock(this->iface);

  return;
}


////////////////////////////////////////////////////////////////////////////////
// Commands
void GzPosition3d::PutCommand(void* client, unsigned char* src, size_t len)
{
  player_position3d_cmd_t *cmd;
    
  assert(len >= sizeof(player_position3d_cmd_t));
  cmd = (player_position3d_cmd_t*) src;

  gz_position3d_lock(this->iface, 1);
  this->iface->data->xspeed = ((int) ntohl(cmd->xspeed)) ;
  this->iface->data->yspeed = ((int) ntohl(cmd->yspeed)) ;
  this->iface->data->zspeed = ((int) ntohl(cmd->zspeed)) ;
  this->iface->data->yawspeed = ((int) ntohl(cmd->yawspeed)) * M_PI / 180;
  this->iface->data->rollspeed = 0;
  this->iface->data->pitchspeed=0;
  gz_position3d_unlock(this->iface);
    
  return;
}


////////////////////////////////////////////////////////////////////////////////
// Handle requests
int GzPosition3d::PutConfig(player_device_id_t* device, void* client, void* req, size_t req_len)
{
  switch (((char*) req)[0])
  {
    case PLAYER_POSITION3D_GET_GEOM_REQ:
      HandleGetGeom(client, req, req_len);
      break;

    case PLAYER_POSITION3D_MOTOR_POWER_REQ:
      HandleMotorPower(client, req, req_len);
      break;

    default:
      if (PutReply(client, PLAYER_MSGTYPE_RESP_NACK) != 0)
        PLAYER_ERROR("PutReply() failed");
      break;
  }
  return 0;
}


////////////////////////////////////////////////////////////////////////////////
// Handle geometry requests.
void GzPosition3d::HandleGetGeom(void *client, void *req, int reqlen)
{
  player_position_geom_t geom;

  // TODO: get correct dimensions; there are for the P2AT
  // i think this is only for the playerv client .. not really a necessity??  
  geom.subtype = PLAYER_POSITION3D_GET_GEOM_REQ;
  geom.pose[0] = htons((int) (0));
  geom.pose[1] = htons((int) (0));
  geom.pose[2] = htons((int) (0));
  geom.size[0] = htons((int) (0.53 * 1000));
  geom.size[1] = htons((int) (0.38 * 1000));

  if (PutReply(client, PLAYER_MSGTYPE_RESP_ACK, NULL, &geom, sizeof(geom)) != 0)
    PLAYER_ERROR("PutReply() failed");
  
  return;
}


////////////////////////////////////////////////////////////////////////////////
// Handle motor power 
void GzPosition3d::HandleMotorPower(void *client, void *req, int reqlen)
{
  player_position_power_config_t *power;
  
  assert((size_t) reqlen >= sizeof(player_position_power_config_t));
  power = (player_position_power_config_t*) req;

  gz_position3d_lock(this->iface, 1);
  this->iface->data->cmd_enable_motors = power->value;
  gz_position3d_unlock(this->iface);

  if (PutReply(client, PLAYER_MSGTYPE_RESP_ACK, NULL, NULL, 0) != 0)
    PLAYER_ERROR("PutReply() failed");
  
  return;
}
