#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

#include <arpa/inet.h> // htonl, etc.
#include <list>

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("PathVectorRouing");

static uint32_t readInt(uint8_t* buf, uint32_t size, uint32_t& value) {
  NS_ASSERT(size >= sizeof(uint32_t));
  value = ntohl(*(uint32_t*)buf);
  return sizeof(uint32_t);
}

static uint32_t writeInt(uint8_t* buf, uint32_t size, uint32_t value) {
  NS_ASSERT(size >= sizeof(uint32_t));
  *(uint32_t*)buf = htonl(value);
  return sizeof(uint32_t);
}
