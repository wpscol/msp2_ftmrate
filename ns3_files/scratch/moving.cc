#include <chrono>
#include <filesystem>
#include <map>
#include <string>

#include "ns3/applications-module.h"
#include "ns3/buildings-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/node-container.h"
#include "ns3/node-list.h"
// #include "ns3/ns3-ai-module.h"
#include "ns3/ssid.h"
#include "ns3/vector.h"
#include "ns3/wifi-net-device.h"
#include "ns3/yans-wifi-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("moving");

/***** Functions declarations *****/

void ChangePower(NodeContainer wifiStaNode, uint8_t powerLevel);
uint64_t GetReceivedBits(Ptr<Node> sinkNode, Ptr<Node> sourceNode);
void GetWarmupFlows();
void InstallTrafficGenerator(Ptr<ns3::Node> fromNode, Ptr<ns3::Node> toNode, uint32_t port, DataRate offeredLoad,
                             uint32_t packetSize);
void MeasurementPoint(Ptr<Node> staNode, Ptr<Node> apNode);
void PopulateARPcache();
void PowerCallback(std::string path, Ptr<const Packet> packet, double txPowerW);
void StartMovement(Ptr<Node> staNode);
void UpdateDistance(Ptr<Node> staNode, Ptr<Node> apNode);

/***** Global variables and constants *****/

#define DISTANCE_UPDATE_INTERVAL 0.005
#define DEFAULT_TX_POWER 16.0206

std::map<uint32_t, uint64_t> warmupFlows;
uint64_t warmupFlowsSum;

FlowMonitorHelper flowmon;
Ptr<FlowMonitor> monitor;
std::ostringstream csvOutput;
std::string wifiManagerName = "";

double velocity = 1.;
double fuzzTime = 1.;
double warmupTime = 5.;
double simulationTime = 56.;
double measurementsInterval = 2.;
double delta = 0.;    // difference between 2 power levels in dB
double interval = 4.; // mean (exponential) interval between power change

/***** Main with scenario definition *****/

