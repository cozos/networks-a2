static Ipv4AddressHelper address;
static PointToPointHelper pointToPoint;
static NodeContainer nodes;
static bool** nodeMatrix = 0;

static const uint16_t port = 9999;

static void DisableLink(Ptr<Ipv4> ip1, uint32_t if1, Ptr<Ipv4> ip2, uint32_t if2) {
  ip1->SetDown(if1);
  ip2->SetDown(if2);
}

static bool setupNodePair(uint32_t i, uint32_t j, double disable = 0) {
  // test pairing - please keep this output
  cout << Simulator::Now() << ' ' << i << ' ' << j;
  if (nodeMatrix[i][j]) {
    cout << " duplicate" << endl;
    return false;
  }
  nodeMatrix[i][j] = nodeMatrix[j][i] = true;
  // get nodes
  Ptr<Node> n1 = nodes.Get(i);
  Ptr<Node> n2 = nodes.Get(j);
  // set up devices
  NetDeviceContainer devices = pointToPoint.Install(n1, n2);
  Ipv4InterfaceContainer interfaces = address.Assign(devices);
  // create sockets, bind and "connect"
  TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
  Ptr<Socket> s1 = Socket::CreateSocket(n1, tid);
  Ptr<Socket> s2 = Socket::CreateSocket(n2, tid);
  s1->Bind( InetSocketAddress(interfaces.GetAddress(0), port) );
  s2->Bind( InetSocketAddress(interfaces.GetAddress(1), port) );
  s1->Connect( InetSocketAddress(interfaces.GetAddress(1), port) );
  s2->Connect( InetSocketAddress(interfaces.GetAddress(0), port) );
  // add sockets to pvr applications
  Ptr<PathVectorNode> pvr1 = DynamicCast<PathVectorNode>(n1->GetApplication(0));
  Ptr<PathVectorNode> pvr2 = DynamicCast<PathVectorNode>(n2->GetApplication(0));
  pvr1->AddSocket(s1);
  pvr2->AddSocket(s2);
  cout << " paired" << endl;
  if (disable > 0) {
    pair<Ptr<Ipv4>,uint32_t> p1 = interfaces.Get(0);
    pair<Ptr<Ipv4>,uint32_t> p2 = interfaces.Get(1);
    Simulator::Schedule(Seconds(disable), &DisableLink, p1.first, p1.second, p2.first, p2.second);
  }
  // increment network addresses
  address.NewNetwork();
  return true;
}

int main(int argc, char *argv[]) {

  // command-line arguments with default values
  uint32_t seed      = 1;
  uint32_t nodeCount = 10;
  uint32_t linkCount = 15;
  double   interval  = 20.0;
  double   timeout   = 60.0;
  double   duration  = 600.0;

  // parse command-line arguments
  CommandLine cmd;
  cmd.AddValue("seed",     "random seed (1)", seed);
  cmd.AddValue("nodes",    "number of nodes (10)", nodeCount);
  cmd.AddValue("links",    "number of links (15)", linkCount);
  cmd.AddValue("interval", "interval in seconds (20.0)", interval);
  cmd.AddValue("timeout",  "timeout in seconds (60.0)", timeout);
  cmd.AddValue("duration", "simulation time in seconds (600.0)", duration);
  cmd.Parse(argc, argv);

  // perform sanity check on node & link counts
  if (linkCount < nodeCount - 1) {
    cerr << "cannot build a connected network of "
         << nodeCount << " nodes with only " << linkCount << " links" << endl;
    exit(1);
  }

  // create nodes and routing applications
  nodes.Create(nodeCount);
  InternetStackHelper internet;
  internet.Install(nodes);
  nodeMatrix = new bool*[nodeCount];
  for (uint32_t i = 0; i < nodeCount; i += 1) {
    nodeMatrix[i] = new bool[nodeCount];
    for (uint32_t j = 0; j < nodeCount; j += 1) {
      nodeMatrix[i][j] = false;
    }
    nodeMatrix[i][i] = true;
    Ptr<PathVectorNode> app = CreateObject<PathVectorNode>(i, nodeCount, interval, timeout);
    nodes.Get(i)->AddApplication(app);
    app->SetStartTime(Seconds(0));
    app->SetStopTime(Seconds(duration));
  }

  // configure link and addressing helpers
  pointToPoint.SetDeviceAttribute("DataRate", StringValue ("10Mbps"));
  pointToPoint.SetChannelAttribute("Delay", StringValue ("1ms"));
  address.SetBase ("10.1.1.0", "255.255.255.0");

  // set up random number generator
  RngSeedManager::SetSeed(seed);
  Ptr<UniformRandomVariable> nodeRNG = CreateObject<UniformRandomVariable>();

  // iterate through nodes: pick random previous node for initial link
  for (uint32_t i = 1; i < nodeCount; i += 1) {
    uint32_t j = nodeRNG->GetInteger(0, i-1);
    if (!setupNodePair(i,j)) {
      cerr << "internal error linking nodes " << i << " and " << j << endl;
      exit(1);
    }
  }

  // sprinkle rest of links randomly, disable at half of simulation duration
  for (uint32_t links = nodeCount - 1; links < linkCount; ) {
    uint32_t i = nodeRNG->GetInteger(0, nodeCount-1);
    uint32_t j = nodeRNG->GetInteger(0, nodeCount-1);
    if (setupNodePair(i, j, duration/2)) links += 1;
  }

  // run simulation
  Simulator::Stop(Seconds(duration));
  Simulator::Run();
  Simulator::Destroy();

  // clean up memory
  for (uint32_t i = 0; i < nodeCount; i += 1) delete [] nodeMatrix[i];
  delete [] nodeMatrix;

  // done
  return 0;
}
