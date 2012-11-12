/*
 * boblight
 * Copyright (C) Bob  2012 
 * 
 * boblight is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * boblight is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "util/log.h"
#include "util/misc.h"
#include "devicespi.h"
#include "util/timeutils.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

CDeviceSPI::CDeviceSPI(CClientsHandler& clients) : m_timer(&m_stop), CDevice(clients)
{
  m_buff = NULL;
  m_buffsize = 0;
  m_fd = -1;
}

void CDeviceSPI::Sync()
{
  if (m_allowsync)
    m_timer.Signal();
}

bool CDeviceSPI::SetupDevice()
{
  m_timer.SetInterval(m_interval);

  m_fd = open(m_output.c_str(), O_RDWR);
  if (m_fd == -1)
  {
    LogError("%s: Unable to open %s: %s", m_name.c_str(), m_output.c_str(), GetErrno().c_str());
    return false;
  }

  int value = SPI_MODE_0;

  if (ioctl(m_fd, SPI_IOC_WR_MODE, &value) == -1)
  {
    LogError("%s: SPI_IOC_WR_MODE: %s", m_name.c_str(), GetErrno().c_str());
    return false;
  }

  if (ioctl(m_fd, SPI_IOC_RD_MODE, &value) == -1)
  {
    LogError("%s: SPI_IOC_RD_MODE: %s", m_name.c_str(), GetErrno().c_str());
    return false;
  }

  value = 8;

  if (ioctl(m_fd, SPI_IOC_WR_BITS_PER_WORD, &value) == -1)
  {
    LogError("%s: SPI_IOC_WR_BITS_PER_WORD: %s", m_name.c_str(), GetErrno().c_str());
    return false;
  }

  if (ioctl(m_fd, SPI_IOC_RD_BITS_PER_WORD, &value) == -1)
  {
    LogError("%s: SPI_IOC_RD_BITS_PER_WORD: %s", m_name.c_str(), GetErrno().c_str());
    return false;
  }

  value = m_rate;

  if (ioctl(m_fd, SPI_IOC_WR_MAX_SPEED_HZ, &value) == -1)
  {
    LogError("%s: SPI_IOC_WR_MAX_SPEED_HZ: %s", m_name.c_str(), GetErrno().c_str());
    return false;
  }

  if (ioctl(m_fd, SPI_IOC_RD_MAX_SPEED_HZ, &value) == -1)
  {
    LogError("%s: SPI_IOC_RD_MAX_SPEED_HZ: %s", m_name.c_str(), GetErrno().c_str());
    return false;
  }

  m_buffsize = m_channels.size() + 1;
  m_buff = new uint8_t[m_buffsize];
  memset(m_buff, 0x80, m_channels.size());
  //the LPD8806 needs a null byte at the end to reset the internal byte counter
  m_buff[m_buffsize - 1] = 0;

  //write out the buffer to turn off all leds
  if (!WriteBuffer())
    return false;

  return true;
}

bool CDeviceSPI::WriteOutput()
{
  //get the channel values from the clientshandler
  int64_t now = GetTimeUs();
  m_clients.FillChannels(m_channels, now, this);

  //put the values in the buffer, big endian
  for (int i = 0; i < m_channels.size(); i++)
  {
    int output = Round32(m_channels[i].GetValue(now) * 127.0f);
    output = Clamp(output, 0, 127);
    m_buff[i] = output | 0x80; //for the LPD8806, high bit needs to be always set
  }

  if (!WriteBuffer())
    return false;

  m_timer.Wait();
  
  return true;
}

void CDeviceSPI::CloseDevice()
{
  if (m_fd != -1)
  {
    //turn off all leds
    memset(m_buff, 0x80, m_channels.size());
    WriteBuffer();

    close(m_fd);
    m_fd = -1;
  }

  delete[] m_buff;
  m_buff = NULL;
  m_buffsize = 0;
}

bool CDeviceSPI::WriteBuffer()
{
  spi_ioc_transfer spi = {};
  spi.tx_buf = (__u64)m_buff;
  spi.len = m_buffsize;

  int returnv = ioctl(m_fd, SPI_IOC_MESSAGE(1), &spi);
  if (returnv == -1)
  {
    LogError("%s: %s %s", m_name.c_str(), m_output.c_str(), GetErrno().c_str());
    return false;
  }

  if (m_debug)
  {
    for (int i = 0; i < m_buffsize; i++)
      printf("%x ", m_buff[i]);
  }

  return true;
}

