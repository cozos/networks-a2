#include "pvr-top.h"
#include <time.h>
#include <vector>
#include <map>

using namespace std;

class PathVectorNode : public Application {
  uint32_t ID;
  uint32_t nodeCount;
  double interval;
  double timeout;

  /*************************** Data Members *****************************/
  /**********************************************************************/

  struct AdvertisementPathPacket {
    vector<uint32_t> *hops;
    uint32_t destination;
  };

  struct Advertisement {
    // <Destination, Hops>
    map<uint32_t, vector<uint32_t>* > *paths;
    time_t expiryDate;
  };

  // <Neighbour, Advertisement>
  map<uint32_t, Advertisement> *advertisementMap;

  /**********************************************************************/
  /**********************************************************************/

  EventId nextEvent;
  Ptr<UniformRandomVariable> rng;
  list< Ptr<Socket> > socketList;

  static const uint32_t maxMsgSize = 1024;

  static void sendHelper(PathVectorNode* This) {
    This->send();
    This->scheduleSend();
  }

  void scheduleSend() {
    double wait = rng->GetValue(0, interval);
    nextEvent = Simulator::Schedule(Seconds(wait), PathVectorNode::sendHelper, this);
  }

  virtual void StartApplication() {
    rng = CreateObject<UniformRandomVariable>();
    scheduleSend();
  }

  virtual void StopApplication() {
    Simulator::Cancel(nextEvent);
  }

  // students need to implement a proper version of this routine
  void send() {
    // create message buffer
    uint8_t buffer[maxMsgSize];
    uint32_t ptr = writeInt(buffer, maxMsgSize, ID);

    // send via all sockets
    list< Ptr<Socket> >::iterator iter;
    for (iter = socketList.begin(); iter != socketList->end(); ++iter) {
      Ptr<Packet> packet = Create<Packet>( buffer, ptr );
      (*iter)->Send(packet);
    }
  }

  // students need to implement a proper version of this routine
  void receive(Ptr<Socket> socket) {
    while (Ptr<Packet> packet = socket->Recv()) {
      uint8_t buffer[maxMsgSize];
      uint32_t size = packet->CopyData(buffer, maxMsgSize);
      uint32_t neighbour;
      uint32_t ptr = readInt(buffer, size, neighbour);

      vector<AdvertisementPathPacket> *advertisedPathVector; // TODO: Deserialize.

      // Update our PathVector
      for (vector<AdvertisementPathPacket>::iterator itAdvertised = advertisedPathVector->begin(); itAdvertised != advertisedPathVector->end;/*No Increment*/){
        AdvertisementPathPacket advertisedPathPacket = *itAdvertised;

        if (advertisedPathPacket.destination == this.ID ||
            advertisedPathPacket.hops->contains(this.ID)) {
          removePathVector(neighbour, advertisedPathPacket.destination);
        } else {
          insertPathVector(neighbour, advertisedPathPacket);
        }

        deleteAdvertisementPathPacket(advertisedPathPacket);
      }

      NS_ASSERT(ptr == size);
    }
    checkAllTimeouts();
  }


  /************************* Private Routines ***************************/
  /**********************************************************************/

  void insertPathVector(uint32_t neighbour, AdvertisementPathPacket &advertisedShortestPath) {
    bool foundExistingPathFlag = false;

    for (vector<ShortestPath>::iterator it_current = pathVector->begin(); it_current != pathVector->end();/*No Increment*/) {
      ShortestPath currentShortestPath = *it_current;

      if (currentShortestPath.destination == advertisedShortestPath.destination) {
        foundExistingPathFlag = true;
        if (currentShortestPath.hops->size() <= advertisedShortestPath.hops->size()) { // During ties the new path wins because reasons.
          currentShortestPath.neighbour = neighbour;
          delete *currentShortestPath.hops;
          currentShortestPath.hops = new vector<uint32_t>(advertisedShortestPath.hops);
          currentShortestPath.expiryDate = calculateExpirationDate();
        }
      }

      ++it_current;
    }

    // If we didn't find any existing paths in the pathVector, create a new one and add it.
    if (!foundExistingPathFlag) {
      AdvertisementPath newPath;
      newPath.neighbour = neighbour;
      newPath.hops = new vector<uint32_t>(advertisedShortestPath.hops);
      newPath.destination = advertisedShortestPath.destination;
      newPath.expiryDate = calculateExpirationDate();
      pathVector->insert(newPath);
    }
  }

  void removePathVector(neighbour, destination) {
    map<uint32_t, Advertisement>::iterator it = advertisementMap->find(neighbour);
    if (it != advertisementMap->end()) {
      Advertisement advertisement = it->second;

      map<uint32_t, AdvertisementPath>::iterator it2 = advertisement.paths->find(destination);
      if (it2 != advertisement.paths->end()) {
        delete it2->second; // Free the Path Vector first.
        advertisement.paths.erase(it2);
      }
    }
  }

  // Calculate the TTL of a PathVector being created.
  time_t calculateExpirationDate() {
    return time(0) + this.timeout;
  }

  // students need to implement a proper version of this routine
  void checkAllTimeouts() {
    map<uint32_t, Advertisement>::iterator it;
    for (it = advertisementMap->begin(); it != advertisementMap->end();/*No Increment*/) {
      if (time(0) > *it.expiryDate) /* Check if path expired*/ {
        deleteAdvertisement(it->second);
        it = v.erase(it);
      } else {
        ++it;
      }
    }
  }

  void deleteAdvertisement(Advertisement &advertisement) {
    map<uint32_t, vector<uint32_t>* >::iterator it;
    for (it = advertisement.paths.begin(); it != advertisement.paths.end();/*No Increment*/) {
      delete it.second;
    }
    delete advertisement.paths;
    delete advertisement;
  }

  void deleteAdvertisementPathPacket(AdvertisementPathPacket &advertisementPathPacket) {
    delete advertisementPathPacket.hops;
    delete advertisementPathPacket;
  }

  /**********************************************************************/
  /**********************************************************************/

public:
  PathVectorNode(uint32_t id, uint32_t nc, double i, double t)
    : ID(id), nodeCount(nc), interval(i), timeout(t) {
    // students might need to add code here
  }

  void AddSocket(Ptr<Socket> socket) {
    socket->SetRecvCallback(MakeCallback(&PathVectorNode::receive, this));
    socketList.push_back(socket);
  }
};

#include "pvr-main.h"
