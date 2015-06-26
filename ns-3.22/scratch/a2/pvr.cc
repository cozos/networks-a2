#include "pvr-top.h"
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

  struct Advertisement {
    // <Destination, Hops>
    map<uint32_t, vector<uint32_t>* > *paths;
    Time expiryDate;
  };

  // Structure of packets sent and received.
  struct AdvertisementPathPacket {
    uint32_t destination;
    vector<uint32_t> *hops;
  };

  // The shortestPaths that the node would advertise.
  vector<AdvertisementPathPacket> *shortestPaths;

  // <Neighbour, Advertisement>
  map<uint32_t, Advertisement*> *advertisementMap;

  // To check if we need to recompute the shortestPaths.
  bool dirty;

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
    // send via all sockets
    list< Ptr<Socket> >::iterator iter;
    for (iter = socketList.begin(); iter != socketList.end(); ++iter) {
      for(unsigned int i = 0; i < this->shortestPaths->size(); i++) {
        uint8_t buffer[maxMsgSize];
        uint32_t ptr = writeInt(buffer, maxMsgSize, ID);
        serializePathVector(this->shortestPaths->at(i), buffer, ptr);
        Ptr<Packet> packet = Create<Packet>( buffer, ptr );
        (*iter)->Send(packet);
      }
    }
  }

  // students need to implement a proper version of this routine
  void receive(Ptr<Socket> socket) {
    while (Ptr<Packet> packet = socket->Recv()) {
      uint8_t buffer[maxMsgSize];
      uint32_t size = packet->CopyData(buffer, maxMsgSize);
      uint32_t neighbour;
      uint32_t ptr = readInt(buffer, size, neighbour);

      AdvertisementPathPacket *advertisedPath = deserializePathVector(buffer, size, ptr);
      Advertisement *storedAdvertisement = getAdvertisement(neighbour);

      // If we couldn't find a stored advertisement, then store it.
      if (storedAdvertisement == NULL) {
        storeAdvertisement(neighbour, advertisedPath);
      } else {
        compareAndReplaceAdvertisement(storedAdvertisement, advertisedPath);
      }

      freeAdvertisementPathPacket(*advertisedPath);

      NS_ASSERT(ptr == size);
    }
    checkAllTimeouts();
  }

  /************************* Private Routines ***************************/
  /**********************************************************************/

  /*
   * Serializes the shortest PathVector into a buffer
   */
  void serializePathVector(AdvertisementPathPacket path, uint8_t *buffer, uint32_t &ptr) {
    vector<uint32_t> *hops = path.hops;

    ptr += writeInt(buffer + ptr, maxMsgSize - ptr, path.destination);
    ptr += writeInt(buffer + ptr, maxMsgSize - ptr, hops->size());

    for (unsigned int j = 0; j < hops->size(); j++) {
      ptr += writeInt(buffer + ptr, maxMsgSize - ptr, hops->at(j));
    }
  }

  /*
   * Deserializes the Packet into a vector.
   */
  AdvertisementPathPacket* deserializePathVector(uint8_t* buf, uint8_t size, uint32_t &ptr) {
    AdvertisementPathPacket *advertisedPath = new AdvertisementPathPacket();

    uint32_t destination;
    ptr += readInt(buf + ptr, size - ptr, destination);
    advertisedPath->destination = destination;

    uint32_t hopSize;
    ptr += readInt(buf + ptr, size - ptr, hopSize);
    advertisedPath->hops = new vector<uint32_t>(hopSize);

    for(unsigned int j = 0; j < hopSize; j++) {
      uint32_t hop;
      ptr += readInt(buf + ptr, size - ptr, hop);
      (*advertisedPath->hops)[j] = hop;
    }

    return advertisedPath;
  }

  /*
   * Stores the advertised path vector in advertisementMap corresponding to the neighbour.
   */
  void storeAdvertisement(uint32_t neighbour, AdvertisementPathPacket *advertisedPath) {
    map<uint32_t, vector<uint32_t>* > *shortestPaths =  new map<uint32_t, vector<uint32_t>* >();
    (*shortestPaths)[advertisedPath.destination] = advertisedPath.hops;
    Advertisement *newAdvertisement = new Advertisement();
    newAdvertisement->paths = shortestPaths;
    newAdvertisement->expiryDate = calculateExpirationDate();
    (*advertisementMap)[neighbour] = newAdvertisement;
    this->dirty = true;
  }

  /*
   * RETURNS TRUE IF COMPARE EQUAL, FALSE OTHERWISE.
   *
   * Compares the stored advertisement to the advertised paths.
   * If the stored advertisement's paths are mismatched with the advertised paths,
   * then we replace it.
   */
  bool compareAndReplaceAdvertisement(Advertisement *storedAdvertisement, AdvertisementPathPacket *advertisedPathPacket) {
    vector<uint32_t> *advertisedPath = advertisedPathPacket.hops;
    map<uint32_t, vector<uint32_t>* >::iterator it = storedAdvertisement->paths->find(advertisedPathPacket.destination);
    if (it == storedAdvertisement->paths->end() || !comparePaths(it->second, advertisedPath)) {
      (*storedAdvertisement->paths)[advertisedPathPacket.destination] = advertisedPath;
      storedAdvertisement->expiryDate = calculateExpirationDate();
      this->dirty = true;
    }
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

  /*
   * Finds an Advertisement from the advertisementMap
   */
  Advertisement* getAdvertisement(uint32_t neighbour) {
    map<uint32_t, Advertisement*>::iterator it = advertisementMap->find(neighbour);
    Advertisement *advertisement = NULL;
    if (it != advertisementMap->end()) {
      advertisement = it->second;
    }
    return advertisement;
  }

  // Calculate the TTL of a PathVector being created.
  Time calculateExpirationDate() {
    Time expirationTime = Simulator::Now();
    expirationTime += NanoSeconds(timeout);
    return expirationTime;
  }

  // students need to implement a proper version of this routine
  void checkAllTimeouts() {
    map<uint32_t, Advertisement*>::iterator it;
    for (it = advertisementMap->begin(); it != advertisementMap->end();/*No Increment*/) {
      if (Simulator::Now().GetNanoSeconds() > it->second->expiryDate.GetNanoSeconds()) /* Check if path expired*/ {
        cout << "[" << ID << "]" << " Deleted " << it->second->expiryDate.GetNanoSeconds() << " | Current Time " << Simulator::Now().GetNanoSeconds() << " | Timeout " << timeout << endl;
        freeAdvertisement(*(it->second));
        advertisementMap->erase(it++);
        this->dirty = true;
      } else {
        cout << "[" << ID << "]" << " Didn't delete " << it->second->expiryDate.GetNanoSeconds() << " | Current Time " << Simulator::Now().GetNanoSeconds() << " | Timeout " << timeout << " | Difference " << Simulator::Now().GetNanoSeconds() - it->second->expiryDate.GetNanoSeconds()<<endl;
        ++it;
      }
    }
  }

  /*
   * Frees the Advertisement struct
   */
  void freeAdvertisement(Advertisement &advertisement) {
    map<uint32_t, vector<uint32_t>* >::iterator it;
    for (it = advertisement.paths->begin(); it != advertisement.paths->end(); ++it) {
      delete it->second;
    }
    delete advertisement.paths;
  }

  /*
   * Frees the AdvertisementPathPacket struct
   */
  void freeAdvertisementPathPacket(AdvertisementPathPacket &advertisementPathPacket) {
    delete advertisementPathPacket.hops;
  }

  /**********************************************************************/
  /**********************************************************************/

public:
  PathVectorNode(uint32_t id, uint32_t nc, double i, double t)
    : ID(id), nodeCount(nc), interval(i), timeout(t) {
    this->dirty = false;
    this->advertisementMap = new map<uint32_t, Advertisement*>();
    this->shortestPaths = new vector<AdvertisementPathPacket>();

    for(int i = 0; i < 1; i++) {
      AdvertisementPathPacket derp;
      derp.destination = i;
      derp.hops = new vector<uint32_t>();
      for (int j = 0; j < 1; j++) {
        derp.hops->push_back(64 + j);
      }
      shortestPaths->push_back(derp);
    }
  }

  void AddSocket(Ptr<Socket> socket) {
    socket->SetRecvCallback(MakeCallback(&PathVectorNode::receive, this));
    socketList.push_back(socket);
  }
};

#include "pvr-main.h"
