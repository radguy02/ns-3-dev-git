#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"

using namespace std;
using namespace ns3;

int main(){

    //created sta and ap nodes
    NodeContainer apNodes;
    NodeContainer staNodes;
    apNodes.Create(1);
    staNodes.Create(1);

    //configuring wifi standard
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);

    //creating channel
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();

    //creating phy
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    //ssid
    Ssid ssid = Ssid("RADGuy");

    //creating devices
    NetDeviceContainer apDevice;
    NetDeviceContainer staDevice;

    //configuring mac
    WifiMacHelper mac;

    //sta setup with mac 
    mac.SetType("ns3::StaWifiMac", 
                "Ssid", SsidValue(ssid));

    staDevice = wifi.Install(phy, mac, staNodes);

    //ap setup with mac
    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid));
    
    apDevice = wifi.Install(phy, mac, apNodes);
    
    //assigning mobility
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

    positionAlloc->Add(Vector(0.0, 0.0, 0.0)); //ap
    positionAlloc->Add(Vector(5.0, 0.0, 0.0)); //sta 
    mobility.SetPositionAllocator(positionAlloc);
    //basically sta and ap is 5 metres away
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNodes);
    mobility.Install(staNodes);
    
    //creating internet stack
    //we are installing the stack to the nodes, not the device
    //as we need the internet on the computer not the nic
    InternetStackHelper stack;
    stack.Install(apNodes);
    stack.Install(staNodes);
    
    //assigning ipv4 addresses
    Ipv4AddressHelper address;
    address.SetBase("192.168.1.0", "255.255.255.0");
    
    Ipv4InterfaceContainer apInterface;
    Ipv4InterfaceContainer staInterface;

    staInterface = address.Assign(staDevice);
    apInterface = address.Assign(apDevice);
    
    //configuring applciaitons

    //reciever (sta)
    uint16_t port = 5000; //sta port for recieving
    //setting up packet sink
    PacketSinkHelper packetsinkhelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    
    ApplicationContainer serverApp;
    serverApp = packetsinkhelper.Install(staNodes);

    //sender (ap)
    OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(staInterface.GetAddress(0), port));
    
    onoff.SetAttribute("PacketSize", UintegerValue(1024));
    onoff.SetAttribute("DataRate", DataRateValue(DataRate("10Mbps")));
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    
    ApplicationContainer clientApp;
    clientApp = onoff.Install(apNodes);


    //start simulation
    serverApp.Start(Seconds(0.0));
    clientApp.Start(Seconds(1.0));

    serverApp.Stop(Seconds(11.0));
    clientApp.Stop(Seconds(10.0));
    
    Simulator::Stop(Seconds(11.0));
    Simulator::Run();


    //read the stats
    //get the PacketSink object from the ApplicationContainer
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(serverApp.Get(0));

    //total bytes received
    uint64_t totalBytes = sink->GetTotalRx();

    //calculate throughput
    double throughput =
        (totalBytes * 8.0) / (9.0 * 1000000.0);   //Mbps

    cout << "\n========== Simulation Results ==========\n";
    cout << "Total Bytes Received : " << totalBytes << " Bytes\n";
    cout << "Throughput           : " << throughput << " Mbps\n";
    cout << "========================================\n";

    Simulator::Destroy();
    
    return 0;
}