int main(int argc, char* argv[]) {
  // Initialize default simulation parameters
  std::string rateAdaptationManager = "ns3::MinstrelHtWifiManager";
  std::string tcpCongestionAlg = "ns3::TcpNewReno";
  std::string pcapName = "";
  std::string csvPath = "results.csv";
  std::string lossModel = "Nakagami";

  bool ampdu = true;
  bool enableRtsCts = false;
  uint32_t packetSize = 1500;
  uint32_t dataRate = 125;
  uint32_t channelWidth = 20;
  uint32_t minGI = 3200;
  double startPosition = 0.;
  double wallInterval = 0.;
  double wallLoss = 3.;

  // Parse command line arguments
  CommandLine cmd;
  cmd.AddValue("ampdu", "Use AMPDU (boolean flag)", ampdu);
  cmd.AddValue("channelWidth", "Channel width (MHz)", channelWidth);
  cmd.AddValue("csvPath", "Path to output CSV file", csvPath);
  cmd.AddValue("dataRate", "Traffic generator data rate (Mb/s)", dataRate);
  cmd.AddValue("delta", "Power change (dBm)", delta);
  cmd.AddValue("enableRtsCts", "Flag set to enable CTS/RTS protocol", enableRtsCts);
  cmd.AddValue("fuzzTime", "Maximum fuzz value (s)", fuzzTime);
  cmd.AddValue("interval", "Interval between power change (s)", interval);
  cmd.AddValue("lossModel", "Propagation loss model to use (LogDistance, Nakagami)", lossModel);
  cmd.AddValue("measurementsInterval", "Interval between successive measurement points (s)", measurementsInterval);
  cmd.AddValue("manager", "Rate adaptation manager", rateAdaptationManager);
  cmd.AddValue("managerName", "Name of the Wi-Fi manager in CSV", wifiManagerName);
  cmd.AddValue("minGI", "Shortest guard interval (ns)", minGI);
  cmd.AddValue("packetSize", "Packets size (B)", packetSize);
  cmd.AddValue("pcapName", "Name of a PCAP file generated from the AP", pcapName);
  cmd.AddValue("simulationTime", "Duration of simulation (s)", simulationTime);
  cmd.AddValue("startPosition", "Starting position of main station on X axis (m)", startPosition);
  cmd.AddValue("tcpCongestionAlg", "Congestion algorithm used by TCP protocol", tcpCongestionAlg);
  cmd.AddValue("velocity", "Station velocity (m/s)", velocity);
  cmd.AddValue("wallInterval", "Position of the wall from the AP (m)", wallInterval);
  cmd.AddValue("wallLoss", "Loss of the wall (dB)", wallLoss);
  cmd.AddValue("warmupTime", "Duration of warmup stage (s)", warmupTime);
  cmd.Parse(argc, argv);

  if (wifiManagerName.empty()) {
    wifiManagerName = rateAdaptationManager;
  }

  // Print simulation settings to screen
  std::cout << std::endl
            << "Simulating an IEEE 802.11ax devices with the following settings:" << std::endl
            << "- frequency band: 5 GHz" << std::endl
            << "- max data rate: " << dataRate << " Mb/s" << std::endl
            << "- channel width: " << channelWidth << " Mhz" << std::endl
            << "- shortest guard interval: " << minGI << " ns" << std::endl
            << "- packets size: " << packetSize << " B" << std::endl
            << "- AMPDU: " << ampdu << std::endl
            << "- RTS/CTS protocol enabled: " << enableRtsCts << std::endl
            << "- rate adaptation manager: " << rateAdaptationManager << std::endl
            << "- number of stations: 1" << std::endl
            << "- simulation time: " << simulationTime << " s" << std::endl
            << "- warmup time: " << warmupTime << " s" << std::endl
            << "- max fuzz time: " << fuzzTime << " s" << std::endl
            << "- delta: " << delta << " dBm" << std::endl
            << "- interval: " << interval << " s" << std::endl
            << "- loss model: " << lossModel << std::endl
            << "- mobility model: Moving" << std::endl
            << "- tcpCongestionAlg: " << tcpCongestionAlg << std::endl
            << "- velocity: " << velocity << " m/s" << std::endl
            << "- startPosition: " << startPosition << " m" << std::endl
            << "- wallInterval: " << wallInterval << " m" << std::endl
            << "- wallLoss: " << wallLoss << " dB" << std::endl
            << std::endl;

  // Configure TCP Congestion algorithm
  Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue(tcpCongestionAlg));

  // Create AP and station
  NodeContainer wifiApNode(1);
  NodeContainer wifiStaNode(1);

  // Configure mobility
  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  mobility.Install(wifiApNode);
  mobility.Install(wifiStaNode);
  wifiStaNode.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector3D(startPosition, 0., 0.));
  // wifiStaNode.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector3D(startPosition, 0., .)); // 2m  AP <--> STA

  // Print position of each node
  std::cout << "Node positions:" << std::endl;

  // AP position
  Ptr<MobilityModel> position = wifiApNode.Get(0)->GetObject<MobilityModel>();
  Vector3D pos = position->GetPosition();
  std::cout << "AP:\tx=" << pos.x << ", y=" << pos.y << std::endl;

  // Stations positions
  for (auto node = wifiStaNode.Begin(); node != wifiStaNode.End(); ++node) {
    position = (*node)->GetObject<MobilityModel>();
    pos = position->GetPosition();
    std::cout << "Sta " << (*node)->GetId() << ":\tx=" << pos.x << ", y=" << pos.y << std::endl;
  }

  std::cout << std::endl;

  // Place a building
  if (wallInterval > 0.) {
    double finalPosition = simulationTime * velocity + startPosition;
    uint32_t rooms = (uint32_t)((finalPosition + wallInterval) / wallInterval);

    Ptr<Building> b = CreateObject<Building>();
    b->SetBoundaries(Box(0., rooms * wallInterval, -5., 5., -5., 5.));
    b->SetBuildingType(Building::Office);
    b->SetExtWallsType(Building::ConcreteWithoutWindows);
    b->SetNRoomsX(rooms);

    BuildingsHelper::Install(wifiApNode);
    BuildingsHelper::Install(wifiStaNode);
  }

  // Configure wireless channel
  YansWifiChannelHelper channelHelper;

  if (wallInterval > 0) {
    channelHelper.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channelHelper.AddPropagationLoss("ns3::HybridBuildingsPropagationLossModel", "Frequency", DoubleValue(5418e6),
                                     "ShadowSigmaOutdoor", DoubleValue(0.), "ShadowSigmaIndoor", DoubleValue(0.),
                                     "ShadowSigmaExtWalls", DoubleValue(0.), "InternalWallLoss", DoubleValue(wallLoss),
                                     "CitySize", StringValue("Small"), "Environment", StringValue("OpenAreas"));
  } else {
    channelHelper = YansWifiChannelHelper::Default();
  }

  if (lossModel == "Nakagami") {
    // Add Nakagami fading to the default log distance model
    channelHelper.AddPropagationLoss("ns3::NakagamiPropagationLossModel");
  } else if (lossModel != "LogDistance") {
    std::cerr << "Selected incorrect loss model!";
    return 1;
  }

  YansWifiPhyHelper phy;
  phy.SetChannel(channelHelper.Create());

  // Configure two power levels
  phy.Set("TxPowerLevels", UintegerValue(2));
  phy.Set("TxPowerStart", DoubleValue(DEFAULT_TX_POWER - delta));
  phy.Set("TxPowerEnd", DoubleValue(DEFAULT_TX_POWER));

  // Configure MAC layer
  WifiMacHelper mac;
  WifiHelper wifi;

  wifi.SetStandard(WIFI_STANDARD_80211ax);
  wifi.SetRemoteStationManager(rateAdaptationManager);

  // Enable or disable CTS/RTS
  uint64_t ctsThrLow = 100;
  uint64_t ctsThrHigh = 100000000; // Arbitrarly large value, 100 MB for now
  UintegerValue ctsThr = (enableRtsCts ? UintegerValue(ctsThrLow) : UintegerValue(ctsThrHigh));
  Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", ctsThr);

  // Set SSID
  Ssid ssid = Ssid("ns3-80211ax");
  mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "MaxMissedBeacons",
              UintegerValue(1000)); // prevents exhaustion of association IDs

  // Create and configure Wi-Fi interfaces
  NetDeviceContainer staDevice;
  staDevice = wifi.Install(phy, mac, wifiStaNode);

  mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));

  NetDeviceContainer apDevice;
  apDevice = wifi.Install(phy, mac, wifiApNode);

  // Manage AMPDU aggregation
  if (!ampdu) {
    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize", UintegerValue(0));
  }

  // Set channel width and shortest GI
  Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/ChannelSettings",
              StringValue("{0, " + std::to_string(channelWidth) + ", BAND_5GHZ, 0}"));

  Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/HeConfiguration/GuardInterval",
              TimeValue(NanoSeconds(minGI)));

  // Install an Internet stack
  InternetStackHelper stack;
  stack.Install(wifiApNode);
  stack.Install(wifiStaNode);

  // Configure IP addressing
  Ipv4AddressHelper address("192.168.1.0", "255.255.255.0");
  Ipv4InterfaceContainer staNodeInterface = address.Assign(staDevice);
  Ipv4InterfaceContainer apNodeInterface = address.Assign(apDevice);

  // PopulateArpCache
  PopulateARPcache();

  // Configure applications
  DataRate applicationDataRate = DataRate(dataRate * 1e6);
  uint32_t portNumber = 9;

  InstallTrafficGenerator(wifiStaNode.Get(0), wifiApNode.Get(0), portNumber, applicationDataRate, packetSize);

  // Install FlowMonitor
  monitor = flowmon.InstallAll();
  Simulator::Schedule(Seconds(warmupTime), &GetWarmupFlows);

  // Generate PCAP at AP
  if (!pcapName.empty()) {
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
    phy.EnablePcap(pcapName, apDevice);
  }

  // Register callback for power change
  Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyTxBegin", MakeCallback(PowerCallback));

  // Schedule all power changes
  // The interval between each change follows the exponential distribution
  double time = warmupTime;
  bool maxPower = false;

  // Create random number generator with parameters: seed = global seed, stream = 2^63 + 1, substream/run = 1
  RngStream rng(RngSeedManager::GetSeed(), ((1ULL) << 63) + 1, 1);

  while (time < warmupTime + simulationTime) {
    // Draw from the exponential distribution ( [ -1/lambda * ln(x) ] ~ Exp[lambda] where x ~ Uniform[0, 1] )
    time += -interval * std::log(rng.RandU01());
    Simulator::Schedule(Seconds(time), &ChangePower, wifiStaNode, maxPower);
    maxPower = !maxPower;
  }

  // Register distance measurements
  Simulator::ScheduleNow(&UpdateDistance, wifiStaNode.Get(0), wifiApNode.Get(0));

  // Schedule station movement and first measurement point
  Simulator::Schedule(Seconds(warmupTime), &MeasurementPoint, wifiStaNode.Get(0), wifiApNode.Get(0));
  Simulator::Schedule(Seconds(warmupTime), &StartMovement, wifiStaNode.Get(0));

  // Define simulation stop time
  Simulator::Stop(Seconds(warmupTime + simulationTime));

  // Record start time
  std::cout << "Starting simulation..." << std::endl;
  auto start = std::chrono::high_resolution_clock::now();

  // Run the simulation!
  Simulator::Run();

  // Record stop time and count duration
  auto finish = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = finish - start;

  std::cout << "Done!" << std::endl << "Elapsed time: " << elapsed.count() << " s" << std::endl << std::endl;

  // Calculate throughput
  double totalThr = 0.;

  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
  std::cout << "Results: " << std::endl;

  for (auto& stat : stats) {
    double flow = (8 * stat.second.rxBytes - warmupFlows[stat.first]) / (simulationTime * 1e6);
    totalThr += flow;

    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(stat.first);
    std::cout << "Flow " << stat.first << " (" << t.sourceAddress << " -> " << t.destinationAddress
              << ")\tThroughput: " << flow << " Mb/s" << std::endl;
  }

  std::cout << std::endl << "Network throughput: " << totalThr << " Mb/s" << std::endl << std::endl;

  // Print results to std output
  std::cout << "mobility,manager,delta,interval,velocity,distance,time,nWifi,nWifiReal,seed,throughput" << std::endl
            << csvOutput.str();

  // Print results to file
  std::ofstream outputFile(csvPath);
  outputFile << csvOutput.str();
  std::cout << std::endl << "Simulation data saved to: " << csvPath << std::endl << std::endl;

  // Clean-up
  Simulator::Destroy();

  return 0;
}

