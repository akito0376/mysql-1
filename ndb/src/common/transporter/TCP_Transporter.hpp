/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

//****************************************************************************
//
//  AUTHOR
//      �sa Fransson
//
//  NAME
//      TCP_Transporter
//
//  DESCRIPTION
//      A TCP_Transporter instance is created when TCP/IP-communication 
//      shall be used (user specified). It handles connect, disconnect, 
//      send and receive.
//
//
//
//***************************************************************************/
#ifndef TCP_Transporter_H
#define TCP_Transporter_H

#include "Transporter.hpp"
#include "SendBuffer.hpp"

#include <NdbTCP.h>

struct ReceiveBuffer {
  Uint32 *startOfBuffer;    // Pointer to start of the receive buffer 
  Uint32 *readPtr;          // Pointer to start reading data
  
  char   *insertPtr;        // Pointer to first position in the receiveBuffer
                            // in which to insert received data. Earlier
                            // received incomplete messages (slack) are 
                            // copied into the first part of the receiveBuffer

  Uint32 sizeOfData;        // In bytes
  Uint32 sizeOfBuffer;
  
  bool init(int bytes);
  void destroy();
  
  void clear();
  void incompleteMessage();
};

class TCP_Transporter : public Transporter {
  friend class TransporterRegistry;
private:
  // Initialize member variables
  TCP_Transporter(int sendBufferSize, int maxReceiveSize,
		  int port, 
		  const char *rHostName, 
		  const char *lHostName,
		  NodeId rHostId, NodeId lHostId,
		  int byteorder,
		  bool compression, bool checksum, bool signalId,
		  Uint32 reportFreq = 4096);
  
  // Disconnect, delete send buffers and receive buffer
  virtual ~TCP_Transporter();

  /**
   * Allocate buffers for sending and receiving
   */
  bool initTransporter();

  Uint32 * getWritePtr(Uint32 lenBytes, Uint32 prio);
  void updateWritePtr(Uint32 lenBytes, Uint32 prio);
  
  bool hasDataToSend() const ;

  /**
   * Retrieves the contents of the send buffers and writes it on 
   * the external TCP/IP interface until the send buffers are empty
   * and as long as write is possible.
   */
  bool doSend();
  
  /**
   * It reads the external TCP/IP interface once 
   * and puts the data in the receiveBuffer
   */
  int doReceive(); 

  /**
   * Returns socket (used for select)
   */
  NDB_SOCKET_TYPE getSocket() const;

  /**
   * Get Receive Data
   *
   *  Returns - no of bytes to read
   *            and set ptr
   */
  virtual Uint32 getReceiveData(Uint32 ** ptr);
  
  /**
   * Update receive data ptr
   */
  virtual void updateReceiveDataPtr(Uint32 bytesRead);

protected:
  /**
   * Setup client/server and perform connect/accept
   * Is used both by clients and servers
   * A client connects to the remote server
   * A server accepts any new connections
   */
  bool connectImpl(Uint32 timeOutMillis);
  
  /**
   * Disconnects a TCP/IP node. Empty send and receivebuffer.
   */
  void disconnectImpl();
  
private:
  /**
   * Send buffers
   */
  SendBuffer m_sendBuffer;
  
  const bool isServer;
  const unsigned int port;

  // Sending/Receiving socket used by both client and server
  NDB_SOCKET_TYPE theSocket;   
  
  Uint32 maxReceiveSize;
  
  /**
   * Remote host name/and address
   */
  char remoteHostName[256];
  struct in_addr remoteHostAddress;
  struct in_addr localHostAddress;

  /**
   * Socket options
   */
  int sockOptRcvBufSize;
  int sockOptSndBufSize;
  int sockOptNodelay;
  int sockOptTcpMaxSeg;

  void setSocketOptions();

  static bool setSocketNonBlocking(NDB_SOCKET_TYPE aSocket);
  
  bool sendIsPossible(struct timeval * timeout);

  /**
   * startTCPServer - None blocking
   *
   *   create a server socket
   *   bind
   *   listen
   *
   * Note: Does not call accept
   */
  bool startTCPServer();

  /**
   * acceptClient - Blocking
   *
   *   Accept a connection
   *   checks if "right" client has connected
   *   if so
   *      close server socket
   *   else
   *      close newly created socket and goto begin
   */
  bool acceptClient(struct timeval * timeout);
  
  /**
   * Creates a client socket
   *
   * Note does not call connect
   */
  bool createClientSocket();
  
  /**
   * connectClient - Blocking
   *
   *   connects to remote host 
   */
  bool connectClient(struct timeval * timeout);
  
  /**
   * Statistics
   */
  Uint32 reportFreq;
  Uint32 receiveCount;
  Uint64 receiveSize;
  Uint32 sendCount;
  Uint64 sendSize;

  ReceiveBuffer receiveBuffer;

#if defined NDB_OSE || defined NDB_SOFTOSE
  PROCESS theReceiverPid;
#endif
};

inline
NDB_SOCKET_TYPE
TCP_Transporter::getSocket() const {
  return theSocket;
}

inline
Uint32
TCP_Transporter::getReceiveData(Uint32 ** ptr){
  (* ptr) = receiveBuffer.readPtr;
  return receiveBuffer.sizeOfData;
}

inline
void
TCP_Transporter::updateReceiveDataPtr(Uint32 bytesRead){
  char * ptr = (char *)receiveBuffer.readPtr;
  ptr += bytesRead;
  receiveBuffer.readPtr = (Uint32*)ptr;
  receiveBuffer.sizeOfData -= bytesRead;
  receiveBuffer.incompleteMessage();
}

inline
bool
TCP_Transporter::hasDataToSend() const {
  return m_sendBuffer.dataSize > 0;
}

inline
bool
ReceiveBuffer::init(int bytes){
#ifdef DEBUG_TRANSPORTER
  ndbout << "Allocating " << bytes << " bytes as receivebuffer" << endl;
#endif

  startOfBuffer = new Uint32[((bytes + 0) >> 2) + 1];
  sizeOfBuffer  = bytes + sizeof(Uint32);
  clear();
  return true;
}

inline
void
ReceiveBuffer::destroy(){
  delete[] startOfBuffer;
  sizeOfBuffer  = 0;
  startOfBuffer = 0;
  clear();
}

inline
void
ReceiveBuffer::clear(){
  readPtr    = startOfBuffer;
  insertPtr  = (char *)startOfBuffer;
  sizeOfData = 0;
}

inline
void
ReceiveBuffer::incompleteMessage() {
  if(startOfBuffer != readPtr){
    if(sizeOfData != 0)
      memmove(startOfBuffer, readPtr, sizeOfData);
    readPtr   = startOfBuffer;
    insertPtr = ((char *)startOfBuffer) + sizeOfData;
  }
}


#endif // Define of TCP_Transporter_H
