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

  // <Neighbour, Advertisement>
  map<uint32_t, *Advertisement> *advertisementMap;

  struct Advertisement {
    // <Destination, Hops>
    map<uint32_t, vector<uint32_t>* > *paths;
    time_t expiryDate;
  };

  // Structure of packets sent and received.
  struct AdvertisementPathPacket {
    vector<uint32_t> *hops;
    uint32_t destination;
  };

  // To check if we need to recompute the shortestPaths.
  bool dirty;

  // The shortestPaths that the node would advertise.
  vector<AdvertisementPathPacket> shortestPaths;

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

      Advertisement *storedAdvertisement = getAdvertisement(neighbour);

      // If we couldn't find a stored advertisement, then store it.
      if (storedAdvertisement == NULL ||
          compareAdvertisement(storedAdvertisement, advertisedPathVector)) {
        storeAdvertisement(neighbour, advertisedPathVector);
        this.dirty = true;
      }

      NS_ASSERT(ptr == size);
    }
    checkAllTimeouts();
  }


  /************************* Private Routines ***************************/
  /**********************************************************************/


  void storeAdvertisement(uint32_t neighbour, vector<AdvertisementPathPacket> *advertisedPathVector) {
    map<uint32_t, vector<uint32_t>* > *shortestPaths;
    vector<AdvertisementPathPacket>::iterator it;
    for (it = advertisedPathVector->begin(); it != advertisedPathVector->end; ++it) {
      AdvertisementPathPacket path = *it;
      *(shortestPaths)[path.destination] = path.hops;
    }

    Advertisement *newAdvertisement;
    newAdvertisement->paths = shortestPaths;
    newAdvertisement->expiryDate = calculateExpirationDate();
    *(advertisementMap)[neighbour] = newAdvertisement;
  }

  bool compareAdvertisement(Advertisement *storedAdvertisement, vector<AdvertisementPathPacket> *advertisedPathVector) {
    bool changed = false;

    // First, do the comparison to check if they are the same.
    vector<AdvertisementPathPacket>::iterator it;
    for (it = advertisedPathVector->begin(); it != advertisedPathVector->end; ++it) {
      AdvertisementPathPacket advertisedPathPacket = *it;
      vector<uint32_t> *advertisedPath = advertisedPathPacket.hops;

      map<uint32_t, vector<uint32_t>* >::iterator it2 = storedAdvertisement->paths->find(advertisedPathPacket.destination);
      if (it2 != storedAdvertisement->end() ||
          !comparePaths(it2->second, advertisedPath)) {
        changed = true;
      }
    }

    // If the advertisement isn't the same, then we delete it.
    if (changed) {
      deleteAdvertisement(*storeAdvertisement)
    }

    storedAdvertisement.expiryDate = calculateExpirationDate();
    return changed;
  }

  /*
   * Returns true if equal, false if not.
   */
  bool comparePaths(vector<uint32_t> *path1, vector<uint32_t> *path2) {
    vector<uint32_t>::iterator it1;
    vector<uint32_t>::iterator it2 = path2->begin();
    for (it1 = path1->begin(); it1 != path1->end(); ++it1) {
      if (it2 != path2->end() && *it1 == *it2) {
        ++it2;
      } else {
        return false;
      }
    }
    return true;
  }

  Advertisement& getAdvertisement(uint32_t neighbour) {
    map<uint32_t, *Advertisement>::iterator it = advertisementMap->find(neighbour);
    Advertisement *advertisement = NULL;
    if (it != advertisementMap->end()) {
      advertisement = it->second;
    }
    return advertisement;
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
        this.dirty = true;
      } else {
        ++it;
      }
    }
  }

  void deleteAdvertisement(Advertisement &advertisement) {
    map<uint32_t, vector<uint32_t>* >::iterator it;
    for (it = advertisement.paths.begin(); it != advertisement.paths.end(); ++it) {
      delete it.second;
    }
    delete advertisement.paths;
    delete *advertisement;
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
    this.dirty = false;
  }

  void AddSocket(Ptr<Socket> socket) {
    socket->SetRecvCallback(MakeCallback(&PathVectorNode::receive, this));
    socketList.push_back(socket);
  }
};

#include "pvr-main.h"