/***** Function definitions *****/

void ChangePower(NodeContainer wifiStaNodes, uint8_t powerLevel) {
  // Iter through STA nodes and change power for each
  for (auto node = wifiStaNodes.Begin(); node != wifiStaNodes.End(); ++node) {
    std::stringstream devicePath;
    devicePath << "/NodeList/" << (*node)->GetId()
               << "/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/DefaultTxPowerLevel";
    Config::Set(devicePath.str(), UintegerValue(powerLevel));
  }
}

uint64_t GetReceivedBits(Ptr<Node> sinkNode, Ptr<Node> sourceNode) {
  // Get sink and source address
  Ptr<Ipv4> ipv4Sink = sinkNode->GetObject<Ipv4>();
  Ipv4Address sinkAddr = ipv4Sink->GetAddress(1, 0).GetLocal();

  Ptr<Ipv4> ipv4Source = sourceNode->GetObject<Ipv4>();
  Ipv4Address sourceAddr = ipv4Source->GetAddress(1, 0).GetLocal();

  // Get flow statistics
  auto classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  auto stats = monitor->GetFlowStats();
  uint64_t bytes = 0;

  for (auto& stat : stats) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(stat.first);

    if (t.destinationAddress == sinkAddr && t.sourceAddress == sourceAddr) {
      bytes += stat.second.rxBytes;
    }
  }

  return 8 * bytes;
}

