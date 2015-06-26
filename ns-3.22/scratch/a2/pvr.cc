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
    if (dirty) {
      calculateShortestPaths();
      dirty = false;
    }

    list< Ptr<Socket> >::iterator iter;
    for (iter = socketList.begin(); iter != socketList.end(); ++iter) {
      uint8_t initBuffer[maxMsgSize];
      uint32_t initPtr = writeInt(initBuffer, maxMsgSize, 1);
      initPtr += writeInt(initBuffer + initPtr, maxMsgSize - initPtr, ID);
      Ptr<Packet> initPacket = Create<Packet>( initBuffer, initPtr );
      (*iter)->Send(initPacket);
      for(unsigned int i = 0; i < this->shortestPaths->size(); i++) {
        uint8_t buffer[maxMsgSize];
        uint32_t ptr;
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

      uint32_t packetSize;
      uint32_t ptr = readInt(buffer, size, packetSize);
      ptr += readInt(buffer + ptr, size - ptr, neighbour);

      AdvertisementPathPacket *advertisedPath = new AdvertisementPathPacket();
      if (packetSize == 1) {
        advertisedPath->destination = neighbour;
        advertisedPath->hops = new vector<uint32_t>();
      } else {
        advertisedPath = deserializePathVector(buffer, size, ptr);
      }

      Advertisement *storedAdvertisement = getAdvertisement(neighbour);

      // If we couldn't find a stored advertisement, then store it.
      if (storedAdvertisement == NULL) {
        storeAdvertisement(neighbour, advertisedPath);
      } else {
        compareAndReplaceAdvertisement(neighbour, storedAdvertisement, advertisedPath);
      }

      // NS_ASSERT(ptr == size);
    }
    checkAllTimeouts();
  }

  /************************* Private Routines ***************************/
  /**********************************************************************/

  /*
   * Calculates the shortestPaths.
   */
  void calculateShortestPaths() {
    map<uint32_t, vector<uint32_t>* > *shortestPathsMap = new map<uint32_t, vector<uint32_t>* >();

    for (map<uint32_t, Advertisement*>::iterator itMap = advertisementMap->begin(); itMap != advertisementMap->end(); ++itMap) {
      uint32_t neighbour = itMap->first;
      Advertisement *adv = itMap->second;

      (*shortestPathsMap)[neighbour] = new vector<uint32_t>();

      for (map<uint32_t, vector<uint32_t>* >::iterator itDV = adv->paths->begin(); itDV != adv->paths->end(); ++itDV) {
        // Create complete path.
        uint32_t destination = itDV->first;
        vector<uint32_t> *path = new vector<uint32_t>(*itDV->second);
        path->insert(path->begin(), neighbour);

        int pathSize = path->size() + 1;
        int existingLength = getLength(destination, shortestPathsMap);
        if (existingLength == -1 || pathSize < existingLength) {
          (*shortestPathsMap)[destination] = path;
        }
      }
    }

    shortestPaths->clear();

    for (map<uint32_t, vector<uint32_t>* >::iterator itShortestPath = shortestPathsMap->begin(); itShortestPath != shortestPathsMap->end(); ++itShortestPath) {
      AdvertisementPathPacket packet;
      packet.destination = itShortestPath->first;
      packet.hops = itShortestPath->second;
      shortestPaths->push_back(packet);
    }
  }

  /*
   * Helper function for calculateShortestPath
   */
  int getLength(uint32_t node, map<uint32_t, vector<uint32_t>* > *shortestPathsMap) {
    map<uint32_t, vector<uint32_t>* >::iterator it = shortestPathsMap->find(node);
    if (it != shortestPathsMap->end()) {
      return it->second->size();
    } else {
      return -1;
    }
  }

  /*
   * Serializes the shortest PathVector into a buffer
   */
  void serializePathVector(AdvertisementPathPacket path, uint8_t *buffer, uint32_t &ptr) {
    vector<uint32_t> *hops = path.hops;
    uint32_t size = 1 + 1 + 1 + hops->size(); // ID + Destination + Hopsize, Hops

    ptr += writeInt(buffer + ptr, maxMsgSize - ptr, size);
    ptr += writeInt(buffer + ptr, maxMsgSize - ptr, ID);
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
    (*shortestPaths)[advertisedPath->destination] = advertisedPath->hops;
    Advertisement *newAdvertisement = new Advertisement();
    newAdvertisement->paths = shortestPaths;
    newAdvertisement->expiryDate = calculateExpirationDate();
    (*advertisementMap)[neighbour] = newAdvertisement;
    printNew(neighbour, advertisedPath->destination, advertisedPath->hops);
    this->dirty = true;
  }

  /*
   * Compares the stored advertisement to the advertised paths.
   * If the stored advertisement's paths are mismatched with the advertised paths,
   * then we replace it.
   */
  void compareAndReplaceAdvertisement(uint32_t neighbour, Advertisement *storedAdvertisement, AdvertisementPathPacket *advertisedPathPacket) {
    vector<uint32_t> *advertisedPath = advertisedPathPacket->hops;
    map<uint32_t, vector<uint32_t>* >::iterator it = storedAdvertisement->paths->find(advertisedPathPacket->destination);
    if (it != storedAdvertisement->paths->end()) {
      if (advertisedPathPacket->destination == ID) {
        //no-op
      } else if (containsMyself(advertisedPath)) {
        (*storedAdvertisement->paths)[advertisedPathPacket->destination] = new vector<uint32_t>();
        this->dirty = true;
        printNoPaths(neighbour, advertisedPathPacket->destination);
      } else if (!equalPaths(it->second, advertisedPath)){
        (*storedAdvertisement->paths)[advertisedPathPacket->destination] = advertisedPath;
        storedAdvertisement->expiryDate = calculateExpirationDate();
        printUpdate(neighbour, advertisedPathPacket->destination, advertisedPathPacket->hops, it->second);
        this->dirty = true;
      }
    }
  }

  /*
   * RETURNS TRUE IF FINDS OWN ID, FALSE OTHERWISE.
   */
  bool containsMyself(vector<uint32_t> *path) {
    vector<uint32_t>::iterator it = find(path->begin(), path->end(), this->ID);
    return it != path->end();
  }

  /*
   * Returns true if equal, false if not.
   */
  bool equalPaths(vector<uint32_t> *path1, vector<uint32_t> *path2) {
    if (path1->size() != path2->size()) {
      return false;
    }

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
    expirationTime += Seconds(timeout);
    return expirationTime;
  }

  // students need to implement a proper version of this routine
  void checkAllTimeouts() {
    map<uint32_t, Advertisement*>::iterator it;
    for (it = advertisementMap->begin(); it != advertisementMap->end();/*No Increment*/) {
      if (Simulator::Now().GetNanoSeconds() > it->second->expiryDate.GetNanoSeconds()) /* Check if path expired*/ {
        printTimeout(it->first);
        advertisementMap->erase(it++);
        this->dirty = true;
      } else {
        ++it;
      }
    }
  }

  void printNew(uint32_t neighbour, uint32_t destination, vector<uint32_t> *hops) {
    cout << Simulator::Now() << " " << ID;
    for (unsigned int i = 0; i < hops->size(); i++) {
      cout << " " << hops->at(i);
    }
    cout << " " << destination << " : NEW" << endl;
  }

  void printUpdate(uint32_t neighbour, uint32_t destination, vector<uint32_t> *hops, vector<uint32_t> *oldHops) {
    cout << Simulator::Now() << " " << ID;
    for (unsigned int i = 0; i < hops->size(); i++) {
      cout << " " << hops->at(i);
    }
    cout << " " << destination << " : UPDATE";
    for (unsigned int j = 0; j < oldHops->size(); j++) {
      cout << " " << oldHops->at(j);
    }
    cout << endl;
  }

  void printNoPaths(uint32_t neighbour, uint32_t destination) {
    cout << Simulator::Now() << " " << neighbour << " " << destination << " : NO PATH" << endl;
  }

  void printTimeout(uint32_t neighbour) {
    cout << Simulator::Now() << " " << ID << " " << neighbour << " TIMEOUT" << endl;
  }

  /**********************************************************************/
  /**********************************************************************/

public:
  PathVectorNode(uint32_t id, uint32_t nc, double i, double t)
    : ID(id), nodeCount(nc), interval(i), timeout(t) {
    this->dirty = false;
    this->advertisementMap = new map<uint32_t, Advertisement*>();
    this->shortestPaths = new vector<AdvertisementPathPacket>();
  }

  void AddSocket(Ptr<Socket> socket) {
    socket->SetRecvCallback(MakeCallback(&PathVectorNode::receive, this));
    socketList.push_back(socket);
  }
};

#include "pvr-main.h"