void GetWarmupFlows() {
  warmupFlowsSum = 0;

  for (auto& stat : monitor->GetFlowStats()) {
    warmupFlows.insert(std::pair<uint32_t, double>(stat.first, 8 * stat.second.rxBytes));
    warmupFlowsSum += stat.second.rxBytes;
  }

  warmupFlowsSum *= 8;
}

void InstallTrafficGenerator(Ptr<ns3::Node> fromNode, Ptr<ns3::Node> toNode, uint32_t port, DataRate offeredLoad,
                             uint32_t packetSize) {
  // Get sink address
  Ptr<Ipv4> ipv4 = toNode->GetObject<Ipv4>();
  Ipv4Address addr = ipv4->GetAddress(1, 0).GetLocal();

  // Define type of service
  uint8_t tosValue = 0x70; // AC_BE

  // Add random fuzz to app start time
  Ptr<UniformRandomVariable> fuzz = CreateObject<UniformRandomVariable>();
  fuzz->SetAttribute("Min", DoubleValue(0.));
  fuzz->SetAttribute("Max", DoubleValue(fuzzTime));
  fuzz->SetStream(0);
  double applicationsStart = fuzz->GetValue();

  // Configure source and sink
  InetSocketAddress sinkSocket(addr, port);
  sinkSocket.SetTos(tosValue);
  PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", sinkSocket);

  OnOffHelper onOffHelper("ns3::TcpSocketFactory", sinkSocket);
  onOffHelper.SetConstantRate(offeredLoad, packetSize);

  // Configure applications
  ApplicationContainer sinkApplications(packetSinkHelper.Install(toNode));
  ApplicationContainer sourceApplications(onOffHelper.Install(fromNode));

  sinkApplications.Start(Seconds(applicationsStart));
  sinkApplications.Stop(Seconds(warmupTime + simulationTime));
  sourceApplications.Start(Seconds(applicationsStart));
  sourceApplications.Stop(Seconds(warmupTime + simulationTime));
}

void MeasurementPoint(Ptr<Node> staNode, Ptr<Node> apNode) {
  // Calculate metrics since last measurement
  static double lastTime = -1.;
  static uint64_t lastRx = warmupFlowsSum;

  double currentTime = Simulator::Now().GetSeconds();
  if (lastTime == -1.) {
    lastTime = currentTime;
    Simulator::Schedule(Seconds(measurementsInterval), &MeasurementPoint, staNode, apNode);
    return;
  }

  uint64_t receivedBits = GetReceivedBits(apNode, staNode);
  double throughput = (receivedBits - lastRx) / (1e6 * (currentTime - lastTime));

  lastTime = currentTime;
  lastRx = receivedBits;

  // Get current position
  Ptr<MobilityModel> mobility = staNode->GetObject<MobilityModel>();
  Vector pos = mobility->GetPosition();

  // Add current state to CSV
  csvOutput << "Moving," << wifiManagerName << ',' << delta << ',' << interval << ',' << velocity << ',' << pos.x << ','
            << Simulator::Now().GetSeconds() - warmupTime << ",1,1," << RngSeedManager::GetRun() << ',' << throughput
            << std::endl;

  // Schedule next measurement
  Simulator::Schedule(Seconds(measurementsInterval), &MeasurementPoint, staNode, apNode);
}

void PopulateARPcache() {
  Ptr<ArpCache> arp = CreateObject<ArpCache>();
  arp->SetAliveTimeout(Seconds(3600 * 24 * 365));

  for (auto i = NodeList::Begin(); i != NodeList::End(); ++i) {
    Ptr<Ipv4L3Protocol> ip = (*i)->GetObject<Ipv4L3Protocol>();
    ObjectVectorValue interfaces;
    ip->GetAttribute("InterfaceList", interfaces);

    for (auto j = interfaces.Begin(); j != interfaces.End(); j++) {
      Ptr<Ipv4Interface> ipIface = (*j).second->GetObject<Ipv4Interface>();
      Ptr<NetDevice> device = ipIface->GetDevice();
      Mac48Address addr = Mac48Address::ConvertFrom(device->GetAddress());

      for (uint32_t k = 0; k < ipIface->GetNAddresses(); k++) {
        Ipv4Address ipAddr = ipIface->GetAddress(k).GetLocal();
        if (ipAddr == Ipv4Address::GetLoopback()) {
          continue;
        }

        ArpCache::Entry* entry = arp->Add(ipAddr);
        Ipv4Header ipv4Hdr;
        ipv4Hdr.SetDestination(ipAddr);

        Ptr<Packet> p = Create<Packet>(100);
        entry->MarkWaitReply(ArpCache::Ipv4PayloadHeaderPair(p, ipv4Hdr));
        entry->MarkAlive(addr);
      }
    }
  }

  for (auto i = NodeList::Begin(); i != NodeList::End(); ++i) {
    Ptr<Ipv4L3Protocol> ip = (*i)->GetObject<Ipv4L3Protocol>();
    ObjectVectorValue interfaces;
    ip->GetAttribute("InterfaceList", interfaces);

    for (auto j = interfaces.Begin(); j != interfaces.End(); j++) {
      Ptr<Ipv4Interface> ipIface = (*j).second->GetObject<Ipv4Interface>();
      ipIface->SetAttribute("ArpCache", PointerValue(arp));
    }
  }
}

void PowerCallback(std::string path, Ptr<const Packet> packet, double txPowerW) {
  size_t start = 10; // length of "/NodeList/" string
  size_t end = path.find("/DeviceList/");
  std::string nodeId = path.substr(start, end - start);

  Config::Set("/NodeList/" + nodeId +
                  "/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/"
                  "$ns3::MlWifiManager/Power",
              DoubleValue(10 * (3 + log10(txPowerW))));

  Config::Set("/NodeList/" + nodeId +
                  "/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/"
                  "$ns3::OracleWifiManager/Power",
              DoubleValue(10 * (3 + log10(txPowerW))));
}

void StartMovement(Ptr<Node> staNode) {
  // Set station velocity
  auto mobility = staNode->GetObject<ConstantVelocityMobilityModel>();
  mobility->SetVelocity(Vector3D(velocity, 0., 0.));
}

void UpdateDistance(Ptr<Node> staNode, Ptr<Node> apNode) {
  Ptr<MobilityModel> staMobility = staNode->GetObject<MobilityModel>();
  Ptr<MobilityModel> apMobility = apNode->GetObject<MobilityModel>();
  double d = staMobility->GetDistanceFrom(apMobility);

  Config::Set("/NodeList/" + std::to_string(staNode->GetId()) +
                  "/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/"
                  "$ns3::MlWifiManager/Distance",
              DoubleValue(d));

  Config::Set("/NodeList/" + std::to_string(staNode->GetId()) +
                  "/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/"
                  "$ns3::OracleWifiManager/Distance",
              DoubleValue(d));

  Simulator::Schedule(Seconds(DISTANCE_UPDATE_INTERVAL), &UpdateDistance, staNode, apNode);
}
